#include "io/ProjectIO.h"

#include "model/Easing.h"
#include "model/Events.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <tuple>

namespace fs = std::filesystem;

namespace ppn {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

bool readInt(std::istringstream& iss, int& v) {
    return static_cast<bool>(iss >> v);
}
bool readFloat(std::istringstream& iss, float& v) {
    return static_cast<bool>(iss >> v) && std::isfinite(v);
}

std::string timestamp(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return "";
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

} // namespace

bool validProjectName(const std::string& name, std::string* reason) {
    if (name.empty()) {
        if (reason) *reason = "工程名称不能为空";
        return false;
    }
    if (name.size() > 48) {
        if (reason) *reason = "工程名称过长";
        return false;
    }
    for (char c : name) {
        const bool ok = std::isalnum((unsigned char)c) || c == '_' || c == '-';
        if (!ok) {
            if (reason) *reason = "工程名称使用 ASCII 字母、数字、_ 和 -";
            return false;
        }
    }
    return true;
}

bool saveProject(const fs::path& path, const Document& doc, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "无法创建工程文件夹: " + ec.message();
        return false;
    }
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "无法写入工程文件";
            return false;
        }
        out << "PHIPHYSICE-PROJECT 17\n";
        out << "name " << doc.name << "\n";
        out << std::fixed << std::setprecision(3);
        out << "gravity " << doc.gravity << "\n";
        out << std::defaultfloat;
        out << "iterations " << doc.velocityIterations << " " << doc.positionIterations << "\n";
        out << "slowstep " << doc.slowStepGear << "\n";
        out << "eventsplit " << std::max(1, std::min(kEventSplitMax, doc.eventSplit)) << "\n";
        out << "nextids " << doc.nextNoteId << " " << doc.nextJudgeId << " "
            << doc.nextJointId << " " << doc.nextGroupId << " " << doc.nextChainId << " "
            << doc.nextFieldId << "\n";
        out << "[notes]\n";
        out << std::fixed << std::setprecision(3);
        for (const auto& n : doc.notes) {
            out << "note " << n.id << " " << n.type << " " << n.position.x << " "
                << n.position.y << " " << internalAngleToUserDegrees(n.angle) << " "
                << n.group << " "
                << (int)n.gravity << " " << (n.scene ? 1 : 0) << " "
                << clampf(n.radius, 2.0f, 500.0f) << " " << (int)n.color.r << " "
                << (int)n.color.g << " " << (int)n.color.b << " "
                << (n.ghost ? 1 : 0) << "\n";
        }
        out << "[notevars]\n";
        for (const auto& n : doc.notes) {
            for (const auto& [name, value] : n.variables) {
                out << "notevar " << n.id << " " << name << " " << value << "\n";
            }
        }
        out << "[judges]\n";
        for (const auto& j : doc.judges) {
            out << "judge " << j.id << " " << j.position.x << " " << j.position.y << " "
                << internalAngleToUserDegrees(j.angle) << " " << j.width << " "
                << j.height << " "
                << (int)j.color.r << " " << (int)j.color.g << " " << (int)j.color.b << " "
                << (j.ghost ? 1 : 0) << "\n";
        }
        out << "[fields]\n";
        for (const auto& f : doc.fields) {
            out << "field " << f.id << " " << (int)f.kind << " " << (int)f.shape << " "
                << f.position.x << " " << f.position.y << " "
                << internalAngleToUserDegrees(f.angle) << " " << f.width << " "
                << f.height << " " << f.radius << " " << f.strength << " "
                << f.buoyancyDirection.x << " " << f.buoyancyDirection.y << " "
                << f.buoyancy << " " << f.damping << " " << f.vortex << "\n";
        }
        out << "[rev]\n";
        for (const auto& j : doc.revJoints) {
            out << "rev " << j.id << " " << j.noteA << " " << j.noteB << " "
                << j.localA.x << " " << j.localA.y << " " << j.localB.x << " "
                << j.localB.y << " " << j.referenceAngle * RAD2DEG << " "
                << (j.collide ? 1 : 0) << " " << j.chain << "\n";
        }
        out << "[weld]\n";
        for (const auto& j : doc.weldJoints) {
            out << "weld " << j.id << " " << j.noteA << " " << j.noteB << " "
                << j.localA.x << " " << j.localA.y << " " << j.localB.x << " "
                << j.localB.y << " " << j.localA2.x << " " << j.localA2.y << " "
                << j.localB2.x << " " << j.localB2.y << " " << j.group << "\n";
        }
        out << "[events]\n";
        auto writeEvents = [&](const char* owner, int id, const std::vector<Event>& events) {
            for (const auto& event : events) {
                out << owner << " " << id << " " << std::max(0, std::min(kEventTrackCount - 1,
                                                                        event.track))
                    << " " << (int)event.type << " " << std::max(0.0f, event.startBeat)
                    << " " << std::max(event.startBeat + eventSnapBeats(doc.eventSplit),
                                       event.endBeat)
                    << " " << std::max(1, std::min(kEasingCount, event.easing)) << " "
                    << compactExpression(event.startValue.empty() ? "this" : event.startValue)
                    << " "
                    << compactExpression(event.endValue.empty() ? "this" : event.endValue)
                    << " " << (event.instant ? 1 : 0) << " "
                    << compactExpression(event.angleValue.empty() ? "this" : event.angleValue)
                    << "\n";
            }
        };
        for (const auto& n : doc.notes) writeEvents("note", n.id, n.events);
        for (const auto& j : doc.judges) writeEvents("judge", j.id, j.events);
        for (const auto& f : doc.fields) writeEvents("field", f.id, f.events);
        out << "[end]\n";
        out.flush();
        if (!out.good()) {
            out.close();
            fs::remove(temp, ec);
            error = "写入工程文件失败";
            return false;
        }
    }
    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "保存工程失败: " + ec.message();
        return false;
    }
    return true;
}

bool loadProject(const fs::path& path, Document& doc, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "无法打开工程文件";
        return false;
    }
    Document temp;
    temp.clear();
    temp.name = path.stem().string();
    enum class Section { None, Notes, NoteVars, Judges, Floors, Fields, Rev, Weld, Events, End };
    Section section = Section::None;
    std::string line;
    int lineNo = 0;
    bool headerSeen = false;
    int fileVersion = 0;
    std::vector<std::tuple<int, std::string, float>> noteVariables;
    // Version 1 projects stored static platforms in a [floors] section. They are
    // still parsed and converted into judge lines so old files keep working.
    // Versions 1-2 use Y-down design coordinates; version 3+ is Y-up. Version 4
    // adds the [notevars] section, version 5 adds per-note gravity, version 6
    // adds world-anchored force fields, version 7 adds scene blocks, version 8
    // adds gravity-field damping plus the drag / gravity-change field kinds,
    // version 9 adds the slow-step switch, version 10 stores the slow-step
    // gear, real buoyancy fluid density and per-rivet radius / colour,
    // version 11 adds component event timelines, version 12 switches angle
    // display to 0-up, version 13 keeps 0 as the component's default
    // rotation while direction fields still use 0 = up, version 14 adds
    // the gravity-direction event type for physics notes and rivets,
    // version 15 adds momentum events plus the per-event instant switch,
    // version 16 adds the authored ghost flag for notes, scene blocks and
    // judge lines, and version 17 adds the signed vortex field parameter.

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "[notes]") { section = Section::Notes; continue; }
        if (line == "[notevars]") { section = Section::NoteVars; continue; }
        if (line == "[judges]") { section = Section::Judges; continue; }
        if (line == "[floors]") { section = Section::Floors; continue; }
        if (line == "[fields]") { section = Section::Fields; continue; }
        if (line == "[rev]") { section = Section::Rev; continue; }
        if (line == "[weld]") { section = Section::Weld; continue; }
        if (line == "[events]") { section = Section::Events; continue; }
        if (line == "[end]") { section = Section::End; continue; }

        std::istringstream iss(line);
        std::string key;
        iss >> key;

        // The current header is preferred, but projects written before the
        // rename keep loading with their original magic string.
        if (key == "PHIPHYSICE-PROJECT" || key == "POLYNOTE-PROJECT") {
            int version = 0;
            if (!readInt(iss, version) || (version < 1 || version > 17)) {
                error = "不支持的工程文件版本";
                return false;
            }
            fileVersion = version;
            headerSeen = true;
            continue;
        }
        if (!headerSeen) {
            error = "文件头无效，不是 PhiPhysiceEditorNeo 工程文件";
            return false;
        }
        if (key == "name") {
            std::string rest;
            std::getline(iss, rest);
            rest = trim(rest);
            if (!rest.empty()) temp.name = rest;
            continue;
        }
        if (key == "gravity") {
            float value = 0.0f;
            if (readFloat(iss, value)) temp.gravity = clampf(value, 0.0f, 60.0f);
            continue;
        }
        if (key == "iterations") {
            int velocity = 10, position = 4;
            if (readInt(iss, velocity) && readInt(iss, position)) {
                temp.velocityIterations = std::max(4, std::min(20, velocity));
                temp.positionIterations = std::max(2, std::min(10, position));
            }
            continue;
        }
        if (key == "slowstep") {
            int slow = 0;
            if (fileVersion >= 10) {
                if (readInt(iss, slow)) {
                    slow = std::max(0, std::min(kSlowStepMax, slow));
                    slow = (slow + kSlowStepStep / 2) / kSlowStepStep * kSlowStepStep;
                    temp.slowStepGear = std::max(0, std::min(kSlowStepMax, slow));
                }
            } else if (fileVersion >= 9 && readInt(iss, slow)) {
                // Version 9 stored a bool; map it onto the default gear.
                temp.slowStepGear = slow != 0 ? kSlowStepDefault : 0;
            }
            continue;
        }
        if (key == "eventsplit") {
            int split = kEventSplitDefault;
            if (readInt(iss, split))
                temp.eventSplit = std::max(1, std::min(kEventSplitMax, split));
            continue;
        }
        if (key == "nextids") {
            int a = 1, b = 1, c = 1, d = 1, e = 1;
            int judge = 1;
            const bool ok = readInt(iss, a) && readInt(iss, b) && readInt(iss, c) &&
                            readInt(iss, d) && readInt(iss, e);
            if (!ok) continue;
            temp.nextNoteId = std::max(1, a);
            temp.nextJudgeId = std::max(1, b);
            temp.nextJointId = std::max(1, c);
            temp.nextGroupId = std::max(1, d);
            temp.nextChainId = std::max(1, e);
            if (readInt(iss, judge)) {
                // Version 6 reuses the trailing slot for the force-field id.
                if (fileVersion >= 6)
                    temp.nextFieldId = std::max(1, judge);
                else
                    temp.nextJudgeId = std::max(1, judge);
            }
            continue;
        }

        if (section == Section::Notes && key == "note") {
            Note n;
            float angleDeg = 0.0f;
            if (!(iss >> n.id >> n.type) || !readFloat(iss, n.position.x) ||
                !readFloat(iss, n.position.y) || !readFloat(iss, angleDeg) ||
                !readInt(iss, n.group)) {
                error = "第 " + std::to_string(lineNo) + " 行：音符数据无效";
                return false;
            }
            // Version 1-3 note lines may end with a Hold height; it is ignored
            // because Hold height is now expressed through per-note variables.
            // Version 5 appends the per-note gravity direction.
            if (fileVersion >= 5) {
                int gravity = (int)GravityDirection::Down;
                if (!readInt(iss, gravity) || gravity < 0 ||
                    gravity >= kGravityDirectionCount) {
                    error = "第 " + std::to_string(lineNo) + " 行：音符重力方向无效";
                    return false;
                }
                n.gravity = (GravityDirection)gravity;
            }
            if (fileVersion >= 7) {
                int scene = 0;
                if (readInt(iss, scene)) n.scene = scene != 0;
            }
            if (fileVersion >= 10) {
                float radius = 15.0f;
                int r = 172, g = 178, b = 190;
                if (readFloat(iss, radius)) n.radius = clampf(radius, 2.0f, 500.0f);
                if ((iss >> r >> g >> b) && r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
                    b >= 0 && b <= 255) {
                    n.color = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
                }
            }
            if (fileVersion >= 16) {
                int ghost = 0;
                if (readInt(iss, ghost)) n.ghost = ghost != 0;
            }
            n.angle = fileVersion >= 13
                          ? userDegreesToInternalAngle(angleDeg)
                          : (fileVersion == 12 ? wrapAngle((90.0f - angleDeg) * DEG2RAD)
                                               : angleDeg * DEG2RAD);
            temp.notes.push_back(n);
            temp.nextNoteId = std::max(temp.nextNoteId, n.id + 1);
        } else if (section == Section::NoteVars && key == "notevar") {
            int noteId = 0;
            std::string name;
            float value = 0.0f;
            if (!(iss >> noteId >> name) || !readFloat(iss, value)) {
                error = "第 " + std::to_string(lineNo) + " 行：音符变量数据无效";
                return false;
            }
            if (validVariableName(name) && std::isfinite(value))
                noteVariables.emplace_back(noteId, name, value);
        } else if (section == Section::Judges && key == "judge") {
            JudgeLine j;
            float angleDeg = 0.0f;
            if (!(iss >> j.id) || !readFloat(iss, j.position.x) ||
                !readFloat(iss, j.position.y) || !readFloat(iss, angleDeg) ||
                !readFloat(iss, j.width) || !readFloat(iss, j.height)) {
                error = "第 " + std::to_string(lineNo) + " 行：判定线数据无效";
                return false;
            }
            j.angle = fileVersion >= 13
                          ? userDegreesToInternalAngle(angleDeg)
                          : (fileVersion == 12 ? wrapAngle((90.0f - angleDeg) * DEG2RAD)
                                               : angleDeg * DEG2RAD);
            j.width = std::max(20.0f, j.width);
            j.height = std::max(1.0f, j.height);
            int r = 0, g = 0, b = 0;
            if ((iss >> r >> g >> b) && r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
                b >= 0 && b <= 255) {
                j.color = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
            }
            if (fileVersion >= 16) {
                int ghost = 0;
                if (readInt(iss, ghost)) j.ghost = ghost != 0;
            }
            temp.judges.push_back(j);
            temp.nextJudgeId = std::max(temp.nextJudgeId, j.id + 1);
        } else if (section == Section::Floors && key == "floor") {
            // Legacy static platform: becomes a judge line of the same size.
            JudgeLine j;
            float angleDeg = 0.0f;
            if (!(iss >> j.id) || !readFloat(iss, j.position.x) ||
                !readFloat(iss, j.position.y) || !readFloat(iss, angleDeg) ||
                !readFloat(iss, j.width) || !readFloat(iss, j.height)) {
                error = "第 " + std::to_string(lineNo) + " 行：平台数据无效";
                return false;
            }
            j.angle = fileVersion >= 13
                          ? userDegreesToInternalAngle(angleDeg)
                          : (fileVersion == 12 ? wrapAngle((90.0f - angleDeg) * DEG2RAD)
                                               : angleDeg * DEG2RAD);
            j.width = std::max(20.0f, j.width);
            j.height = std::max(1.0f, j.height);
            j.color = {255, 238, 150};
            temp.judges.push_back(j);
            temp.nextJudgeId = std::max(temp.nextJudgeId, j.id + 1);
        } else if (section == Section::Fields && key == "field") {
            ForceField f;
            int kind = 0;
            int shape = 0;
            float angleDeg = 0.0f;
            if (!(iss >> f.id >> kind >> shape) || !readFloat(iss, f.position.x) ||
                !readFloat(iss, f.position.y) || !readFloat(iss, angleDeg) ||
                !readFloat(iss, f.width) || !readFloat(iss, f.height) ||
                !readFloat(iss, f.radius) || !readFloat(iss, f.strength) ||
                !readFloat(iss, f.buoyancyDirection.x) ||
                !readFloat(iss, f.buoyancyDirection.y) || !readFloat(iss, f.buoyancy)) {
                error = "第 " + std::to_string(lineNo) + " 行：力场数据无效";
                return false;
            }
            if (kind < 0 || kind >= kForceFieldKindCount || shape < 0 ||
                shape >= kForceFieldShapeCount) {
                error = "第 " + std::to_string(lineNo) + " 行：力场类型无效";
                return false;
            }
            f.kind = (ForceFieldKind)kind;
            f.shape = (ForceFieldShape)shape;
            f.angle = fileVersion >= 13
                          ? userDegreesToInternalAngle(angleDeg)
                          : (fileVersion == 12 ? wrapAngle((90.0f - angleDeg) * DEG2RAD)
                                               : angleDeg * DEG2RAD);
            f.width = clampf(f.width, 20.0f, 20000.0f);
            f.height = clampf(f.height, 20.0f, 20000.0f);
            f.radius = clampf(f.radius, 10.0f, 20000.0f);
            f.strength = clampf(f.strength, 0.0f, 500.0f);
            f.buoyancy = clampf(f.buoyancy, 0.0f, 500.0f);
            if (fileVersion >= 10) {
                float damping = 0.0f;
                if (readFloat(iss, damping)) f.damping = clampf(damping, 0.0f, 500.0f);
            } else {
                // Versions 7-9 stored the now-removed realistic-buoyancy flag,
                // gravity damping and buoyancy bottom drag in that order. Read
                // and discard the retired tokens so old projects keep loading.
                if (fileVersion >= 7) {
                    int realistic = 0;
                    readInt(iss, realistic);
                }
                if (fileVersion >= 8) {
                    float damping = 0.0f;
                    if (readFloat(iss, damping))
                        f.damping = clampf(damping, 0.0f, 500.0f);
                }
                if (fileVersion >= 9) {
                    float retiredDrag = 0.0f;
                    readFloat(iss, retiredDrag);
                }
                // Old buoyancy values were accelerations in m/s^2. Convert
                // them to a fluid density so the real Archimedes formula gives
                // a comparable lift for default-density notes.
                if (f.kind == ForceFieldKind::Buoyancy)
                    f.buoyancy = clampf(f.buoyancy / 9.8f, 0.0f, 500.0f);
            }
            if (fileVersion >= 17) {
                float vortex = 0.0f;
                if (readFloat(iss, vortex) && f.kind == ForceFieldKind::Vortex)
                    f.vortex = clampf(vortex, -500.0f, 500.0f);
            }
            if (f.kind != ForceFieldKind::Vortex) f.vortex = 0.0f;
            temp.fields.push_back(f);
            temp.nextFieldId = std::max(temp.nextFieldId, f.id + 1);
        } else if (section == Section::Rev && key == "rev") {
            RevJoint j;
            float refDeg = 0.0f;
            int collide = 0;
            if (!(iss >> j.id >> j.noteA >> j.noteB) ||
                !readFloat(iss, j.localA.x) || !readFloat(iss, j.localA.y) ||
                !readFloat(iss, j.localB.x) || !readFloat(iss, j.localB.y) ||
                !readFloat(iss, refDeg) || !readInt(iss, collide) ||
                !readInt(iss, j.chain)) {
                error = "第 " + std::to_string(lineNo) + " 行：铰链数据无效";
                return false;
            }
            j.referenceAngle = refDeg * DEG2RAD;
            j.collide = collide != 0;
            temp.revJoints.push_back(j);
            temp.nextJointId = std::max(temp.nextJointId, j.id + 1);
        } else if (section == Section::Weld && key == "weld") {
            WeldJoint j;
            if (!(iss >> j.id >> j.noteA >> j.noteB) ||
                !readFloat(iss, j.localA.x) || !readFloat(iss, j.localA.y) ||
                !readFloat(iss, j.localB.x) || !readFloat(iss, j.localB.y) ||
                !readFloat(iss, j.localA2.x) || !readFloat(iss, j.localA2.y) ||
                !readFloat(iss, j.localB2.x) || !readFloat(iss, j.localB2.y) ||
                !readInt(iss, j.group)) {
                error = "第 " + std::to_string(lineNo) + " 行：焊接数据无效";
                return false;
            }
            temp.weldJoints.push_back(j);
            temp.nextJointId = std::max(temp.nextJointId, j.id + 1);
        } else if (section == Section::Events &&
                   (key == "note" || key == "judge" || key == "field")) {
            int ownerId = 0;
            int track = 0;
            int type = 0;
            int easing = 1;
            float startBeat = 0.0f;
            float endBeat = 0.0f;
            std::string startValue;
            std::string endValue;
            std::string angleValue;
            int instant = 0;
            if (!(iss >> ownerId >> track >> type) || !readFloat(iss, startBeat) ||
                !readFloat(iss, endBeat) || !readInt(iss, easing)) {
                error = "第 " + std::to_string(lineNo) + " 行：事件数据无效";
                return false;
            }
            if (!(iss >> startValue)) startValue = "this";
            if (!(iss >> endValue)) endValue = "this";
            // Version 15 adds these two trailing fields; older files keep the
            // default non-instant behaviour and the angle defaults to "this".
            if (!(iss >> instant)) instant = 0;
            if (!(iss >> angleValue)) angleValue = "this";
            if (type < 0 || type >= kEventTypeCount) {
                error = "第 " + std::to_string(lineNo) + " 行：事件类型无效";
                return false;
            }
            Event event;
            event.track = std::max(0, std::min(kEventTrackCount - 1, track));
            event.type = (EventType)type;
            event.startBeat = std::max(0.0f, startBeat);
            event.endBeat =
                std::max(event.startBeat + eventSnapBeats(temp.eventSplit), endBeat);
            event.easing = std::max(1, std::min(kEasingCount, easing));
            event.startValue = startValue.empty() ? "this" : startValue;
            event.endValue = endValue.empty() ? "this" : endValue;
            event.instant = instant != 0 || eventTypeIsInstantOnly(event.type);
            event.angleValue = angleValue.empty() ? "this" : angleValue;
            if (key == "note") {
                if (Note* note = temp.findNote(ownerId)) note->events.push_back(event);
            } else if (key == "judge") {
                if (JudgeLine* judge = temp.findJudge(ownerId))
                    judge->events.push_back(event);
            } else if (ForceField* field = temp.findField(ownerId)) {
                field->events.push_back(event);
            }
        }
    }

    if (!headerSeen) {
        error = "文件头无效，不是 PhiPhysiceEditorNeo 工程文件";
        return false;
    }
    // Convert legacy Y-down layouts to the Y-up convention used by the editor.
    // Flipping Y also mirrors rotations, hence the angle and local-anchor flips.
    if (fileVersion > 0 && fileVersion < 3) {
        for (auto& n : temp.notes) {
            n.position.y = -n.position.y;
            n.angle = -n.angle;
        }
        for (auto& j : temp.judges) {
            j.position.y = -j.position.y;
            j.angle = -j.angle;
        }
        for (auto& j : temp.revJoints) {
            j.localA.y = -j.localA.y;
            j.localB.y = -j.localB.y;
            j.referenceAngle = -j.referenceAngle;
        }
        for (auto& j : temp.weldJoints) {
            j.localA.y = -j.localA.y;
            j.localB.y = -j.localB.y;
            j.localA2.y = -j.localA2.y;
            j.localB2.y = -j.localB2.y;
        }
    }
    // Legacy v1 files could reference note types that no longer exist (only
    // Tap / Drag / Flick / Hold remain). Drop those notes and any joint that
    // depended on them instead of rejecting the whole project.
    const auto supported = [](const std::string& type) {
        return type == "Tap" || type == "Drag" || type == "Flick" || type == "Hold" ||
               type == "Rivet";
    };
    temp.notes.erase(std::remove_if(temp.notes.begin(), temp.notes.end(),
                                    [&](const Note& n) { return !supported(n.type); }),
                     temp.notes.end());
    const auto noteExists = [&](int id) { return temp.findNote(id) != nullptr; };
    temp.revJoints.erase(
        std::remove_if(temp.revJoints.begin(), temp.revJoints.end(),
                       [&](const RevJoint& j) {
                           return !noteExists(j.noteB) || (j.noteA >= 0 && !noteExists(j.noteA));
                       }),
        temp.revJoints.end());
    temp.weldJoints.erase(std::remove_if(temp.weldJoints.begin(), temp.weldJoints.end(),
                                         [&](const WeldJoint& j) {
                                             return !noteExists(j.noteA) || !noteExists(j.noteB);
                                         }),
                          temp.weldJoints.end());
    for (const auto& [noteId, name, value] : noteVariables) {
        if (Note* note = temp.findNote(noteId)) note->variables[name] = value;
    }
    if (fileVersion < 9) temp.slowStepGear = 0;
    temp.name = temp.name.empty() ? path.stem().string() : temp.name;
    temp.dirty = false;
    temp.clearHistory();
    doc = std::move(temp);
    return true;
}

std::vector<ProjectInfo> listProjects(const fs::path& folder) {
    std::vector<ProjectInfo> result;
    std::error_code ec;
    fs::create_directories(folder, ec);
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        if (it->path().extension() != ".pnp") continue;
        ProjectInfo info;
        info.name = it->path().stem().string();
        info.path = it->path();
        info.modified = timestamp(it->path());
        result.push_back(info);
    }
    std::sort(result.begin(), result.end(), [](const ProjectInfo& a, const ProjectInfo& b) {
        return a.name < b.name;
    });
    return result;
}

bool deleteProject(const fs::path& path, std::string& error) {
    std::error_code ec;
    if (!fs::remove(path, ec) || ec) {
        error = "删除工程失败: " + ec.message();
        return false;
    }
    return true;
}

} // namespace ppn