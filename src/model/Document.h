#pragma once
// Authoring document: note instances, judge lines, joints and undo history.
// All coordinates are design pixels; angles are radians in memory and degrees
// in the project file.

#include "core/Math.h"
#include "model/Event.h"
#include "model/NoteType.h"

#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ppn {

// The score area every note must stay inside (1350 x 900 design px, centred on
// the origin). It is drawn as a read-only display frame.
inline constexpr float kSceneWidth = 1350.0f;
inline constexpr float kSceneHeight = 900.0f;

// Slow-step simulation is a gear: 0 disables it, 5..50 subdivide the physics
// step while scaling forces down by the same amount. 10 is the default.
inline constexpr int kSlowStepDefault = 10;
inline constexpr int kSlowStepMax = 50;
inline constexpr int kSlowStepStep = 5;

// Direction of the gravity applied to one note. The order is also the project
// file encoding, so existing values stay stable.
enum class GravityDirection {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    UpLeft = 4,
    UpRight = 5,
    DownLeft = 6,
    DownRight = 7,
    None = 8
};
inline constexpr int kGravityDirectionCount = 9;

const char* gravityDirectionName(GravityDirection direction);
Vec2 gravityDirectionVector(GravityDirection direction);

struct Note {
    int id = 0;
    std::string type;
    Vec2 position;   // design px (authored origin; physics uses the area centroid)
    float angle = 0.0f;
    int group = 0;
    // Per-note gravity direction. "Down" matches the scene gravity.
    GravityDirection gravity = GravityDirection::Down;
    // Per-note values for the variables declared by the note type. Names that
    // are absent use the type's default value.
    std::map<std::string, float> variables;
    // Scene block (scenery): the note geometry comes from the scene .note file,
    // but the body is static and stays fixed in the world.
    bool scene = false;
    // Authored ghost state. Ghosted notes render semi-transparent and stop
    // colliding; timeline ghost events can toggle it while the simulation runs.
    bool ghost = false;
    // Rivet-only appearance. Ordinary notes take their outline colour from the
    // .note type instead. Both are saved with the project so every rivet can be
    // customised independently, just like a note shape/colour.
    float radius = 15.0f;
    Rgb color{172, 178, 190};
    // Keyframed parameter events authored on this component's timeline.
    std::vector<Event> events;
};

// Rivets are real placeable components. They use the Note container so they
// share selection/drag/hinge tooling, but they have their own reserved type,
// an editable circular outline (radius + colour) and no collision shape.
inline constexpr const char* kRivetType = "Rivet";
inline bool isRivetNote(const Note& note) { return note.type == kRivetType; }

// The horizontal line notes are judged on. Static for the physics engine and
// not editable on the canvas: it can only be created from the "判定线" tool.
struct JudgeLine {
    int id = 0;
    Vec2 position; // design px
    float angle = 0.0f;
    float width = 4000.0f;
    float height = 5.0f;
    Rgb color{255, 238, 150};
    // Ghosted judge lines keep their colour but draw as dashed strokes and no
    // longer collide. Timeline ghost events can toggle this at runtime.
    bool ghost = false;
    std::vector<Event> events;
};

// Force fields are world-anchored volumes that affect the notes inside them.
// Gravity and repulsion are radial (their strength is highest at the centre);
// buoyancy is a real fluid volume (Archimedes); drag removes momentum without
// adding a force; gravity-change redirects the gravity applied to the note;
// vortex applies a tangential force that orbits notes around its centre.
enum class ForceFieldKind {
    Gravity = 0,
    Repulsion = 1,
    Buoyancy = 2,
    Drag = 3,
    GravityChange = 4,
    Vortex = 5
};
inline constexpr int kForceFieldKindCount = 6;

enum class ForceFieldShape {
    Rectangle = 0,
    Circle = 1
};
inline constexpr int kForceFieldShapeCount = 2;

const char* forceFieldKindName(ForceFieldKind kind);
const char* forceFieldShapeName(ForceFieldShape shape);

struct ForceField {
    int id = 0;
    ForceFieldKind kind = ForceFieldKind::Gravity;
    ForceFieldShape shape = ForceFieldShape::Rectangle;
    Vec2 position;   // design px
    float angle = 0.0f;
    // Placement defaults: a 100 x 100 square or a circle with radius 50.
    float width = 100.0f;  // rectangle design px
    float height = 100.0f; // rectangle design px
    float radius = 50.0f;  // circle design px
    // Radial fields: acceleration (m/s^2) at the centre. Buoyancy fields use
    // buoyancyDirection as their lift direction (default: opposite gravity);
    // gravity-change fields use it as the new gravity arrow direction.
    float strength = 18.0f;
    Vec2 buoyancyDirection{0.0f, 1.0f};
    // Buoyancy fields are a fluid volume. This value is the fluid density in
    // the same kg/m^2 unit as NoteType::density, so Archimedes' formula
    //     F = fluidDensity * displacedArea * gravity
    // decides whether a note rises (lighter than the fluid) or sinks.
    float buoyancy = 1.2f;
    // Extra momentum damping applied by a gravity field (0 = disabled). Drag
    // fields reuse strength as their damping coefficient and never add force.
    float damping = 0.0f;
    // Vortex fields apply a tangential acceleration around the centre. The sign
    // selects the orbit direction in user terms: positive is clockwise, negative
    // is counter-clockwise. The value is the maximum acceleration at the centre
    // and fades to zero on the field border.
    float vortex = 0.0f;
    std::vector<Event> events;
};

// True when a design-space point lies inside a field volume.
bool forceFieldContainsPoint(const ForceField& field, const Vec2& point);

// File-backed field definition. Each file under components/fields describes the
// default volume and strength for one ForceFieldKind.
struct ForceFieldTemplate {
    ForceFieldKind kind = ForceFieldKind::Gravity;
    std::string displayName;
    ForceField defaults;
};

class ForceFieldLibrary {
public:
    std::filesystem::path folder;
    std::vector<ForceFieldTemplate> types;
    std::vector<std::string> errors;

    bool load(const std::filesystem::path& folderPath);
    bool ensureDefaults();
    const ForceFieldTemplate* find(ForceFieldKind kind) const;
};

// Defaults for the non-note components stored under components/.
struct ComponentDefaults {
    float rivetRadius = 15.0f;
    Rgb rivetColor{172, 178, 190};
    JudgeLine judge;
};

bool loadComponentDefaults(const std::filesystem::path& physicsDir,
                           const std::filesystem::path& sceneDir,
                           ComponentDefaults& out);

struct RevJoint {
    int id = 0;
    int noteA = -1;  // -1 means world anchor
    int noteB = 0;
    Vec2 localA;     // design px, note local (world point when noteA == -1)
    Vec2 localB;
    float referenceAngle = 0.0f;
    bool collide = false;
    int chain = 0;
};

struct WeldJoint {
    int id = 0;
    int noteA = 0;
    int noteB = 0;
    Vec2 localA;
    Vec2 localB;
    Vec2 localA2;
    Vec2 localB2;
    int group = 0;
};

struct DocumentState {
    std::vector<Note> notes;
    std::vector<JudgeLine> judges;
    std::vector<ForceField> fields;
    std::vector<RevJoint> revJoints;
    std::vector<WeldJoint> weldJoints;
    int nextNoteId = 1;
    int nextJudgeId = 1;
    int nextFieldId = 1;
    int nextJointId = 1;
    int nextGroupId = 1;
    int nextChainId = 1;
    float gravity = 9.8f;
    int velocityIterations = 10;
    int positionIterations = 4;
    int slowStepGear = kSlowStepDefault;
    int eventSplit = kEventSplitDefault;
};

class Document {
public:
    std::string name = "未命名工程";
    std::vector<Note> notes;
    std::vector<JudgeLine> judges;
    std::vector<ForceField> fields;
    std::vector<RevJoint> revJoints;
    std::vector<WeldJoint> weldJoints;
    int nextNoteId = 1;
    int nextJudgeId = 1;
    int nextFieldId = 1;
    int nextJointId = 1;
    int nextGroupId = 1;
    int nextChainId = 1;
    float gravity = 9.8f;
    int velocityIterations = 10;
    int positionIterations = 4;
    // Global slow-step simulation gear. 0 disables it; values 5..50 subdivide
    // each physics step and divide the force / field strength by the gear. The
    // result keeps the same trajectory while sampling collisions more often.
    // Persisted in the project file.
    int slowStepGear = kSlowStepDefault;
    // Timeline subdivision per beat (4 = quarter-beat snap). Persisted.
    int eventSplit = kEventSplitDefault;
    bool dirty = false;

    void clear();
    // An empty project: no notes, a single judge line at (0, -270) and the
    // default physics settings.
    void makeDefaultScene();

    Note* findNote(int id);
    const Note* findNote(int id) const;
    JudgeLine* findJudge(int id);
    const JudgeLine* findJudge(int id) const;
    ForceField* findField(int id);
    const ForceField* findField(int id) const;

    int addNote(const std::string& type, const Vec2& position, float angle = 0.0f,
                bool scene = false);
    int addJudge(const Vec2& position, float width, float height, float angle = 0.0f,
                 const Rgb& color = {255, 238, 150});
    int addForceField(ForceField field);
    void removeNote(int id);
    void removeJudge(int id);
    void removeField(int id);
    void removeJointsForNote(int id);
    // Renumbers the component list in creation order (1..N) and remaps joints
    // for notes. Returns old -> new id pairs for the caller's own references.
    std::vector<std::pair<int, int>> renumberNotes();
    std::vector<std::pair<int, int>> renumberJudges();
    std::vector<std::pair<int, int>> renumberFields();

    int addRevJoint(int noteA, int noteB, const Vec2& worldAnchor,
                    bool collideConnected, int chainId = 0);
    int addWorldAnchorJoint(int noteB, const Vec2& worldAnchor);
    int addWeldJoint(int noteA, int noteB, int group, const Vec2& p1, const Vec2& p2);

    Vec2 revAnchorWorld(const RevJoint& j, bool sideA) const;

    DocumentState snapshot() const;
    void restore(const DocumentState& state, bool markDirty = true);

    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    void clearHistory();
    void markDirty() { dirty = true; }

    int groupForNote(int id) const;
    void setGroupForNote(int id, int group);
    void ungroup(int group);

private:
    std::deque<DocumentState> undoStack_;
    std::deque<DocumentState> redoStack_;
    static constexpr size_t kMaxHistory = 80;
};

} // namespace ppn