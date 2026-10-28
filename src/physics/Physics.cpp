#include "physics/Physics.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>
#include <unordered_set>

namespace ppn {
namespace {

constexpr float kLinearSlop = 0.005f;
constexpr float kMaxLinearCorrection = 0.2f;
constexpr float kBaumgarte = 0.18f;
constexpr float kRestitutionThreshold = 0.8f;
constexpr float kMaxLinearVelocity = 80.0f;
constexpr float kMaxAngularVelocity = 60.0f;
// Concave bodies collide as a set of convex pieces. Pieces meet along internal
// diagonals, so a manifold is only kept when its contact points lie on the real
// silhouette of both bodies; otherwise internal edges generate bogus normals
// and the note visibly twitches while resting on another note.
constexpr float kBoundaryTolerance = 0.02f;
constexpr int kMaxContactsPerPair = 8;
// Drag coefficient used by the quadratic fluid drag term. Buoyancy fields are a
// real fluid volume: lift comes from Archimedes' formula and the opposing drag
// is 0.5 * density * Cd * submergedArea * |v| * v.
constexpr float kBuoyancyDragCoefficient = 1.2f;
// Low-speed viscous term. Real fluids have both quadratic (inertial) and linear
// (viscous) drag; the linear part only matters around the settling point.
constexpr float kBuoyancyLinearDrag = 0.8f;

bool pointInTriangle(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p) {
    const float d1 = cross(b - a, p - a);
    const float d2 = cross(c - b, p - b);
    const float d3 = cross(a - c, p - c);
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}

float orient(const Vec2& a, const Vec2& b, const Vec2& c) {
    return cross(b - a, c - a);
}

bool onSegment(const Vec2& a, const Vec2& b, const Vec2& p) {
    return std::fabs(orient(a, b, p)) < 1e-5f &&
           p.x >= std::min(a.x, b.x) - 1e-5f && p.x <= std::max(a.x, b.x) + 1e-5f &&
           p.y >= std::min(a.y, b.y) - 1e-5f && p.y <= std::max(a.y, b.y) + 1e-5f;
}

bool segmentsIntersect(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const float o1 = orient(a, b, c);
    const float o2 = orient(a, b, d);
    const float o3 = orient(c, d, a);
    const float o4 = orient(c, d, b);
    if (((o1 > 1e-6f && o2 < -1e-6f) || (o1 < -1e-6f && o2 > 1e-6f)) &&
        ((o3 > 1e-6f && o4 < -1e-6f) || (o3 < -1e-6f && o4 > 1e-6f))) {
        return true;
    }
    if (onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b))
        return true;
    return false;
}

Vec2 edgeNormal(const Vec2& v1, const Vec2& v2) {
    const Vec2 e = v2 - v1;
    return normalize(Vec2{e.y, -e.x});
}

float findMaxSeparation(const Poly& polyA, const Poly& polyB, int* edgeIndex) {
    float best = -FLT_MAX;
    int bestIndex = 0;
    for (size_t i = 0; i < polyA.size(); ++i) {
        const Vec2& v1 = polyA[i];
        const Vec2& v2 = polyA[(i + 1) % polyA.size()];
        const Vec2 n = edgeNormal(v1, v2);
        if (lengthSq(n) < 0.5f) continue;
        float minProj = FLT_MAX;
        for (const auto& v : polyB) minProj = std::min(minProj, dot(n, v));
        const float sep = minProj - dot(n, v1);
        if (sep > best) {
            best = sep;
            bestIndex = (int)i;
        }
    }
    if (edgeIndex) *edgeIndex = bestIndex;
    return best;
}

void computeIncidentEdge(const Poly& poly, const Vec2& refNormal, Vec2 out[2]) {
    float best = FLT_MAX;
    int bestIndex = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2& v1 = poly[i];
        const Vec2& v2 = poly[(i + 1) % poly.size()];
        const Vec2 n = edgeNormal(v1, v2);
        if (lengthSq(n) < 0.5f) continue;
        const float d = dot(n, refNormal);
        if (d < best) {
            best = d;
            bestIndex = (int)i;
        }
    }
    out[0] = poly[bestIndex];
    out[1] = poly[(bestIndex + 1) % poly.size()];
}

// Sutherland-Hodgman clipping of a subject polygon by a convex CCW polygon.
// Used by the buoyancy field to integrate the displaced area exactly (for
// rectangles) or through a fine polygonal circle approximation.
Poly clipPolygonToConvex(const Poly& subject, const Poly& clip) {
    if (subject.size() < 3 || clip.size() < 3) return {};
    Poly output = subject;
    for (size_t e = 0; e < clip.size() && !output.empty(); ++e) {
        const Vec2 a = clip[e];
        const Vec2 b = clip[(e + 1) % clip.size()];
        const Vec2 edge = b - a;
        if (lengthSq(edge) < 1e-12f) continue;
        Poly input = std::move(output);
        output.clear();
        for (size_t i = 0; i < input.size(); ++i) {
            const Vec2 p = input[i];
            const Vec2 q = input[(i + 1) % input.size()];
            const float dp = cross(edge, p - a);
            const float dq = cross(edge, q - a);
            const bool pInside = dp >= -1e-7f;
            const bool qInside = dq >= -1e-7f;
            if (pInside) output.push_back(p);
            if (pInside != qInside) {
                const float denom = dp - dq;
                if (std::fabs(denom) > 1e-9f)
                    output.push_back(lerp(p, q, dp / denom));
            }
        }
    }
    return output;
}

int clipSegment(const Vec2 in[2], Vec2 out[2], const Vec2& n, float c) {
    const float d0 = dot(n, in[0]) - c;
    const float d1 = dot(n, in[1]) - c;
    int count = 0;
    if (d0 <= 0.0f) out[count++] = in[0];
    if (d1 <= 0.0f) out[count++] = in[1];
    if (d0 * d1 < 0.0f) {
        const float t = d0 / (d0 - d1);
        const Vec2 p = lerp(in[0], in[1], t);
        if (count == 0) {
            out[count++] = p;
        } else if (count == 1) {
            out[count++] = p;
        } else {
            out[1] = p;
        }
    }
    if (count > 2) count = 2;
    return count;
}

bool isConvexOutline(const Poly& outline) {
    const int n = (int)outline.size();
    if (n < 3) return false;
    float sign = 0.0f;
    for (int i = 0; i < n; ++i) {
        const Vec2& a = outline[(size_t)i];
        const Vec2& b = outline[(size_t)((i + 1) % n)];
        const Vec2& c = outline[(size_t)((i + 2) % n)];
        const float turn = cross(b - a, c - b);
        if (std::fabs(turn) < 1e-7f) continue;
        if (sign == 0.0f)
            sign = turn;
        else if ((turn > 0.0f) != (sign > 0.0f))
            return false;
    }
    return sign != 0.0f;
}

float distanceSqToOutline(const Poly& outline, const Vec2& point) {
    float best = FLT_MAX;
    const int n = (int)outline.size();
    for (int i = 0; i < n; ++i) {
        const Vec2& a = outline[(size_t)i];
        const Vec2& b = outline[(size_t)((i + 1) % n)];
        const Vec2 ab = b - a;
        const float lenSq = lengthSq(ab);
        const float t = lenSq > 1e-12f ? clampf(dot(point - a, ab) / lenSq, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, lengthSq(point - (a + ab * t)));
    }
    return best;
}

struct PointJoint {
    Body* a = nullptr;
    Body* b = nullptr;
    Vec2 localA;
    Vec2 localB;
};

Vec2 jointAnchorA(const PointJoint& j) {
    return j.a ? j.a->worldPoint(j.localA) : j.localA;
}
Vec2 jointAnchorB(const PointJoint& j) {
    return j.b ? j.b->worldPoint(j.localB) : j.localB;
}

void solvePointVelocity(const PointJoint& j) {
    Body* a = j.a;
    Body* b = j.b;
    if (!b) return;
    const float imA = a ? a->invMass : 0.0f;
    const float iiA = a ? a->invInertia : 0.0f;
    const float imB = b->invMass;
    const float iiB = b->invInertia;
    if (imA + iiA + imB + iiB <= 0.0f) return;

    const Vec2 pa = jointAnchorA(j);
    const Vec2 pb = jointAnchorB(j);
    const Vec2 ra = a ? (pa - a->position) : Vec2{};
    const Vec2 rb = pb - b->position;
    const Vec2 va = a ? (a->velocity + cross(a->angularVelocity, ra)) : Vec2{};
    const Vec2 vb = b->velocity + cross(b->angularVelocity, rb);
    const Vec2 dv = vb - va;

    const float k11 = imA + imB + iiA * ra.y * ra.y + iiB * rb.y * rb.y;
    const float k12 = -iiA * ra.x * ra.y - iiB * rb.x * rb.y;
    const float k22 = imA + imB + iiA * ra.x * ra.x + iiB * rb.x * rb.x;
    const float det = k11 * k22 - k12 * k12;
    if (std::fabs(det) < 1e-9f) return;

    const Vec2 rhs{-dv.x, -dv.y};
    const Vec2 impulse{(k22 * rhs.x - k12 * rhs.y) / det,
                       (-k12 * rhs.x + k11 * rhs.y) / det};

    if (a) {
        a->velocity -= impulse * imA;
        a->angularVelocity -= iiA * cross(ra, impulse);
    }
    b->velocity += impulse * imB;
    b->angularVelocity += iiB * cross(rb, impulse);
}

void solvePointPosition(const PointJoint& j, float factor) {
    Body* a = j.a;
    Body* b = j.b;
    if (!b) return;
    const float imA = a ? a->invMass : 0.0f;
    const float iiA = a ? a->invInertia : 0.0f;
    const float imB = b->invMass;
    const float iiB = b->invInertia;
    if (imA + iiA + imB + iiB <= 0.0f) return;

    const Vec2 pa = jointAnchorA(j);
    const Vec2 pb = jointAnchorB(j);
    const Vec2 ra = a ? (pa - a->position) : Vec2{};
    const Vec2 rb = pb - b->position;
    const Vec2 c = pb - pa;
    if (lengthSq(c) < 1e-10f) return;

    const float k11 = imA + imB + iiA * ra.y * ra.y + iiB * rb.y * rb.y;
    const float k12 = -iiA * ra.x * ra.y - iiB * rb.x * rb.y;
    const float k22 = imA + imB + iiA * ra.x * ra.x + iiB * rb.x * rb.x;
    const float det = k11 * k22 - k12 * k12;
    if (std::fabs(det) < 1e-9f) return;

    const float scale = clampf(factor, 0.0f, 1.0f);
    const Vec2 rhs{-c.x * scale, -c.y * scale};
    const Vec2 impulse{(k22 * rhs.x - k12 * rhs.y) / det,
                       (-k12 * rhs.x + k11 * rhs.y) / det};

    if (a) {
        a->position -= impulse * imA;
        a->angle -= iiA * cross(ra, impulse);
    }
    b->position += impulse * imB;
    b->angle += iiB * cross(rb, impulse);
}

void wakePair(Body* a, Body* b) {
    if (!a || !b) return;
    if (a->awake && !b->awake && b->type == BodyType::Dynamic) b->wake();
    if (b->awake && !a->awake && a->type == BodyType::Dynamic) a->wake();
}

} // namespace

bool isSimplePolygon(const Poly& polygon) {
    const int n = (int)polygon.size();
    if (n < 3) return false;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (j == i) continue;
            if (j == (i + 1) % n || i == (j + 1) % n) continue; // adjacent edges
            if (segmentsIntersect(polygon[i], polygon[(i + 1) % n],
                                  polygon[j], polygon[(j + 1) % n])) {
                return false;
            }
        }
    }
    return true;
}

bool normalizePolygon(Poly& vertices, std::string* error, int maxVertices) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if ((int)vertices.size() < 3) return fail("至少需要 3 个顶点");
    if ((int)vertices.size() > maxVertices) return fail("顶点数量超出上限");
    for (const auto& v : vertices) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) return fail("顶点坐标无效");
    }

    // Validate the effective outline on a cleaned copy. The authored vertex
    // list is preserved: coincident / overlapping nodes must not be merged.
    Poly cleaned;
    cleaned.reserve(vertices.size());
    for (const auto& v : vertices) {
        if (cleaned.empty() || lengthSq(v - cleaned.back()) > 0.02f * 0.02f)
            cleaned.push_back(v);
    }
    while (cleaned.size() > 1 && lengthSq(cleaned.front() - cleaned.back()) < 0.02f * 0.02f)
        cleaned.pop_back();
    if (cleaned.size() < 3) return fail("至少需要 3 个有效顶点");

    bool changed = true;
    while (changed && cleaned.size() > 3) {
        changed = false;
        for (size_t i = 0; i < cleaned.size(); ++i) {
            const Vec2& prev = cleaned[(i + cleaned.size() - 1) % cleaned.size()];
            const Vec2& cur = cleaned[i];
            const Vec2& next = cleaned[(i + 1) % cleaned.size()];
            const Vec2 e1 = cur - prev;
            const Vec2 e2 = next - cur;
            const float l1 = length(e1);
            const float l2 = length(e2);
            if (l1 < 1e-3f || l2 < 1e-3f) {
                cleaned.erase(cleaned.begin() + i);
                changed = true;
                break;
            }
            if (std::fabs(cross(e1, e2)) / (l1 * l2) < 1e-4f) {
                cleaned.erase(cleaned.begin() + i);
                changed = true;
                break;
            }
        }
    }
    if (cleaned.size() > (size_t)maxVertices) return fail("顶点数量超出上限");

    float area = polygonSignedArea(cleaned);
    if (std::fabs(area) < 1e-3f) return fail("多边形面积过小");
    if (area < 0.0f) {
        std::reverse(vertices.begin(), vertices.end());
        std::reverse(cleaned.begin(), cleaned.end());
        area = -area;
    }
    if (!isSimplePolygon(cleaned)) return fail("多边形边出现自相交");

    // Keep the authored origin and the original node count. NoteType uses the
    // file's (0, 0) as the object origin; the physics body builder re-centres
    // its own copy on the area centroid.
    return true;
}

std::vector<Poly> World::triangulate(const Poly& polygon) {
    std::vector<Poly> result;
    const int n = (int)polygon.size();
    if (n < 3) return result;

    // Coincident authored nodes are preserved in the document / outline, but
    // degenerate edges are skipped for physics triangulation.
    std::vector<int> indices;
    indices.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        if (indices.empty() || lengthSq(polygon[(size_t)i] -
                                        polygon[(size_t)indices.back()]) > 1e-8f)
            indices.push_back(i);
    }
    while (indices.size() > 1 &&
           lengthSq(polygon[(size_t)indices.front()] - polygon[(size_t)indices.back()]) <
               1e-8f)
        indices.pop_back();
    if (indices.size() < 3) return result;

    auto emit = [&](int a, int b, int c) {
        const Vec2& pa = polygon[a];
        const Vec2& pb = polygon[b];
        const Vec2& pc = polygon[c];
        if (std::fabs(cross(pb - pa, pc - pa)) > 1e-9f)
            result.push_back(Poly{pa, pb, pc});
    };

    int guard = 0;
    while (indices.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        const int m = (int)indices.size();
        for (int i = 0; i < m; ++i) {
            const int ia = indices[(i + m - 1) % m];
            const int ib = indices[i];
            const int ic = indices[(i + 1) % m];
            const Vec2& a = polygon[ia];
            const Vec2& b = polygon[ib];
            const Vec2& c = polygon[ic];
            if (cross(b - a, c - b) <= 1e-7f) continue; // reflex or degenerate
            bool contains = false;
            for (int j = 0; j < m; ++j) {
                const int ip = indices[j];
                if (ip == ia || ip == ib || ip == ic) continue;
                if (pointInTriangle(a, b, c, polygon[ip])) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            emit(ia, ib, ic);
            indices.erase(indices.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // Fallback: fan triangulation (normalization should prevent this).
            for (size_t i = 1; i + 1 < indices.size(); ++i)
                emit(indices[0], indices[i], indices[i + 1]);
            indices.clear();
            break;
        }
    }
    if (indices.size() == 3) emit(indices[0], indices[1], indices[2]);
    return result;
}

void Body::setOutline(const Poly& localOutline, float density) {
    outline = localOutline;
    convexOutline = isConvexOutline(outline);
    pieces = World::triangulate(outline);
    computeMass(density);
    if (type == BodyType::Static) {
        mass = inertia = 0.0f;
        invMass = invInertia = 0.0f;
    }
    updateAABB();
}

void Body::computeMass(float density) {
    float totalArea = 0.0f;
    Vec2 center{0.0f, 0.0f};
    float inertiaOrigin = 0.0f;

    for (const auto& tri : pieces) {
        if (tri.size() < 3) continue;
        const Vec2& p1 = tri[0];
        const Vec2& p2 = tri[1];
        const Vec2& p3 = tri[2];
        const float area2 = cross(p2 - p1, p3 - p1);
        if (area2 <= 1e-9f) continue;
        const float area = area2 * 0.5f;
        totalArea += area;
        center += (p1 + p2 + p3) * (area / 3.0f);

        const float intx2 = p1.x * p1.x + p2.x * p2.x + p3.x * p3.x +
                            p1.x * p2.x + p2.x * p3.x + p3.x * p1.x;
        const float inty2 = p1.y * p1.y + p2.y * p2.y + p3.y * p3.y +
                            p1.y * p2.y + p2.y * p3.y + p3.y * p1.y;
        inertiaOrigin += (0.25f / 3.0f) * area2 * (intx2 + inty2);
    }

    if (totalArea <= 1e-8f) {
        mass = 1.0f;
        inertia = 1.0f;
        invMass = 1.0f;
        invInertia = 1.0f;
        radius = polygonMaxRadius(outline);
        return;
    }

    center = center / totalArea;
    mass = density * totalArea;
    inertiaOrigin *= density;
    float inertiaCenter = inertiaOrigin - mass * dot(center, center);
    inertiaCenter = std::max(inertiaCenter, mass * 0.002f);

    mass = std::max(mass, 1e-4f);
    inertia = std::max(inertiaCenter, 1e-6f);
    invMass = 1.0f / mass;
    invInertia = 1.0f / inertia;
    radius = polygonMaxRadius(outline);
}

void Body::updateAABB() {
    if (outline.empty()) {
        aabb.lo = position;
        aabb.hi = position;
        return;
    }
    Vec2 lo{FLT_MAX, FLT_MAX};
    Vec2 hi{-FLT_MAX, -FLT_MAX};
    for (const auto& v : outline) {
        const Vec2 w = worldPoint(v);
        lo.x = std::min(lo.x, w.x);
        lo.y = std::min(lo.y, w.y);
        hi.x = std::max(hi.x, w.x);
        hi.y = std::max(hi.y, w.y);
    }
    aabb.lo = lo;
    aabb.hi = hi;
}

bool Body::containsPoint(const Vec2& world) const {
    if (!aabb.contains(world)) return false;
    for (const auto& tri : pieces) {
        const Vec2 a = worldPoint(tri[0]);
        const Vec2 b = worldPoint(tri[1]);
        const Vec2 c = worldPoint(tri[2]);
        if (pointInTriangle(a, b, c, world)) return true;
    }
    return false;
}

Body* World::createBody() {
    auto body = std::make_unique<Body>();
    body->id = (int)storage.size() + 1;
    Body* raw = body.get();
    storage.push_back(std::move(body));
    bodies.push_back(raw);
    return raw;
}

void World::clear() {
    storage.clear();
    bodies.clear();
    joints.clear();
    contacts.clear();
    forceFields.clear();
    previousContacts_.clear();
    sorted_.clear();
    pairs_.clear();
}

void World::step(float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    const int gear = std::max(0, std::min(kMaxSlowMotionGear, slowMotionGear));
    const int substeps = gear > 0 ? gear : 1;
    const float subDt = dt / (float)substeps;
    // Every sub-step advances by dt/gear, while gravity and field forces are
    // divided by the same gear and act over the full nominal frame. The merged
    // result is the normal trajectory with gear-times finer collision sampling,
    // which prevents fast notes from tunnelling through thin shapes.
    const float forceTime = dt;
    const float forceScale = gear > 0 ? 1.0f / (float)gear : 1.0f;
    for (int i = 0; i < substeps; ++i)
        stepInternal(subDt, forceTime, forceScale);
}

void World::stepInternal(float dt, float forceTime, float forceScale) {
    previousContacts_ = contacts;
    contacts.clear();

    // Wake bodies connected to an awake body.
    for (const auto& j : joints) {
        if (j.a && j.b) wakePair(j.a, j.b);
    }

    const float gravityMagnitude = length(gravity);
    for (Body* body : bodies) {
        if (body->type != BodyType::Dynamic || !body->awake) continue;
        const Vec2 bodyGravity =
            body->useGravityDirection ? body->gravityDirection * gravityMagnitude : gravity;
        body->velocity += bodyGravity * (forceScale * forceTime);
        body->velocity = body->velocity * (1.0f / (1.0f + dt * body->linearDamping));
        body->angularVelocity *= 1.0f / (1.0f + dt * body->angularDamping);
        const float speed = length(body->velocity);
        if (speed > kMaxLinearVelocity) body->velocity = body->velocity * (kMaxLinearVelocity / speed);
        body->angularVelocity = clampf(body->angularVelocity, -kMaxAngularVelocity, kMaxAngularVelocity);
    }

    applyForceFields(forceTime, forceScale);

    for (Body* body : bodies) body->updateAABB();

    broadphase();
    narrowphase();
    prepareContacts(dt);
    warmStart();

    applyJointDamping(dt);

    for (int i = 0; i < velocityIterations; ++i) {
        solveJointVelocity();
        solveVelocityConstraints();
    }

    integratePositions(dt);

    for (int i = 0; i < positionIterations; ++i) {
        solveJointPosition();
        solvePositionConstraints();
    }

    updateSleep(dt);

    for (Body* body : bodies) body->updateAABB();
}

void World::applyForceFields(float forceTime, float forceScale) {
    if (forceFields.empty() || forceTime <= 0.0f || forceScale <= 0.0f) return;
    const float gravityMagnitude = length(gravity);
    const float appliedTime = forceScale * forceTime;

    // Returns true when the world point lies inside the field and writes the
    // normalised distance (0 at the centre, 1 on the border).
    auto classify = [](const ForceFieldData& field, const Vec2& worldPoint,
                       float& normalizedDistance) {
        const Vec2 local = rotateInverse(worldPoint - field.position, field.angle);
        if (field.shape == 1) {
            if (field.radius <= 1e-5f) return false;
            normalizedDistance = length(local) / field.radius;
        } else {
            if (field.halfWidth <= 1e-5f || field.halfHeight <= 1e-5f) return false;
            normalizedDistance = std::max(std::fabs(local.x) / field.halfWidth,
                                          std::fabs(local.y) / field.halfHeight);
        }
        return normalizedDistance <= 1.0f;
    };

    // Convex clip volumes in the field's local frame. A rectangle is exact; a
    // circle uses a 32-gon, which is far below the 5 px sampling error the old
    // point-grid buoyancy had.
    std::vector<Poly> clipVolumes(forceFields.size());
    for (size_t i = 0; i < forceFields.size(); ++i) {
        const ForceFieldData& field = forceFields[i];
        if (field.kind != 2) continue;
        Poly& clip = clipVolumes[i];
        if (field.shape == 1) {
            if (field.radius <= 1e-5f) continue;
            const int steps = 32;
            clip.reserve(steps);
            for (int s = 0; s < steps; ++s) {
                const float a = (float)s / (float)steps * TWO_PI;
                clip.push_back({std::cos(a) * field.radius, std::sin(a) * field.radius});
            }
        } else {
            if (field.halfWidth <= 1e-5f || field.halfHeight <= 1e-5f) continue;
            clip.push_back({-field.halfWidth, -field.halfHeight});
            clip.push_back({field.halfWidth, -field.halfHeight});
            clip.push_back({field.halfWidth, field.halfHeight});
            clip.push_back({-field.halfWidth, field.halfHeight});
        }
    }

    for (Body* body : bodies) {
        if (body->type != BodyType::Dynamic) continue;
        auto dampMomentum = [&](float coefficient) {
            if (coefficient <= 0.0f) return;
            // coefficient already carries forceScale; forceTime is the nominal
            // frame so the accumulated sub-step damping matches normal play.
            const float factor = 1.0f / (1.0f + coefficient * forceTime);
            body->velocity = body->velocity * factor;
            body->angularVelocity *= factor;
        };

        // A gravity-change field redirects the gravity that was integrated at
        // the start of this step. The gravity magnitude stays scene-driven, so
        // the field changes direction only. The last overlapping field wins.
        const ForceFieldData* redirect = nullptr;
        for (const auto& field : forceFields) {
            if (field.kind != 4) continue;
            float distance = 0.0f;
            if (!classify(field, body->position, distance)) continue;
            redirect = &field;
        }
        if (redirect) {
            Vec2 direction = normalize(redirect->buoyancyDirection);
            if (lengthSq(direction) > 0.5f) {
                const Vec2 original = body->useGravityDirection
                                          ? body->gravityDirection * gravityMagnitude
                                          : gravity;
                const Vec2 target = direction * gravityMagnitude;
                body->velocity += (target - original) * appliedTime;
                body->wake();
            }
        }

        for (size_t fieldIndex = 0; fieldIndex < forceFields.size(); ++fieldIndex) {
            const ForceFieldData& field = forceFields[fieldIndex];
            if (field.kind == 4) continue; // already handled above
            float normalizedDistance = 0.0f;
            const bool centreInside = classify(field, body->position, normalizedDistance);

            // Buoyancy is a real Archimedes force: the displaced area is
            // integrated by clipping every convex body piece against the fluid
            // volume, then F = fluidDensity * displacedArea * gravity acts at
            // the centroid of that displaced area. The lift direction comes
            // from the field's buoyancy angle. A quadratic drag term
            // Fd = 0.5 * fluidDensity * Cd * submergedArea * |v| * v then
            // removes energy exactly like a real fluid instead of applying an
            // arbitrary constant damping coefficient.
            if (field.kind == 2) {
                if (field.buoyancy <= 0.0f || gravityMagnitude <= 1e-5f) continue;
                const Poly& clip = clipVolumes[fieldIndex];
                if (clip.size() < 3 || body->pieces.empty()) continue;

                Vec2 gravityDir = body->useGravityDirection ? body->gravityDirection
                                                             : normalize(gravity);
                if (lengthSq(gravityDir) < 1e-4f) gravityDir = normalize(gravity);
                if (lengthSq(gravityDir) < 1e-4f) continue;
                Vec2 liftDirection = normalize(field.buoyancyDirection);
                if (lengthSq(liftDirection) < 0.5f) liftDirection = -normalize(gravityDir);

                float displacedArea = 0.0f;
                Vec2 centroidSum{0.0f, 0.0f};
                for (const Poly& tri : body->pieces) {
                    if (tri.size() < 3) continue;
                    Poly local;
                    local.reserve(tri.size());
                    for (const Vec2& vertex : tri) {
                        const Vec2 worldPoint = body->worldPoint(vertex);
                        local.push_back(rotateInverse(worldPoint - field.position,
                                                       field.angle));
                    }
                    const Poly clipped = clipPolygonToConvex(local, clip);
                    if (clipped.size() < 3) continue;
                    const float area = std::fabs(polygonSignedArea(clipped));
                    if (area <= 1e-9f) continue;
                    displacedArea += area;
                    centroidSum += polygonCentroid(clipped) * area;
                }
                if (displacedArea <= 1e-9f) continue;
                const Vec2 localCentroid = centroidSum / displacedArea;
                const Vec2 worldCentroid =
                    field.position + rotate(localCentroid, field.angle);
                const float forceMagnitude =
                    field.buoyancy * displacedArea * gravityMagnitude;
                body->applyImpulse(liftDirection * (forceMagnitude * appliedTime),
                                   worldCentroid);

                const Vec2 centroidVelocity = body->velocityAt(worldCentroid);
                const float speed = length(centroidVelocity);
                if (speed > 1e-5f) {
                    const float dragMagnitude =
                        displacedArea * speed *
                        (0.5f * field.buoyancy * kBuoyancyDragCoefficient * speed +
                         field.buoyancy * kBuoyancyLinearDrag);
                    const Vec2 dragImpulse =
                        centroidVelocity * (-(dragMagnitude * appliedTime / speed));
                    body->applyImpulse(dragImpulse, worldCentroid);
                }
                body->wake();
                continue;
            }

            if (!centreInside) continue;

            if (field.kind == 5) {
                // Vortex applies a tangential acceleration around the field
                // centre. In the Y-up world a clockwise orbit uses the tangent
                // (y, -x), so the signed parameter directly picks the direction:
                // positive = clockwise, negative = counter-clockwise. Like the
                // radial fields the magnitude fades to zero at the border.
                const Vec2 worldDelta = body->position - field.position;
                if (lengthSq(worldDelta) > 1e-8f && std::fabs(field.vortex) > 1e-4f) {
                    const float falloff = clampf(1.0f - normalizedDistance, 0.0f, 1.0f);
                    const Vec2 tangent = normalize(Vec2{worldDelta.y, -worldDelta.x});
                    const Vec2 acceleration = tangent * (field.vortex * falloff);
                    body->velocity += acceleration * appliedTime;
                    body->wake();
                }
                continue;
            }

            if (field.kind == 3) {
                // Drag removes momentum but never adds an acceleration. Its
                // strength is the damping coefficient (1/s).
                dampMomentum(std::max(0.0f, field.strength) * forceScale);
                body->wake();
                continue;
            }

            // Gravity pulls towards the centre, repulsion pushes outwards.
            // Both are strongest at the centre and fade to zero at the rim.
            // Direction is computed in world space so a rotated rectangular
            // field still pulls towards its actual centre.
            const float falloff = clampf(1.0f - normalizedDistance, 0.0f, 1.0f);
            const Vec2 worldDelta = body->position - field.position;
            Vec2 dir = field.kind == 0 ? normalize(-worldDelta) : normalize(worldDelta);
            const Vec2 acceleration = dir * (field.strength * falloff);
            if (lengthSq(acceleration) > 1e-8f) body->velocity += acceleration * appliedTime;
            if (field.kind == 0 && field.damping > 0.0f)
                dampMomentum(field.damping * forceScale);
            body->wake();
        }
        const float speed = length(body->velocity);
        if (speed > kMaxLinearVelocity)
            body->velocity = body->velocity * (kMaxLinearVelocity / speed);
    }
}

void World::broadphase() {
    sorted_.clear();
    sorted_.reserve(bodies.size());
    for (Body* b : bodies) {
        if (b->outline.empty()) continue;
        sorted_.push_back(b);
    }
    std::sort(sorted_.begin(), sorted_.end(), [](const Body* a, const Body* b) {
        return a->aabb.lo.x < b->aabb.lo.x;
    });

    pairs_.clear();
    for (size_t i = 0; i < sorted_.size(); ++i) {
        Body* a = sorted_[i];
        for (size_t j = i + 1; j < sorted_.size(); ++j) {
            Body* b = sorted_[j];
            if (b->aabb.lo.x > a->aabb.hi.x) break;
            if (b->aabb.lo.y > a->aabb.hi.y || b->aabb.hi.y < a->aabb.lo.y) continue;
            if (a->type == BodyType::Static && b->type == BodyType::Static) continue;
            // Ghosted components are decorative: they never collide with
            // anything, not even with static scenery.
            if (a->ghost || b->ghost) continue;
            if ((a->category & b->mask) == 0 || (b->category & a->mask) == 0) continue;
            if (a->group != 0 && a->group == b->group && a->group < 0) continue;
            if (!a->awake && !b->awake) continue;
            pairs_.emplace_back(a, b);
        }
    }
}

void World::narrowphase() {
    const float boundaryToleranceSq = kBoundaryTolerance * kBoundaryTolerance;
    for (const auto& pair : pairs_) {
        Body* a = pair.first;
        Body* b = pair.second;
        // Keep a stable (a, b) order across frames so warm starting can match
        // the previous frame's contacts even when the broadphase order changes.
        if (a->id > b->id) std::swap(a, b);

        const size_t countA = a->convexOutline ? 1 : a->pieces.size();
        const size_t countB = b->convexOutline ? 1 : b->pieces.size();
        int pairContacts = 0;
        for (size_t ia = 0; ia < countA; ++ia) {
            const Poly& pieceA = a->convexOutline ? a->outline : a->pieces[ia];
            for (size_t ib = 0; ib < countB; ++ib) {
                const Poly& pieceB = b->convexOutline ? b->outline : b->pieces[ib];
                Manifold m;
                if (!World::collidePolygons(pieceA, a->position, a->angle,
                                            pieceB, b->position, b->angle, m)) {
                    continue;
                }

                // Convex bodies collide as their full outline, so only concave
                // pairs can produce internal-edge manifolds that need filtering.
                if (!a->convexOutline || !b->convexOutline) {
                    int kept = 0;
                    Vec2 points[2];
                    float separations[2];
                    for (int i = 0; i < m.count; ++i) {
                        const Vec2 point = m.points[i];
                        if (distanceSqToOutline(a->outline, a->localPoint(point)) >
                            boundaryToleranceSq)
                            continue;
                        if (distanceSqToOutline(b->outline, b->localPoint(point)) >
                            boundaryToleranceSq)
                            continue;
                        points[kept] = point;
                        separations[kept] = m.separations[i];
                        ++kept;
                    }
                    if (kept == 0) continue;
                    m.count = kept;
                    for (int i = 0; i < kept; ++i) {
                        m.points[i] = points[i];
                        m.separations[i] = separations[i];
                    }
                }

                Contact contact;
                contact.a = a;
                contact.b = b;
                contact.normal = m.normal;
                contact.count = std::min(m.count, 2);
                contact.friction = std::sqrt(std::max(0.0f, a->friction * b->friction));
                contact.restitution = std::max(a->restitution, b->restitution);

                int keptPoints = 0;
                for (int i = 0; i < contact.count; ++i) {
                    const Vec2 point = m.points[i];
                    bool duplicate = false;
                    for (size_t ci = 0; ci < contacts.size() && !duplicate; ++ci) {
                        const Contact& existing = contacts[ci];
                        if (existing.a != a || existing.b != b) continue;
                        if (dot(existing.normal, m.normal) < 0.9f) continue;
                        for (int ei = 0; ei < existing.count; ++ei) {
                            const Vec2 oldPoint = a->worldPoint(
                                existing.points[ei].localAnchorA);
                            if (lengthSq(oldPoint - point) < 0.02f * 0.02f) {
                                duplicate = true;
                                break;
                            }
                        }
                    }
                    if (duplicate) continue;
                    contact.points[keptPoints].localAnchorA = a->localPoint(point);
                    contact.points[keptPoints].localAnchorB = b->localPoint(point);
                    contact.points[keptPoints].separation = m.separations[i];
                    ++keptPoints;
                }
                if (keptPoints == 0) continue;
                contact.count = keptPoints;
                contacts.push_back(contact);
                wakePair(a, b);
                ++pairContacts;
                if (pairContacts >= kMaxContactsPerPair) break;
            }
            if (pairContacts >= kMaxContactsPerPair) break;
        }
    }
}

void World::prepareContacts(float dt) {
    for (auto& c : contacts) {
        Body* a = c.a;
        Body* b = c.b;
        const float imA = a->invMass;
        const float iiA = a->invInertia;
        const float imB = b->invMass;
        const float iiB = b->invInertia;
        if (imA + iiA + imB + iiB <= 0.0f) continue;

        for (int pi = 0; pi < c.count; ++pi) {
            auto& cp = c.points[pi];
            const Vec2 pa = a->worldPoint(cp.localAnchorA);
            const Vec2 pb = b->worldPoint(cp.localAnchorB);
            const Vec2 ra = pa - a->position;
            const Vec2 rb = pb - b->position;

            const float rnA = cross(ra, c.normal);
            const float rnB = cross(rb, c.normal);
            const float kn = imA + imB + iiA * rnA * rnA + iiB * rnB * rnB;
            cp.normalMass = kn > 1e-9f ? 1.0f / kn : 0.0f;

            const Vec2 tangent = perp(c.normal);
            const float rtA = cross(ra, tangent);
            const float rtB = cross(rb, tangent);
            const float kt = imA + imB + iiA * rtA * rtA + iiB * rtB * rtB;
            cp.tangentMass = kt > 1e-9f ? 1.0f / kt : 0.0f;

            const Vec2 dv = b->velocityAt(pb) - a->velocityAt(pa);
            const float vn = dot(dv, c.normal);
            const float penetration = std::max(0.0f, -cp.separation - kLinearSlop);
            cp.bias = std::min(kBaumgarte / dt * penetration, 4.0f);
            cp.restitutionBias = 0.0f;
            if (vn < -kRestitutionThreshold && c.restitution > 0.0f) {
                cp.restitutionBias = -c.restitution * vn;
            }

            // Warm start from the previous frame's closest point.
            cp.normalImpulse = 0.0f;
            cp.tangentImpulse = 0.0f;
            for (const auto& old : previousContacts_) {
                if (old.a != a || old.b != b) continue;
                for (int oi = 0; oi < old.count; ++oi) {
                    const Vec2 oldPoint = a->worldPoint(old.points[oi].localAnchorA);
                    if (lengthSq(oldPoint - pa) < 0.05f * 0.05f) {
                        cp.normalImpulse = std::max(0.0f, old.points[oi].normalImpulse);
                        cp.tangentImpulse = old.points[oi].tangentImpulse;
                        break;
                    }
                }
            }
        }
    }
}

void World::warmStart() {
    for (auto& c : contacts) {
        if (!c.a->awake && !c.b->awake) continue;
        const Vec2 tangent = perp(c.normal);
        for (int i = 0; i < c.count; ++i) {
            auto& cp = c.points[i];
            const Vec2 P = c.normal * cp.normalImpulse + tangent * cp.tangentImpulse;
            const Vec2 pa = c.a->worldPoint(cp.localAnchorA);
            const Vec2 pb = c.b->worldPoint(cp.localAnchorB);
            c.a->applyImpulse(-P, pa);
            c.b->applyImpulse(P, pb);
        }
    }
}

void World::solveVelocityConstraints() {
    for (auto& c : contacts) {
        Body* a = c.a;
        Body* b = c.b;
        if ((!a->awake && !b->awake)) continue;
        const Vec2 tangent = perp(c.normal);

        for (int i = 0; i < c.count; ++i) {
            auto& cp = c.points[i];
            const Vec2 pa = a->worldPoint(cp.localAnchorA);
            const Vec2 pb = b->worldPoint(cp.localAnchorB);
            const Vec2 ra = pa - a->position;
            const Vec2 rb = pb - b->position;
            Vec2 dv = b->velocityAt(pb) - a->velocityAt(pa);

            // Normal impulse.
            const float vn = dot(dv, c.normal);
            float lambda = (cp.bias + cp.restitutionBias - vn) * cp.normalMass;
            const float oldNormal = cp.normalImpulse;
            cp.normalImpulse = std::max(oldNormal + lambda, 0.0f);
            lambda = cp.normalImpulse - oldNormal;
            const Vec2 Pn = c.normal * lambda;
            a->velocity -= Pn * a->invMass;
            a->angularVelocity -= a->invInertia * cross(ra, Pn);
            b->velocity += Pn * b->invMass;
            b->angularVelocity += b->invInertia * cross(rb, Pn);

            // Friction impulse.
            dv = b->velocityAt(pb) - a->velocityAt(pa);
            const float vt = dot(dv, tangent);
            float lambdaT = -vt * cp.tangentMass;
            const float maxFriction = c.friction * cp.normalImpulse;
            const float oldTangent = cp.tangentImpulse;
            cp.tangentImpulse = clampf(oldTangent + lambdaT, -maxFriction, maxFriction);
            lambdaT = cp.tangentImpulse - oldTangent;
            const Vec2 Pt = tangent * lambdaT;
            a->velocity -= Pt * a->invMass;
            a->angularVelocity -= a->invInertia * cross(ra, Pt);
            b->velocity += Pt * b->invMass;
            b->angularVelocity += b->invInertia * cross(rb, Pt);
        }
    }
}

void World::integratePositions(float dt) {
    for (Body* body : bodies) {
        if (body->type != BodyType::Dynamic || !body->awake) continue;
        body->position += body->velocity * dt;
        body->angle += body->angularVelocity * dt;
    }
}

void World::solvePositionConstraints() {
    for (auto& c : contacts) {
        Body* a = c.a;
        Body* b = c.b;
        if (!a->awake && !b->awake) continue;
        for (int i = 0; i < c.count; ++i) {
            auto& cp = c.points[i];
            const Vec2 pa = a->worldPoint(cp.localAnchorA);
            const Vec2 pb = b->worldPoint(cp.localAnchorB);
            const Vec2 ra = pa - a->position;
            const Vec2 rb = pb - b->position;
            const float separation = dot(pb - pa, c.normal);
            if (separation >= -kLinearSlop) continue;

            const float rnA = cross(ra, c.normal);
            const float rnB = cross(rb, c.normal);
            const float k = a->invMass + b->invMass +
                            a->invInertia * rnA * rnA + b->invInertia * rnB * rnB;
            if (k <= 1e-9f) continue;

            const float correction = std::max(-kMaxLinearCorrection, separation + kLinearSlop);
            const float lambda = -correction * 0.55f / k;
            const Vec2 P = c.normal * lambda;
            a->position -= P * a->invMass;
            a->angle -= a->invInertia * cross(ra, P);
            b->position += P * b->invMass;
            b->angle += b->invInertia * cross(rb, P);
        }
    }
}

void World::applyJointDamping(float dt) {
    if (dt <= 0.0f) return;
    for (const auto& j : joints) {
        if (j.type != JointType::Revolute || j.damping <= 0.0f || !j.b) continue;
        if ((!j.a || !j.a->awake) && !j.b->awake) continue;
        const float invA = j.a ? j.a->invInertia : 0.0f;
        const float invB = j.b->invInertia;
        const float total = invA + invB;
        if (total <= 1e-9f) continue;
        const float relative = j.b->angularVelocity - (j.a ? j.a->angularVelocity : 0.0f);
        const float factor = 1.0f / (1.0f + j.damping * dt);
        const float delta = relative * (1.0f - factor);
        if (j.a) j.a->angularVelocity += delta * (invA / total);
        j.b->angularVelocity -= delta * (invB / total);
    }
}

void World::solveJointVelocity() {
    for (const auto& j : joints) {
        if (!j.b) continue;
        if (!j.a && j.b->type == BodyType::Static) continue;
        if ((!j.a || !j.a->awake) && !j.b->awake) continue;
        PointJoint p1{j.a, j.b, j.localA, j.localB};
        solvePointVelocity(p1);
        if (j.type == JointType::Weld) {
            PointJoint p2{j.a, j.b, j.localA2, j.localB2};
            solvePointVelocity(p2);
        }
    }
}

void World::solveJointPosition() {
    for (const auto& j : joints) {
        if (!j.b) continue;
        if (!j.a && j.b->type == BodyType::Static) continue;
        if ((!j.a || !j.a->awake) && !j.b->awake) continue;
        PointJoint p1{j.a, j.b, j.localA, j.localB};
        solvePointPosition(p1, 0.45f);
        if (j.type == JointType::Weld) {
            PointJoint p2{j.a, j.b, j.localA2, j.localB2};
            solvePointPosition(p2, 0.45f);
        }
    }
}

void World::updateSleep(float dt) {
    for (Body* body : bodies) {
        if (body->type != BodyType::Dynamic) continue;
        if (!body->awake) continue;
        const float speedSq = lengthSq(body->velocity);
        const float angular = std::fabs(body->angularVelocity);
        if (speedSq < 0.0025f && angular < 0.08f) {
            body->sleepTime += dt;
            if (body->sleepTime > 0.75f) {
                body->awake = false;
                body->velocity = {0.0f, 0.0f};
                body->angularVelocity = 0.0f;
            }
        } else {
            body->sleepTime = 0.0f;
        }
    }
}

bool World::collidePolygons(const Poly& polyA, const Vec2& posA, float angleA,
                            const Poly& polyB, const Vec2& posB, float angleB,
                            Manifold& manifold) {
    manifold.count = 0;
    if (polyA.size() < 3 || polyB.size() < 3) return false;

    Poly wa, wb;
    wa.reserve(polyA.size());
    wb.reserve(polyB.size());
    for (const auto& v : polyA) wa.push_back(posA + rotate(v, angleA));
    for (const auto& v : polyB) wb.push_back(posB + rotate(v, angleB));

    int edgeA = 0, edgeB = 0;
    const float sepA = findMaxSeparation(wa, wb, &edgeA);
    if (sepA > 0.0f) return false;
    const float sepB = findMaxSeparation(wb, wa, &edgeB);
    if (sepB > 0.0f) return false;

    const bool refA = sepA > sepB + 1e-4f;
    const Poly& ref = refA ? wa : wb;
    const Poly& inc = refA ? wb : wa;
    const int refIndex = refA ? edgeA : edgeB;

    const Vec2 v1 = ref[refIndex];
    const Vec2 v2 = ref[(refIndex + 1) % ref.size()];
    const Vec2 tangent = normalize(v2 - v1);
    if (lengthSq(tangent) < 0.5f) return false;
    const Vec2 refNormal{tangent.y, -tangent.x};

    Vec2 incident[2];
    computeIncidentEdge(inc, refNormal, incident);

    Vec2 clipped1[2];
    Vec2 clipped2[2];
    const int n1 = clipSegment(incident, clipped1, -tangent, -dot(tangent, v1));
    if (n1 < 2) return false;
    const int n2 = clipSegment(clipped1, clipped2, tangent, dot(tangent, v2));
    if (n2 < 2) return false;

    manifold.normal = refA ? refNormal : -refNormal;
    int count = 0;
    for (int i = 0; i < n2; ++i) {
        const float separation = dot(refNormal, clipped2[i] - v1);
        if (separation <= 0.0f && count < 2) {
            bool duplicate = false;
            for (int k = 0; k < count; ++k) {
                if (lengthSq(manifold.points[k] - clipped2[i]) < 1e-8f) duplicate = true;
            }
            if (duplicate) continue;
            manifold.points[count] = clipped2[i];
            manifold.separations[count] = separation;
            ++count;
        }
    }
    manifold.count = count;
    return count > 0;
}

float pointInPolygonDistance(const Poly& polygon, const Vec2& p) {
    bool inside = false;
    float bestDistSq = FLT_MAX;
    const int n = (int)polygon.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = polygon[i];
        const Vec2& b = polygon[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
        const Vec2 ab = b - a;
        const float t = clampf(dot(p - a, ab) / std::max(1e-9f, lengthSq(ab)), 0.0f, 1.0f);
        const Vec2 closest = a + ab * t;
        bestDistSq = std::min(bestDistSq, lengthSq(p - closest));
    }
    const float d = std::sqrt(bestDistSq);
    return inside ? d : -d;
}

} // namespace ppn