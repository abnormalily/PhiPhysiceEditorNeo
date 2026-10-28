#include "app/App.h"

#include "app/PlatformDialog.h"
#include "model/Events.h"
#include "raylib.h"
#include "ui/Layout.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace ppn {
namespace {

fs::path findDataRoot() {
    std::vector<fs::path> bases;
    // raylib returns the application directory as a native narrow string: ANSI on
    // Windows and UTF-8 elsewhere, which matches std::filesystem::path's narrow
    // constructor on each platform.
    if (const char* appDir = GetApplicationDirectory())
        bases.emplace_back(fs::path(std::string(appDir)));
    bases.push_back(fs::current_path());

    for (const auto& base : bases) {
        fs::path p = base;
        for (int depth = 0; depth < 6; ++depth) {
            std::error_code ec;
            if (fs::exists(p / "components", ec) && fs::exists(p / "assets", ec)) {
                auto canonical = fs::weakly_canonical(p, ec);
                return ec ? p : canonical;
            }
            if (!p.has_parent_path()) break;
            fs::path parent = p.parent_path();
            if (parent == p) break;
            p = parent;
        }
    }
    std::error_code ec;
    fs::path fallback = bases.empty() ? fs::current_path() : bases.front();
    fs::create_directories(fallback / "components", ec);
    return fallback;
}

std::string trimTextCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

void replaceAllText(std::string& text, const std::string& token, const std::string& value) {
    if (token.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
}

std::string unescapeRuleText(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            const char next = raw[++i];
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

struct PrefabExportRules {
    std::string prefix;
    std::string componentPrefix;
    std::string parameter;
    std::string betweenParameters;
    std::string componentSuffix;
    std::string betweenComponents;
    std::string suffix;
    std::vector<std::string> parameters;
};

struct PrefabExportTime {
    std::string year;
    std::string month;
    std::string day;
    std::string hour;
    std::string minute;
    std::string second;
};

struct PrefabExportContext {
    int componentCount = 0;
    int eventCount = 0;
    int index = 0;
    int id = 0;
    std::string type;
    int scene = 0;
    int segmentIndex = 0;
    int segmentNumber = 0;
    int paramIndex = 0;
    std::string param;
    std::string value;
    std::string nextValue;
    int split = 0;
    int splitIndex = 0;
    int nextSplitIndex = 0;
    float beat = 0.0f;
    float nextBeat = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float angle = 0.0f;
    float beats = 0.0f;
    float nextX = 0.0f;
    float nextY = 0.0f;
    float nextAngle = 0.0f;
    float nextBeats = 0.0f;
    PrefabExportTime time;
};

PrefabExportRules loadPrefabExportRules(const std::filesystem::path& path) {
    PrefabExportRules rules;
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return rules;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trimTextCopy(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trimTextCopy(trimmed.substr(0, equals));
        const std::string value = trimmed.substr(equals + 1);
        if (key == "prefix") rules.prefix = value;
        else if (key == "component_prefix") rules.componentPrefix = value;
        else if (key == "parameter") rules.parameter = value;
        else if (key == "between_parameters") rules.betweenParameters = value;
        else if (key == "component_suffix") rules.componentSuffix = value;
        else if (key == "between_components") rules.betweenComponents = value;
        else if (key == "suffix") rules.suffix = value;
        else if (key == "parameters") {
            rules.parameters.clear();
            std::string item;
            std::stringstream stream(value);
            while (std::getline(stream, item, ',')) {
                item = trimTextCopy(item);
                if (!item.empty()) rules.parameters.push_back(item);
            }
        }
    }
    return rules;
}

std::string expandPrefabTemplate(const std::string& raw, const PrefabExportContext& ctx) {
    std::string out = unescapeRuleText(raw);
    const std::string beat = ui::format("%.3f", ctx.beat);
    const std::string nextBeat = ui::format("%.3f", ctx.nextBeat);
    replaceAllText(out, "{componentCount}", std::to_string(ctx.componentCount));
    replaceAllText(out, "{eventCount}", std::to_string(ctx.eventCount));
    replaceAllText(out, "{index}", std::to_string(ctx.index));
    replaceAllText(out, "{id}", std::to_string(ctx.id));
    replaceAllText(out, "{type}", ctx.type);
    replaceAllText(out, "{scene}", std::to_string(ctx.scene));
    replaceAllText(out, "{segmentIndex}", std::to_string(ctx.segmentIndex));
    replaceAllText(out, "{segmentNumber}", std::to_string(ctx.segmentNumber));
    replaceAllText(out, "{paramIndex}", std::to_string(ctx.paramIndex));
    replaceAllText(out, "{paramType}", std::to_string(ctx.paramIndex));
    replaceAllText(out, "{param}", ctx.param);
    replaceAllText(out, "{value}", ctx.value);
    replaceAllText(out, "{nextValue}", ctx.nextValue);
    replaceAllText(out, "{split}", std::to_string(ctx.split));
    replaceAllText(out, "{beatWhole}", std::to_string(ctx.splitIndex / ctx.split));
    replaceAllText(out, "{beatPart}", std::to_string(ctx.splitIndex % ctx.split));
    replaceAllText(out, "{nextBeatWhole}",
                   std::to_string(ctx.nextSplitIndex / ctx.split));
    replaceAllText(out, "{nextBeatPart}",
                   std::to_string(ctx.nextSplitIndex % ctx.split));
    replaceAllText(out, "{splitIndex}", std::to_string(ctx.splitIndex));
    replaceAllText(out, "{splitNumber}", std::to_string(ctx.splitIndex + 1));
    replaceAllText(out, "{beat}", beat);
    replaceAllText(out, "{beats}", beat);
    replaceAllText(out, "{nextBeat}", nextBeat);
    replaceAllText(out, "{nextBeats}", nextBeat);
    replaceAllText(out, "{x}", ui::format("%.3f", ctx.x));
    replaceAllText(out, "{y}", ui::format("%.3f", ctx.y));
    replaceAllText(out, "{angle}", ui::format("%.3f", ctx.angle));
    replaceAllText(out, "{beats}", ui::format("%.3f", ctx.beats));
    replaceAllText(out, "{nextX}", ui::format("%.3f", ctx.nextX));
    replaceAllText(out, "{nextY}", ui::format("%.3f", ctx.nextY));
    replaceAllText(out, "{nextAngle}", ui::format("%.3f", ctx.nextAngle));
    replaceAllText(out, "{nextBeats}", ui::format("%.3f", ctx.nextBeats));
    replaceAllText(out, "{year}", ctx.time.year);
    replaceAllText(out, "{month}", ctx.time.month);
    replaceAllText(out, "{day}", ctx.time.day);
    replaceAllText(out, "{hour}", ctx.time.hour);
    replaceAllText(out, "{minute}", ctx.time.minute);
    replaceAllText(out, "{second}", ctx.time.second);
    return out;
}

Vec2 midpoint(const Vec2& a, const Vec2& b) { return (a + b) * 0.5f; }

struct DisjointSet {
    std::unordered_map<int, int> parent;
    void add(int x) { if (!parent.count(x)) parent[x] = x; }
    int find(int x) {
        add(x);
        int root = x;
        while (parent[root] != root) root = parent[root];
        while (parent[x] != root) {
            int next = parent[x];
            parent[x] = root;
            x = next;
        }
        return root;
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[b] = a;
    }
};

} // namespace

Rectangle App::canvasRect() const { return layout::canvas(); }

// Design coordinates are Y-up: positive Y is higher on screen. Screen space is
// Y-down, so every projection flips the vertical axis.
Vec2 App::designToScreen(const Vec2& p) const {
    const Rectangle c = canvasRect();
    return Vec2{c.x + c.width * 0.5f + (p.x - camera.center.x) * camera.zoom,
                c.y + c.height * 0.5f - (p.y - camera.center.y) * camera.zoom};
}

Vec2 App::screenToDesign(const Vec2& s) const {
    const Rectangle c = canvasRect();
    return Vec2{camera.center.x + (s.x - (c.x + c.width * 0.5f)) / camera.zoom,
                camera.center.y - (s.y - (c.y + c.height * 0.5f)) / camera.zoom};
}

Vec2 App::shapeToScreen(const Vec2& p) const {
    const Rectangle c = layout::shapeCanvas();
    return Vec2{c.x + c.width * 0.5f + (p.x - shape.center.x) * shape.zoom,
                c.y + c.height * 0.5f - (p.y - shape.center.y) * shape.zoom};
}

Vec2 App::shapeScreenToDesign(const Vec2& s) const {
    const Rectangle c = layout::shapeCanvas();
    return Vec2{shape.center.x + (s.x - (c.x + c.width * 0.5f)) / shape.zoom,
                shape.center.y - (s.y - (c.y + c.height * 0.5f)) / shape.zoom};
}

bool App::isModalOpen() const {
    return shape.open || showSettings || eventEditor.open || showExportDialog ||
           projectDialog != ProjectDialogMode::None;
}

bool App::init() {
    ensureRoot();
    ensureExportRulesFile();
    loadComponentDefaults(notesDir, sceneDir, componentDefaults);
    settingsPath = rootDir / "settings.ini";
    const bool settingsFileExists = fs::exists(settingsPath);
    Settings loaded;
    if (loadSettings(settingsPath, loaded)) settings = loaded;
    if (!settingsFileExists) settings.rivetRadius = componentDefaults.rivetRadius;
    rivetPlacementColor = componentDefaults.rivetColor;
    lightMode = settings.lightMode;
    themeColor = std::max(0, std::min(kAccentCount - 1, settings.accent));
    showGrid = settings.showGrid;
    showJoints = settings.showJoints;
    showLabels = settings.showLabels;
    showWireframe = settings.showWireframe;
    showContactPoints = settings.showContactPoints;
    showGridLabels = settings.showGridLabels;
    theme::setLightMode(lightMode);
    theme::setAccentColor(themeColor);
    applyWindowIcon();

    library.folder = notesDir;
    library.load(notesDir);
    sceneLibrary.folder = sceneDir;
    sceneLibrary.load(sceneDir);
    fieldLibrary.folder = fieldsDir;
    fieldLibrary.ensureDefaults();
    fieldLibrary.load(fieldsDir);

    if (!library.types.empty()) selectedTypeName = library.types.front().name;

    // The editor always starts on an empty stage: one judge line at (0, -270).
    doc.makeDefaultScene();
    if (!doc.judges.empty()) {
        doc.judges.front().width = componentDefaults.judge.width;
        doc.judges.front().height = componentDefaults.judge.height;
        doc.judges.front().color = componentDefaults.judge.color;
    }
    doc.gravity = settings.gravity;
    doc.velocityIterations = settings.velocityIterations;
    doc.positionIterations = settings.positionIterations;
    doc.slowStepGear = settings.slowStepGear;
    applyGlobalRivetRadius();
    projectPath.clear();

    rebuildWorld();
    paused = true;
    simTime = 0.0f;
    accumulator = 0.0f;
    resetCamera();

    refreshProjectList();
    const int componentErrors = (int)(library.errors.size() + sceneLibrary.errors.size() +
                                      fieldLibrary.errors.size());
    if (componentErrors > 0)
        notifyError(ui::format("有 %d 个原件文件未能载入", componentErrors));
    return true;
}

void App::ensureRoot() {
    rootDir = findDataRoot();
    componentsDir = rootDir / "components";
    notesDir = componentsDir / "physics";
    sceneDir = componentsDir / "scene";
    fieldsDir = componentsDir / "fields";
    projectsDir = rootDir / "projects";
    assetsDir = rootDir / "assets";
    // Bundled derivative font (SIL OFL 1.1); the legacy names are still
    // accepted so an old installation keeps working after an in-place update.
    const fs::path fontCandidates[] = {
        assetsDir / "fonts" / "PhiPhysiceNeoSans-Regular.ttf",
        assetsDir / "fonts" / "NotoSansSC-Regular.ttf",
        assetsDir / "fonts" / "NotoSansSC-Regular.otf",
    };
    std::error_code fontEc;
    fontPath.clear();
    for (const auto& candidate : fontCandidates) {
        if (fs::exists(candidate, fontEc)) {
            fontPath = candidate;
            break;
        }
    }
    if (fontPath.empty()) fontPath = fontCandidates[0];
    std::error_code ec;
    fs::create_directories(notesDir, ec);
    fs::create_directories(sceneDir, ec);
    fs::create_directories(fieldsDir, ec);
    fs::create_directories(projectsDir, ec);
    // Remove the legacy empty notes folder once it is no longer in use.
    const fs::path legacyNotes = rootDir / "notes";
    if (fs::is_directory(legacyNotes, ec) && fs::is_empty(legacyNotes, ec))
        fs::remove(legacyNotes, ec);
}

const NoteLibrary& App::typeLibraryFor(bool scene) const {
    return scene ? sceneLibrary : library;
}

const NoteType* App::typeForNote(const Note& note) const {
    if (isRivetNote(note)) return nullptr;
    return typeLibraryFor(note.scene).find(note.type);
}

NoteType* App::activeType() {
    if (selectedTypeName.empty()) return nullptr;
    return placingScene ? sceneLibrary.find(selectedTypeName) : library.find(selectedTypeName);
}
const NoteType* App::activeType() const {
    if (selectedTypeName.empty()) return nullptr;
    return placingScene ? sceneLibrary.find(selectedTypeName) : library.find(selectedTypeName);
}
int App::activeTypeIndex() const {
    const NoteLibrary& source = placingScene ? sceneLibrary : library;
    for (size_t i = 0; i < source.types.size(); ++i)
        if (source.types[i].name == selectedTypeName) return (int)i;
    return -1;
}

Body* App::bodyForNote(int id) {
    auto it = noteBodies.find(id);
    return it == noteBodies.end() ? nullptr : it->second;
}
Body* App::bodyForJudge(int id) {
    auto it = judgeBodies.find(id);
    return it == judgeBodies.end() ? nullptr : it->second;
}

Poly App::noteVertices(const Note& note) const {
    if (isRivetNote(note)) {
        // Rivets use a reserved type and their own editable circular outline.
        // They are real library components rather than decorations attached to
        // notes, so the radius comes from the instance.
        Poly points;
        const float radius = clampf(note.radius, 2.0f, 500.0f);
        points.reserve(32);
        for (int i = 0; i < 32; ++i) {
            const float angle = (float)i / 32.0f * TWO_PI;
            points.push_back({std::cos(angle) * radius, std::sin(angle) * radius});
        }
        return points;
    }
    const NoteType* type = typeForNote(note);
    if (!type) return {};
    return type->renderVertices(note.variables);
}

Vec2 App::noteCentroid(const Note& note) const {
    const Poly vertices = noteVertices(note);
    if (vertices.size() < 3) return {0.0f, 0.0f};
    return polygonCentroid(vertices);
}

Vec2 App::noteCenterWorld(const Note& note) const {
    if (isRivetNote(note)) return note.position;
    return note.position + rotate(noteCentroid(note), note.angle);
}

Vec2 App::jointAnchorWorld(const RevJoint& joint, bool sideA) const {
    if (sideA && joint.noteA < 0) return joint.localA;
    const int noteId = sideA ? joint.noteA : joint.noteB;
    const Note* note = doc.findNote(noteId);
    if (!note) return sideA ? joint.localA : joint.localB;
    const Vec2 documentAnchor = sideA ? joint.localA : joint.localB;
    auto bodyIt = noteBodies.find(noteId);
    if (bodyIt != noteBodies.end() && bodyIt->second) {
        const Vec2 centroid = noteCentroid(*note);
        return bodyIt->second->worldPoint((documentAnchor - centroid) / PPM) * PPM;
    }
    return note->position + rotate(documentAnchor, note->angle);
}

bool App::noteContainsDesignPoint(const Note& note, const Vec2& point) const {
    const Poly local = noteVertices(note);
    if (local.size() < 3) return false;
    const Vec2 inNote = rotateInverse(point - note.position, note.angle);
    return pointInPolygonDistance(local, inNote) >= 0.0f;
}

Vec2 App::noteBodyPosition(const Note& note) const {
    // The document stores the authored origin; rigid bodies are integrated
    // around their area centroid, so offset the body by the local centroid.
    return (note.position + rotate(noteCentroid(note), note.angle)) / PPM;
}

float App::noteVariableValue(const Note& note, const NoteVariable& variable) const {
    const auto it = note.variables.find(variable.name);
    return it == note.variables.end() ? variable.value : it->second;
}

void App::setNoteVariable(int noteId, const std::string& name, float value) {
    Note* note = doc.findNote(noteId);
    if (!note) return;
    const NoteType* type = typeForNote(*note);
    if (!type || !type->findVariable(name)) return;
    inspectorEditBegin();
    note->variables[name] = std::isfinite(value) ? value : 0.0f;
    paused = true;
    simTime = 0.0f;
    // Node bindings can move every vertex, so rebuild the note body.
    rebuildWorld();
    doc.dirty = true;
}

void App::notify(const std::string& text, Color color) {
    toasts.push_back(Toast{text, color, 0.0f, 3.8f});
    if (toasts.size() > 5) toasts.erase(toasts.begin());
    statusMessage = text;
    statusMessageAge = 0.0f;
}
void App::notifyInfo(const std::string& text) { notify(text, theme::textSoft); }
void App::notifyError(const std::string& text) { notify(text, theme::danger); }

void App::setTool(Tool t) {
    tool = t;
    if (t != Tool::Place) placingScene = false;
}
void App::setSelectedType(const std::string& name) {
    if (!library.find(name)) return;
    selectedTypeName = name;
    placingScene = false;
    librarySection = LibrarySection::Physics;
    tool = Tool::Place;
}

void App::selectSceneNote(const std::string& name) {
    if (!sceneLibrary.find(name)) return;
    selectedTypeName = name;
    placingScene = true;
    librarySection = LibrarySection::Scene;
    tool = Tool::Place;
    notifyInfo(ui::format("场景块 %s（静态，固定在世界中） · 按 %s 放置", name.c_str(),
                          bindingLabel(settings.shortcuts[(int)ShortcutAction::PlaceComponent])
                              .c_str()));
}

void App::selectRivet(bool scene) {
    selectedTypeName = kRivetType;
    placingScene = scene;
    librarySection = scene ? LibrarySection::Scene : LibrarySection::Physics;
    tool = Tool::Place;
    notifyInfo(ui::format("%s铆钉（不参与碰撞） · 按 %s 放置",
                          scene ? "场景" : "物理",
                          bindingLabel(settings.shortcuts[(int)ShortcutAction::PlaceComponent])
                              .c_str()));
}

void App::selectForceFieldPlacement(ForceFieldKind kind, ForceFieldShape shape) {
    (void)shape;
    placingFieldKind = kind;
    // The component library only offers rectangles. A placed field can be
    // switched to a circle in the inspector afterwards.
    placingFieldShape = ForceFieldShape::Rectangle;
    placingScene = false;
    librarySection = LibrarySection::Force;
    tool = Tool::ForceField;
    notifyInfo(ui::format("%s · 按 %s 放置", forceFieldKindName(kind),
                          bindingLabel(settings.shortcuts[(int)ShortcutAction::PlaceComponent])
                              .c_str()));
}

void App::cancelPlacement() {
    const bool wasPlacing = tool == Tool::Place || tool == Tool::JudgeLine ||
                            tool == Tool::ForceField;
    tool = Tool::Select;
    placingScene = false;
    if (wasPlacing) notifyInfo("已退出放置模式");
}

void App::togglePlay() {
    if (paused) {
        paused = false;
        stepRequested = false;
        accumulator = 0.0f;
        for (Body* b : world.bodies) {
            if (b->type == BodyType::Dynamic) {
                b->awake = true;
                b->sleepTime = 0.0f;
            }
        }
        notifyInfo("模拟运行中");
    } else {
        paused = true;
        notifyInfo("已暂停");
    }
}

void App::requestStep() {
    paused = true;
    stepRequested = true;
}

void App::resetSimulation() {
    paused = true;
    stepRequested = false;
    simTime = 0.0f;
    accumulator = 0.0f;
    rebuildWorld();
}

void App::prepareForEditing() {
    if (!paused) {
        paused = true;
        notifyInfo("编辑时已自动暂停");
    }
    if (simTime > 1e-4f) {
        simTime = 0.0f;
        accumulator = 0.0f;
        rebuildWorld();
        notifyInfo("已回到编辑布局");
    }
}

void App::rebuildWorld() {
    world.clear();
    if (simTime <= 1e-4f) eventSimBeat = -1.0f;
    runtimeFieldsValid = false;
    runtimeFields.clear();
    // The scene is Y-up, so gravity pulls towards negative Y.
    world.gravity = {0.0f, -clampf(doc.gravity, 0.0f, 60.0f)};
    world.velocityIterations = std::max(4, std::min(20, doc.velocityIterations));
    world.positionIterations = std::max(2, std::min(10, doc.positionIterations));
    world.slowMotionGear = std::max(0, std::min(kSlowStepMax, doc.slowStepGear));
    noteBodies.clear();
    judgeBodies.clear();
    missingTypes.clear();

    // Judge lines are static bodies: notes bounce on them but they never move.
    for (const auto& j : doc.judges) {
        Body* body = world.createBody();
        body->type = BodyType::Static;
        body->position = j.position / PPM;
        body->angle = j.angle;
        const float hw = std::max(10.0f, j.width * 0.5f) / PPM;
        const float hh = std::max(0.5f, j.height * 0.5f) / PPM;
        Poly box{{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        body->setOutline(box, 1.0f);
        body->friction = 0.72f;
        body->restitution = 0.02f;
        body->tag = j.id;
        body->ghost = j.ghost;
        body->awake = false;
        body->category = 0x0002u;
        body->mask = 0xFFFFFFFFu;
        body->updateAABB();
        judgeBodies[j.id] = body;
    }

    // Notes that are connected by a non-colliding joint should not collide with
    // each other. Union them and give every component a negative collision group.
    DisjointSet dsu;
    for (const auto& n : doc.notes) dsu.add(n.id);
    for (const auto& j : doc.revJoints) {
        if (!j.collide && j.noteA >= 0 && j.noteB >= 0) dsu.unite(j.noteA, j.noteB);
    }
    for (const auto& j : doc.weldJoints) dsu.unite(j.noteA, j.noteB);
    std::unordered_map<int, int> componentSize;
    for (const auto& n : doc.notes) componentSize[dsu.find(n.id)]++;
    std::unordered_map<int, int> groupOfRoot;
    int nextGroup = 1;
    for (const auto& [root, count] : componentSize) {
        if (count > 1) groupOfRoot[root] = -(nextGroup++);
    }

    // Bodies are integrated around their area centroid. The document keeps the
    // authored origin (for Hold that is the bottom centre), so remember where
    // each note's centroid sits in its local frame for joint conversion.
    std::unordered_map<int, Vec2> noteCenters;
    for (const auto& n : doc.notes) {
        const bool rivet = isRivetNote(n);
        const NoteType* type = typeForNote(n);
        if (!type && !rivet) {
            if (std::find(missingTypes.begin(), missingTypes.end(), n.type) == missingTypes.end())
                missingTypes.push_back(n.type);
            continue;
        }
        const Poly rendered = noteVertices(n);
        const Vec2 center = rendered.size() >= 3 ? polygonCentroid(rendered) : Vec2{0.0f, 0.0f};
        noteCenters[n.id] = center;
        Poly local;
        local.reserve(rendered.size());
        for (const auto& v : rendered) local.push_back((v - center) / PPM);

        Body* body = world.createBody();
        // Scene blocks and scene rivets stay fixed in the world. Physics rivets
        // are dynamic point connectors, but neither rivet type collides.
        body->type = n.scene ? BodyType::Static : BodyType::Dynamic;
        body->position = (n.position + rotate(center, n.angle)) / PPM;
        body->angle = n.angle;
        body->setOutline(local, rivet ? 1.0f : type->density);
        body->linearDamping = 0.02f;
        body->angularDamping = 0.55f;
        body->tag = n.id;
        body->ghost = n.ghost;
        if (rivet) {
            body->friction = 0.4f;
            body->restitution = 0.0f;
            body->linearDamping = 0.45f;
            body->angularDamping = 0.9f;
            body->useGravityDirection = !n.scene;
            if (!n.scene) body->gravityDirection = gravityDirectionVector(n.gravity);
            body->category = 0x0000u;
            body->mask = 0x0000u;
            body->awake = !n.scene;
        } else {
            body->friction = type->friction;
            body->restitution = type->restitution;
            if (n.scene) {
                body->useGravityDirection = false;
                body->category = 0x0002u;
                body->awake = false;
            } else {
                body->useGravityDirection = true;
                body->gravityDirection = gravityDirectionVector(n.gravity);
                body->category = 0x0001u;
                auto git = groupOfRoot.find(dsu.find(n.id));
                body->group = git == groupOfRoot.end() ? 0 : git->second;
            }
            body->mask = 0xFFFFFFFFu;
        }
        body->updateAABB();
        noteBodies[n.id] = body;
    }

    // Document joints store anchors relative to the authored note origin; body
    // local frames are centred on the area centroid.
    auto bodyLocal = [&](int noteId, const Vec2& anchor) {
        const auto it = noteCenters.find(noteId);
        return (it == noteCenters.end() ? anchor : anchor - it->second) / PPM;
    };

    for (const auto& j : doc.revJoints) {
        Body* a = j.noteA >= 0 ? bodyForNote(j.noteA) : nullptr;
        Body* b = bodyForNote(j.noteB);
        if (!b) continue;
        if (j.noteA >= 0 && !a) continue;
        const Note* noteA = j.noteA >= 0 ? doc.findNote(j.noteA) : nullptr;
        const Note* noteB = doc.findNote(j.noteB);
        if (!noteB) continue;

        // A hinge is drawn between the two note attachment points. The physical
        // pin sits halfway between them whenever both sides belong to notes, so
        // moving/rotating one note refreshes the constraint instead of leaving a
        // stale pin behind. A world-anchored hinge keeps its fixed world point.
        Vec2 pivot;
        if (!noteA) {
            pivot = j.localA;
        } else {
            const Vec2 anchorA = noteA->position + rotate(j.localA, noteA->angle);
            const Vec2 anchorB = noteB->position + rotate(j.localB, noteB->angle);
            // A hinge attached to a rivet pivots at that rivet's centre; two
            // ordinary notes share a pin halfway between their anchor points.
            if (isRivetNote(*noteA)) pivot = anchorA;
            else if (isRivetNote(*noteB)) pivot = anchorB;
            else pivot = (anchorA + anchorB) * 0.5f;
        }
        const Vec2 localA = noteA ? rotateInverse(pivot - noteA->position, noteA->angle)
                                  : pivot;
        const Vec2 localB = rotateInverse(pivot - noteB->position, noteB->angle);

        Joint joint;
        joint.type = JointType::Revolute;
        joint.id = j.id;
        joint.a = a;
        joint.b = b;
        joint.localA = noteA ? bodyLocal(j.noteA, localA) : localA / PPM;
        joint.localB = bodyLocal(j.noteB, localB);
        joint.referenceAngle = j.referenceAngle;
        joint.collideConnected = j.collide;
        joint.damping = 3.0f;
        joint.tag = j.id;
        world.joints.push_back(joint);
    }

    for (const auto& j : doc.weldJoints) {
        Body* a = bodyForNote(j.noteA);
        Body* b = bodyForNote(j.noteB);
        if (!a || !b) continue;
        Joint joint;
        joint.type = JointType::Weld;
        joint.id = j.id;
        joint.a = a;
        joint.b = b;
        joint.localA = bodyLocal(j.noteA, j.localA);
        joint.localB = bodyLocal(j.noteB, j.localB);
        joint.localA2 = bodyLocal(j.noteA, j.localA2);
        joint.localB2 = bodyLocal(j.noteB, j.localB2);
        joint.tag = j.id;
        world.joints.push_back(joint);
    }

    for (Body* body : world.bodies) {
        body->velocity = {0.0f, 0.0f};
        body->angularVelocity = 0.0f;
        body->awake = true;
        body->sleepTime = 0.0f;
        body->updateAABB();
    }

    syncWorldForceFields();
}

void App::syncWorldForceFields() {
    world.forceFields.clear();
    for (const auto& f : fieldsForDisplay()) {
        ForceFieldData w;
        w.id = f.id;
        w.kind = (int)f.kind;
        w.shape = (int)f.shape;
        w.position = f.position / PPM;
        w.angle = f.angle;
        w.halfWidth = std::max(10.0f, f.width * 0.5f) / PPM;
        w.halfHeight = std::max(10.0f, f.height * 0.5f) / PPM;
        w.radius = std::max(10.0f, f.radius) / PPM;
        w.strength = f.strength;
        Vec2 dir = normalize(f.buoyancyDirection);
        w.buoyancyDirection = lengthSq(dir) > 0.5f ? dir : Vec2{0.0f, 1.0f};
        w.buoyancy = f.buoyancy;
        w.damping = f.damping;
        w.vortex = f.vortex;
        world.forceFields.push_back(w);
    }
}

void App::refreshLibrary() {
    library.load(notesDir);
    sceneLibrary.load(sceneDir);
    fieldLibrary.ensureDefaults();
    fieldLibrary.load(fieldsDir);
    if (!activeType()) {
        const NoteLibrary& source = placingScene ? sceneLibrary : library;
        selectedTypeName = source.types.empty() ? "" : source.types.front().name;
    }
    rebuildWorld();
}

void App::refreshProjectList() { projectList = listProjects(projectsDir); }

void App::ensureSelectionValid() {
    selectedNotes.erase(std::remove_if(selectedNotes.begin(), selectedNotes.end(),
                                       [&](int id) { return !doc.findNote(id); }),
                        selectedNotes.end());
    if (selectedJudge >= 0 && !doc.findJudge(selectedJudge)) selectedJudge = -1;
    if (selectedField >= 0 && !doc.findField(selectedField)) selectedField = -1;
}

void App::placeNoteAt(const Vec2& p) {
    const bool rivet = selectedTypeName == kRivetType;
    const NoteType* type = activeType();
    if (!type && !rivet) {
        notifyError("请先在左侧选择一个原件");
        return;
    }
    prepareForEditing();
    doc.pushUndo();
    const Vec2 snapped{std::round(p.x), std::round(p.y)};
    const std::string typeName = rivet ? std::string(kRivetType) : type->name;
    doc.addNote(typeName, snapped, 0.0f, placingScene);
    doc.renumberNotes();
    const int id = doc.notes.empty() ? -1 : doc.notes.back().id;
    if (rivet) {
        if (Note* placed = doc.findNote(id)) {
            placed->radius = clampf(settings.rivetRadius, 2.0f, 500.0f);
            placed->color = rivetPlacementColor;
        }
    }
    selectedNotes = {id};
    selectedJudge = -1;
    selectedField = -1;
    paused = true;
    rebuildWorld();
    doc.dirty = true;
    if (rivet) {
        notifyInfo(ui::format("已放置%s铆钉  #%d（不参与碰撞）",
                              placingScene ? "场景" : "物理", id));
    } else if (placingScene) {
        notifyInfo(ui::format("已放置场景块 %s  #%d（固定在世界中）",
                              type->name.c_str(), id));
    } else {
        notifyInfo(ui::format("已放置 %s  #%d", type->name.c_str(), id));
    }
}

JudgeLine App::judgePlacementTemplate() const {
    // When a judge line is selected its current width/height/angle/colour drive
    // the placement preview, so the ghost always matches what will be created.
    if (const JudgeLine* selected = doc.findJudge(selectedJudge)) return *selected;
    JudgeLine line = componentDefaults.judge;
    line.position = {0.0f, 0.0f};
    line.angle = 0.0f;
    return line;
}

void App::addJudgeAt(const Vec2& p) {
    prepareForEditing();
    doc.pushUndo();
    const JudgeLine t = judgePlacementTemplate();
    doc.addJudge({std::round(p.x), std::round(p.y)}, t.width, t.height, t.angle,
                 t.color);
    doc.renumberJudges();
    const int id = doc.judges.empty() ? -1 : doc.judges.back().id;
    selectedJudge = id;
    selectedNotes.clear();
    selectedField = -1;
    paused = true;
    rebuildWorld();
    doc.dirty = true;
    notifyInfo(ui::format("已放置判定线  #%d", id));
}

void App::placeForceFieldAt(const Vec2& p) {
    prepareForEditing();
    doc.pushUndo();
    ForceField field;
    if (const ForceFieldTemplate* type = fieldLibrary.find(placingFieldKind))
        field = type->defaults;
    field.kind = placingFieldKind;
    field.shape = placingFieldShape;
    field.position = {std::round(p.x), std::round(p.y)};
    field.angle = 0.0f;
    doc.addForceField(field);
    doc.renumberFields();
    const int id = doc.fields.empty() ? -1 : doc.fields.back().id;
    selectedField = id;
    selectedNotes.clear();
    selectedJudge = -1;
    paused = true;
    rebuildWorld();
    doc.dirty = true;
    notifyInfo(ui::format("已放置 %s（%s） #%d", forceFieldKindName(field.kind),
                          forceFieldShapeName(field.shape), id));
}

void App::deleteSelection() {
    if (selectedNotes.empty() && selectedJudge < 0 && selectedField < 0) return;
    prepareForEditing();
    doc.pushUndo();
    const int noteCount = (int)selectedNotes.size();
    for (int id : selectedNotes) doc.removeNote(id);
    if (selectedJudge >= 0) doc.removeJudge(selectedJudge);
    if (selectedField >= 0) doc.removeField(selectedField);
    const int judgeCount = selectedJudge >= 0 ? 1 : 0;
    const int fieldCount = selectedField >= 0 ? 1 : 0;
    selectedNotes.clear();
    selectedJudge = -1;
    selectedField = -1;
    rebuildWorld();
    doc.dirty = true;
    notifyInfo(ui::format("已删除 %d 个对象 / %d 条判定线 / %d 个力场（Ctrl+Z 撤销）",
                          noteCount, judgeCount, fieldCount));
}

void App::deleteNote(int id) {
    if (!doc.findNote(id)) return;
    selectedNotes = {id};
    selectedJudge = -1;
    deleteSelection();
}
void App::deleteJudge(int id) {
    if (!doc.findJudge(id)) return;
    selectedJudge = id;
    selectedNotes.clear();
    selectedField = -1;
    deleteSelection();
}

void App::deleteField(int id) {
    if (!doc.findField(id)) return;
    selectedField = id;
    selectedNotes.clear();
    selectedJudge = -1;
    deleteSelection();
}

void App::focusSelection() {
    Vec2 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 hi{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    bool any = false;
    auto include = [&](const Body* body) {
        if (!body) return;
        any = true;
        lo.x = std::min(lo.x, body->aabb.lo.x * PPM);
        lo.y = std::min(lo.y, body->aabb.lo.y * PPM);
        hi.x = std::max(hi.x, body->aabb.hi.x * PPM);
        hi.y = std::max(hi.y, body->aabb.hi.y * PPM);
    };
    if (!selectedNotes.empty()) {
        for (int id : selectedNotes) include(bodyForNote(id));
    } else if (selectedJudge >= 0) {
        include(bodyForJudge(selectedJudge));
    } else if (const ForceField* field = doc.findField(selectedField)) {
        const float hw = field->shape == ForceFieldShape::Circle ? field->radius
                                                                 : field->width * 0.5f;
        const float hh = field->shape == ForceFieldShape::Circle ? field->radius
                                                                 : field->height * 0.5f;
        any = true;
        lo.x = field->position.x - hw;
        lo.y = field->position.y - hh;
        hi.x = field->position.x + hw;
        hi.y = field->position.y + hh;
    } else {
        for (const auto& n : doc.notes) include(bodyForNote(n.id));
        for (const auto& j : doc.judges) include(bodyForJudge(j.id));
        for (const auto& f : doc.fields) {
            const float hw = f.shape == ForceFieldShape::Circle ? f.radius : f.width * 0.5f;
            const float hh = f.shape == ForceFieldShape::Circle ? f.radius : f.height * 0.5f;
            any = true;
            lo.x = std::min(lo.x, f.position.x - hw);
            lo.y = std::min(lo.y, f.position.y - hh);
            hi.x = std::max(hi.x, f.position.x + hw);
            hi.y = std::max(hi.y, f.position.y + hh);
        }
    }
    if (!any) {
        resetCamera();
        return;
    }
    const Rectangle c = canvasRect();
    const float w = std::max(120.0f, hi.x - lo.x + 180.0f);
    const float h = std::max(120.0f, hi.y - lo.y + 180.0f);
    camera.zoom = clampf(std::min(c.width / w, c.height / h), 0.04f, 3.0f);
    camera.center = (lo + hi) * 0.5f;
    notifyInfo("视图已聚焦");
}

void App::resetCamera() {
    camera.center = {0.0f, 0.0f};
    // The editor opens zoomed out far enough to show the whole 1350 x 900
    // reference frame on the default 16:9 window.
    camera.zoom = 0.5f;
}

void App::rotateSelection(float deltaRadians) {
    if (selectedNotes.empty() && selectedJudge < 0 && selectedField < 0) return;
    prepareForEditing();
    doc.pushUndo();

    std::vector<int> validIds;
    for (int id : selectedNotes) {
        if (doc.findNote(id)) validIds.push_back(id);
    }
    if (validIds.size() > 1) {
        // Multi-selection rotates as one composition: every note angle changes
        // and every authored origin orbits the centre of the selected notes.
        Vec2 pivot{0.0f, 0.0f};
        for (int id : validIds) pivot += doc.findNote(id)->position;
        pivot = pivot / (float)validIds.size();
        for (int id : validIds) {
            Note* n = doc.findNote(id);
            n->position = pivot + rotate(n->position - pivot, deltaRadians);
            n->angle += deltaRadians;
            syncBodyToNote(id);
        }
    } else {
        for (int id : validIds) {
            Note* n = doc.findNote(id);
            n->angle += deltaRadians;
            syncBodyToNote(id);
        }
    }
    if (selectedJudge >= 0) {
        JudgeLine* j = doc.findJudge(selectedJudge);
        if (j) {
            j->angle += deltaRadians;
            syncBodyToJudge(selectedJudge);
        }
    }
    if (selectedField >= 0) {
        if (ForceField* f = doc.findField(selectedField)) {
            f->angle += deltaRadians;
            syncWorldForceFields();
        }
    }
    if (hasHingeForNotes(validIds)) rebuildWorld();
    doc.dirty = true;
}

void App::createChain() {
    if (selectedNotes.empty()) return;
    prepareForEditing();
    doc.pushUndo();
    if (selectedNotes.size() == 1) {
        const int id = selectedNotes.front();
        Note* n = doc.findNote(id);
        if (n) {
            doc.addWorldAnchorJoint(id, noteCenterWorld(*n));
            notifyInfo("已从铆钉添加世界锚点");
        }
    } else {
        const int chain = doc.nextChainId++;
        int created = 0;
        for (size_t i = 0; i + 1 < selectedNotes.size(); ++i) {
            Note* a = doc.findNote(selectedNotes[i]);
            Note* b = doc.findNote(selectedNotes[i + 1]);
            if (!a || !b) continue;
            const Vec2 anchorA = noteCenterWorld(*a);
            const Vec2 anchorB = noteCenterWorld(*b);
            const int jointId = doc.addRevJoint(a->id, b->id, anchorA, false, chain);
            if (jointId == 0) continue;
            // Keep both rivet attachment points in the document. The physics
            // pin is derived from their midpoint in rebuildWorld().
            for (auto& joint : doc.revJoints) {
                if (joint.id == jointId) {
                    joint.localB = rotateInverse(anchorB - b->position, b->angle);
                    break;
                }
            }
            ++created;
        }
        notifyInfo(ui::format("已为 %d 个对象创建 %d 条铰链", (int)selectedNotes.size(),
                              created));
    }
    rebuildWorld();
    doc.dirty = true;
}

void App::createWorldAnchor() {
    if (selectedNotes.empty()) return;
    prepareForEditing();
    doc.pushUndo();
    for (int id : selectedNotes) {
        Note* n = doc.findNote(id);
        if (n) doc.addWorldAnchorJoint(id, noteCenterWorld(*n));
    }
    rebuildWorld();
    doc.dirty = true;
    notifyInfo("已固定到世界空间");
}

bool App::hasHingeForNote(int id) const {
    for (const auto& joint : doc.revJoints) {
        if (joint.noteA == id || joint.noteB == id) return true;
    }
    return false;
}

bool App::hasHingeForNotes(const std::vector<int>& ids) const {
    for (int id : ids) {
        if (hasHingeForNote(id)) return true;
    }
    return false;
}

void App::rebuildHingesForNotes(const std::vector<int>& ids) {
    if (!hasHingeForNotes(ids)) return;
    // Rebuilding re-derives every hinge pin from the current rivet/anchor
    // positions, which repairs connected hinges after a note is moved.
    rebuildWorld();
}

void App::moveSelectionWithKeyboard(float dt) {
    if (tool != Tool::Select || ui::isEditingText()) {
        keyboardMoveUndoPushed = false;
        keyboardMoveMoved = false;
        return;
    }
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool super = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    Vec2 direction{0.0f, 0.0f};
    if (!ctrl && !alt && !super) {
        if (IsKeyDown(KEY_A)) direction.x -= 1.0f;
        if (IsKeyDown(KEY_D)) direction.x += 1.0f;
        if (IsKeyDown(KEY_S)) direction.y -= 1.0f;
        if (IsKeyDown(KEY_W)) direction.y += 1.0f;
    }
    const bool hasSelection = !selectedNotes.empty() || selectedJudge >= 0 || selectedField >= 0;
    if (direction == Vec2{0.0f, 0.0f} || !hasSelection) {
        if (keyboardMoveMoved) {
            std::vector<int> ids = selectedNotes;
            rebuildHingesForNotes(ids);
            keyboardMoveMoved = false;
        }
        keyboardMoveUndoPushed = false;
        return;
    }

    if (!keyboardMoveUndoPushed) {
        prepareForEditing();
        doc.pushUndo();
        keyboardMoveUndoPushed = true;
    }
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const float speed = (shift ? 720.0f : 240.0f) * dt;
    const Vec2 delta = direction * speed;
    for (int id : selectedNotes) {
        Note* n = doc.findNote(id);
        if (!n) continue;
        n->position += delta;
        syncBodyToNote(id);
    }
    if (selectedJudge >= 0) {
        if (JudgeLine* j = doc.findJudge(selectedJudge)) {
            j->position += delta;
            syncBodyToJudge(selectedJudge);
        }
    }
    if (selectedField >= 0) {
        if (ForceField* f = doc.findField(selectedField)) {
            f->position += delta;
            syncWorldForceFields();
        }
    }
    doc.dirty = true;
    keyboardMoveMoved = true;
}

void App::cycleActiveComponent(int delta) {
    enum class ItemKind { PhysicsNote, PhysicsRivet, SceneNote, SceneRivet, Judge, Field };
    struct Item {
        ItemKind kind = ItemKind::PhysicsNote;
        std::string noteName;
        std::string label;
        ForceFieldKind field = ForceFieldKind::Gravity;
    };

    // 1 / 3 cycle inside the section that is currently being placed or viewed:
    // physical blocks, scene blocks, or force fields. They no longer always
    // switch through the physics list.
    LibrarySection section = librarySection;
    if (tool == Tool::ForceField) {
        section = LibrarySection::Force;
    } else if (tool == Tool::JudgeLine) {
        section = LibrarySection::Scene;
    } else if (tool == Tool::Place) {
        section = placingScene ? LibrarySection::Scene : LibrarySection::Physics;
    }

    std::vector<Item> items;
    if (section == LibrarySection::Force) {
        const ForceFieldKind kinds[] = {ForceFieldKind::Gravity, ForceFieldKind::Repulsion,
                                        ForceFieldKind::Buoyancy, ForceFieldKind::Drag,
                                        ForceFieldKind::GravityChange, ForceFieldKind::Vortex};
        for (ForceFieldKind kind : kinds) {
            Item item;
            item.kind = ItemKind::Field;
            item.label = forceFieldKindName(kind);
            item.field = kind;
            items.push_back(item);
        }
    } else if (section == LibrarySection::Scene) {
        for (const auto& type : sceneLibrary.types) {
            Item item;
            item.kind = ItemKind::SceneNote;
            item.noteName = type.name;
            item.label = type.name;
            items.push_back(item);
        }
        Item rivet;
        rivet.kind = ItemKind::SceneRivet;
        rivet.label = "场景铆钉";
        items.push_back(rivet);
        Item judge;
        judge.kind = ItemKind::Judge;
        judge.label = "判定线";
        items.push_back(judge);
    } else {
        for (const auto& type : library.types) {
            Item item;
            item.kind = ItemKind::PhysicsNote;
            item.noteName = type.name;
            item.label = type.name;
            items.push_back(item);
        }
        Item rivet;
        rivet.kind = ItemKind::PhysicsRivet;
        rivet.label = "铆钉";
        items.push_back(rivet);
    }
    if (items.empty()) return;

    int current = -1;
    for (int i = 0; i < (int)items.size(); ++i) {
        const Item& item = items[(size_t)i];
        switch (item.kind) {
            case ItemKind::PhysicsNote:
                if (tool == Tool::Place && !placingScene && selectedTypeName == item.noteName)
                    current = i;
                break;
            case ItemKind::SceneNote:
                if (tool == Tool::Place && placingScene && selectedTypeName == item.noteName)
                    current = i;
                break;
            case ItemKind::PhysicsRivet:
                if (tool == Tool::Place && !placingScene && selectedTypeName == kRivetType)
                    current = i;
                break;
            case ItemKind::SceneRivet:
                if (tool == Tool::Place && placingScene && selectedTypeName == kRivetType)
                    current = i;
                break;
            case ItemKind::Judge:
                if (tool == Tool::JudgeLine) current = i;
                break;
            case ItemKind::Field:
                if (tool == Tool::ForceField && placingFieldKind == item.field) current = i;
                break;
        }
        if (current >= 0) break;
    }
    if (current < 0) current = delta > 0 ? -1 : 0;
    const int total = (int)items.size();
    int index = (current + delta) % total;
    if (index < 0) index += total;
    const Item& chosen = items[(size_t)index];

    const std::string placeKey =
        bindingLabel(settings.shortcuts[(int)ShortcutAction::PlaceComponent]);
    switch (chosen.kind) {
        case ItemKind::PhysicsNote:
            selectedTypeName = chosen.noteName;
            placingScene = false;
            librarySection = LibrarySection::Physics;
            tool = Tool::Place;
            break;
        case ItemKind::SceneNote:
            selectedTypeName = chosen.noteName;
            placingScene = true;
            librarySection = LibrarySection::Scene;
            tool = Tool::Place;
            break;
        case ItemKind::PhysicsRivet:
            selectedTypeName = kRivetType;
            placingScene = false;
            librarySection = LibrarySection::Physics;
            tool = Tool::Place;
            break;
        case ItemKind::SceneRivet:
            selectedTypeName = kRivetType;
            placingScene = true;
            librarySection = LibrarySection::Scene;
            tool = Tool::Place;
            break;
        case ItemKind::Judge:
            placingScene = false;
            librarySection = LibrarySection::Scene;
            tool = Tool::JudgeLine;
            break;
        case ItemKind::Field:
            placingFieldKind = chosen.field;
            placingFieldShape = ForceFieldShape::Rectangle;
            placingScene = false;
            librarySection = LibrarySection::Force;
            tool = Tool::ForceField;
            break;
    }
    const char* sectionName = section == LibrarySection::Force
                                  ? "力场"
                                  : (section == LibrarySection::Scene ? "场景块" : "物理块");
    notifyInfo(ui::format("%s %d/%d：%s · 按 %s 放置", sectionName, index + 1, total,
                          chosen.label.c_str(), placeKey.c_str()));
}

void App::placeActiveComponentAtMouse() {
    if (isModalOpen() || ui::isEditingText()) return;
    if (!ui::mouseIn(canvasRect()) || ui::mouseConsumed()) return;
    const Vec2 position = screenToDesign(ui::mouse());
    mouseDesign = position;
    if (tool == Tool::JudgeLine) {
        addJudgeAt(position);
        return;
    }
    if (tool == Tool::ForceField) {
        placeForceFieldAt(position);
        return;
    }
    if (selectedTypeName != kRivetType && !activeType()) {
        notifyError("请先在左侧选择一个原件");
        return;
    }
    if (tool != Tool::Place) tool = Tool::Place;
    placeNoteAt(position);
}

void App::duplicateSelectionForDrag() {
    if (selectedNotes.empty()) return;
    prepareForEditing();
    doc.pushUndo();
    std::vector<int> duplicates;
    duplicates.reserve(selectedNotes.size());
    for (int id : selectedNotes) {
        const Note* source = doc.findNote(id);
        if (!source) continue;
        Note copy = *source;
        copy.id = doc.nextNoteId++;
        doc.notes.push_back(copy);
        duplicates.push_back(copy.id);
    }
    if (duplicates.empty()) return;
    const auto mapping = doc.renumberNotes();
    std::unordered_map<int, int> ids(mapping.begin(), mapping.end());
    for (int& id : duplicates) {
        const auto it = ids.find(id);
        if (it != ids.end()) id = it->second;
    }
    selectedNotes = std::move(duplicates);
    selectedJudge = -1;
    selectedField = -1;
    rebuildWorld();
    doc.dirty = true;
}

int App::hitTestHingeAnchor(const Vec2& design, bool& sideA) const {
    if (!showJoints) return -1;
    const Vec2 mouseScreen = designToScreen(design);
    int bestJoint = -1;
    float bestDistance = 10.0f;
    bool bestSideA = false;
    for (const auto& joint : doc.revJoints) {
        const Vec2 a = jointAnchorWorld(joint, true);
        const Vec2 b = jointAnchorWorld(joint, false);
        const float da = length(designToScreen(a) - mouseScreen);
        const float db = length(designToScreen(b) - mouseScreen);
        if (da < bestDistance) {
            bestDistance = da;
            bestJoint = joint.id;
            bestSideA = true;
        }
        if (db < bestDistance) {
            bestDistance = db;
            bestJoint = joint.id;
            bestSideA = false;
        }
    }
    sideA = bestSideA;
    return bestJoint;
}

void App::beginHingeAnchorDrag(int jointId, bool sideA) {
    drag.mode = DragState::Mode::JointAnchor;
    drag.jointId = jointId;
    drag.jointSideA = sideA;
    drag.jointChanged = false;
    drag.startDesign = mouseDesign;
    drag.lastDesign = mouseDesign;
    drag.moved = false;
    drag.undoPushed = false;
}

void App::updateHingeAnchorDrag() {
    if (drag.mode != DragState::Mode::JointAnchor) return;
    mouseDesign = screenToDesign(ui::mouse());
    const Vec2 delta = mouseDesign - drag.startDesign;
    if (!drag.moved && length(delta) * camera.zoom > 3.0f) {
        drag.moved = true;
        doc.pushUndo();
    }
    if (!drag.moved) return;

    for (auto& joint : doc.revJoints) {
        if (joint.id != drag.jointId) continue;
        Note* note = nullptr;
        if (drag.jointSideA && joint.noteA >= 0) note = doc.findNote(joint.noteA);
        else if (!drag.jointSideA) note = doc.findNote(joint.noteB);
        if (!note) {
            // The yellow world-anchor endpoint can be dragged freely.
            if (drag.jointSideA && joint.noteA < 0) {
                joint.localA = mouseDesign;
                drag.jointChanged = true;
                doc.dirty = true;
                rebuildWorld();
            }
            return;
        }
        if (!noteContainsDesignPoint(*note, mouseDesign)) return;
        Vec2 local = rotateInverse(mouseDesign - note->position, note->angle);
        // Snap onto the rivet when close, so hinge links naturally attach to it.
        const Vec2 rivet = noteCentroid(*note);
        const float snapRadius = isRivetNote(*note) ? clampf(note->radius, 2.0f, 500.0f)
                                                    : 15.0f;
        if (length(local - rivet) <= snapRadius) local = rivet;
        if (drag.jointSideA) joint.localA = local;
        else joint.localB = local;
        drag.jointChanged = true;
        doc.dirty = true;
        rebuildWorld();
        return;
    }
}

void App::finishHingeAnchorDrag() {
    const bool changed = drag.jointChanged;
    drag.mode = DragState::Mode::None;
    drag.jointId = -1;
    drag.jointChanged = false;
    drag.moved = false;
    if (changed) {
        rebuildWorld();
        notifyInfo("铰链端点已更新");
    }
}

void App::weldSelection() {
    if (selectedNotes.size() < 2) {
        notifyError("焊接至少需要选择两个音符");
        return;
    }
    prepareForEditing();
    doc.pushUndo();
    const int group = doc.nextGroupId++;
    for (int id : selectedNotes) doc.setGroupForNote(id, group);
    for (size_t i = 0; i + 1 < selectedNotes.size(); ++i) {
        Note* a = doc.findNote(selectedNotes[i]);
        Note* b = doc.findNote(selectedNotes[i + 1]);
        if (!a || !b) continue;
        const Vec2 p1 = a->position;
        const Vec2 p2 = p1 + rotate(Vec2{56.0f, 0.0f}, a->angle);
        doc.addWeldJoint(a->id, b->id, group, p1, p2);
    }
    rebuildWorld();
    doc.dirty = true;
    notifyInfo(ui::format("已焊接 %d 个音符", (int)selectedNotes.size()));
}

void App::ungroupSelection() {
    std::set<int> groups;
    for (int id : selectedNotes) {
        if (Note* n = doc.findNote(id); n && n->group != 0) groups.insert(n->group);
    }
    if (groups.empty()) {
        notifyError("所选音符不在任何焊接组中");
        return;
    }
    prepareForEditing();
    doc.pushUndo();
    for (int g : groups) doc.ungroup(g);
    rebuildWorld();
    doc.dirty = true;
    notifyInfo("已解除焊接组");
}

void App::removeSelectionJoints() {
    if (selectedNotes.empty()) return;
    prepareForEditing();
    doc.pushUndo();
    std::set<int> ids(selectedNotes.begin(), selectedNotes.end());
    doc.revJoints.erase(std::remove_if(doc.revJoints.begin(), doc.revJoints.end(),
                                       [&](const RevJoint& j) {
                                           return ids.count(j.noteA) || ids.count(j.noteB);
                                       }),
                        doc.revJoints.end());
    std::set<int> groups;
    doc.weldJoints.erase(std::remove_if(doc.weldJoints.begin(), doc.weldJoints.end(),
                                        [&](const WeldJoint& j) {
                                            if (ids.count(j.noteA) || ids.count(j.noteB)) {
                                                groups.insert(j.group);
                                                return true;
                                            }
                                            return false;
                                        }),
                         doc.weldJoints.end());
    for (int id : ids) doc.setGroupForNote(id, 0);
    for (int g : groups) {
        for (auto& n : doc.notes)
            if (n.group == g) n.group = 0;
    }
    doc.dirty = true;
    rebuildWorld();
    notifyInfo("已移除所选音符的关节");
}

void App::inspectorEditBegin() {
    prepareForEditing();
    if (!inspectorUndoPushed) {
        doc.pushUndo();
        inspectorUndoPushed = true;
    }
}

void App::setNotePosition(int id, const Vec2& position) {
    Note* n = doc.findNote(id);
    if (!n) return;
    inspectorEditBegin();
    n->position = position;
    syncBodyToNote(id);
    if (hasHingeForNote(id)) rebuildWorld();
    doc.dirty = true;
}

void App::setNoteAngle(int id, float radians) {
    Note* n = doc.findNote(id);
    if (!n) return;
    inspectorEditBegin();
    n->angle = radians;
    syncBodyToNote(id);
    if (hasHingeForNote(id)) rebuildWorld();
    doc.dirty = true;
}

void App::setNoteGravity(int noteId, GravityDirection direction) {
    Note* note = doc.findNote(noteId);
    if (!note || note->gravity == direction) return;
    inspectorEditBegin();
    note->gravity = direction;
    if (Body* body = bodyForNote(noteId)) {
        body->useGravityDirection = true;
        body->gravityDirection = gravityDirectionVector(direction);
        body->wake();
    }
    doc.dirty = true;
}

void App::setSelectionGravity(GravityDirection direction) {
    // One undo step covers the whole batch; setNoteGravity() reuses the same
    // inspector undo slot until the mouse is released.
    for (int id : selectedNotes) {
        const Note* note = doc.findNote(id);
        if (!note || note->scene) continue;
        setNoteGravity(id, direction);
    }
}

void App::setNoteGhost(int noteId, bool ghost) {
    Note* note = doc.findNote(noteId);
    if (!note || isRivetNote(*note) || note->ghost == ghost) return;
    inspectorEditBegin();
    note->ghost = ghost;
    if (Body* body = bodyForNote(noteId)) {
        body->ghost = ghost;
        if (!ghost) body->wake();
    }
    doc.dirty = true;
}

void App::setSelectionGhost(bool ghost) {
    // One undo step covers the batch, even across physics and scene blocks.
    for (int id : selectedNotes) setNoteGhost(id, ghost);
}

void App::setJudgeGhost(int judgeId, bool ghost) {
    JudgeLine* judge = doc.findJudge(judgeId);
    if (!judge || judge->ghost == ghost) return;
    inspectorEditBegin();
    judge->ghost = ghost;
    if (Body* body = bodyForJudge(judgeId)) body->ghost = ghost;
    doc.dirty = true;
}

void App::setRivetColor(int noteId, const Rgb& color) {
    Note* note = doc.findNote(noteId);
    if (!note || !isRivetNote(*note)) return;
    if (note->color.r == color.r && note->color.g == color.g && note->color.b == color.b)
        return;
    inspectorEditBegin();
    note->color = color;
    rivetPlacementColor = color;
    doc.dirty = true;
}

void App::setGlobalRivetRadius(float radius) {
    const float value = clampf(radius, 2.0f, 500.0f);
    settings.rivetRadius = value;
    bool changed = false;
    for (auto& note : doc.notes) {
        if (!isRivetNote(note)) continue;
        if (std::fabs(note.radius - value) > 0.001f) {
            note.radius = value;
            changed = true;
        }
    }
    if (changed) {
        rebuildWorld();
        doc.dirty = true;
    }
    if (!screenshotMode) saveSettings();
}

void App::applyGlobalRivetRadius() {
    for (auto& note : doc.notes)
        if (isRivetNote(note)) note.radius = clampf(settings.rivetRadius, 2.0f, 500.0f);
}

void App::setSceneGravity(float gravity) {
    const float value = clampf(gravity, 0.0f, 60.0f);
    doc.gravity = value;
    settings.gravity = value;
    world.gravity = {0.0f, -value};
    doc.dirty = true;
    for (Body* b : world.bodies)
        if (b->type == BodyType::Dynamic) b->wake();
    if (!screenshotMode) saveSettings();
}

void App::setSolverIterations(int velocity, int position) {
    const int v = std::max(4, std::min(20, velocity));
    const int p = std::max(2, std::min(10, position));
    doc.velocityIterations = v;
    doc.positionIterations = p;
    world.velocityIterations = v;
    world.positionIterations = p;
    settings.velocityIterations = v;
    settings.positionIterations = p;
    doc.dirty = true;
    if (!screenshotMode) saveSettings();
}

void App::setSlowStepGear(int gear) {
    // Only the documented gears are selectable: 0 (off) and 5..50 in steps of 5.
    gear = std::max(0, std::min(kSlowStepMax, gear));
    gear = (gear + kSlowStepStep / 2) / kSlowStepStep * kSlowStepStep;
    gear = std::max(0, std::min(kSlowStepMax, gear));
    settings.slowStepGear = gear;
    if (doc.slowStepGear != gear) {
        doc.slowStepGear = gear;
        world.slowMotionGear = gear;
        doc.dirty = true;
        for (Body* b : world.bodies)
            if (b->type == BodyType::Dynamic) b->wake();
    }
    if (!screenshotMode) saveSettings();
}

void App::setBpm(float bpm) {
    settings.bpm = clampf(bpm, 1.0f, 1000.0f);
    if (!screenshotMode) saveSettings();
}

std::vector<Event>* App::eventEditorEvents() {
    switch (eventEditor.target) {
        case EventEditorState::Target::Note: {
            Note* note = doc.findNote(eventEditor.targetId);
            return note ? &note->events : nullptr;
        }
        case EventEditorState::Target::Judge: {
            JudgeLine* judge = doc.findJudge(eventEditor.targetId);
            return judge ? &judge->events : nullptr;
        }
        case EventEditorState::Target::Field: {
            ForceField* field = doc.findField(eventEditor.targetId);
            return field ? &field->events : nullptr;
        }
        default: break;
    }
    return nullptr;
}

const std::vector<Event>* App::eventEditorEvents() const {
    switch (eventEditor.target) {
        case EventEditorState::Target::Note: {
            const Note* note = doc.findNote(eventEditor.targetId);
            return note ? &note->events : nullptr;
        }
        case EventEditorState::Target::Judge: {
            const JudgeLine* judge = doc.findJudge(eventEditor.targetId);
            return judge ? &judge->events : nullptr;
        }
        case EventEditorState::Target::Field: {
            const ForceField* field = doc.findField(eventEditor.targetId);
            return field ? &field->events : nullptr;
        }
        default: break;
    }
    return nullptr;
}

void App::openEventEditor() {
    eventEditor = EventEditorState{};
    if (selectedField >= 0) {
        eventEditor.target = EventEditorState::Target::Field;
        eventEditor.targetId = selectedField;
    } else if (selectedJudge >= 0) {
        eventEditor.target = EventEditorState::Target::Judge;
        eventEditor.targetId = selectedJudge;
    } else if (selectedNotes.size() == 1) {
        eventEditor.target = EventEditorState::Target::Note;
        eventEditor.targetId = selectedNotes.front();
    } else {
        notifyError("请选择一个原件");
        return;
    }
    if (!eventEditorEvents()) {
        eventEditor = EventEditorState{};
        notifyError("原件已不存在");
        return;
    }
    prepareForEditing();
    eventEditor.open = true;
    eventEditor.timeStart = 0.0f;
    eventEditor.beatsVisible = 16.0f;
    eventEditor.activeTrack = 0;
    mouseInCanvas = false;
    hoveredNote = -1;
}

void App::closeEventEditor() {
    eventEditor = EventEditorState{};
    ui::cancelTextEditing();
}

void App::createEventAt(float startBeat, float endBeat, int track) {
    std::vector<Event>* events = eventEditorEvents();
    if (!events) return;
    startBeat = std::max(0.0f, startBeat);
    endBeat = std::max(0.0f, endBeat);
    if (endBeat < startBeat) std::swap(startBeat, endBeat);
    const float snap = eventSnapBeats(doc.eventSplit);
    if (endBeat - startBeat < snap) endBeat = startBeat + snap;
    track = std::max(0, std::min(kEventTrackCount - 1, track));

    Event event;
    event.track = track;
    event.startBeat = startBeat;
    event.endBeat = endBeat;
    event.startValue = "this";
    event.endValue = "this";
    event.easing = 1;
    switch (eventEditor.target) {
        case EventEditorState::Target::Field:
            if (const ForceField* field = doc.findField(eventEditor.targetId)) {
                const auto types = eventTypesForField(field->kind);
                if (!types.empty()) event.type = types.front();
            }
            break;
        case EventEditorState::Target::Note: {
            const Note* note = doc.findNote(eventEditor.targetId);
            const auto types = note ? eventTypesForNote(note->scene, isRivetNote(*note))
                                    : std::vector<EventType>{};
            if (!types.empty()) event.type = types.front();
            break;
        }
        case EventEditorState::Target::Judge: {
            const auto types = eventTypesForJudge();
            if (!types.empty()) event.type = types.front();
            break;
        }
        default:
            break;
    }
    if (eventTypeIsInstantOnly(event.type)) {
        event.instant = true;
        event.endBeat = event.startBeat + snap;
    }

    doc.pushUndo();
    events->push_back(event);
    eventEditor.selected = (int)events->size() - 1;
    eventEditor.selection = {eventEditor.selected};
    eventEditor.error.clear();
    doc.dirty = true;
    eventEditor.pendingStart = -1.0f;
    eventEditor.pendingTrack = -1;
}

void App::deleteSelectedEvent() {
    std::vector<Event>* events = eventEditorEvents();
    if (!events) return;
    std::vector<int> indices = eventEditor.selection;
    if (indices.empty() && eventEditor.selected >= 0) indices = {eventEditor.selected};
    indices.erase(std::remove_if(indices.begin(), indices.end(), [&](int index) {
                      return index < 0 || index >= (int)events->size();
                  }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) return;
    doc.pushUndo();
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        events->erase(events->begin() + *it);
    eventEditor.selection.clear();
    eventEditor.selected = -1;
    eventEditor.error.clear();
    doc.dirty = true;
}

void App::copySelectedEvents(bool cut) {
    std::vector<Event>* events = eventEditorEvents();
    if (!events) return;
    std::vector<int> indices = eventEditor.selection;
    if (indices.empty() && eventEditor.selected >= 0) indices = {eventEditor.selected};
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    std::vector<int> valid;
    for (int index : indices)
        if (index >= 0 && index < (int)events->size()) valid.push_back(index);
    if (valid.empty()) return;

    eventClipboard.clear();
    eventClipboardAnchorBeat = 1e30f;
    eventClipboardAnchorTrack = 0;
    for (int index : valid) {
        const Event& source = (*events)[(size_t)index];
        eventClipboard.push_back(source);
        if (source.startBeat < eventClipboardAnchorBeat) {
            eventClipboardAnchorBeat = source.startBeat;
            eventClipboardAnchorTrack = source.track;
        }
    }
    if (cut) {
        doc.pushUndo();
        for (auto it = valid.rbegin(); it != valid.rend(); ++it)
            events->erase(events->begin() + *it);
        eventEditor.selection.clear();
        eventEditor.selected = -1;
        doc.dirty = true;
    }
    notifyInfo(ui::format("%s %d 个事件", cut ? "已剪切" : "已复制",
                          (int)valid.size()));
}

std::vector<Event> App::clipboardPreviewAt(float beat, int track) const {
    std::vector<Event> result;
    if (eventClipboard.empty()) return result;
    const float snap = eventSnapBeats(doc.eventSplit);
    float offset = snapEventBeat(beat, doc.eventSplit) - eventClipboardAnchorBeat;
    float minStart = eventClipboard.front().startBeat;
    for (const Event& event : eventClipboard)
        minStart = std::min(minStart, event.startBeat);
    if (minStart + offset < 0.0f) offset = -minStart;
    const int trackOffset = track - eventClipboardAnchorTrack;
    for (const Event& event : eventClipboard) {
        const int newTrack = event.track + trackOffset;
        if (newTrack < 0 || newTrack >= kEventTrackCount) continue;
        Event copy = event;
        copy.startBeat = std::max(0.0f, snapEventBeat(event.startBeat + offset,
                                                      doc.eventSplit));
        copy.endBeat = std::max(copy.startBeat + snap,
                                snapEventBeat(event.endBeat + offset, doc.eventSplit));
        copy.track = newTrack;
        result.push_back(copy);
    }
    return result;
}

void App::pasteEventsAtCursor() {
    std::vector<Event>* events = eventEditorEvents();
    if (!events || eventClipboard.empty()) return;
    const Rectangle tracks = layout::eventTracks();
    if (!ui::mouseIn(tracks)) return;
    const float beat = eventEditor.timeStart +
                       (tracks.y + tracks.height - ui::mouse().y) / tracks.height *
                           eventEditor.beatsVisible;
    const int track = std::max(
        0, std::min(kEventTrackCount - 1,
                    (int)((ui::mouse().x - tracks.x) /
                          (tracks.width / (float)kEventTrackCount))));
    const std::vector<Event> copies = clipboardPreviewAt(beat, track);
    if (copies.empty()) return;
    doc.pushUndo();
    const size_t first = events->size();
    for (const Event& copy : copies) events->push_back(copy);
    eventEditor.selection.clear();
    for (size_t i = first; i < events->size(); ++i)
        eventEditor.selection.push_back((int)i);
    eventEditor.selected = (int)events->size() - 1;
    eventEditor.error.clear();
    doc.dirty = true;
    notifyInfo(ui::format("已粘贴 %d 个事件", (int)copies.size()));
}

bool App::isEventCopySource(EventEditorState::Target target, int id) const {
    return eventCopyTargetActive && eventCopySourceTarget == target &&
           eventCopySourceId == id;
}

void App::beginCopyEventsToOther() {
    std::vector<Event>* events = eventEditorEvents();
    if (!events) return;
    std::vector<int> indices = eventEditor.selection;
    if (indices.empty() && eventEditor.selected >= 0) indices = {eventEditor.selected};
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    eventCopySourceEvents.clear();
    for (int index : indices)
        if (index >= 0 && index < (int)events->size())
            eventCopySourceEvents.push_back((*events)[(size_t)index]);
    if (eventCopySourceEvents.empty()) return;
    eventCopySourceTarget = eventEditor.target;
    eventCopySourceId = eventEditor.targetId;
    eventCopyTargetActive = true;
    tool = Tool::Select;
    placingScene = false;
    drag.mode = DragState::Mode::None;
    selectionBoxActive = false;
    eventCopySelectedNotes.clear();
    eventCopySelectedJudge = -1;
    eventCopySelectedField = -1;
    eventCopyBoxBase.clear();
    notifyInfo("请选择要接收事件的原件，然后点击确认复制");
}

void App::confirmEventCopyToTargets() {
    if (!eventCopyTargetActive) return;

    struct TargetInfo {
        EventEditorState::Target target;
        int id;
        std::vector<Event>* events;
        std::vector<EventType> allowed;
    };
    std::vector<TargetInfo> targets;
    for (int id : eventCopySelectedNotes) {
        if (eventCopySourceTarget == EventEditorState::Target::Note &&
            id == eventCopySourceId)
            continue;
        Note* note = doc.findNote(id);
        if (!note) continue;
        targets.push_back({EventEditorState::Target::Note, id, &note->events,
                           eventTypesForNote(note->scene, isRivetNote(*note))});
    }
    if (eventCopySelectedJudge >= 0 &&
        !(eventCopySourceTarget == EventEditorState::Target::Judge &&
          eventCopySelectedJudge == eventCopySourceId)) {
        if (JudgeLine* judge = doc.findJudge(eventCopySelectedJudge))
            targets.push_back({EventEditorState::Target::Judge, judge->id, &judge->events,
                               eventTypesForJudge()});
    }
    if (eventCopySelectedField >= 0 &&
        !(eventCopySourceTarget == EventEditorState::Target::Field &&
          eventCopySelectedField == eventCopySourceId)) {
        if (ForceField* field = doc.findField(eventCopySelectedField))
            targets.push_back({EventEditorState::Target::Field, field->id, &field->events,
                               eventTypesForField(field->kind)});
    }
    if (targets.empty()) {
        notifyError("请先选择要接收事件的原件");
        return;
    }

    auto overlaps = [](const Event& a, const Event& b) {
        return a.startBeat < b.endBeat - 1e-4f && b.startBeat < a.endBeat - 1e-4f;
    };
    auto trackFree = [&](const std::vector<Event>& list, int track, const Event& event) {
        for (const Event& other : list)
            if (other.track == track && overlaps(other, event)) return false;
        return true;
    };

    struct TargetAdditions {
        TargetInfo* target;
        std::vector<Event> events;
    };
    std::vector<TargetAdditions> additions;
    for (TargetInfo& target : targets) {
        if (!target.events) continue;
        std::vector<Event> working = *target.events;
        TargetAdditions added{&target, {}};
        for (const Event& source : eventCopySourceEvents) {
            if (std::find(target.allowed.begin(), target.allowed.end(), source.type) ==
                target.allowed.end())
                continue;
            const int desired = std::max(0, std::min(kEventTrackCount - 1, source.track));
            int track = -1;
            if (trackFree(working, desired, source)) {
                track = desired;
            } else {
                for (int step = 1; step < kEventTrackCount && track < 0; ++step) {
                    const int left = desired - step;
                    const int right = desired + step;
                    if (left >= 0 && trackFree(working, left, source)) track = left;
                    else if (right < kEventTrackCount &&
                             trackFree(working, right, source))
                        track = right;
                }
            }
            if (track < 0) continue;
            Event copy = source;
            copy.track = track;
            working.push_back(copy);
            added.events.push_back(copy);
        }
        if (!added.events.empty()) additions.push_back(std::move(added));
    }

    if (additions.empty()) {
        notifyError("目标原件没有可接收的事件类型");
        return;
    }
    eventCopyTargetActive = false;
    eventCopySourceEvents.clear();
    eventCopySourceTarget = EventEditorState::Target::None;
    eventCopySourceId = -1;
    eventCopySelectedNotes.clear();
    eventCopySelectedJudge = -1;
    eventCopySelectedField = -1;
    eventCopyBoxBase.clear();
    doc.pushUndo();
    int copied = 0;
    for (TargetAdditions& addition : additions) {
        for (const Event& event : addition.events)
            addition.target->events->push_back(event);
        copied += (int)addition.events.size();
    }
    doc.dirty = true;
    notifyInfo(ui::format("已复制 %d 个事件到 %d 个原件", copied,
                          (int)additions.size()));
}

void App::cancelEventCopyToTargets() {
    if (!eventCopyTargetActive) return;
    eventCopyTargetActive = false;
    eventCopySourceEvents.clear();
    eventCopySourceTarget = EventEditorState::Target::None;
    eventCopySourceId = -1;
    eventCopySelectedNotes.clear();
    eventCopySelectedJudge = -1;
    eventCopySelectedField = -1;
    eventCopyBoxBase.clear();
    notifyInfo("已取消复制到其他原件");
}

void App::setEventSplit(int split) {
    split = std::max(1, std::min(kEventSplitMax, split));
    if (doc.eventSplit == split) return;
    doc.pushUndo();
    doc.eventSplit = split;
    doc.dirty = true;
}

const std::vector<ForceField>& App::fieldsForDisplay() const {
    return runtimeFieldsValid ? runtimeFields : doc.fields;
}

void App::applyEventsAtTime() {
    const float bpm = settings.bpm > 0.0f ? settings.bpm : 180.0f;
    const float beat = simTime * bpm / 60.0f;
    // The simulation only moves forward except when it is reset, so the elapsed
    // slice is enough to fire impulse events exactly once and to spread a
    // non-instant impulse over its duration.
    float previousBeat = eventSimBeat;
    if (beat + 1e-4f < previousBeat) previousBeat = -1.0f;
    eventSimBeat = beat;

    for (const auto& note : doc.notes) {
        Note evaluated = note;
        if (!note.events.empty()) applyEventsToNote(evaluated, beat);
        Body* body = bodyForNote(note.id);
        if (!body) continue;
        // Ghosting is a live collision switch: update the body before any other
        // event handling so the very next physics step skips its contacts.
        if (body->ghost != evaluated.ghost) {
            body->ghost = evaluated.ghost;
            if (!body->ghost) body->wake();
        }
        // Notes without events need no transform work, but their ghost state
        // above still resets if the final ghost event was deleted.
        if (note.events.empty()) continue;

        bool moveX = false, moveY = false, rotateNote = false, gravity = false;
        for (const auto& event : note.events) {
            const bool active = beat + 1e-4f >= event.startBeat &&
                                beat <= event.endBeat + 1e-4f;
            if (!active) continue;
            if (event.type == EventType::MoveX) moveX = true;
            if (event.type == EventType::MoveY) moveY = true;
            if (event.type == EventType::Rotate) rotateNote = true;
            if (event.type == EventType::GravityDirection) gravity = true;
        }
        const Vec2 bodyPosition =
            (evaluated.position + rotate(noteCentroid(evaluated), evaluated.angle)) / PPM;
        if (moveX) {
            body->position.x = bodyPosition.x;
            body->velocity.x = 0.0f;
        }
        if (moveY) {
            body->position.y = bodyPosition.y;
            body->velocity.y = 0.0f;
        }
        if (rotateNote) {
            body->angle = evaluated.angle;
            body->angularVelocity = 0.0f;
        }
        // Scene blocks and scene rivets stay world-fixed, so their gravity event
        // only updates the authored value (if one is ever present in a file).
        if (gravity && !note.scene) {
            body->useGravityDirection = true;
            body->gravityDirection = gravityDirectionVector(evaluated.gravity);
        }
        if (moveX || moveY || rotateNote || gravity) {
            body->awake = true;
            body->updateAABB();
        }

        // Momentum events are not keyframed note properties: they add an
        // impulse to the live body. Instant events apply the whole amount at
        // their start beat; regular events spread the total over the duration.
        if (body->type == BodyType::Dynamic && !note.scene) {
            for (const Event& event : note.events) {
                if (!eventTypeIsMomentum(event.type)) continue;
                float fromBeat = 0.0f;
                float toBeat = 0.0f;
                if (event.instant) {
                    if (!(previousBeat < event.startBeat - 1e-6f &&
                          beat + 1e-6f >= event.startBeat))
                        continue;
                    fromBeat = toBeat = event.startBeat;
                } else {
                    fromBeat = std::max(previousBeat, event.startBeat);
                    toBeat = std::min(beat, event.endBeat);
                    if (toBeat <= fromBeat + 1e-6f) continue;
                }
                const float duration =
                    std::max(1e-4f, event.endBeat - event.startBeat);
                const float sampleBeat =
                    event.instant ? event.startBeat : (fromBeat + toBeat) * 0.5f;
                float momentum = 0.0f;
                if (!evaluateEventValueAt(event, 0.0f, sampleBeat, momentum))
                    continue;
                const float fraction =
                    event.instant ? 1.0f
                                  : clampf((toBeat - fromBeat) / duration, 0.0f, 1.0f);
                const float amount = momentum * fraction;
                if (std::fabs(amount) < 1e-8f) continue;
                if (event.type == EventType::DirectionalMomentum) {
                    // The angle expression's "this" is the current live angle,
                    // not the authored layout angle, so a note that is already
                    // spinning can push along its current facing.
                    float angleDegrees = internalAngleToUserDegrees(body->angle);
                    if (!evaluateEventExpressionAt(event, event.angleValue,
                                                   angleDegrees, sampleBeat,
                                                   angleDegrees))
                        continue;
                    const Vec2 impulse =
                        userDegreesToDirection(angleDegrees) * amount;
                    body->applyImpulse(impulse, body->position);
                } else {
                    // Positive rotational momentum turns clockwise, matching
                    // the editor's user-facing rotation convention.
                    body->angularVelocity += body->invInertia * -amount;
                }
                body->wake();
            }
        }
    }

    for (const auto& judge : doc.judges) {
        JudgeLine evaluated = judge;
        if (!judge.events.empty()) applyEventsToJudge(evaluated, beat);
        Body* body = bodyForJudge(judge.id);
        if (!body) continue;
        if (body->ghost != evaluated.ghost) body->ghost = evaluated.ghost;
        if (judge.events.empty()) continue;
        for (const auto& event : judge.events) {
            const bool active = beat + 1e-4f >= event.startBeat &&
                                beat <= event.endBeat + 1e-4f;
            if (!active) continue;
            if (event.type == EventType::MoveX) body->position.x = evaluated.position.x / PPM;
            if (event.type == EventType::MoveY) body->position.y = evaluated.position.y / PPM;
            if (event.type == EventType::Rotate) body->angle = evaluated.angle;
        }
        body->updateAABB();
    }

    bool fieldEvents = false;
    for (const auto& field : doc.fields)
        if (!field.events.empty()) fieldEvents = true;
    if (fieldEvents) {
        runtimeFields = doc.fields;
        for (auto& field : runtimeFields) applyEventsToField(field, beat);
        runtimeFieldsValid = true;
        syncWorldForceFields();
    } else {
        runtimeFieldsValid = false;
        runtimeFields.clear();
    }
}

void App::persistDisplaySettings() {
    settings.showGrid = showGrid;
    settings.showJoints = showJoints;
    settings.showLabels = showLabels;
    settings.showWireframe = showWireframe;
    settings.showContactPoints = showContactPoints;
    settings.showGridLabels = showGridLabels;
    if (!screenshotMode) saveSettings();
}

void App::setJudgeAngle(int id, float radians) {
    JudgeLine* j = doc.findJudge(id);
    if (!j) return;
    inspectorEditBegin();
    j->angle = radians;
    syncBodyToJudge(id);
    doc.dirty = true;
}

void App::setJudgeSize(int id, float width, float height) {
    JudgeLine* j = doc.findJudge(id);
    if (!j) return;
    const float w = clampf(width, 20.0f, 20000.0f);
    const float h = clampf(height, 1.0f, 400.0f);
    if (std::fabs(j->width - w) < 0.01f && std::fabs(j->height - h) < 0.01f) return;
    inspectorEditBegin();
    j->width = w;
    j->height = h;
    rebuildWorld();
    doc.dirty = true;
}

void App::setJudgeColor(int id, const Rgb& color) {
    JudgeLine* j = doc.findJudge(id);
    if (!j) return;
    if (j->color.r == color.r && j->color.g == color.g && j->color.b == color.b) return;
    inspectorEditBegin();
    j->color = color;
    doc.dirty = true;
}

void App::setFieldShape(int id, ForceFieldShape shape) {
    ForceField* f = doc.findField(id);
    if (!f || f->shape == shape) return;
    inspectorEditBegin();
    f->shape = shape;
    runtimeFieldsValid = false;
    syncWorldForceFields();
    doc.dirty = true;
}

void App::setFieldParameters(int id, const ForceField& values) {
    ForceField* f = doc.findField(id);
    if (!f) return;
    inspectorEditBegin();
    f->position = values.position;
    f->angle = values.angle;
    f->width = clampf(values.width, 20.0f, 20000.0f);
    f->height = clampf(values.height, 20.0f, 20000.0f);
    f->radius = clampf(values.radius, 10.0f, 20000.0f);
    f->strength = clampf(values.strength, 0.0f, 500.0f);
    f->buoyancy = clampf(values.buoyancy, 0.0f, 500.0f);
    f->damping = clampf(values.damping, 0.0f, 500.0f);
    f->vortex = f->kind == ForceFieldKind::Vortex
                    ? clampf(values.vortex, -500.0f, 500.0f)
                    : 0.0f;
    Vec2 dir = normalize(values.buoyancyDirection);
    f->buoyancyDirection = lengthSq(dir) > 0.5f ? dir : Vec2{0.0f, 1.0f};
    runtimeFieldsValid = false;
    syncWorldForceFields();
    doc.dirty = true;
}

void App::undo() {
    if (!doc.undo()) {
        notifyInfo("没有可撤销的操作");
        return;
    }
    paused = true;
    simTime = 0.0f;
    accumulator = 0.0f;
    rebuildWorld();
    ensureSelectionValid();
    notifyInfo("已撤销");
}

void App::redo() {
    if (!doc.redo()) {
        notifyInfo("没有可重做的操作");
        return;
    }
    paused = true;
    simTime = 0.0f;
    accumulator = 0.0f;
    rebuildWorld();
    ensureSelectionValid();
    notifyInfo("已重做");
}

void App::saveProjectInteractive() {
    if (projectPath.empty()) {
        openProjectDialog(ProjectDialogMode::SaveAs);
        return;
    }
    std::string error;
    if (saveProject(projectPath, doc, error)) {
        doc.dirty = false;
        refreshProjectList();
        notifyInfo("工程已保存：" + projectPath.filename().string());
    } else {
        notifyError("保存失败：" + error);
    }
}

void App::saveProjectAs(const std::string& name) {
    std::string reason;
    if (!validProjectName(name, &reason)) {
        projectDialogError = reason;
        return;
    }
    const fs::path path = projectsDir / (name + ".pnp");
    if (name != doc.name && fs::exists(path)) {
        projectDialogError = "已存在同名工程，请换一个名称";
        return;
    }
    doc.name = name;
    std::string error;
    if (!saveProject(path, doc, error)) {
        projectDialogError = error;
        return;
    }
    projectPath = path;
    doc.dirty = false;
    projectDialog = ProjectDialogMode::None;
    projectDialogError.clear();
    refreshProjectList();
    notifyInfo("已保存为 " + name + ".pnp");
}

void App::createProject(const std::string& name) {
    std::string reason;
    if (!validProjectName(name, &reason)) {
        projectDialogError = reason;
        return;
    }
    const fs::path path = projectsDir / (name + ".pnp");
    if (fs::exists(path)) {
        projectDialogError = "已存在同名工程";
        return;
    }
    // A new project starts with no notes and a single judge line at (0, -270).
    doc.makeDefaultScene();
    if (!doc.judges.empty()) {
        doc.judges.front().width = componentDefaults.judge.width;
        doc.judges.front().height = componentDefaults.judge.height;
        doc.judges.front().color = componentDefaults.judge.color;
    }
    doc.gravity = settings.gravity;
    doc.velocityIterations = settings.velocityIterations;
    doc.positionIterations = settings.positionIterations;
    doc.slowStepGear = settings.slowStepGear;
    doc.name = name;
    doc.dirty = true;
    std::string saveError;
    if (!saveProject(path, doc, saveError)) {
        projectDialogError = "创建工程失败：" + saveError;
        return;
    }
    doc.dirty = false;
    projectPath = path;
    paused = true;
    simTime = 0.0f;
    accumulator = 0.0f;
    selectedNotes.clear();
    selectedJudge = -1;
    selectedField = -1;
    rebuildWorld();
    resetCamera();
    projectDialog = ProjectDialogMode::None;
    projectDialogError.clear();
    refreshProjectList();
    notifyInfo("已创建工程 " + name + "（Ctrl+S 保存）");
}

void App::loadProjectFile(const fs::path& path) {
    Document loaded;
    std::string error;
    if (!loadProject(path, loaded, error)) {
        notifyError("打开工程失败：" + error);
        return;
    }
    doc = std::move(loaded);
    // Scene controls live in Settings > Basic; keep them in sync with the
    // project that was just opened, then persist them for the next launch.
    settings.gravity = doc.gravity;
    settings.velocityIterations = doc.velocityIterations;
    settings.positionIterations = doc.positionIterations;
    settings.slowStepGear = doc.slowStepGear;
    applyGlobalRivetRadius();
    if (!screenshotMode) saveSettings();
    projectPath = path;
    paused = true;
    simTime = 0.0f;
    accumulator = 0.0f;
    selectedNotes.clear();
    selectedJudge = -1;
    selectedField = -1;
    hoveredNote = -1;
    rebuildWorld();
    resetCamera();
    refreshProjectList();
    doc.dirty = false;
    notifyInfo("已打开工程 " + doc.name);
}

void App::deleteProjectFile(const fs::path& path) {
    std::string error;
    if (!deleteProject(path, error)) {
        notifyError(error);
        return;
    }
    if (projectPath == path) {
        projectPath.clear();
        doc.dirty = true;
    }
    refreshProjectList();
    notifyInfo("工程文件已删除");
}

void App::openProjectDialog(ProjectDialogMode mode) {
    projectDialog = mode;
    projectNameDraft = mode == ProjectDialogMode::New ? "Project" : doc.name;
    std::string reason;
    if (!validProjectName(projectNameDraft, &reason)) projectNameDraft = "Project";
    projectDialogError.clear();
    showSettings = false;
    shape.open = false;
}

void App::setProjectDialogResult(bool accept) {
    if (!accept) {
        projectDialog = ProjectDialogMode::None;
        projectDialogError.clear();
        return;
    }
    if (projectDialog == ProjectDialogMode::New) {
        createProject(projectNameDraft);
    } else if (projectDialog == ProjectDialogMode::SaveAs) {
        saveProjectAs(projectNameDraft);
    }
}

void App::openShapeEditor(int typeIndex, bool scene) {
    const NoteLibrary& source = scene ? sceneLibrary : library;
    if (typeIndex < 0 || typeIndex >= (int)source.types.size()) return;
    const NoteType& t = source.types[typeIndex];
    shape = ShapeDraft{};
    shape.open = true;
    shape.editingExisting = true;
    shape.scene = scene;
    shape.originalName = t.name;
    shape.name = t.name;
    shape.vertices = t.vertices;
    shape.density = t.density;
    shape.friction = t.friction;
    shape.restitution = t.restitution;
    shape.color = t.color;
    shape.selectedVertex = 0;
    shape.error.clear();
    shape.deleteArmed = false;
    shape.holdOriginal = t.isHold();
    shape.variables = t.variables;
    shape.bindings = t.bindings;
    shape.formTab = 0;
    shape.newVariableName = "height";
    shape.newVariableDisplay = "高度";
    shape.newVariableValue = 0.0f;
    shape.newBindingVertex = 0;
    shape.newBindingX = "this";
    shape.newBindingY = "this";
    fitShapeView();
    showSettings = false;
    projectDialog = ProjectDialogMode::None;
    ui::cancelTextEditing();
}

void App::addShapeVariable() {
    std::string reason;
    if (!validVariableName(shape.newVariableName, &reason)) {
        shape.error = reason;
        return;
    }
    for (const auto& variable : shape.variables) {
        if (variable.name == shape.newVariableName) {
            shape.error = "变量名已存在：" + shape.newVariableName;
            return;
        }
    }
    NoteVariable variable;
    variable.name = shape.newVariableName;
    variable.displayName = shape.newVariableDisplay.empty() ? shape.newVariableName
                                                             : shape.newVariableDisplay;
    variable.value = shape.newVariableValue;
    shape.variables.push_back(variable);
    shape.newVariableName = "var" + std::to_string(shape.variables.size() + 1);
    shape.newVariableDisplay = "变量 " + std::to_string(shape.variables.size());
    shape.newVariableValue = 0.0f;
    shape.error.clear();
}

void App::deleteShapeVariable(int index) {
    if (index < 0 || index >= (int)shape.variables.size()) return;
    shape.variables.erase(shape.variables.begin() + index);
    shape.error.clear();
}

void App::addShapeBinding() {
    if (shape.newBindingVertex < 0 ||
        shape.newBindingVertex >= (int)shape.vertices.size()) {
        shape.error = "节点编号超出顶点范围";
        return;
    }
    for (const auto& binding : shape.bindings) {
        if (binding.vertex == shape.newBindingVertex) {
            shape.error = "该节点已经有变量绑定";
            return;
        }
    }
    const std::string x = compactExpression(shape.newBindingX);
    const std::string y = compactExpression(shape.newBindingY);
    const Vec2 base = shape.vertices[(size_t)shape.newBindingVertex];
    std::vector<NoteVariable> xVariables = shape.variables;
    xVariables.push_back({"this", "this", base.x});
    std::vector<NoteVariable> yVariables = shape.variables;
    yVariables.push_back({"this", "this", base.y});
    float unused = 0.0f;
    std::string reason;
    if (!evaluateNoteExpression(x, xVariables, unused, &reason)) {
        shape.error = "X 表达式：" + reason;
        return;
    }
    if (!evaluateNoteExpression(y, yVariables, unused, &reason)) {
        shape.error = "Y 表达式：" + reason;
        return;
    }
    shape.bindings.push_back(NodeBinding{shape.newBindingVertex, x, y});
    shape.newBindingX = "this";
    shape.newBindingY = "this";
    shape.error.clear();
}

void App::deleteShapeBinding(int index) {
    if (index < 0 || index >= (int)shape.bindings.size()) return;
    shape.bindings.erase(shape.bindings.begin() + index);
    shape.error.clear();
}

void App::setLightMode(bool light, bool persist) {
    lightMode = light;
    settings.lightMode = light;
    theme::setLightMode(light);
    theme::setAccentColor(themeColor);
    if (persist && !screenshotMode) saveSettings();
}

void App::setThemeColor(int index) {
    index = std::max(0, std::min(kAccentCount - 1, index));
    themeColor = index;
    settings.accent = index;
    theme::setAccentColor(index);
    if (!screenshotMode) saveSettings();
}

void App::ensureExportRulesFile() {
    const fs::path rulesPath = rootDir / "export_rules.txt";
    std::error_code ec;
    fs::create_directories(rootDir, ec);
    if (fs::exists(rulesPath, ec)) return;
    std::ofstream out(rulesPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << "# PhiPhysiceEditorNeo 预制事件导出规则\n";
    out << "# 本文件完全决定输出内容；程序不内置任何输出文本。\n";
    out << "# 使用 key=value 形式；# 开头为注释。\n";
    out << "# parameters：要输出的参数列表，逗号分隔，例如 x,y,angle\n";
    out << "# prefix / suffix / component_prefix / component_suffix / between_components / between_parameters / parameter\n";
    out << "# 占位符详见程序目录说明。\n";
    out << "parameters=";
}

void App::browseExportDirectory() {
    std::string path = settings.exportDirectory;
    if (!browseFolderDialog(path) || path.empty()) return;
    settings.exportDirectory = path;
    if (!screenshotMode) saveSettings();
    notifyInfo("预制事件目录已设置");
}

void App::openExportDialog() {
    showExportDialog = true;
    exportSplitInput.clear();
    exportEndInput.clear();
    exportDialogError.clear();
    ui::cancelTextEditing();
}

void App::closeExportDialog() {
    showExportDialog = false;
    exportDialogError.clear();
    ui::cancelTextEditing();
}

void App::confirmExportDialog() {
    const std::string splitText = trimTextCopy(exportSplitInput);
    const std::string endText = trimTextCopy(exportEndInput);
    char* splitEnd = nullptr;
    char* endEnd = nullptr;
    const long split = std::strtol(splitText.c_str(), &splitEnd, 10);
    const float end = std::strtof(endText.c_str(), &endEnd);
    if (splitText.empty() || !splitEnd || *splitEnd != '\0' || split <= 0 ||
        split > 1000) {
        exportDialogError = "切分必须是 1–1000 的整数";
        return;
    }
    if (endText.empty() || !endEnd || *endEnd != '\0' || !std::isfinite(end) ||
        end <= 0.0f) {
        exportDialogError = "结束时间必须是大于 0 的 beats 数值";
        return;
    }
    exportSplit = (int)split;
    exportEndBeat = end;
    if (!exportPrefabEvents()) return;
    closeExportDialog();
}

bool App::exportPrefabEvents() {
    ensureExportRulesFile();
    const fs::path rulesPath = rootDir / "export_rules.txt";
    const PrefabExportRules rules = loadPrefabExportRules(rulesPath);
    if (exportSplit <= 0) {
        notifyError("导出切分必须大于 0");
        return false;
    }
    if (exportEndBeat <= 0.0f) {
        notifyError("导出结束时间必须大于 0");
        return false;
    }

    fs::path targetDir = settings.exportDirectory.empty()
                             ? rootDir / "exports"
                             : fs::path(settings.exportDirectory);
    if (targetDir.is_relative()) targetDir = rootDir / targetDir;
    std::error_code ec;
    if (!fs::is_directory(targetDir, ec)) {
        notifyError("预制事件目录不存在或不是文件夹：" + targetDir.string());
        return false;
    }

    struct ExportComponent {
        Note note;
        int index = 0;
    };
    std::vector<ExportComponent> components;
    for (const Note& note : doc.notes) {
        if (!NoteLibrary::supportedName(note.type)) continue;
        ExportComponent item;
        item.note = note;
        item.index = (int)components.size();
        components.push_back(std::move(item));
    }
    if (components.empty()) {
        notifyError("没有可导出的 Tap / Drag / Flick / Hold 原件");
        return false;
    }

    PrefabExportTime time;
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y", &local);
        time.year = buffer;
        std::strftime(buffer, sizeof(buffer), "%m", &local);
        time.month = buffer;
        std::strftime(buffer, sizeof(buffer), "%d", &local);
        time.day = buffer;
        std::strftime(buffer, sizeof(buffer), "%H", &local);
        time.hour = buffer;
        std::strftime(buffer, sizeof(buffer), "%M", &local);
        time.minute = buffer;
        std::strftime(buffer, sizeof(buffer), "%S", &local);
        time.second = buffer;
    }

    if (rules.parameters.empty()) {
        notifyError("export_rules.txt 缺少 parameters 参数列表");
        return false;
    }
    if (rules.parameter.empty()) {
        notifyError("export_rules.txt 缺少 parameter 输出模板");
        return false;
    }
    for (const std::string& name : rules.parameters) {
        if (name != "x" && name != "y" && name != "angle" && name != "beats") {
            notifyError("export_rules.txt 中存在未知参数：" + name);
            return false;
        }
    }

    const float step = 1.0f / (float)exportSplit;
    const float endBeat = exportEndBeat;
    const int sampleCount =
        (int)std::floor(endBeat / step + 1e-4f) + 1;
    const int segmentCount = std::max(0, sampleCount - 1);
    const int eventCount = (int)components.size() * segmentCount *
                           (int)rules.parameters.size();

    // Run the actual physics simulation from the authored layout. Sampling is
    // done at the requested beat grid, then the editor is reset to layout.
    struct ExportSample {
        float x = 0.0f;
        float y = 0.0f;
        float angle = 0.0f;
        float beats = 0.0f;
    };
    std::vector<std::vector<ExportSample>> samples(
        components.size(), std::vector<ExportSample>((size_t)sampleCount));
    auto recordSample = [&](int sampleIndex, float beat) {
        for (size_t c = 0; c < components.size(); ++c) {
            const Note& note = components[c].note;
            ExportSample sample;
            sample.beats = beat;
            if (const Note* current = doc.findNote(note.id)) {
                sample.x = current->position.x;
                sample.y = current->position.y;
                sample.angle = internalAngleToUserDegrees(current->angle);
                if (Body* body = bodyForNote(note.id)) {
                    const Vec2 centre = noteCentroid(*current);
                    const Vec2 origin =
                        body->position * PPM - rotate(centre, body->angle);
                    sample.x = origin.x;
                    sample.y = origin.y;
                    sample.angle = internalAngleToUserDegrees(body->angle);
                }
            }
            samples[c][(size_t)sampleIndex] = sample;
        }
    };

    const float bpm = settings.bpm > 0.0f ? settings.bpm : 180.0f;
    resetSimulation();
    recordSample(0, 0.0f);
    for (int sampleIndex = 1; sampleIndex < sampleCount; ++sampleIndex) {
        const float sampleBeat = step * (float)sampleIndex;
        const float targetTime = sampleBeat * 60.0f / bpm;
        int guard = 0;
        while (simTime + 1e-6f < targetTime && guard++ < 2000000) {
            const float dt = std::min(fixedStep, targetTime - simTime);
            world.step(dt);
            simTime += dt;
            applyEventsAtTime();
        }
        recordSample(sampleIndex, sampleBeat);
    }
    resetSimulation();

    PrefabExportContext base;
    base.componentCount = (int)components.size();
    base.eventCount = eventCount;
    base.split = exportSplit;
    base.time = time;

    auto parameterValue = [](const ExportSample& sample, const std::string& name)
        -> std::string {
        if (name == "x") return ui::format("%.3f", sample.x);
        if (name == "y") return ui::format("%.3f", sample.y);
        if (name == "angle") return ui::format("%.3f", sample.angle);
        return ui::format("%.3f", sample.beats);
    };

    std::string output;
    output.reserve(4096);
    output += expandPrefabTemplate(rules.prefix, base);
    for (size_t c = 0; c < components.size(); ++c) {
        const ExportComponent& component = components[c];
        PrefabExportContext componentBase = base;
        componentBase.index = component.index;
        componentBase.id = component.note.id;
        componentBase.type = component.note.type;
        componentBase.scene = component.note.scene ? 1 : 0;
        output += expandPrefabTemplate(rules.componentPrefix, componentBase);
        bool firstParameter = true;

        for (int segment = 0; segment < segmentCount; ++segment) {
            const ExportSample& start = samples[c][(size_t)segment];
            const ExportSample& next = samples[c][(size_t)(segment + 1)];
            for (size_t p = 0; p < rules.parameters.size(); ++p) {
                PrefabExportContext ctx = componentBase;
                ctx.segmentIndex = segment;
                ctx.segmentNumber = segment + 1;
                ctx.paramIndex = (int)p + 1;
                ctx.param = rules.parameters[p];
                ctx.splitIndex = segment;
                ctx.nextSplitIndex = segment + 1;
                ctx.beat = start.beats;
                ctx.nextBeat = next.beats;
                ctx.x = start.x;
                ctx.y = start.y;
                ctx.angle = start.angle;
                ctx.beats = start.beats;
                ctx.nextX = next.x;
                ctx.nextY = next.y;
                ctx.nextAngle = next.angle;
                ctx.nextBeats = next.beats;
                ctx.value = parameterValue(start, ctx.param);
                ctx.nextValue = parameterValue(next, ctx.param);
                if (!firstParameter)
                    output += unescapeRuleText(rules.betweenParameters);
                output += expandPrefabTemplate(rules.parameter, ctx);
                firstParameter = false;
            }
        }

        output += expandPrefabTemplate(rules.componentSuffix, componentBase);
        if (c + 1 < components.size())
            output += expandPrefabTemplate(rules.betweenComponents, componentBase);
    }
    output += expandPrefabTemplate(rules.suffix, base);


    const fs::path outputPath = targetDir / "NotePhysics.json";
    // Create the output file when the selected folder does not contain it yet;
    // an existing NotePhysics.json is overwritten below.
    if (!fs::exists(outputPath, ec)) {
        std::ofstream create(outputPath, std::ios::binary);
        if (!create.is_open()) {
            const int error = errno;
            notifyError("无法创建 " + outputPath.string() + "：" +
                        std::strerror(error));
            return false;
        }
    }
    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        const int error = errno;
        notifyError("无法写入 " + outputPath.string() + "：" + std::strerror(error));
        return false;
    }
    out << output;
    out.flush();
    if (!out.good()) {
        notifyError("写入失败：" + outputPath.string());
        return false;
    }
    notifyInfo("预制事件已导出：" + outputPath.string());
    return true;
}

void App::saveSettings() {
    if (settingsPath.empty()) return;
    std::string error;
    if (!ppn::saveSettings(settingsPath, settings, error)) notifyError("设置保存失败：" + error);
}

void App::resetShortcuts() {
    settings.resetShortcuts();
    shortcutCapture = -1;
    if (!screenshotMode) saveSettings();
    notifyInfo("快捷键已恢复默认");
}

void App::assignShortcut(ShortcutAction action, const KeyBinding& binding) {
    const int index = (int)action;
    if (index < 0 || index >= kShortcutCount || binding.key == 0) return;
    for (int i = 0; i < kShortcutCount; ++i) {
        if (i == index) continue;
        const KeyBinding& other = settings.shortcuts[i];
        if (other.key == binding.key && other.ctrl == binding.ctrl &&
            other.shift == binding.shift) {
            notifyError(std::string("该快捷键已用于“") + shortcutLabel((ShortcutAction)i) +
                        "”");
            return;
        }
    }
    settings.shortcuts[index] = binding;
    if (!screenshotMode) saveSettings();
    notifyInfo(std::string("已设置“") + shortcutLabel(action) + "”为 " +
               bindingLabel(binding));
}

bool App::shortcutPressed(ShortcutAction action) const {
    const int index = (int)action;
    if (index < 0 || index >= kShortcutCount) return false;
    const KeyBinding& b = settings.shortcuts[index];
    if (b.key == 0) return false;
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    return IsKeyPressed(b.key) && ctrl == b.ctrl && shift == b.shift;
}

void App::applyWindowIcon() {
    const fs::path iconPath = assetsDir / "icon.png";
    std::ifstream file(iconPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return;
    const std::streamsize size = file.tellg();
    if (size <= 0) return;
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes((size_t)size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) return;
    Image icon = LoadImageFromMemory(".png", bytes.data(), (int)bytes.size());
    if (icon.data != nullptr) {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
}

void App::saveShapeDraft() {
    std::string reason;
    if (!NoteLibrary::validName(shape.name, &reason)) {
        shape.error = reason;
        return;
    }
    NoteLibrary& target = shape.scene ? sceneLibrary : library;
    const NoteType* existing = target.find(shape.name);
    if (existing && (!shape.editingExisting || shape.name != shape.originalName)) {
        shape.error = "已存在同名原件类型，请换一个名称";
        return;
    }
    if (!NoteLibrary::supportedName(shape.name)) {
        shape.error = "可保存的类型为 Tap、Drag、Flick、Hold";
        return;
    }
    NoteType type;
    type.name = shape.name;
    type.vertices = shape.vertices;
    type.density = shape.density;
    type.friction = shape.friction;
    type.restitution = shape.restitution;
    type.color = shape.color;
    type.variables = shape.variables;
    type.bindings = shape.bindings;
    for (auto& binding : type.bindings) {
        binding.x = compactExpression(binding.x);
        binding.y = compactExpression(binding.y);
    }

    std::string error;
    if (!target.saveType(type, error, true)) {
        shape.error = error;
        return;
    }
    target.reload();
    selectedTypeName = shape.name;
    placingScene = shape.scene;
    paused = true;
    simTime = 0.0f;
    rebuildWorld();
    doc.dirty = true;
    shape.open = false;
    shape.error.clear();
    notifyInfo(ui::format("已保存 %s.note", shape.name.c_str()));
}

void App::deleteShapeType() {
    if (!shape.editingExisting) {
        shape.error = "新形状尚未保存";
        return;
    }
    if (hasTypeInDocument(shape.originalName, shape.scene)) {
        shape.error = "工程中仍有原件使用该类型";
        return;
    }
    if (!shape.deleteArmed) {
        shape.deleteArmed = true;
        shape.error = "再次点击“删除文件”以确认";
        return;
    }
    NoteLibrary& target = shape.scene ? sceneLibrary : library;
    std::string error;
    if (!target.deleteType(shape.originalName, error)) {
        shape.error = error;
        return;
    }
    target.load(shape.scene ? sceneDir : notesDir);
    if (!target.find(selectedTypeName))
        selectedTypeName = target.types.empty() ? "" : target.types.front().name;
    rebuildWorld();
    doc.dirty = true;
    shape.open = false;
    notifyInfo("音符类型文件已删除");
}

void App::addShapeVertex() {
    if (shape.vertices.size() >= 64) {
        shape.error = "已达到 64 个顶点";
        return;
    }
    const Vec2 anchor = shape.vertices.empty() ? Vec2{0, 0} : shape.vertices.back();
    // Y-up: add the new vertex above and to the right of the last one.
    shape.vertices.push_back({anchor.x + 48.0f, anchor.y + 36.0f});
    shape.selectedVertex = (int)shape.vertices.size() - 1;
    shape.error.clear();
}

void App::deleteSelectedShapeVertex() {
    if (shape.vertices.size() <= 3) {
        shape.error = "至少需要 3 个顶点";
        return;
    }
    if (shape.selectedVertex < 0 || shape.selectedVertex >= (int)shape.vertices.size()) return;
    const int removed = shape.selectedVertex;
    shape.vertices.erase(shape.vertices.begin() + removed);
    // Keep variable bindings attached to the same physical nodes.
    shape.bindings.erase(std::remove_if(shape.bindings.begin(), shape.bindings.end(),
                                        [&](const NodeBinding& b) {
                                            return b.vertex == removed;
                                        }),
                         shape.bindings.end());
    for (auto& binding : shape.bindings)
        if (binding.vertex > removed) --binding.vertex;
    shape.selectedVertex = std::min(removed, (int)shape.vertices.size() - 1);
    shape.error.clear();
}

void App::fitShapeView() {
    float extent = 1.0f;
    for (const auto& v : shape.vertices) extent = std::max(extent, length(v));
    extent = std::max(extent, 60.0f);
    const Rectangle c = layout::shapeCanvas();
    shape.zoom = clampf(std::min((c.width - 150.0f) / (2.0f * extent),
                                 (c.height - 150.0f) / (2.0f * extent)),
                        0.12f, 8.0f);
    shape.center = {0.0f, 0.0f};
}

void App::syncBodyToNote(int noteId) {
    Note* n = doc.findNote(noteId);
    Body* body = bodyForNote(noteId);
    if (!n || !body) return;
    body->position = noteBodyPosition(*n);
    body->angle = n->angle;
    body->ghost = n->ghost;
    body->velocity = {0.0f, 0.0f};
    body->angularVelocity = 0.0f;
    body->awake = true;
    body->updateAABB();
}

void App::syncBodyToJudge(int judgeId) {
    JudgeLine* j = doc.findJudge(judgeId);
    Body* body = bodyForJudge(judgeId);
    if (!j || !body) return;
    body->position = j->position / PPM;
    body->angle = j->angle;
    body->ghost = j->ghost;
    body->updateAABB();
}

void App::markDocumentDirty() { doc.dirty = true; }

bool App::hasTypeInDocument(const std::string& name, bool scene) const {
    for (const auto& n : doc.notes)
        if (n.scene == scene && n.type == name) return true;
    return false;
}

void App::validateAndRefreshSelection() {
    ensureSelectionValid();
}

void App::update(float dt) {
    statusMessageAge += dt;
    if (!showSettings) shortcutCapture = -1;
    if (screenshotMode) {
        // Documentation screenshots must be reproducible: do not let the real
        // cursor position or a stray click change the prepared selection.
        mouseInCanvas = false;
        hoveredNote = -1;
        updateSimulation(dt);
        updateToasts(dt);
        return;
    }
    if (!ui::isEditingText()) {
        if (eventEditor.open) {
            if (eventCopyTargetActive) {
                handleEventCopyOverlayInput();
            } else {
                handleEventEditorInput();
                mouseInCanvas = false;
                hoveredNote = -1;
            }
        } else if (showExportDialog) {
            if (IsKeyPressed(KEY_ESCAPE)) closeExportDialog();
            mouseInCanvas = false;
            hoveredNote = -1;
        } else if (showSettings) {
            handleSettingsInput();
            mouseInCanvas = false;
            hoveredNote = -1;
        } else if (projectDialog != ProjectDialogMode::None) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                projectDialog = ProjectDialogMode::None;
                projectDialogError.clear();
            }
            mouseInCanvas = false;
            hoveredNote = -1;
        } else if (shape.open) {
            handleShapeInput();
        } else {
            handleShortcuts();
            moveSelectionWithKeyboard(dt);
            if (!showSettings) handleCanvasInput();
        }
    } else {
        mouseInCanvas = false;
    }
    updateSimulation(dt);
    updateToasts(dt);
}

void App::updateSimulation(float dt) {
    if (!paused) {
        accumulator += std::min(dt, 0.1f);
        int steps = 0;
        while (accumulator >= fixedStep && steps < maxSubSteps) {
            world.step(fixedStep);
            simTime += fixedStep;
            applyEventsAtTime();
            accumulator -= fixedStep;
            ++steps;
        }
        if (steps == maxSubSteps) accumulator = 0.0f;
    } else if (stepRequested) {
        world.step(fixedStep);
        simTime += fixedStep;
        applyEventsAtTime();
        stepRequested = false;
    }
}

void App::updateToasts(float dt) {
    for (auto& t : toasts) t.age += dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [](const Toast& t) { return t.age > t.duration; }),
                 toasts.end());
}

void App::handleSettingsInput() {
    if (shortcutCapture >= 0) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            shortcutCapture = -1;
            return;
        }
        int key = 0;
        while ((key = GetKeyPressed()) != 0) {
            if (key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT ||
                key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL ||
                key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT ||
                key == KEY_LEFT_SUPER || key == KEY_RIGHT_SUPER)
                continue;
            if (key == KEY_ESCAPE) {
                shortcutCapture = -1;
                return;
            }
            KeyBinding binding;
            binding.key = key;
            binding.ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            binding.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            const ShortcutAction action = (ShortcutAction)shortcutCapture;
            shortcutCapture = -1;
            assignShortcut(action, binding);
            return;
        }
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        showSettings = false;
        shortcutCapture = -1;
        ui::cancelTextEditing();
    }
}

void App::handleEventEditorInput() {
    const Rectangle tracks = layout::eventTracks();
    const float trackWidth = tracks.width / (float)kEventTrackCount;
    const float snap = eventSnapBeats(doc.eventSplit);
    auto beatAt = [&](float y) {
        return eventEditor.timeStart +
               (tracks.y + tracks.height - y) / tracks.height * eventEditor.beatsVisible;
    };
    auto beatToY = [&](float beat) {
        return tracks.y + tracks.height - (beat - eventEditor.timeStart) /
                                               eventEditor.beatsVisible * tracks.height;
    };
    auto trackAt = [&](float x) {
        return std::max(0, std::min(kEventTrackCount - 1,
                                   (int)((x - tracks.x) / trackWidth)));
    };
    auto hitEvent = [&](const Vec2& mouse) {
        const std::vector<Event>* events = eventEditorEvents();
        if (!events) return -1;
        const int track = trackAt(mouse.x);
        for (int i = (int)events->size() - 1; i >= 0; --i) {
            const Event& event = (*events)[(size_t)i];
            if (event.track != track) continue;
            const float bottom = beatToY(event.startBeat);
            const float top = beatToY(event.endBeat);
            if (mouse.y >= std::min(top, bottom) - 2.0f &&
                mouse.y <= std::max(top, bottom) + 2.0f)
                return i;
        }
        return -1;
    };
    auto isSelected = [&](int index) {
        return std::find(eventEditor.selection.begin(), eventEditor.selection.end(),
                         index) != eventEditor.selection.end();
    };
    auto selectOnly = [&](int index) {
        eventEditor.selection = index >= 0 ? std::vector<int>{index} : std::vector<int>{};
        eventEditor.selected = index;
    };
    auto toggleSelection = [&](int index) {
        if (index < 0) return;
        auto it = std::find(eventEditor.selection.begin(), eventEditor.selection.end(),
                            index);
        if (it == eventEditor.selection.end()) {
            eventEditor.selection.push_back(index);
            eventEditor.selected = index;
        } else {
            eventEditor.selection.erase(it);
            eventEditor.selected = eventEditor.selection.empty()
                                       ? -1
                                       : eventEditor.selection.back();
        }
    };
    auto beginSelectionDrag = [&](int primary, EventEditorState::DragMode mode) {
        std::vector<Event>* events = eventEditorEvents();
        if (!events) return;
        if (primary >= 0 && !isSelected(primary)) selectOnly(primary);
        if (eventEditor.selection.empty() && primary >= 0) selectOnly(primary);
        eventEditor.dragIndices.clear();
        eventEditor.dragOriginals.clear();
        for (int index : eventEditor.selection) {
            if (index < 0 || index >= (int)events->size()) continue;
            eventEditor.dragIndices.push_back(index);
            eventEditor.dragOriginals.push_back((*events)[(size_t)index]);
        }
        if (eventEditor.dragIndices.empty()) return;
        eventEditor.dragMode = mode;
        eventEditor.dragMouse = ui::mouse();
        eventEditor.dragMoved = false;
        eventEditor.dragUndoPushed = false;
        if (primary >= 0) {
            eventEditor.dragStartBeat = (*events)[(size_t)primary].startBeat;
            eventEditor.dragEndBeat = (*events)[(size_t)primary].endBeat;
            eventEditor.dragTrack = (*events)[(size_t)primary].track;
        }
    };

    // Finish a rubber-band selection when the mouse is released.
    if (eventEditor.marqueeActive) {
        if (ui::mouseDown(0)) return;
        const Vec2 a = eventEditor.marqueeStart;
        const Vec2 b = ui::mouse();
        const Rectangle box{std::min(a.x, b.x), std::min(a.y, b.y),
                            std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
        eventEditor.selection = eventEditor.marqueeBase;
        if (const std::vector<Event>* events = eventEditorEvents()) {
            for (size_t i = 0; i < events->size(); ++i) {
                const Event& event = (*events)[i];
                const float y1 = beatToY(event.startBeat);
                const float y2 = beatToY(event.endBeat);
                const Rectangle rect{tracks.x + event.track * trackWidth + 4.0f,
                                     std::min(y1, y2), trackWidth - 8.0f,
                                     std::max(5.0f, std::fabs(y2 - y1))};
                if (CheckCollisionRecs(box, rect) &&
                    !isSelected((int)i))
                    eventEditor.selection.push_back((int)i);
            }
        }
        eventEditor.selected = eventEditor.selection.empty()
                                   ? -1
                                   : eventEditor.selection.back();
        eventEditor.marqueeActive = false;
        eventEditor.marqueeBase.clear();
        return;
    }

    if (eventEditor.dragMode != EventEditorState::DragMode::None) {
        const bool held = eventEditor.dragMode == EventEditorState::DragMode::End
                              ? ui::mouseDown(MOUSE_BUTTON_RIGHT)
                              : ui::mouseDown(0);
        if (held) {
            std::vector<Event>* events = eventEditorEvents();
            if (!events || eventEditor.dragIndices.empty()) {
                eventEditor.dragMode = EventEditorState::DragMode::None;
                return;
            }
            const Vec2 mouse = ui::mouse();
            const float rawDelta =
                (eventEditor.dragMouse.y - mouse.y) / tracks.height *
                eventEditor.beatsVisible;
            float delta = snapEventBeat(rawDelta, doc.eventSplit);
            auto beginEdit = [&] {
                if (eventEditor.dragUndoPushed) return;
                doc.pushUndo();
                eventEditor.dragUndoPushed = true;
            };

            if (eventEditor.dragMode == EventEditorState::DragMode::Move) {
                float minStart = 1e30f;
                int minTrack = kEventTrackCount;
                int maxTrack = -1;
                for (const Event& original : eventEditor.dragOriginals) {
                    minStart = std::min(minStart, original.startBeat);
                    minTrack = std::min(minTrack, original.track);
                    maxTrack = std::max(maxTrack, original.track);
                }
                delta = std::max(delta, -minStart);
                int trackDelta =
                    (int)std::lround((mouse.x - eventEditor.dragMouse.x) / trackWidth);
                trackDelta = std::max(trackDelta, -minTrack);
                trackDelta = std::min(trackDelta, kEventTrackCount - 1 - maxTrack);
                bool changed = false;
                for (size_t i = 0; i < eventEditor.dragIndices.size(); ++i) {
                    const int index = eventEditor.dragIndices[i];
                    if (index < 0 || index >= (int)events->size()) continue;
                    const Event& original = eventEditor.dragOriginals[i];
                    Event& event = (*events)[(size_t)index];
                    const float newStart = std::max(0.0f, original.startBeat + delta);
                    const float newEnd = newStart + (original.endBeat - original.startBeat);
                    const int newTrack = original.track + trackDelta;
                    if (newStart != event.startBeat || newEnd != event.endBeat ||
                        newTrack != event.track)
                        changed = true;
                    event.startBeat = newStart;
                    event.endBeat = newEnd;
                    event.track = newTrack;
                }
                if (trackDelta != 0) eventEditor.activeTrack =
                    std::max(0, std::min(kEventTrackCount - 1,
                                         eventEditor.dragTrack + trackDelta));
                if (changed) {
                    beginEdit();
                    eventEditor.dragMoved = true;
                }
            } else if (eventEditor.dragMode == EventEditorState::DragMode::Start) {
                float lo = -1e30f;
                float hi = 1e30f;
                for (const Event& original : eventEditor.dragOriginals) {
                    lo = std::max(lo, -original.startBeat);
                    hi = std::min(hi, original.endBeat - snap - original.startBeat);
                }
                delta = clampf(delta, lo, hi);
                bool changed = false;
                for (size_t i = 0; i < eventEditor.dragIndices.size(); ++i) {
                    const int index = eventEditor.dragIndices[i];
                    if (index < 0 || index >= (int)events->size()) continue;
                    const Event& original = eventEditor.dragOriginals[i];
                    Event& event = (*events)[(size_t)index];
                    const float newStart = std::max(0.0f, original.startBeat + delta);
                    if (newStart != event.startBeat) changed = true;
                    event.startBeat = newStart;
                    event.endBeat = original.endBeat;
                }
                if (changed) {
                    beginEdit();
                    eventEditor.dragMoved = true;
                }
            } else {
                float lo = -1e30f;
                for (const Event& original : eventEditor.dragOriginals)
                    lo = std::max(lo, original.startBeat + snap - original.endBeat);
                delta = std::max(delta, lo);
                bool changed = false;
                for (size_t i = 0; i < eventEditor.dragIndices.size(); ++i) {
                    const int index = eventEditor.dragIndices[i];
                    if (index < 0 || index >= (int)events->size()) continue;
                    const Event& original = eventEditor.dragOriginals[i];
                    Event& event = (*events)[(size_t)index];
                    const float newEnd = std::max(original.startBeat + snap,
                                                  original.endBeat + delta);
                    if (newEnd != event.endBeat) changed = true;
                    event.startBeat = original.startBeat;
                    event.endBeat = newEnd;
                }
                if (changed) {
                    beginEdit();
                    eventEditor.dragMoved = true;
                }
            }
            if (eventEditor.dragMoved) {
                doc.dirty = true;
                eventEditor.error.clear();
            }
            return;
        }
        eventEditor.dragMode = EventEditorState::DragMode::None;
        eventEditor.dragIndices.clear();
        eventEditor.dragOriginals.clear();
        eventEditor.dragMoved = false;
        eventEditor.dragUndoPushed = false;
        return;
    }

    const bool overTracks = ui::mouseIn(tracks);
    if (overTracks) eventEditor.activeTrack = trackAt(ui::mouse().x);

    if (IsKeyPressed(KEY_ESCAPE)) {
        closeEventEditor();
        return;
    }
    if (IsKeyPressed(KEY_DELETE)) {
        deleteSelectedEvent();
        return;
    }

    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_C)) {
        copySelectedEvents(false);
        return;
    }
    if (ctrl && IsKeyPressed(KEY_X)) {
        copySelectedEvents(true);
        return;
    }
    if (ctrl && IsKeyPressed(KEY_V)) {
        pasteEventsAtCursor();
        return;
    }

    if ((IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_R)) &&
        overTracks) {
        const float beat = std::max(0.0f, snapEventBeat(beatAt(ui::mouse().y),
                                                        doc.eventSplit));
        const int track = trackAt(ui::mouse().x);
        if (eventEditor.pendingStart < 0.0f) {
            eventEditor.pendingStart = beat;
            eventEditor.pendingTrack = track;
            eventEditor.activeTrack = track;
        } else {
            createEventAt(eventEditor.pendingStart, beat, eventEditor.pendingTrack);
        }
        return;
    }

    if (overTracks && std::fabs(ui::wheel()) > 0.001f) {
        // The event timeline scrolls in the opposite direction from the canvas
        // and scrollable panels; only this wheel interaction is reversed.
        eventEditor.timeStart += ui::wheel() * eventEditor.beatsVisible * 0.12f;
        eventEditor.timeStart = std::max(0.0f, eventEditor.timeStart);
        return;
    }

    if (!overTracks) return;
    const bool z = IsKeyDown(KEY_Z);
    const bool additive = ctrl || IsKeyDown(KEY_LEFT_SHIFT) ||
                          IsKeyDown(KEY_RIGHT_SHIFT);
    const bool left = ui::mousePressed(0) && !ui::mouseConsumed();
    const bool right = ui::mousePressed(MOUSE_BUTTON_RIGHT) && !ui::mouseConsumed();
    if (right && z) {
        const int hit = hitEvent(ui::mouse());
        if (hit >= 0) {
            ui::consumeMouse();
            if (!isSelected(hit)) selectOnly(hit);
            beginSelectionDrag(hit, EventEditorState::DragMode::End);
        }
        return;
    }
    if (left) {
        ui::consumeMouse();
        const int hit = hitEvent(ui::mouse());
        if (hit >= 0) {
            if (additive) {
                toggleSelection(hit);
            } else {
                beginSelectionDrag(hit, z ? EventEditorState::DragMode::Start
                                          : EventEditorState::DragMode::Move);
            }
        } else {
            eventEditor.marqueeActive = true;
            eventEditor.marqueeStart = ui::mouse();
            eventEditor.marqueeBase = additive ? eventEditor.selection
                                               : std::vector<int>{};
            if (!additive) selectOnly(-1);
            if (additive && !eventEditor.selection.empty())
                eventEditor.selected = eventEditor.selection.back();
        }
        return;
    }
}

void App::handleEventCopyOverlayInput() {
    const Rectangle canvas = canvasRect();
    const bool over = ui::mouseIn(canvas);
    mouseInCanvas = over;
    if (over) mouseDesign = screenToDesign(ui::mouse());

    if (IsKeyPressed(KEY_ESCAPE)) {
        cancelEventCopyToTargets();
        return;
    }
    if (over && std::fabs(ui::wheel()) > 0.001f)
        cameraZoomAt(ui::mouse(), std::pow(1.12f, ui::wheel()));

    // Finish a rubber-band selection without touching the scene's selection.
    if (drag.mode == DragState::Mode::BoxSelect) {
        if (ui::mouseDown(0)) {
            updateDrag();
            return;
        }
        eventCopySelectedNotes = eventCopyBoxBase;
        if (drag.moved) {
            const Vec2 a = drag.startDesign;
            const Vec2 b = over ? mouseDesign : screenToDesign(ui::mouse());
            const float lox = std::min(a.x, b.x), hix = std::max(a.x, b.x);
            const float loy = std::min(a.y, b.y), hiy = std::max(a.y, b.y);
            for (const auto& note : doc.notes) {
                if (isEventCopySource(EventEditorState::Target::Note, note.id)) continue;
                Body* body = bodyForNote(note.id);
                if (!body) continue;
                const float bx0 = body->aabb.lo.x * PPM, bx1 = body->aabb.hi.x * PPM;
                const float by0 = body->aabb.lo.y * PPM, by1 = body->aabb.hi.y * PPM;
                if (bx1 >= lox && bx0 <= hix && by1 >= loy && by0 <= hiy) {
                    if (std::find(eventCopySelectedNotes.begin(),
                                  eventCopySelectedNotes.end(),
                                  note.id) == eventCopySelectedNotes.end())
                        eventCopySelectedNotes.push_back(note.id);
                }
            }
        }
        drag.mode = DragState::Mode::None;
        drag.moved = false;
        drag.undoPushed = false;
        selectionBoxActive = false;
        eventCopyBoxBase.clear();
        return;
    }

    // The copy overlay is view-only: navigation and selection are the only
    // interactions. Right / middle drag pans, never moves or picks a component.
    if (over && !ui::mouseConsumed()) {
        if (ui::mousePressed(MOUSE_BUTTON_RIGHT)) {
            drag.mode = DragState::Mode::Camera;
            drag.cameraButton = MOUSE_BUTTON_RIGHT;
        } else if (ui::mousePressed(MOUSE_BUTTON_MIDDLE)) {
            drag.mode = DragState::Mode::Camera;
            drag.cameraButton = MOUSE_BUTTON_MIDDLE;
        }
    }
    if (drag.mode == DragState::Mode::Camera) {
        if (!ui::mouseDown(MOUSE_BUTTON_MIDDLE) && !ui::mouseDown(MOUSE_BUTTON_RIGHT)) {
            drag.mode = DragState::Mode::None;
        } else {
            const Vec2 delta = ui::mouseDelta() / camera.zoom;
            camera.center.x -= delta.x;
            camera.center.y += delta.y;
        }
        return;
    }

    if (!over) return;
    hoveredNote = hitTestNote(mouseDesign);
    if (!ui::mousePressed(0) || ui::mouseConsumed()) return;
    ui::consumeMouse();

    const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ||
                          IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const int noteId = hitTestNote(mouseDesign);
    if (noteId >= 0) {
        if (isEventCopySource(EventEditorState::Target::Note, noteId)) return;
        auto it = std::find(eventCopySelectedNotes.begin(),
                            eventCopySelectedNotes.end(), noteId);
        if (additive) {
            if (it == eventCopySelectedNotes.end())
                eventCopySelectedNotes.push_back(noteId);
            else
                eventCopySelectedNotes.erase(it);
        } else {
            eventCopySelectedNotes = {noteId};
        }
        eventCopySelectedJudge = -1;
        eventCopySelectedField = -1;
        return;
    }

    const int judgeId = hitTestJudge(mouseDesign);
    if (judgeId >= 0) {
        if (isEventCopySource(EventEditorState::Target::Judge, judgeId)) return;
        eventCopySelectedJudge = judgeId;
        eventCopySelectedNotes.clear();
        eventCopySelectedField = -1;
        return;
    }
    const int fieldId = hitTestField(mouseDesign);
    if (fieldId >= 0) {
        if (isEventCopySource(EventEditorState::Target::Field, fieldId)) return;
        eventCopySelectedField = fieldId;
        eventCopySelectedNotes.clear();
        eventCopySelectedJudge = -1;
        return;
    }

    eventCopyBoxBase = additive ? eventCopySelectedNotes : std::vector<int>{};
    if (!additive) {
        eventCopySelectedNotes.clear();
        eventCopySelectedJudge = -1;
        eventCopySelectedField = -1;
    }
    drag.mode = DragState::Mode::BoxSelect;
    drag.startDesign = mouseDesign;
    drag.lastDesign = mouseDesign;
    drag.moved = false;
    drag.undoPushed = false;
    selectionBoxActive = true;
    const Vec2 s = designToScreen(drag.startDesign);
    selectionBoxScreen = {s.x, s.y, 0, 0};
}

void App::handleShortcuts() {
    // Esc is contextual: leave placement first, then clear the selection, and
    // only when the canvas is idle does it open settings. F1 is no longer used.
    if (IsKeyPressed(KEY_ESCAPE)) {
        ui::cancelTextEditing();
        auto stopDrag = [&] {
            drag.mode = DragState::Mode::None;
            drag.cameraButton = 0;
            drag.moved = false;
            drag.undoPushed = false;
            drag.jointId = -1;
            selectionBoxActive = false;
        };
        if (tool != Tool::Select) {
            stopDrag();
            cancelPlacement();
            return;
        }
        if (!selectedNotes.empty() || selectedJudge >= 0 || selectedField >= 0) {
            selectedNotes.clear();
            selectedJudge = -1;
            selectedField = -1;
            stopDrag();
            notifyInfo("已取消选择");
            return;
        }
        stopDrag();
        showSettings = true;
        shortcutCapture = -1;
        ui::cancelTextEditing();
        return;
    }
    if (shortcutPressed(ShortcutAction::PreviousComponent)) cycleActiveComponent(-1);
    if (shortcutPressed(ShortcutAction::NextComponent)) cycleActiveComponent(1);
    if (shortcutPressed(ShortcutAction::PlaceComponent)) placeActiveComponentAtMouse();
    if (shortcutPressed(ShortcutAction::Undo)) undo();
    if (shortcutPressed(ShortcutAction::Redo)) redo();
    if (shortcutPressed(ShortcutAction::SaveAs)) openProjectDialog(ProjectDialogMode::SaveAs);
    if (shortcutPressed(ShortcutAction::Save)) saveProjectInteractive();
    if (shortcutPressed(ShortcutAction::NewProject))
        openProjectDialog(ProjectDialogMode::New);
    if (shortcutPressed(ShortcutAction::SelectAll)) {
        selectedNotes.clear();
        for (const auto& n : doc.notes) selectedNotes.push_back(n.id);
        selectedJudge = -1;
        selectedField = -1;
    }
    if (shortcutPressed(ShortcutAction::PlayPause)) togglePlay();
    if (shortcutPressed(ShortcutAction::ResetSimulation)) {
        resetSimulation();
        notifyInfo("模拟已重置");
    }
    if (shortcutPressed(ShortcutAction::DeleteSelection)) deleteSelection();
    if (shortcutPressed(ShortcutAction::RotateLeft)) rotateSelection(15.0f * DEG2RAD);
    if (shortcutPressed(ShortcutAction::RotateRight)) rotateSelection(-15.0f * DEG2RAD);
    if (shortcutPressed(ShortcutAction::FocusSelection)) focusSelection();
    if (shortcutPressed(ShortcutAction::ResetView)) resetCamera();
    if (shortcutPressed(ShortcutAction::SelectTool)) tool = Tool::Select;
    if (shortcutPressed(ShortcutAction::JudgeTool)) {
        librarySection = LibrarySection::Scene;
        placingScene = false;
        tool = Tool::JudgeLine;
    }
    bool displayChanged = false;
    if (shortcutPressed(ShortcutAction::ToggleGrid)) {
        showGrid = !showGrid;
        displayChanged = true;
    }
    if (shortcutPressed(ShortcutAction::ToggleJoints)) {
        showJoints = !showJoints;
        displayChanged = true;
    }
    if (shortcutPressed(ShortcutAction::ToggleLabels)) {
        showLabels = !showLabels;
        displayChanged = true;
    }
    if (shortcutPressed(ShortcutAction::ToggleContacts)) {
        showContactPoints = !showContactPoints;
        displayChanged = true;
    }
    if (displayChanged) persistDisplaySettings();
}

int App::hitTestNote(const Vec2& design) const {
    const Vec2 m = design / PPM;
    for (auto it = doc.notes.rbegin(); it != doc.notes.rend(); ++it) {
        auto bit = noteBodies.find(it->id);
        if (bit == noteBodies.end()) continue;
        const Body* body = bit->second;
        if (body->aabb.contains(m) && body->containsPoint(m)) return it->id;
    }
    return -1;
}

int App::hitTestJudge(const Vec2& design) const {
    const Vec2 m = design / PPM;
    for (auto it = doc.judges.rbegin(); it != doc.judges.rend(); ++it) {
        auto bit = judgeBodies.find(it->id);
        if (bit == judgeBodies.end()) continue;
        const Body* body = bit->second;
        // Pick against the body's local outline. The world AABB is not a local
        // frame, so using it here made rotated judge lines highlight and pick
        // in the wrong place.
        Vec2 lo{1e9f, 1e9f};
        Vec2 hi{-1e9f, -1e9f};
        for (const Vec2& v : body->outline) {
            lo.x = std::min(lo.x, v.x);
            lo.y = std::min(lo.y, v.y);
            hi.x = std::max(hi.x, v.x);
            hi.y = std::max(hi.y, v.y);
        }
        const Vec2 center = (lo + hi) * 0.5f;
        const Vec2 half = (hi - lo) * 0.5f;
        const Vec2 local = body->localPoint(m) - center;
        const float tol = 8.0f / PPM;
        if (std::fabs(local.x) <= half.x + tol && std::fabs(local.y) <= half.y + tol)
            return it->id;
    }
    return -1;
}

int App::hitTestField(const Vec2& design) const {
    const auto& fields = fieldsForDisplay();
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
        if (forceFieldContainsPoint(*it, design)) return it->id;
    }
    return -1;
}

void App::handleCanvasInput() {
    const Rectangle canvas = canvasRect();
    const bool over = ui::mouseIn(canvas);
    const bool placing = tool != Tool::Select;
    mouseInCanvas = over;
    if (over) mouseDesign = screenToDesign(ui::mouse());

    if (over && std::fabs(ui::wheel()) > 0.001f) {
        cameraZoomAt(ui::mouse(), std::pow(1.12f, ui::wheel()));
    }

    // Hover is needed before navigation: right/middle drags may only pan when
    // the cursor is not inside any note.
    if (over && !isModalOpen()) {
        hoveredNote = hitTestNote(mouseDesign);
    } else if (!over) {
        hoveredNote = -1;
    }

    // Right button: drag a hinge endpoint when close to one, otherwise pan only
    // over empty canvas. Clicking inside a note without an endpoint does nothing.
    if (over && ui::mousePressed(MOUSE_BUTTON_RIGHT) && !ui::mouseConsumed()) {
        bool sideA = false;
        const int hingeId = hitTestHingeAnchor(mouseDesign, sideA);
        if (hingeId >= 0) {
            ui::consumeMouse();
            prepareForEditing();
            mouseDesign = screenToDesign(ui::mouse());
            beginHingeAnchorDrag(hingeId, sideA);
        } else if (hoveredNote < 0) {
            drag.mode = DragState::Mode::Camera;
            drag.cameraButton = MOUSE_BUTTON_RIGHT;
        }
    }
    if (over && ui::mousePressed(MOUSE_BUTTON_MIDDLE) && hoveredNote < 0 &&
        !ui::mouseConsumed()) {
        drag.mode = DragState::Mode::Camera;
        drag.cameraButton = MOUSE_BUTTON_MIDDLE;
    }
    if (drag.mode == DragState::Mode::Camera) {
        if (!ui::mouseDown(MOUSE_BUTTON_MIDDLE) && !ui::mouseDown(MOUSE_BUTTON_RIGHT)) {
            drag.mode = DragState::Mode::None;
        } else {
            // Y-up design space: dragging the canvas down raises the camera
            // centre instead of lowering it.
            const Vec2 delta = ui::mouseDelta() / camera.zoom;
            camera.center.x -= delta.x;
            camera.center.y += delta.y;
        }
    }

    if (drag.mode == DragState::Mode::JointAnchor) {
        if (ui::mouseDown(MOUSE_BUTTON_RIGHT)) {
            updateHingeAnchorDrag();
        } else {
            finishHingeAnchorDrag();
        }
    }

    if (over && placing) SetMouseCursor(MOUSE_CURSOR_CROSSHAIR);

    if (over && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        prepareForEditing();
        mouseDesign = screenToDesign(ui::mouse());

        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const int noteId = hitTestNote(mouseDesign);
        if (noteId >= 0) {
            auto already = std::find(selectedNotes.begin(), selectedNotes.end(), noteId);
            if (shift) {
                if (already != selectedNotes.end())
                    selectedNotes.erase(already);
                else
                    selectedNotes.push_back(noteId);
                selectedJudge = -1;
                selectedField = -1;
                return;
            }
            if (already == selectedNotes.end()) {
                selectedNotes = {noteId};
            }
            selectedJudge = -1;
            selectedField = -1;
            // Ctrl + left starts a duplicate-and-drag gesture. The copies keep
            // the note type, angle, gravity, variables and scene flag.
            const bool duplicated = ctrl && tool == Tool::Select;
            if (duplicated) duplicateSelectionForDrag();
            beginNoteDrag();
            // The duplicate already pushed one undo snapshot; do not push a
            // second one for the drag that immediately follows.
            drag.undoPushed = duplicated;
            return;
        }

        const int judgeId = hitTestJudge(mouseDesign);
        if (judgeId >= 0) {
            selectedJudge = judgeId;
            selectedNotes.clear();
            selectedField = -1;
            beginJudgeDrag();
            return;
        }

        const int fieldId = hitTestField(mouseDesign);
        if (fieldId >= 0) {
            selectedField = fieldId;
            selectedNotes.clear();
            selectedJudge = -1;
            beginFieldDrag();
            return;
        }

        if (!shift) {
            selectedNotes.clear();
            selectedJudge = -1;
            selectedField = -1;
        }
        drag.mode = DragState::Mode::BoxSelect;
        drag.startDesign = mouseDesign;
        drag.lastDesign = mouseDesign;
        drag.moved = false;
        drag.undoPushed = false;
        selectionBoxActive = true;
        const Vec2 s = designToScreen(drag.startDesign);
        selectionBoxScreen = {s.x, s.y, 0, 0};
    }

    if (drag.mode == DragState::Mode::Notes || drag.mode == DragState::Mode::JudgeLine ||
        drag.mode == DragState::Mode::Field || drag.mode == DragState::Mode::BoxSelect) {
        if (ui::mouseDown(0)) {
            updateDrag();
        } else if (ui::mouseReleased(0) || !ui::mouseDown(0)) {
            finishDrag();
        }
    }
}

void App::beginNoteDrag() {
    drag.mode = DragState::Mode::Notes;
    drag.startDesign = mouseDesign;
    drag.lastDesign = mouseDesign;
    drag.noteStart.clear();
    drag.moved = false;
    drag.undoPushed = false;
    for (int id : selectedNotes) {
        Note* n = doc.findNote(id);
        if (n) drag.noteStart.emplace_back(id, n->position);
        if (Body* b = bodyForNote(id)) {
            b->velocity = {0.0f, 0.0f};
            b->angularVelocity = 0.0f;
            b->awake = true;
        }
    }
}

void App::beginJudgeDrag() {
    drag.mode = DragState::Mode::JudgeLine;
    drag.startDesign = mouseDesign;
    drag.lastDesign = mouseDesign;
    drag.moved = false;
    drag.undoPushed = false;
    if (JudgeLine* j = doc.findJudge(selectedJudge)) drag.judgeStart = j->position;
}

void App::beginFieldDrag() {
    drag.mode = DragState::Mode::Field;
    drag.startDesign = mouseDesign;
    drag.lastDesign = mouseDesign;
    drag.moved = false;
    drag.undoPushed = false;
    if (ForceField* f = doc.findField(selectedField)) drag.fieldStart = f->position;
}

void App::updateDrag() {
    mouseDesign = screenToDesign(ui::mouse());
    const Vec2 delta = mouseDesign - drag.startDesign;
    if (drag.mode == DragState::Mode::Notes) {
        if (!drag.moved && length(delta) * camera.zoom > 3.0f) {
            drag.moved = true;
            if (!drag.undoPushed) doc.pushUndo();
        }
        if (drag.moved) {
            for (const auto& [id, start] : drag.noteStart) {
                Note* n = doc.findNote(id);
                if (!n) continue;
                n->position = start + delta;
                syncBodyToNote(id);
            }
            doc.dirty = true;
        }
    } else if (drag.mode == DragState::Mode::JudgeLine) {
        if (!drag.moved && length(delta) * camera.zoom > 3.0f) {
            drag.moved = true;
            if (!drag.undoPushed) doc.pushUndo();
        }
        if (drag.moved) {
            if (JudgeLine* j = doc.findJudge(selectedJudge)) {
                j->position = drag.judgeStart + delta;
                syncBodyToJudge(selectedJudge);
                doc.dirty = true;
            }
        }
    } else if (drag.mode == DragState::Mode::Field) {
        if (!drag.moved && length(delta) * camera.zoom > 3.0f) {
            drag.moved = true;
            if (!drag.undoPushed) doc.pushUndo();
        }
        if (drag.moved) {
            if (ForceField* f = doc.findField(selectedField)) {
                f->position = drag.fieldStart + delta;
                syncWorldForceFields();
                doc.dirty = true;
            }
        }
    } else if (drag.mode == DragState::Mode::BoxSelect) {
        drag.lastDesign = mouseDesign;
        if (length(delta) * camera.zoom > 4.0f) drag.moved = true;
        const Vec2 a = designToScreen(drag.startDesign);
        const Vec2 b = designToScreen(mouseDesign);
        selectionBoxScreen = {std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x),
                              std::fabs(b.y - a.y)};
    }
}

void App::finishDrag() {
    if (drag.mode == DragState::Mode::BoxSelect) {
        if (drag.moved) {
            const Vec2 a = drag.startDesign;
            const Vec2 b = screenToDesign(ui::mouse());
            const float lox = std::min(a.x, b.x), hix = std::max(a.x, b.x);
            const float loy = std::min(a.y, b.y), hiy = std::max(a.y, b.y);
            const bool additive =
                IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ||
                IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (!additive) {
                selectedNotes.clear();
                selectedField = -1;
            }
            for (const auto& n : doc.notes) {
                Body* body = bodyForNote(n.id);
                if (!body) continue;
                const float bx0 = body->aabb.lo.x * PPM, bx1 = body->aabb.hi.x * PPM;
                const float by0 = body->aabb.lo.y * PPM, by1 = body->aabb.hi.y * PPM;
                if (bx1 >= lox && bx0 <= hix && by1 >= loy && by0 <= hiy) {
                    if (std::find(selectedNotes.begin(), selectedNotes.end(), n.id) ==
                        selectedNotes.end())
                        selectedNotes.push_back(n.id);
                }
            }
            selectedJudge = -1;
            selectedField = -1;
            if (!selectedNotes.empty())
                notifyInfo(ui::format("已选择 %d 个音符", (int)selectedNotes.size()));
        }
    }
    if (drag.mode == DragState::Mode::Notes && drag.moved) {
        std::vector<int> movedIds;
        movedIds.reserve(drag.noteStart.size());
        for (const auto& [id, start] : drag.noteStart) {
            (void)start;
            movedIds.push_back(id);
        }
        if (hasHingeForNotes(movedIds)) {
            rebuildWorld();
            notifyInfo("已自动修复相连铰链的位置");
        }
    }
    drag.mode = DragState::Mode::None;
    drag.cameraButton = 0;
    selectionBoxActive = false;
    drag.moved = false;
    drag.undoPushed = false;
}

void App::cameraZoomAt(const Vec2& screenPos, float factor) {
    const Vec2 before = screenToDesign(screenPos);
    camera.zoom = clampf(camera.zoom * factor, 0.02f, 4.0f);
    const Vec2 after = screenToDesign(screenPos);
    camera.center += before - after;
}

void App::handleShapeInput() {
    if (ui::isEditingText()) return;
    if (IsKeyPressed(KEY_ESCAPE)) {
        shape.open = false;
        shape.error.clear();
        ui::cancelTextEditing();
        return;
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) {
        if (shape.selectedVertex >= 0) {
            deleteSelectedShapeVertex();
            return;
        }
    }
    const Rectangle sc = layout::shapeCanvas();
    const bool over = ui::mouseIn(sc);
    if (!over && drag.mode != DragState::Mode::ShapeVertex &&
        drag.mode != DragState::Mode::ShapePan)
        return;

    if (over && std::fabs(ui::wheel()) > 0.001f) {
        const Vec2 before = shapeScreenToDesign(ui::mouse());
        shape.zoom = clampf(shape.zoom * std::pow(1.12f, ui::wheel()), 0.08f, 12.0f);
        const Vec2 after = shapeScreenToDesign(ui::mouse());
        shape.center += before - after;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) ||
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (over) drag.mode = DragState::Mode::ShapePan;
    }
    if (drag.mode == DragState::Mode::ShapePan) {
        if (!ui::mouseDown(MOUSE_BUTTON_MIDDLE) && !ui::mouseDown(MOUSE_BUTTON_RIGHT)) {
            drag.mode = DragState::Mode::None;
        } else {
            const Vec2 delta = ui::mouseDelta() / shape.zoom;
            shape.center.x -= delta.x;
            shape.center.y += delta.y;
        }
    }

    if (over && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        const Vec2 m = ui::mouse();
        int nearest = -1;
        float best = 14.0f;
        for (size_t i = 0; i < shape.vertices.size(); ++i) {
            const Vec2 s = shapeToScreen(shape.vertices[i]);
            const float d = length(s - m);
            if (d < best) {
                best = d;
                nearest = (int)i;
            }
        }
        if (nearest >= 0) {
            shape.selectedVertex = nearest;
            drag.mode = DragState::Mode::ShapeVertex;
            drag.shapeStartVertex = shape.vertices[nearest];
            shape.error.clear();
        } else {
            const double now = GetTime();
            if (now - lastEmptyClickTime < 0.34 &&
                length(m - lastEmptyClickPos) < 10.0f) {
                if (shape.vertices.size() < 64) {
                    Vec2 p = shapeScreenToDesign(m);
                    p.x = std::round(p.x);
                    p.y = std::round(p.y);
                    shape.vertices.push_back(p);
                    shape.selectedVertex = (int)shape.vertices.size() - 1;
                    shape.error.clear();
                }
                lastEmptyClickTime = -100.0;
            } else {
                shape.selectedVertex = -1;
                lastEmptyClickTime = now;
                lastEmptyClickPos = m;
            }
        }
    }

    if (drag.mode == DragState::Mode::ShapeVertex) {
        if (ui::mouseDown(0)) {
            if (shape.selectedVertex >= 0 &&
                shape.selectedVertex < (int)shape.vertices.size()) {
                Vec2 p = shapeScreenToDesign(ui::mouse());
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    p.x = std::round(p.x / 10.0f) * 10.0f;
                    p.y = std::round(p.y / 10.0f) * 10.0f;
                } else {
                    p.x = std::round(p.x);
                    p.y = std::round(p.y);
                }
                p.x = clampf(p.x, -4000.0f, 4000.0f);
                p.y = clampf(p.y, -4000.0f, 4000.0f);
                shape.vertices[shape.selectedVertex] = p;
            }
        } else {
            drag.mode = DragState::Mode::None;
        }
    }
}

void App::draw() { editorui::draw(*this); }

} // namespace ppn