#pragma once
// Editor application state: note library, document, live physics world,
// selection, camera, tools, modals and input handling.

#include "app/Settings.h"
#include "core/Math.h"
#include "io/ProjectIO.h"
#include "model/Document.h"
#include "model/NoteType.h"
#include "physics/Physics.h"
#include "ui/Ui.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ppn {

class App;
namespace editorui { void draw(App& app); }

enum class Tool { Select, Place, JudgeLine, ForceField };
enum class LeftTab { Library, Projects };
enum class LibrarySection { Physics = 0, Scene = 1, Force = 2 };
enum class ProjectDialogMode { None, SaveAs, New };

struct CameraView {
    Vec2 center{0.0f, 0.0f}; // design px (Y-up)
    float zoom = 0.5f;       // screen px per design px
};

// Numeric text input state shared by the inspector number fields.
struct NumberFieldState {
    uint32_t fieldId = 0;
    uint32_t undoFieldId = 0;
    std::string buffer;
    float value = 0.0f;
    double lastCommitTime = -100.0;
};

// Shape editor draft: the four built in note types plus any custom type that is
// still referenced by the document. Variables and node bindings are edited here.
struct ShapeDraft {
    bool open = false;
    bool editingExisting = false;
    bool scene = false; // editing a component from components/scene
    std::string originalName;
    std::string name = "NewShape";
    Poly vertices;
    float density = 1.0f;
    float friction = 0.45f;
    float restitution = 0.08f;
    Rgb color{125, 179, 247};
    int selectedVertex = 0;
    float zoom = 1.0f;
    Vec2 center{0.0f, 0.0f};
    std::string error;
    bool deleteArmed = false;
    bool holdOriginal = false;
    // Variables declared in the .note file and node bindings that follow them.
    std::vector<NoteVariable> variables;
    std::vector<NodeBinding> bindings;
    int formTab = 0; // 0 = shape/material, 1 = variables and node bindings
    std::string newVariableName = "height";
    std::string newVariableDisplay = "高度";
    float newVariableValue = 0.0f;
    int newBindingVertex = 0;
    std::string newBindingX = "this";
    std::string newBindingY = "this";
};

struct Toast {
    std::string text;
    Color color;
    float age = 0.0f;
    float duration = 3.8f;
};

// Timeline editor for one selected component. Times are stored in beats and
// snapped to 1/4 beat when placing or editing markers.
struct EventEditorState {
    enum class Target { None, Note, Judge, Field };
    enum class DragMode { None, Move, Start, End };
    bool open = false;
    Target target = Target::None;
    int targetId = -1;
    // Primary selection used by the parameter form; selection holds every
    // highlighted event for group drag / copy / delete.
    int selected = -1;
    std::vector<int> selection;
    int activeTrack = 0;
    int pendingTrack = -1;
    float pendingStart = -1.0f;
    float timeStart = 0.0f;
    float beatsVisible = 16.0f;
    std::string error;
    DragMode dragMode = DragMode::None;
    Vec2 dragMouse;
    float dragStartBeat = 0.0f;
    float dragEndBeat = 0.0f;
    int dragTrack = 0;
    bool dragMoved = false;
    bool dragUndoPushed = false;
    // Snapshots for whole-selection moves and start/end adjustments.
    std::vector<int> dragIndices;
    std::vector<Event> dragOriginals;
    // Rubber-band selection.
    bool marqueeActive = false;
    Vec2 marqueeStart;
    std::vector<int> marqueeBase;
};

struct DragState {
    enum class Mode {
        None,
        Notes,
        JudgeLine,
        Field,
        BoxSelect,
        ShapeVertex,
        ShapePan,
        Camera,
        JointAnchor
    };
    Mode mode = Mode::None;
    Vec2 startDesign;
    Vec2 lastDesign;
    std::vector<std::pair<int, Vec2>> noteStart;
    Vec2 judgeStart;
    Vec2 fieldStart;
    Vec2 shapeStartVertex;
    bool moved = false;
    bool undoPushed = false;
    // Hinge endpoint being dragged with the right mouse button.
    int jointId = -1;
    bool jointSideA = false;
    bool jointChanged = false;
    // Camera pan gesture remembers which button started it.
    int cameraButton = 0;
};

class App {
public:
    bool init();
    void update(float dt);
    void draw();

    // Data and physics.
    NoteLibrary library;
    NoteLibrary sceneLibrary;
    ForceFieldLibrary fieldLibrary;
    ComponentDefaults componentDefaults;
    Document doc;
    World world;
    std::filesystem::path rootDir;
    std::filesystem::path componentsDir;
    std::filesystem::path notesDir;   // components/physics
    std::filesystem::path sceneDir;   // components/scene
    std::filesystem::path fieldsDir;  // components/fields
    std::filesystem::path projectsDir;
    std::filesystem::path assetsDir;
    std::filesystem::path fontPath;
    std::filesystem::path projectPath;
    std::filesystem::path settingsPath;
    std::unordered_map<int, Body*> noteBodies;
    std::unordered_map<int, Body*> judgeBodies;
    std::vector<ProjectInfo> projectList;
    std::vector<std::string> missingTypes;

    // Simulation.
    bool paused = true;
    float simTime = 0.0f;
    float accumulator = 0.0f;
    float fixedStep = 1.0f / 120.0f;
    int maxSubSteps = 6;
    bool stepRequested = false;
    // Last beat processed by applyEventsAtTime. Momentum events use it to fire
    // once, or to distribute their total impulse across the elapsed slice.
    float eventSimBeat = -1.0f;
    CameraView camera;

    // Editing.
    Tool tool = Tool::Select;
    std::string selectedTypeName;
    // True while Tool::Place creates a static scene block instead of a note.
    bool placingScene = false;
    std::vector<int> selectedNotes;
    int selectedJudge = -1;
    int selectedField = -1;
    int hoveredNote = -1;
    LeftTab leftTab = LeftTab::Library;
    LibrarySection librarySection = LibrarySection::Physics;
    // Pending force-field placement chosen from the component library.
    ForceFieldKind placingFieldKind = ForceFieldKind::Gravity;
    ForceFieldShape placingFieldShape = ForceFieldShape::Rectangle;
    std::string search;
    bool showGrid = true;
    bool showJoints = true;
    bool showLabels = true;
    bool showWireframe = false;
    bool showContactPoints = false;
    bool showGridLabels = true;
    bool lightMode = false;
    // Colour used for newly placed rivets. Editing a rivet updates it so the
    // placement ghost follows the last rivet style. The radius is global and
    // lives in Settings.
    Rgb rivetPlacementColor{172, 178, 190};

    // Settings window and persistent preferences.
    bool showSettings = false;
    int settingsTab = 0;     // 0 = basic, 1 = appearance, 2 = shortcuts
    int themeColor = 0;      // 0..7 accent palette
    int shortcutCapture = -1; // ShortcutAction index waiting for a key press
    Settings settings;
    EventEditorState eventEditor;

    // Event clipboard shared by the timeline and the copy-to-other flow.
    std::vector<Event> eventClipboard;
    float eventClipboardAnchorBeat = 0.0f;
    int eventClipboardAnchorTrack = 0;
    // Selection inside the copy-to-other overlay. It is deliberately separate
    // from the scene's selectedNotes / selectedJudge / selectedField.
    std::vector<int> eventCopySelectedNotes;
    int eventCopySelectedJudge = -1;
    int eventCopySelectedField = -1;

    // Runtime copy of force fields after applying their timeline events. The
    // authored document stays untouched so editing/saving always uses base values.
    std::vector<ForceField> runtimeFields;
    bool runtimeFieldsValid = false;

    // Modals.
    ProjectDialogMode projectDialog = ProjectDialogMode::None;
    std::string projectNameDraft;
    std::string projectDialogError;
    ShapeDraft shape;

    // Number fields (angle / size / height) used by the inspector.
    std::unordered_map<int, NumberFieldState> inspectorNumbers;
    int inspectorNumberSerial = 1;

    // Toasts and transient UI.
    std::vector<Toast> toasts;
    std::string statusMessage = "就绪";
    float statusMessageAge = 0.0f;
    double armedActionTime = -100.0;
    std::string armedAction;
    bool screenshotMode = false;

    // Interaction state.
    DragState drag;
    Vec2 mouseDesign;
    bool mouseInCanvas = false;
    double lastEmptyClickTime = -100.0;
    Vec2 lastEmptyClickPos;
    bool selectionBoxActive = false;
    Rectangle selectionBoxScreen{};
    bool inspectorUndoPushed = false;
    bool keyboardMoveUndoPushed = false;
    bool keyboardMoveMoved = false;

    // ---- helpers used by the UI ----
    Rectangle canvasRect() const;
    Vec2 designToScreen(const Vec2& p) const;
    Vec2 screenToDesign(const Vec2& s) const;
    Vec2 shapeToScreen(const Vec2& p) const;
    Vec2 shapeScreenToDesign(const Vec2& s) const;

    bool isModalOpen() const;
    bool isPlaying() const { return !paused; }
    NoteType* activeType();
    const NoteType* activeType() const;
    int activeTypeIndex() const;
    Body* bodyForNote(int id);
    Body* bodyForJudge(int id);
    // Note vertices with this note's variable values applied, relative to the
    // note's authored origin.
    Poly noteVertices(const Note& note) const;
    // Area centroid of the rendered outline, in note-origin local design px.
    Vec2 noteCentroid(const Note& note) const;
    // Centre used when creating a hinge: rivets use their own position, notes
    // use the area centroid of their rendered outline.
    Vec2 noteCenterWorld(const Note& note) const;
    // Current visual endpoint of a revolute hinge (document anchor transformed
    // by the live body when the simulation is running).
    Vec2 jointAnchorWorld(const RevJoint& joint, bool sideA) const;
    bool noteContainsDesignPoint(const Note& note, const Vec2& point) const;
    // World position (metres) of the note's centre of gravity for its body.
    Vec2 noteBodyPosition(const Note& note) const;
    // Type lookup depends on whether the instance is a physics or scene block.
    const NoteLibrary& typeLibraryFor(bool scene) const;
    const NoteType* typeForNote(const Note& note) const;
    // Variable value used by a note: its override, otherwise the type default.
    float noteVariableValue(const Note& note, const NoteVariable& variable) const;
    void setNoteVariable(int noteId, const std::string& name, float value);

    void notify(const std::string& text, Color color);
    void notifyInfo(const std::string& text);
    void notifyError(const std::string& text);

    void setTool(Tool t);
    void setSelectedType(const std::string& name);
    // Scene block placement uses the same note geometry file as the matching
    // physics block, but creates a world-fixed static object.
    void selectSceneNote(const std::string& name);
    // Rivets are standalone library components: a dynamic physics rivet or a
    // static scene rivet. Neither participates in collision.
    void selectRivet(bool scene);
    void selectForceFieldPlacement(ForceFieldKind kind, ForceFieldShape shape);
    void cancelPlacement();
    void togglePlay();
    void requestStep();
    void resetSimulation();
    void prepareForEditing();
    void rebuildWorld();
    void refreshLibrary();
    void refreshProjectList();
    void ensureSelectionValid();

    void placeNoteAt(const Vec2& p);
    void addJudgeAt(const Vec2& p);
    void placeForceFieldAt(const Vec2& p);
    // Size/colour used by the judge-line placement preview and by addJudgeAt.
    JudgeLine judgePlacementTemplate() const;
    void deleteSelection();
    void deleteNote(int id);
    void deleteJudge(int id);
    void deleteField(int id);
    void focusSelection();
    void resetCamera();
    void rotateSelection(float deltaRadians);
    void createChain();
    void createWorldAnchor();
    void weldSelection();
    void ungroupSelection();
    void removeSelectionJoints();
    // Inspector edits (push a single undo step while a control is dragged).
    void setNotePosition(int id, const Vec2& position);
    void setNoteAngle(int id, float radians);
    void setJudgeAngle(int id, float radians);
    void setJudgeSize(int id, float width, float height);
    void setJudgeColor(int id, const Rgb& color);
    void setFieldShape(int id, ForceFieldShape shape);
    void setFieldParameters(int id, const ForceField& values);
    void syncWorldForceFields();
    void inspectorEditBegin();

    void undo();
    void redo();
    void saveProjectInteractive();
    void saveProjectAs(const std::string& name);
    void loadProjectFile(const std::filesystem::path& path);
    void createProject(const std::string& name);
    void deleteProjectFile(const std::filesystem::path& path);
    void openProjectDialog(ProjectDialogMode mode);

    void openShapeEditor(int typeIndex, bool scene = false);
    void saveShapeDraft();
    void deleteShapeType();
    void addShapeVertex();
    void deleteSelectedShapeVertex();
    void fitShapeView();
    void addShapeVariable();
    void deleteShapeVariable(int index);
    void addShapeBinding();
    void deleteShapeBinding(int index);
    void setLightMode(bool light, bool persist = true);
    void setThemeColor(int index);
    void saveSettings();
    void resetShortcuts();
    void assignShortcut(ShortcutAction action, const KeyBinding& binding);
    bool shortcutPressed(ShortcutAction action) const;
    void setNoteGravity(int noteId, GravityDirection direction);
    // Applies one gravity direction to every selected physics note/rivet; scene
    // blocks in a mixed selection are ignored because they are world-fixed.
    void setSelectionGravity(GravityDirection direction);
    // Authored ghost state (semi-transparent, no collisions). Ghost events can
    // still toggle the live body while the simulation runs.
    void setNoteGhost(int noteId, bool ghost);
    // Batch version for multi-selection. Rivets are already non-colliding and
    // keep their original appearance, so they are skipped.
    void setSelectionGhost(bool ghost);
    void setJudgeGhost(int judgeId, bool ghost);
    // Rivet colour editing (radius is the global library setting).
    void setRivetColor(int noteId, const Rgb& color);
    void setGlobalRivetRadius(float radius);
    void applyGlobalRivetRadius();
    // Settings > Basic scene controls.
    void setSceneGravity(float gravity);
    void setSolverIterations(int velocity, int position);
    // Slow-step gear: 0 disables it, otherwise forces are divided by the gear.
    void setSlowStepGear(int gear);
    // Playback tempo used by the millisecond / beats read-out.
    void setBpm(float bpm);
    // Event timeline editor for the current selection.
    void openEventEditor();
    void closeEventEditor();
    void handleEventEditorInput();
    void handleEventCopyOverlayInput();
    std::vector<Event>* eventEditorEvents();
    const std::vector<Event>* eventEditorEvents() const;
    void createEventAt(float startBeat, float endBeat, int track);
    void deleteSelectedEvent();
    void copySelectedEvents(bool cut);
    void pasteEventsAtCursor();
    std::vector<Event> clipboardPreviewAt(float beat, int track) const;
    void beginCopyEventsToOther();
    void confirmEventCopyToTargets();
    void cancelEventCopyToTargets();
    bool eventCopyOverlayActive() const { return eventCopyTargetActive; }
    bool isEventCopySource(EventEditorState::Target target, int id) const;
    EventEditorState::Target eventCopySourceComponentTarget() const {
        return eventCopySourceTarget;
    }
    int eventCopySourceComponentId() const { return eventCopySourceId; }
    void setEventSplit(int split);
    // Prefab event export dialog (opened from the top bar).
    bool showExportDialog = false;
    std::string exportSplitInput;
    std::string exportEndInput;
    std::string exportDialogError;
    int exportSplit = 4;
    float exportEndBeat = 4.0f;
    void openExportDialog();
    void closeExportDialog();
    void confirmExportDialog();
    void browseExportDirectory();
    bool exportPrefabEvents();
    void applyEventsAtTime();
    const std::vector<ForceField>& fieldsForDisplay() const;
    // Writes the display toggles shown in Settings > Basic to settings.ini.
    void persistDisplaySettings();
    void applyWindowIcon();

    void syncBodyToNote(int noteId);
    void syncBodyToJudge(int judgeId);
    void markDocumentDirty();

    // Methods used by widgets.
    void setProjectDialogResult(bool accept);

private:
    void updateSimulation(float dt);
    void updateToasts(float dt);
    void handleShortcuts();
    void handleSettingsInput();
    void handleCanvasInput();
    void moveSelectionWithKeyboard(float dt);
    void cycleActiveComponent(int delta);
    void placeActiveComponentAtMouse();
    void duplicateSelectionForDrag();
    bool hasHingeForNote(int id) const;
    bool hasHingeForNotes(const std::vector<int>& ids) const;
    void rebuildHingesForNotes(const std::vector<int>& ids);
    int hitTestHingeAnchor(const Vec2& design, bool& sideA) const;
    void beginHingeAnchorDrag(int jointId, bool sideA);
    void updateHingeAnchorDrag();
    void finishHingeAnchorDrag();
    void handleShapeInput();
    void beginNoteDrag();
    void beginJudgeDrag();
    void beginFieldDrag();
    void updateDrag();
    void finishDrag();
    void cameraZoomAt(const Vec2& screenPos, float factor);
    // Copy-to-other-components mode started from the event editor.
    bool eventCopyTargetActive = false;
    std::vector<Event> eventCopySourceEvents;
    EventEditorState::Target eventCopySourceTarget = EventEditorState::Target::None;
    int eventCopySourceId = -1;
    std::vector<int> eventCopyBoxBase;
    int hitTestNote(const Vec2& design) const;
    int hitTestJudge(const Vec2& design) const;
    int hitTestField(const Vec2& design) const;
    bool hasTypeInDocument(const std::string& name, bool scene) const;
    void validateAndRefreshSelection();
    void ensureRoot();
    void ensureExportRulesFile();
};

} // namespace ppn