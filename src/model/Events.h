#pragma once
// Event parameter access and expression evaluation shared by the editor, the
// project file and the runtime timeline.

#include "model/Document.h"
#include "model/Event.h"

#include <string>
#include <vector>

namespace ppn {

const char* eventTypeName(EventType type);
const char* eventTypeId(EventType type);
bool eventTypeFromId(const std::string& id, EventType& out);

// Physics notes and rivets also expose the gravity-direction event; static
// scene blocks do not, because the physics world ignores their gravity.
// Ordinary physics notes additionally expose rotational momentum; rivets only
// expose directional momentum.
std::vector<EventType> eventTypesForNote(bool scene, bool rivet = false);
std::vector<EventType> eventTypesForJudge();
std::vector<EventType> eventTypesForField(ForceFieldKind kind);
bool eventTypeAllowedForField(EventType type, ForceFieldKind kind);
// Ghost events are switches, not animated parameters: they always jump at
// their start beat and ignore their duration/easing.
inline bool eventTypeIsInstantOnly(EventType type) { return type == EventType::Ghost; }

float eventValueForNote(const Note& note, EventType type);
void setEventValueForNote(Note& note, EventType type, float value);
float eventValueForJudge(const JudgeLine& judge, EventType type);
void setEventValueForJudge(JudgeLine& judge, EventType type, float value);
float eventValueForField(const ForceField& field, EventType type);
void setEventValueForField(ForceField& field, EventType type, float value);

bool evaluateEventExpression(const std::string& expression, float thisValue, float t,
                             float et, float& out, std::string* error = nullptr);
// Progress of an event at an absolute beat. Instant events report 1.0 as soon
// as they start, so every parameter type finishes in a single step.
float eventProgressAt(const Event& event, float beat);
// Evaluates one of the event's expressions at an absolute beat, using the
// event's easing and instant flag.
bool evaluateEventExpressionAt(const Event& event, const std::string& expression,
                               float thisValue, float beat, float& out,
                               std::string* error = nullptr);
// Evaluates the start -> end interpolation of an event at an absolute beat.
bool evaluateEventValueAt(const Event& event, float thisValue, float beat, float& out,
                          std::string* error = nullptr);
// Applies every event whose start beat has been reached, in chronological order.
void applyEventsToNote(Note& note, float beat, std::string* error = nullptr);
void applyEventsToJudge(JudgeLine& judge, float beat, std::string* error = nullptr);
void applyEventsToField(ForceField& field, float beat, std::string* error = nullptr);

} // namespace ppn
