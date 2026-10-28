#pragma once
// Easing ids and names for the 28 curves in EasingFunctions.h. The evaluator
// receives a normalised progress (0..1) and returns the eased progress.

#include "core/Math.h"
#include "model/EasingFunctions.h"

#include <cmath>

namespace ppn {

inline constexpr int kEasingCount = 28;

inline const char* easingName(int id) {
    static const char* names[kEasingCount] = {
        "线性",         "正弦缓入", "正弦缓出", "二次缓出", "二次缓入",
        "正弦缓入缓出", "二次缓入缓出", "三次缓出", "三次缓入", "四次缓出",
        "四次缓入",     "三次缓入缓出", "四次缓入缓出", "五次缓出", "五次缓入",
        "指数缓出",     "指数缓入", "圆形缓出", "圆形缓入", "回弹缓出",
        "回弹缓入",     "圆形缓入缓出", "回弹缓入缓出", "弹性缓出", "弹性缓入",
        "弹跳缓出",     "弹跳缓入", "弹跳缓入缓出"};
    if (id < 1 || id > kEasingCount) return names[0];
    return names[id - 1];
}

inline float easingProgress(int id, float t) {
    t = clampf(t, 0.0f, 1.0f);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const double value = easingCurve(id, t);
    return std::isfinite(value) ? (float)value : t;
}

} // namespace ppn
