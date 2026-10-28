#include "model/Events.h"

#include "model/Easing.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ppn {
namespace {

const char* kEventNames[kEventTypeCount] = {
    "X移动", "Y移动", "旋转", "宽度", "高度", "半径", "强度", "流体密度", "阻尼", "方向角度",
    "重力方向", "方位动量施加", "旋转动量施加", "虚化", "涡流强度"};

const char* kEventIds[kEventTypeCount] = {
    "move_x", "move_y", "rotate", "width", "height", "radius", "strength", "buoyancy",
    "damping", "direction", "gravity_direction", "directional_momentum",
    "rotational_momentum", "ghost", "vortex_strength"};

const std::vector<EventType> kBaseNoteEventTypes = {EventType::MoveX, EventType::MoveY,
                                                    EventType::Rotate};

std::string trimText(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

template <typename Getter, typename Setter>
void applyEvents(std::vector<Event>& events, float beat, Getter getter, Setter setter,
                 std::string* error) {
    if (events.empty()) return;
    std::vector<size_t> order(events.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (events[a].startBeat != events[b].startBeat)
            return events[a].startBeat < events[b].startBeat;
        return a < b;
    });

    auto evaluateAt = [&](const Event& event, float atBeat, float thisValue,
                          float& value) {
        std::string expressionError;
        if (!evaluateEventValueAt(event, thisValue, atBeat, value, &expressionError)) {
            if (error && error->empty()) *error = expressionError;
            return false;
        }
        return true;
    };

    // `this` is the parameter value at the event's own start instant. Replay
    // earlier events of the same parameter at that instant so overlapping
    // keyframes stay well-defined regardless of the playhead position.
    std::vector<float> thisValues(events.size(), 0.0f);
    for (size_t step = 0; step < order.size(); ++step) {
        const size_t index = order[step];
        const Event& event = events[index];
        float state = getter(event.type);
        for (size_t previous = 0; previous < step; ++previous) {
            const size_t otherIndex = order[previous];
            const Event& other = events[otherIndex];
            if (other.type != event.type) continue;
            float replayValue = state;
            if (evaluateAt(other, event.startBeat, thisValues[otherIndex], replayValue))
                state = replayValue;
        }
        thisValues[index] = state;
    }

    for (size_t index : order) {
        const Event& event = events[index];
        if (beat < event.startBeat) continue;
        float value = thisValues[index];
        if (evaluateAt(event, beat, thisValues[index], value))
            setter(event.type, value);
    }
}

} // namespace

const char* eventTypeName(EventType type) {
    const int index = (int)type;
    if (index < 0 || index >= kEventTypeCount) return "";
    return kEventNames[index];
}

const char* eventTypeId(EventType type) {
    const int index = (int)type;
    if (index < 0 || index >= kEventTypeCount) return "";
    return kEventIds[index];
}

bool eventTypeFromId(const std::string& id, EventType& out) {
    for (int i = 0; i < kEventTypeCount; ++i) {
        if (id == kEventIds[i]) {
            out = (EventType)i;
            return true;
        }
    }
    return false;
}

std::vector<EventType> eventTypesForNote(bool scene, bool rivet) {
    std::vector<EventType> types = kBaseNoteEventTypes;
    if (!scene) {
        types.push_back(EventType::GravityDirection);
        types.push_back(EventType::DirectionalMomentum);
        if (!rivet) types.push_back(EventType::RotationalMomentum);
    }
    // Tap / Drag / Flick / Hold (physics and scene variants) expose ghosting.
    // Rivets are already non-colliding point helpers, so they keep their
    // original event set.
    if (!rivet) types.push_back(EventType::Ghost);
    return types;
}

std::vector<EventType> eventTypesForJudge() {
    std::vector<EventType> types = kBaseNoteEventTypes;
    types.push_back(EventType::Ghost);
    return types;
}

std::vector<EventType> eventTypesForField(ForceFieldKind kind) {
    std::vector<EventType> types = {EventType::MoveX,   EventType::MoveY, EventType::Rotate,
                                    EventType::Width,   EventType::Height,
                                    EventType::Radius};
    switch (kind) {
        case ForceFieldKind::Gravity:
            types.push_back(EventType::Strength);
            types.push_back(EventType::Damping);
            break;
        case ForceFieldKind::Repulsion:
        case ForceFieldKind::Drag:
            types.push_back(EventType::Strength);
            break;
        case ForceFieldKind::Buoyancy:
            types.push_back(EventType::Buoyancy);
            types.push_back(EventType::Direction);
            break;
        case ForceFieldKind::GravityChange:
            types.push_back(EventType::Direction);
            break;
        case ForceFieldKind::Vortex:
            types.push_back(EventType::VortexStrength);
            break;
    }
    return types;
}

bool eventTypeAllowedForField(EventType type, ForceFieldKind kind) {
    const auto types = eventTypesForField(kind);
    return std::find(types.begin(), types.end(), type) != types.end();
}

float eventValueForNote(const Note& note, EventType type) {
    switch (type) {
        case EventType::MoveX: return note.position.x;
        case EventType::MoveY: return note.position.y;
        case EventType::Rotate: return internalAngleToUserDegrees(note.angle);
        case EventType::GravityDirection: return (float)(int)note.gravity;
        case EventType::Ghost: return note.ghost ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

void setEventValueForNote(Note& note, EventType type, float value) {
    switch (type) {
        case EventType::MoveX: note.position.x = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::MoveY: note.position.y = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::Rotate: note.angle = userDegreesToInternalAngle(value); break;
        case EventType::GravityDirection: {
            // The parameter is the GravityDirection index; interpolation between
            // two directions snaps to the nearest one.
            const float clamped =
                clampf(value, 0.0f, (float)(kGravityDirectionCount - 1));
            note.gravity = (GravityDirection)(int)std::lround(clamped);
            break;
        }
        case EventType::Ghost: note.ghost = value >= 0.5f; break;
        default: break;
    }
}

float eventValueForJudge(const JudgeLine& judge, EventType type) {
    switch (type) {
        case EventType::MoveX: return judge.position.x;
        case EventType::MoveY: return judge.position.y;
        case EventType::Rotate: return internalAngleToUserDegrees(judge.angle);
        case EventType::Ghost: return judge.ghost ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

void setEventValueForJudge(JudgeLine& judge, EventType type, float value) {
    switch (type) {
        case EventType::MoveX: judge.position.x = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::MoveY: judge.position.y = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::Rotate: judge.angle = userDegreesToInternalAngle(value); break;
        case EventType::Ghost: judge.ghost = value >= 0.5f; break;
        default: break;
    }
}

float eventValueForField(const ForceField& field, EventType type) {
    switch (type) {
        case EventType::MoveX: return field.position.x;
        case EventType::MoveY: return field.position.y;
        case EventType::Rotate: return internalAngleToUserDegrees(field.angle);
        case EventType::Width: return field.width;
        case EventType::Height: return field.height;
        case EventType::Radius: return field.radius;
        case EventType::Strength: return field.strength;
        case EventType::Buoyancy: return field.buoyancy;
        case EventType::Damping: return field.damping;
        case EventType::Direction:
            return directionToUserDegrees(field.buoyancyDirection);
        case EventType::VortexStrength: return field.vortex;
        default: return 0.0f;
    }
}

void setEventValueForField(ForceField& field, EventType type, float value) {
    switch (type) {
        case EventType::MoveX: field.position.x = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::MoveY: field.position.y = clampf(value, -10000.0f, 10000.0f); break;
        case EventType::Rotate: field.angle = userDegreesToInternalAngle(value); break;
        case EventType::Width: field.width = clampf(value, 20.0f, 20000.0f); break;
        case EventType::Height: field.height = clampf(value, 20.0f, 20000.0f); break;
        case EventType::Radius: field.radius = clampf(value, 10.0f, 20000.0f); break;
        case EventType::Strength: field.strength = clampf(value, 0.0f, 500.0f); break;
        case EventType::Buoyancy: field.buoyancy = clampf(value, 0.0f, 500.0f); break;
        case EventType::Damping: field.damping = clampf(value, 0.0f, 500.0f); break;
        case EventType::Direction:
            field.buoyancyDirection = userDegreesToDirection(value);
            break;
        case EventType::VortexStrength:
            field.vortex = clampf(value, -500.0f, 500.0f);
            break;
        default: break;
    }
}

bool evaluateEventExpression(const std::string& expression, float thisValue, float t,
                             float et, float& out, std::string* error) {
    const std::string text = trimText(expression);
    if (text.empty() || text == "this") {
        out = thisValue;
        return true;
    }
    const std::vector<NoteVariable> variables = {{"this", "this", thisValue},
                                                 {"t", "t", t},
                                                 {"et", "et", et}};
    float value = thisValue;
    if (!evaluateNoteExpression(text, variables, value, error)) return false;
    if (!std::isfinite(value)) {
        if (error) *error = "结果不是有限数值";
        return false;
    }
    out = value;
    return true;
}

float eventProgressAt(const Event& event, float beat) {
    if (event.instant) return 1.0f;
    const float duration = event.endBeat - event.startBeat;
    return duration > 1e-4f
               ? clampf((beat - event.startBeat) / duration, 0.0f, 1.0f)
               : 1.0f;
}

bool evaluateEventExpressionAt(const Event& event, const std::string& expression,
                               float thisValue, float beat, float& out,
                               std::string* error) {
    const float t = eventProgressAt(event, beat);
    const int easing = std::max(1, std::min(kEasingCount, event.easing));
    return evaluateEventExpression(expression, thisValue, t, easingProgress(easing, t),
                                   out, error);
}

bool evaluateEventValueAt(const Event& event, float thisValue, float beat, float& out,
                          std::string* error) {
    float startValue = thisValue;
    float endValue = thisValue;
    std::string expressionError;
    if (!evaluateEventExpressionAt(event, event.startValue, thisValue, beat, startValue,
                                   &expressionError)) {
        if (error)
            *error = std::string(eventTypeName(event.type)) + " 起始值：" + expressionError;
        return false;
    }
    if (!evaluateEventExpressionAt(event, event.endValue, thisValue, beat, endValue,
                                   &expressionError)) {
        if (error)
            *error = std::string(eventTypeName(event.type)) + " 结束值：" + expressionError;
        return false;
    }
    const float t = eventProgressAt(event, beat);
    const int easing = std::max(1, std::min(kEasingCount, event.easing));
    const float et = easingProgress(easing, t);
    out = startValue * (1.0f - et) + endValue * et;
    if (!std::isfinite(out)) {
        if (error) *error = std::string(eventTypeName(event.type)) + "：结果不是有限数值";
        return false;
    }
    return true;
}

void applyEventsToNote(Note& note, float beat, std::string* error) {
    applyEvents(
        note.events, beat, [&](EventType type) { return eventValueForNote(note, type); },
        [&](EventType type, float value) { setEventValueForNote(note, type, value); }, error);
}

void applyEventsToJudge(JudgeLine& judge, float beat, std::string* error) {
    applyEvents(
        judge.events, beat, [&](EventType type) { return eventValueForJudge(judge, type); },
        [&](EventType type, float value) { setEventValueForJudge(judge, type, value); },
        error);
}

void applyEventsToField(ForceField& field, float beat, std::string* error) {
    applyEvents(
        field.events, beat, [&](EventType type) { return eventValueForField(field, type); },
        [&](EventType type, float value) { setEventValueForField(field, type, value); },
        error);
}

} // namespace ppn
