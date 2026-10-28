#pragma once
// Keyframe events authored on a component timeline. Times are measured in
// beats; expressions are evaluated every frame while the simulation plays.

#include <cmath>
#include <string>

namespace ppn {

enum class EventType {
    MoveX = 0,
    MoveY,
    Rotate,
    Width,
    Height,
    Radius,
    Strength,
    Buoyancy,
    Damping,
    Direction,
    // Per-component gravity direction. Only physics blocks and physics rivets
    // expose it; the value is the GravityDirection index (0..8) used by the
    // project file, so `this` starts from the note's authored direction.
    GravityDirection,
    // Linear impulse along an angle. Physics notes and physics rivets expose
    // it. The event value is the total momentum added over the event duration;
    // the separate angle expression selects the direction (0 = up, positive
    // clockwise) and its `this` is the note's current angle.
    DirectionalMomentum,
    // Angular impulse for ordinary physics notes (not rivets). Positive turns
    // clockwise to match the editor's rotation convention.
    RotationalMomentum,
    // Ghost switch. 0 restores collisions, any value >= 0.5 ghosts the
    // component (semi-transparent note / dashed judge line, no collisions).
    // Ghost events are always instantaneous: they snap at their start beat.
    Ghost,
    // Signed tangential acceleration for vortex fields. Positive orbits
    // clockwise, negative counter-clockwise.
    VortexStrength,
    Count
};

inline constexpr int kEventTypeCount = (int)EventType::Count;
inline bool eventTypeIsMomentum(EventType type) {
    return type == EventType::DirectionalMomentum ||
           type == EventType::RotationalMomentum;
}
inline constexpr int kEventTrackCount = 10;
// Project-level timeline subdivision: 4 means quarter-beat snap points.
inline constexpr int kEventSplitDefault = 4;
inline constexpr int kEventSplitMax = 64;

inline float eventSnapBeats(int split) {
    if (split < 1) split = 1;
    if (split > kEventSplitMax) split = kEventSplitMax;
    return 1.0f / (float)split;
}

inline float snapEventBeat(float beat, int split) {
    const float step = eventSnapBeats(split);
    return std::round(beat / step) * step;
}

struct Event {
    int track = 0; // 0..9, left to right
    EventType type = EventType::MoveX;
    float startBeat = 0.0f;
    float endBeat = 1.0f;
    std::string startValue = "this";
    std::string endValue = "this";
    int easing = 1; // Easing curve id, 1..28
    // When true, the event jumps straight to its end state at the start beat;
    // the event duration and easing no longer affect the result.
    bool instant = false;
    // Directional-momentum events only: direction expression in user degrees
    // (0 = up, positive clockwise). `this` is the note's current angle.
    std::string angleValue = "this";
};

} // namespace ppn
