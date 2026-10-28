#include "model/Document.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace ppn {
namespace {
constexpr float kDiagonal = 0.70710678f;
}

const char* forceFieldKindName(ForceFieldKind kind) {
    switch (kind) {
        case ForceFieldKind::Gravity: return "引力场";
        case ForceFieldKind::Repulsion: return "斥力场";
        case ForceFieldKind::Buoyancy: return "浮力场";
        case ForceFieldKind::Drag: return "阻力场";
        case ForceFieldKind::GravityChange: return "引力改变场";
        case ForceFieldKind::Vortex: return "涡流场";
    }
    return "力场";
}

const char* forceFieldShapeName(ForceFieldShape shape) {
    switch (shape) {
        case ForceFieldShape::Rectangle: return "矩形";
        case ForceFieldShape::Circle: return "圆形";
    }
    return "矩形";
}

bool forceFieldContainsPoint(const ForceField& field, const Vec2& point) {
    const Vec2 local = rotateInverse(point - field.position, field.angle);
    if (field.shape == ForceFieldShape::Circle) {
        return lengthSq(local) <= field.radius * field.radius;
    }
    return std::fabs(local.x) <= field.width * 0.5f &&
           std::fabs(local.y) <= field.height * 0.5f;
}

namespace {
std::string trimFieldText(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

const ForceFieldKind kFieldKinds[] = {ForceFieldKind::Gravity, ForceFieldKind::Repulsion,
                                      ForceFieldKind::Buoyancy, ForceFieldKind::Drag,
                                      ForceFieldKind::GravityChange, ForceFieldKind::Vortex};
const char* kFieldFileNames[] = {"Gravity.note", "Repulsion.note", "Buoyancy.note",
                                 "Drag.note", "GravityChange.note", "Vortex.note"};
const char* kFieldNames[] = {"引力场", "斥力场", "浮力场", "阻力场", "引力改变场",
                             "涡流场"};
} // namespace

bool ForceFieldLibrary::load(const std::filesystem::path& folderPath) {
    folder = folderPath;
    types.clear();
    errors.clear();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        errors.push_back("无法创建力场文件夹: " + ec.message());
        return false;
    }
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(folder,
                                                std::filesystem::directory_options::skip_permission_denied,
                                                ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".note")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in.is_open()) {
            errors.push_back(file.filename().string() + ": 无法打开");
            continue;
        }
        ForceFieldTemplate item;
        item.defaults = ForceField{};
        item.defaults.shape = ForceFieldShape::Rectangle;
        item.defaults.width = 100.0f;
        item.defaults.height = 100.0f;
        item.defaults.radius = 50.0f;
        item.defaults.strength = 18.0f;
        item.defaults.buoyancy = 1.2f;
        item.defaults.vortex = 20.0f;
        item.displayName = "";
        int kind = -1;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '') line.pop_back();
            line = trimFieldText(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::string key;
            iss >> key;
            if (key == "kind") {
                iss >> kind;
            } else if (key == "name") {
                std::string rest;
                std::getline(iss, rest);
                item.displayName = trimFieldText(rest);
            } else if (key == "shape") {
                int shape = 0;
                if (iss >> shape) item.defaults.shape = (ForceFieldShape)std::clamp(shape, 0, 1);
            } else if (key == "width") {
                iss >> item.defaults.width;
            } else if (key == "height") {
                iss >> item.defaults.height;
            } else if (key == "radius") {
                iss >> item.defaults.radius;
            } else if (key == "strength") {
                iss >> item.defaults.strength;
            } else if (key == "buoyancy") {
                iss >> item.defaults.buoyancy;
            } else if (key == "damping") {
                iss >> item.defaults.damping;
            } else if (key == "vortex") {
                iss >> item.defaults.vortex;
            } else if (key == "dirx") {
                iss >> item.defaults.buoyancyDirection.x;
            } else if (key == "diry") {
                iss >> item.defaults.buoyancyDirection.y;
            }
        }
        if (kind < 0 || kind >= kForceFieldKindCount) {
            errors.push_back(file.filename().string() + ": kind 无效");
            continue;
        }
        item.kind = (ForceFieldKind)kind;
        item.defaults.kind = item.kind;
        if (item.kind != ForceFieldKind::Vortex) item.defaults.vortex = 0.0f;
        item.defaults.width = clampf(item.defaults.width, 20.0f, 20000.0f);
        item.defaults.height = clampf(item.defaults.height, 20.0f, 20000.0f);
        item.defaults.radius = clampf(item.defaults.radius, 10.0f, 20000.0f);
        item.defaults.strength = clampf(item.defaults.strength, 0.0f, 500.0f);
        item.defaults.buoyancy = clampf(item.defaults.buoyancy, 0.0f, 500.0f);
        item.defaults.damping = clampf(item.defaults.damping, 0.0f, 500.0f);
        item.defaults.vortex = clampf(item.defaults.vortex, -500.0f, 500.0f);
        if (item.displayName.empty())
            item.displayName = forceFieldKindName(item.kind);
        bool duplicate = false;
        for (const auto& existing : types)
            if (existing.kind == item.kind) duplicate = true;
        if (duplicate) {
            errors.push_back(file.filename().string() + ": 力场类型重复，已忽略");
            continue;
        }
        types.push_back(std::move(item));
    }
    return true;
}

bool ForceFieldLibrary::ensureDefaults() {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) return false;
    for (int i = 0; i < kForceFieldKindCount; ++i) {
        const std::filesystem::path path = folder / kFieldFileNames[i];
        if (std::filesystem::exists(path, ec)) continue;
        ForceField field;
        field.kind = kFieldKinds[i];
        field.shape = ForceFieldShape::Rectangle;
        field.width = 100.0f;
        field.height = 100.0f;
        field.radius = 50.0f;
        field.strength = field.kind == ForceFieldKind::Vortex       ? 0.0f
                         : field.kind == ForceFieldKind::Gravity      ? 24.0f
                         : field.kind == ForceFieldKind::Repulsion  ? 20.0f
                         : field.kind == ForceFieldKind::Drag       ? 10.0f
                                                                    : 18.0f;
        field.buoyancy = 1.2f;
        field.vortex = field.kind == ForceFieldKind::Vortex ? 20.0f : 0.0f;
        field.buoyancyDirection = {0.0f, 1.0f};
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            errors.push_back(path.filename().string() + ": 无法写入默认力场");
            continue;
        }
        out << "# PhiPhysiceEditorNeo field v1\n";
        out << "kind " << (int)field.kind << "\n";
        out << "name " << kFieldNames[i] << "\n";
        out << "shape " << (int)field.shape << "\n";
        out << "width " << field.width << "\n";
        out << "height " << field.height << "\n";
        out << "radius " << field.radius << "\n";
        out << "strength " << field.strength << "\n";
        out << "buoyancy " << field.buoyancy << "\n";
        out << "damping " << field.damping << "\n";
        out << "vortex " << field.vortex << "\n";
        out << "dirx " << field.buoyancyDirection.x << "\n";
        out << "diry " << field.buoyancyDirection.y << "\n";
    }
    return true;
}

const ForceFieldTemplate* ForceFieldLibrary::find(ForceFieldKind kind) const {
    for (const auto& type : types)
        if (type.kind == kind) return &type;
    return nullptr;
}

bool loadComponentDefaults(const std::filesystem::path& physicsDir,
                           const std::filesystem::path& sceneDir,
                           ComponentDefaults& out) {
    auto readFile = [](const std::filesystem::path& path,
                       const std::function<void(const std::string&, std::istringstream&)>& apply) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '') line.pop_back();
            line = trimFieldText(line);
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            std::string key;
            iss >> key;
            apply(key, iss);
        }
        return true;
    };

    const std::filesystem::path rivetPath = physicsDir / "Rivet.note";
    readFile(rivetPath, [&](const std::string& key, std::istringstream& iss) {
        if (key == "radius") {
            float radius = out.rivetRadius;
            if (iss >> radius) out.rivetRadius = clampf(radius, 2.0f, 500.0f);
        } else if (key == "color") {
            int r = 0, g = 0, b = 0;
            if (iss >> r >> g >> b)
                out.rivetColor = {(unsigned char)std::clamp(r, 0, 255),
                                  (unsigned char)std::clamp(g, 0, 255),
                                  (unsigned char)std::clamp(b, 0, 255)};
        }
    });

    const std::filesystem::path judgePath = sceneDir / "JudgeLine.note";
    return readFile(judgePath, [&](const std::string& key, std::istringstream& iss) {
        if (key == "width") {
            float width = out.judge.width;
            if (iss >> width) out.judge.width = clampf(width, 20.0f, 20000.0f);
        } else if (key == "height") {
            float height = out.judge.height;
            if (iss >> height) out.judge.height = clampf(height, 1.0f, 400.0f);
        } else if (key == "color") {
            int r = 0, g = 0, b = 0;
            if (iss >> r >> g >> b)
                out.judge.color = {(unsigned char)std::clamp(r, 0, 255),
                                   (unsigned char)std::clamp(g, 0, 255),
                                   (unsigned char)std::clamp(b, 0, 255)};
        }
    });
}

const char* gravityDirectionName(GravityDirection direction) {
    switch (direction) {
        case GravityDirection::Up: return "上";
        case GravityDirection::Down: return "下";
        case GravityDirection::Left: return "左";
        case GravityDirection::Right: return "右";
        case GravityDirection::UpLeft: return "左上";
        case GravityDirection::UpRight: return "右上";
        case GravityDirection::DownLeft: return "左下";
        case GravityDirection::DownRight: return "右下";
        case GravityDirection::None: return "无";
    }
    return "下";
}

Vec2 gravityDirectionVector(GravityDirection direction) {
    switch (direction) {
        case GravityDirection::Up: return {0.0f, 1.0f};
        case GravityDirection::Down: return {0.0f, -1.0f};
        case GravityDirection::Left: return {-1.0f, 0.0f};
        case GravityDirection::Right: return {1.0f, 0.0f};
        case GravityDirection::UpLeft: return {-kDiagonal, kDiagonal};
        case GravityDirection::UpRight: return {kDiagonal, kDiagonal};
        case GravityDirection::DownLeft: return {-kDiagonal, -kDiagonal};
        case GravityDirection::DownRight: return {kDiagonal, -kDiagonal};
        case GravityDirection::None: return {0.0f, 0.0f};
    }
    return {0.0f, -1.0f};
}

void Document::clear() {
    notes.clear();
    judges.clear();
    fields.clear();
    revJoints.clear();
    weldJoints.clear();
    nextNoteId = 1;
    nextJudgeId = 1;
    nextFieldId = 1;
    nextJointId = 1;
    nextGroupId = 1;
    nextChainId = 1;
    gravity = 9.8f;
    velocityIterations = 10;
    positionIterations = 4;
    slowStepGear = kSlowStepDefault;
    eventSplit = kEventSplitDefault;
    dirty = false;
    clearHistory();
}

void Document::makeDefaultScene() {
    clear();
    // A fresh project starts empty: only the judge line below the stage.
    addJudge({0.0f, -270.0f}, 4000.0f, 5.0f, 0.0f);
    dirty = false;
    clearHistory();
}

Note* Document::findNote(int id) {
    for (auto& n : notes)
        if (n.id == id) return &n;
    return nullptr;
}

const Note* Document::findNote(int id) const {
    for (const auto& n : notes)
        if (n.id == id) return &n;
    return nullptr;
}

JudgeLine* Document::findJudge(int id) {
    for (auto& j : judges)
        if (j.id == id) return &j;
    return nullptr;
}

const JudgeLine* Document::findJudge(int id) const {
    for (const auto& j : judges)
        if (j.id == id) return &j;
    return nullptr;
}

ForceField* Document::findField(int id) {
    for (auto& f : fields)
        if (f.id == id) return &f;
    return nullptr;
}

const ForceField* Document::findField(int id) const {
    for (const auto& f : fields)
        if (f.id == id) return &f;
    return nullptr;
}

int Document::addNote(const std::string& type, const Vec2& position, float angle,
                       bool scene) {
    Note n;
    n.id = nextNoteId++;
    n.type = type;
    n.position = position;
    n.angle = angle;
    n.scene = scene;
    notes.push_back(n);
    dirty = true;
    return n.id;
}

int Document::addJudge(const Vec2& position, float width, float height, float angle,
                       const Rgb& color) {
    JudgeLine j;
    j.id = nextJudgeId++;
    j.position = position;
    j.width = std::max(20.0f, width);
    j.height = std::max(1.0f, height);
    j.angle = angle;
    j.color = color;
    judges.push_back(j);
    dirty = true;
    return j.id;
}

int Document::addForceField(ForceField field) {
    field.id = nextFieldId++;
    field.width = std::max(20.0f, field.width);
    field.height = std::max(20.0f, field.height);
    field.radius = std::max(10.0f, field.radius);
    field.strength = std::max(0.0f, field.strength);
    field.buoyancy = std::max(0.0f, field.buoyancy);
    field.damping = std::max(0.0f, field.damping);
    field.vortex = field.kind == ForceFieldKind::Vortex
                       ? clampf(field.vortex, -500.0f, 500.0f)
                       : 0.0f;
    fields.push_back(field);
    dirty = true;
    return field.id;
}

std::vector<std::pair<int, int>> Document::renumberNotes() {
    std::vector<std::pair<int, int>> mapping;
    mapping.reserve(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        const int next = (int)i + 1;
        if (notes[i].id != next) mapping.emplace_back(notes[i].id, next);
        notes[i].id = next;
    }
    if (!mapping.empty()) {
        std::unordered_map<int, int> ids(mapping.begin(), mapping.end());
        auto remap = [&](int id) {
            const auto it = ids.find(id);
            return it == ids.end() ? id : it->second;
        };
        for (auto& joint : revJoints) {
            if (joint.noteA >= 0) joint.noteA = remap(joint.noteA);
            joint.noteB = remap(joint.noteB);
        }
        for (auto& joint : weldJoints) {
            joint.noteA = remap(joint.noteA);
            joint.noteB = remap(joint.noteB);
        }
    }
    nextNoteId = (int)notes.size() + 1;
    return mapping;
}

std::vector<std::pair<int, int>> Document::renumberJudges() {
    std::vector<std::pair<int, int>> mapping;
    mapping.reserve(judges.size());
    for (size_t i = 0; i < judges.size(); ++i) {
        const int next = (int)i + 1;
        if (judges[i].id != next) mapping.emplace_back(judges[i].id, next);
        judges[i].id = next;
    }
    nextJudgeId = (int)judges.size() + 1;
    return mapping;
}

std::vector<std::pair<int, int>> Document::renumberFields() {
    std::vector<std::pair<int, int>> mapping;
    mapping.reserve(fields.size());
    for (size_t i = 0; i < fields.size(); ++i) {
        const int next = (int)i + 1;
        if (fields[i].id != next) mapping.emplace_back(fields[i].id, next);
        fields[i].id = next;
    }
    nextFieldId = (int)fields.size() + 1;
    return mapping;
}

void Document::removeJointsForNote(int id) {
    revJoints.erase(std::remove_if(revJoints.begin(), revJoints.end(), [&](const RevJoint& j) {
                        return j.noteA == id || j.noteB == id;
                    }),
                    revJoints.end());
    weldJoints.erase(std::remove_if(weldJoints.begin(), weldJoints.end(), [&](const WeldJoint& j) {
                         return j.noteA == id || j.noteB == id;
                     }),
                     weldJoints.end());
}

void Document::removeNote(int id) {
    removeJointsForNote(id);
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const Note& n) { return n.id == id; }),
                notes.end());
    dirty = true;
}

void Document::removeJudge(int id) {
    judges.erase(std::remove_if(judges.begin(), judges.end(),
                                [&](const JudgeLine& j) { return j.id == id; }),
                 judges.end());
    dirty = true;
}

void Document::removeField(int id) {
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [&](const ForceField& f) { return f.id == id; }),
                 fields.end());
    dirty = true;
}

int Document::addRevJoint(int noteA, int noteB, const Vec2& worldAnchor,
                          bool collideConnected, int chainId) {
    Note* b = findNote(noteB);
    if (!b) return 0;
    RevJoint j;
    j.id = nextJointId++;
    j.noteA = noteA;
    j.noteB = noteB;
    if (noteA >= 0) {
        Note* a = findNote(noteA);
        if (!a) return 0;
        j.localA = rotateInverse(worldAnchor - a->position, a->angle);
        j.referenceAngle = wrapAngle(b->angle - a->angle);
    } else {
        j.localA = worldAnchor;
        j.referenceAngle = 0.0f;
    }
    j.localB = rotateInverse(worldAnchor - b->position, b->angle);
    j.collide = collideConnected;
    j.chain = chainId;
    revJoints.push_back(j);
    dirty = true;
    return j.id;
}

int Document::addWorldAnchorJoint(int noteB, const Vec2& worldAnchor) {
    return addRevJoint(-1, noteB, worldAnchor, false, 0);
}

int Document::addWeldJoint(int noteA, int noteB, int group, const Vec2& p1, const Vec2& p2) {
    Note* a = findNote(noteA);
    Note* b = findNote(noteB);
    if (!a || !b) return 0;
    WeldJoint j;
    j.id = nextJointId++;
    j.noteA = noteA;
    j.noteB = noteB;
    j.localA = rotateInverse(p1 - a->position, a->angle);
    j.localB = rotateInverse(p1 - b->position, b->angle);
    j.localA2 = rotateInverse(p2 - a->position, a->angle);
    j.localB2 = rotateInverse(p2 - b->position, b->angle);
    j.group = group;
    weldJoints.push_back(j);
    dirty = true;
    return j.id;
}

Vec2 Document::revAnchorWorld(const RevJoint& j, bool sideA) const {
    if (sideA) {
        if (j.noteA < 0) return j.localA;
        const Note* a = findNote(j.noteA);
        return a ? a->position + rotate(j.localA, a->angle) : j.localA;
    }
    const Note* b = findNote(j.noteB);
    return b ? b->position + rotate(j.localB, b->angle) : j.localB;
}

int Document::groupForNote(int id) const {
    const Note* n = findNote(id);
    return n ? (int)n->group : 0;
}

void Document::setGroupForNote(int id, int group) {
    if (Note* n = findNote(id)) n->group = group;
}

void Document::ungroup(int group) {
    if (group == 0) return;
    for (auto& n : notes)
        if (n.group == group) n.group = 0;
    weldJoints.erase(std::remove_if(weldJoints.begin(), weldJoints.end(),
                                    [&](const WeldJoint& j) { return j.group == group; }),
                     weldJoints.end());
    dirty = true;
}

DocumentState Document::snapshot() const {
    DocumentState s;
    s.notes = notes;
    s.judges = judges;
    s.fields = fields;
    s.revJoints = revJoints;
    s.weldJoints = weldJoints;
    s.nextNoteId = nextNoteId;
    s.nextJudgeId = nextJudgeId;
    s.nextFieldId = nextFieldId;
    s.nextJointId = nextJointId;
    s.nextGroupId = nextGroupId;
    s.nextChainId = nextChainId;
    s.gravity = gravity;
    s.velocityIterations = velocityIterations;
    s.positionIterations = positionIterations;
    s.slowStepGear = slowStepGear;
    s.eventSplit = eventSplit;
    return s;
}

void Document::restore(const DocumentState& s, bool mark) {
    notes = s.notes;
    judges = s.judges;
    fields = s.fields;
    revJoints = s.revJoints;
    weldJoints = s.weldJoints;
    nextNoteId = s.nextNoteId;
    nextJudgeId = s.nextJudgeId;
    nextFieldId = s.nextFieldId;
    nextJointId = s.nextJointId;
    nextGroupId = s.nextGroupId;
    nextChainId = s.nextChainId;
    gravity = s.gravity;
    velocityIterations = s.velocityIterations;
    positionIterations = s.positionIterations;
    slowStepGear = s.slowStepGear;
    eventSplit = std::max(1, std::min(kEventSplitMax, s.eventSplit));
    if (mark) dirty = true;
}

void Document::pushUndo() {
    undoStack_.push_back(snapshot());
    if (undoStack_.size() > kMaxHistory) undoStack_.pop_front();
    redoStack_.clear();
}

bool Document::undo() {
    if (undoStack_.empty()) return false;
    redoStack_.push_back(snapshot());
    DocumentState s = undoStack_.back();
    undoStack_.pop_back();
    restore(s);
    return true;
}

bool Document::redo() {
    if (redoStack_.empty()) return false;
    undoStack_.push_back(snapshot());
    DocumentState s = redoStack_.back();
    redoStack_.pop_back();
    restore(s);
    return true;
}

void Document::clearHistory() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace ppn