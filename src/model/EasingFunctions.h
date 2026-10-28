#pragma once
// The 28 easing curves offered by the event timeline.
//
// These are written from the standard easing definitions (linear, sine, quad,
// cubic, quart, quint, expo, circ, back, elastic and bounce). Keeping the
// implementation here avoids shipping a third-party easing header whose
// licensing cannot be verified.

#include <cmath>

namespace ppn {
namespace easing_detail {

inline double bounceOut(double t) {
    constexpr double n1 = 7.5625;
    constexpr double d1 = 2.75;
    if (t < 1.0 / d1) return n1 * t * t;
    if (t < 2.0 / d1) {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1) {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}

inline double bounceIn(double t) { return 1.0 - bounceOut(1.0 - t); }

inline double bounceInOut(double t) {
    if (t < 0.5) return (1.0 - bounceOut(1.0 - 2.0 * t)) * 0.5;
    return (1.0 + bounceOut(2.0 * t - 1.0)) * 0.5;
}

} // namespace easing_detail

// id is 1..28; t is normalised progress in [0, 1].
inline double easingCurve(int id, double t) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double c1 = 1.70158;
    constexpr double c2 = c1 * 1.525;
    constexpr double c3 = c1 + 1.0;
    constexpr double c4 = (2.0 * kPi) / 3.0;

    switch (id) {
        case 1: return t;
        case 2: return 1.0 - std::cos(t * kPi * 0.5);
        case 3: return std::sin(t * kPi * 0.5);
        case 4: return 1.0 - (1.0 - t) * (1.0 - t);
        case 5: return t * t;
        case 6: return -(std::cos(kPi * t) - 1.0) * 0.5;
        case 7:
            return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) * 0.5;
        case 8: return 1.0 - std::pow(1.0 - t, 3.0);
        case 9: return t * t * t;
        case 10: return 1.0 - std::pow(1.0 - t, 4.0);
        case 11: return t * t * t * t;
        case 12:
            return t < 0.5 ? 4.0 * t * t * t
                           : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) * 0.5;
        case 13:
            return t < 0.5 ? 8.0 * t * t * t * t
                           : 1.0 - std::pow(-2.0 * t + 2.0, 4.0) * 0.5;
        case 14: return 1.0 - std::pow(1.0 - t, 5.0);
        case 15: return t * t * t * t * t;
        case 16:
            return t >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
        case 17:
            return t <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
        case 18:
            return std::sqrt(1.0 - (t - 1.0) * (t - 1.0));
        case 19:
            return 1.0 - std::sqrt(1.0 - t * t);
        case 20: {
            const double u = t - 1.0;
            return 1.0 + c3 * u * u * u + c1 * u * u;
        }
        case 21:
            return c3 * t * t * t - c1 * t * t;
        case 22: {
            if (t < 0.5) {
                const double u = 2.0 * t;
                return (1.0 - std::sqrt(1.0 - u * u)) * 0.5;
            }
            const double u = -2.0 * t + 2.0;
            return (std::sqrt(1.0 - u * u) + 1.0) * 0.5;
        }
        case 23: {
            if (t < 0.5) {
                const double u = 2.0 * t;
                return 0.5 * u * u * ((c2 + 1.0) * u - c2);
            }
            const double u = 2.0 * t - 2.0;
            return 0.5 * (u * u * ((c2 + 1.0) * u + c2) + 2.0);
        }
        case 24:
            if (t <= 0.0) return 0.0;
            if (t >= 1.0) return 1.0;
            return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * c4) + 1.0;
        case 25:
            if (t <= 0.0) return 0.0;
            if (t >= 1.0) return 1.0;
            return -std::pow(2.0, 10.0 * t - 10.0) *
                   std::sin((t * 10.0 - 10.75) * c4);
        case 26: return easing_detail::bounceOut(t);
        case 27: return easing_detail::bounceIn(t);
        case 28: return easing_detail::bounceInOut(t);
        default: return t;
    }
}

} // namespace ppn
