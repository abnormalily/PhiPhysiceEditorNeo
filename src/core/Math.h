#pragma once
// PhiPhysiceEditorNeo - compact 2D math helpers.
// All physics quantities are in meters; design/UI coordinates are in "design px"
// with PPM design pixels per meter.

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <vector>

#include "raylib.h"

// raylib exports PI/DEG2RAD/RAD2DEG as macros; the engine uses its own
// namespaced constants with identical values.
#ifdef PI
#undef PI
#endif
#ifdef DEG2RAD
#undef DEG2RAD
#endif
#ifdef RAD2DEG
#undef RAD2DEG
#endif

namespace ppn {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float PPM = 100.0f; // design pixels per meter

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator-() const { return {-x, -y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    // Interop with raylib drawing calls.
    operator Vector2() const { return Vector2{x, y}; }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }
inline float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
inline float cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }
inline float cross(const Vec2& a, const Vec2& b, const Vec2& c) {
    return cross(b - a, c - a);
}
// scalar z cross vector
inline Vec2 cross(float s, const Vec2& v) { return {-s * v.y, s * v.x}; }
inline float lengthSq(const Vec2& v) { return dot(v, v); }
inline float length(const Vec2& v) { return std::sqrt(lengthSq(v)); }
inline Vec2 normalize(const Vec2& v) {
    const float len = length(v);
    return len > 1e-8f ? v / len : Vec2{0.0f, 0.0f};
}
inline Vec2 perp(const Vec2& v) { return {-v.y, v.x}; }
inline Vec2 rotate(const Vec2& v, float angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}
inline Vec2 rotateInverse(const Vec2& v, float angle) {
    const float s = std::sin(angle), c = std::cos(angle);
    return {c * v.x + s * v.y, -s * v.x + c * v.y};
}
inline Vec2 lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float signf(float v) { return v < 0.0f ? -1.0f : 1.0f; }
inline bool nearlyZero(float v, float eps = 1e-6f) { return std::fabs(v) < eps; }
inline float safeAcos(float v) { return std::acos(clampf(v, -1.0f, 1.0f)); }
inline float wrapAngle(float a) {
    while (a > PI) a -= TWO_PI;
    while (a < -PI) a += TWO_PI;
    return a;
}

// User-facing rotations use 0 = the component's default orientation, positive
// clockwise and negative counter-clockwise. Direction values (buoyancy,
// gravity-change, direction events) use 0 = up. The physics/editor internals
// keep the usual math convention: 0 = +X, positive counter-clockwise.
inline float wrapDegrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}
inline float internalAngleToUserDegrees(float radians) {
    return -radians * RAD2DEG;
}
inline float userDegreesToInternalAngle(float degrees) {
    return -degrees * DEG2RAD;
}
inline float directionToUserDegrees(const Vec2& direction) {
    if (lengthSq(direction) < 1e-8f) return 0.0f;
    return wrapDegrees(90.0f - std::atan2(direction.y, direction.x) * RAD2DEG);
}
inline Vec2 userDegreesToDirection(float degrees) {
    const float radians = (90.0f - degrees) * DEG2RAD;
    return {std::cos(radians), std::sin(radians)};
}

struct Rot {
    float s = 0.0f;
    float c = 1.0f;
    Rot() = default;
    explicit Rot(float angle) : s(std::sin(angle)), c(std::cos(angle)) {}
    Vec2 apply(const Vec2& v) const { return {c * v.x - s * v.y, s * v.x + c * v.y}; }
    Vec2 applyInverse(const Vec2& v) const { return {c * v.x + s * v.y, -s * v.x + c * v.y}; }
};

struct AABB {
    Vec2 lo{0.0f, 0.0f};
    Vec2 hi{0.0f, 0.0f};
    bool overlaps(const AABB& o) const {
        return !(o.lo.x > hi.x || o.hi.x < lo.x || o.lo.y > hi.y || o.hi.y < lo.y);
    }
    bool contains(const Vec2& p) const {
        return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y;
    }
    Vec2 center() const { return (lo + hi) * 0.5f; }
    Vec2 size() const { return hi - lo; }
};

using Poly = std::vector<Vec2>;

inline float polygonSignedArea(const Poly& p) {
    float a = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5f;
}

inline Vec2 polygonCentroid(const Poly& p) {
    float a = 0.0f;
    Vec2 c{0.0f, 0.0f};
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % p.size()];
        const float d = u.x * v.y - v.x * u.y;
        a += d;
        c += (u + v) * d;
    }
    if (std::fabs(a) < 1e-9f) {
        c = {0.0f, 0.0f};
        for (const auto& v : p) c += v;
        return c / (float)std::max<size_t>(1, p.size());
    }
    return c / (3.0f * a);
}

inline float polygonMaxRadius(const Poly& p) {
    float r = 0.0f;
    for (const auto& v : p) r = std::max(r, length(v));
    return r;
}

inline Vec2 designToMeters(const Vec2& p) { return p / PPM; }
inline Vec2 metersToDesign(const Vec2& p) { return p * PPM; }

} // namespace ppn
