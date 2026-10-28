#pragma once
// PhiPhysiceEditorNeo - self-contained impulse based 2D rigid body engine.
// Supports convex polygon rigid bodies and revolute / two-point weld joints.
// No external physics dependency.

#include "core/Math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ppn {

enum class BodyType { Static, Dynamic };

struct Body {
    int id = 0;
    BodyType type = BodyType::Dynamic;
    Vec2 position;                 // meters
    float angle = 0.0f;
    Vec2 velocity;                 // m/s
    float angularVelocity = 0.0f;  // rad/s

    float mass = 0.0f;
    float invMass = 0.0f;
    float inertia = 0.0f;
    float invInertia = 0.0f;

    float friction = 0.45f;
    float restitution = 0.05f;
    float linearDamping = 0.03f;
    float angularDamping = 0.35f;

    int group = 0; // negative groups never collide with themselves
    uint32_t category = 0x0001u;
    uint32_t mask = 0xFFFFFFFFu;

    std::vector<Poly> pieces; // local convex pieces (triangles)
    Poly outline;             // local outline for rendering and picking
    bool convexOutline = false; // outline is convex and can be used as one piece
    AABB aabb;
    bool awake = true;
    float sleepTime = 0.0f;
    int tag = 0;              // application id (note / floor)
    float radius = 0.0f;      // local bounding radius
    // Per-body gravity direction. The magnitude comes from World::gravity, so
    // changing the scene gravity still affects every note. "None" is {0, 0}.
    bool useGravityDirection = false;
    Vec2 gravityDirection{0.0f, -1.0f};
    // Ghosted bodies stay renderable but are skipped by the broadphase, so
    // they never produce contacts or collision impulses.
    bool ghost = false;

    void setOutline(const Poly& localOutline, float density);
    void computeMass(float density);
    void updateAABB();

    void wake() { awake = true; sleepTime = 0.0f; }
    bool isStatic() const { return type == BodyType::Static; }

    Vec2 worldPoint(const Vec2& local) const {
        return position + rotate(local, angle);
    }
    Vec2 localPoint(const Vec2& world) const {
        return rotateInverse(world - position, angle);
    }
    Vec2 velocityAt(const Vec2& worldPoint) const {
        return velocity + cross(angularVelocity, worldPoint - position);
    }
    void applyImpulse(const Vec2& impulse, const Vec2& point) {
        if (type != BodyType::Dynamic) return;
        velocity += impulse * invMass;
        angularVelocity += invInertia * cross(point - position, impulse);
    }
    void applyForce(const Vec2& force, const Vec2& point) {
        if (type != BodyType::Dynamic) return;
        velocity += force * (invMass); // caller pre-multiplies dt
        angularVelocity += invInertia * cross(point - position, force);
    }
    void applyTorque(float torque) {
        if (type == BodyType::Dynamic) angularVelocity += invInertia * torque;
    }
    bool containsPoint(const Vec2& world) const;
    void setTransform(const Vec2& pos, float ang) {
        position = pos; angle = ang; updateAABB(); wake();
    }
};

struct ContactPoint {
    Vec2 localAnchorA;
    Vec2 localAnchorB;
    float separation = 0.0f;
    float normalImpulse = 0.0f;
    float tangentImpulse = 0.0f;
    float normalMass = 0.0f;
    float tangentMass = 0.0f;
    float bias = 0.0f;
    float restitutionBias = 0.0f;
};

struct Contact {
    Body* a = nullptr;
    Body* b = nullptr;
    Vec2 normal; // from a to b
    ContactPoint points[2];
    int count = 0;
    float friction = 0.0f;
    float restitution = 0.0f;
};

enum class JointType { Revolute, Weld };

struct Joint {
    JointType type = JointType::Revolute;
    int id = 0;
    Body* a = nullptr; // nullptr means world (static)
    Body* b = nullptr;
    Vec2 localA;       // meters, body-local; world point when a == nullptr
    Vec2 localB;
    Vec2 localA2;      // weld second anchor
    Vec2 localB2;
    float referenceAngle = 0.0f;
    bool collideConnected = false;
    // Relative angular velocity damping for revolute hinges (1/s). It removes
    // swing energy without locking the free rotation of the joint.
    float damping = 3.0f;
    int tag = 0;
};

// World-anchored force volume. All units are meters; strength is an
// acceleration (m/s^2) so every note reacts the same regardless of mass.
struct ForceFieldData {
    int id = 0;
    int kind = 0;  // 0 gravity, 1 repulsion, 2 buoyancy, 3 drag, 4 gravity change, 5 vortex
    int shape = 0; // 0 rectangle, 1 circle
    Vec2 position;
    float angle = 0.0f;
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    float radius = 0.0f;
    float strength = 0.0f;            // radial maximum at the centre
    Vec2 buoyancyDirection{0.0f, 1.0f}; // buoyancy lift / gravity-change direction
    // Fluid density for buoyancy fields. Real buoyancy is computed from the
    // displaced area: F = fluidDensity * displacedArea * |gravity|.
    float buoyancy = 0.0f;
    float damping = 0.0f;             // extra momentum damping for gravity fields
    // Tangential acceleration for vortex fields. Positive orbits clockwise,
    // negative counter-clockwise; the magnitude fades to zero on the border.
    float vortex = 0.0f;
};

struct Manifold {
    Vec2 normal; // from a to b
    Vec2 points[2];
    float separations[2] = {0.0f, 0.0f};
    int count = 0;
};

class World {
public:
    Vec2 gravity{0.0f, -9.8f}; // Y-up world: gravity pulls down (negative Y)
    int velocityIterations = 10;
    int positionIterations = 4;
    // Slow-step gear. 0 disables the mode; 5..50 subdivide every step and
    // calculate gravity / force fields at 1/gear strength. The scaled force is
    // applied over the full frame (the gear-multiplied playback), so the
    // trajectory matches normal playback while collisions are sampled at the
    // finer sub-step. Higher gears reduce tunnelling at a higher CPU cost.
    int slowMotionGear = 0;
    static constexpr int kMaxSlowMotionGear = 50;

    std::vector<std::unique_ptr<Body>> storage;
    std::vector<Body*> bodies;
    std::vector<Joint> joints;
    std::vector<Contact> contacts;
    std::vector<ForceFieldData> forceFields;

    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Body* createBody();
    void clear();
    void step(float dt);
    int bodyCount() const { return (int)bodies.size(); }

    // Exposed for self tests and editor hit tests.
    static bool collidePolygons(const Poly& polyA, const Vec2& posA, float angleA,
                                const Poly& polyB, const Vec2& posB, float angleB,
                                Manifold& manifold);
    static std::vector<Poly> triangulate(const Poly& polygon);

private:
    std::vector<Contact> previousContacts_;
    std::vector<Body*> sorted_;
    std::vector<std::pair<Body*, Body*>> pairs_;

    void stepInternal(float dt, float forceTime, float forceScale);
    void applyForceFields(float forceTime, float forceScale);
    void broadphase();
    void narrowphase();
    void prepareContacts(float dt);
    void warmStart();
    void solveVelocityConstraints();
    void integratePositions(float dt);
    void solvePositionConstraints();
    void updateSleep(float dt);
    void applyJointDamping(float dt);
    void solveJointVelocity();
    void solveJointPosition();
};

// Validates and normalizes an outline while preserving its authored origin.
// Returns false when the polygon is degenerate, self-intersecting or has too
// few vertices. Callers that need a centre-of-mass local frame subtract the
// area centroid themselves.
bool normalizePolygon(Poly& vertices, std::string* error = nullptr, int maxVertices = 64);
bool isSimplePolygon(const Poly& polygon);
float pointInPolygonDistance(const Poly& polygon, const Vec2& p); // signed-ish, >0 inside

} // namespace ppn