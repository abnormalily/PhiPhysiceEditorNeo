#include "app/Settings.h"

#include "core/Math.h"
#include "raylib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace ppn {
namespace {

constexpr int kMaxShortcutKey = 512;

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

const char* kShortcutIds[kShortcutCount] = {
    "play_pause",       "reset_simulation", "select_tool",      "judge_tool",
    "previous_component", "place_component", "next_component",  "rotate_left",
    "rotate_right",     "delete_selection", "focus_selection", "reset_view",
    "toggle_grid",      "toggle_joints",    "toggle_labels",    "toggle_contacts",
    "undo",             "redo",             "save",             "save_as",
    "new_project",      "select_all"};

const char* kShortcutLabels[kShortcutCount] = {
    "播放 / 暂停", "重置模拟", "选择工具", "判定线工具",
    "上一个原件", "放置原件", "下一个原件", "向左旋转",
    "向右旋转",    "删除所选", "聚焦所选", "重置视图",
    "显示网格",    "显示关节", "显示标签", "显示接触点",
    "撤销",        "重做",     "保存工程", "另存为",
    "新建工程",    "全选音符"};

static_assert(sizeof(kShortcutIds) / sizeof(kShortcutIds[0]) == kShortcutCount,
              "shortcut id table must match ShortcutAction");
static_assert(sizeof(kShortcutLabels) / sizeof(kShortcutLabels[0]) == kShortcutCount,
              "shortcut label table must match ShortcutAction");

int keyForLetter(char c) { return (int)c; }

} // namespace

KeyBinding defaultShortcut(ShortcutAction action) {
    KeyBinding b;
    switch (action) {
        case ShortcutAction::PlayPause: b.key = KEY_SPACE; break;
        case ShortcutAction::ResetSimulation: b.key = KEY_R; break;
        case ShortcutAction::SelectTool: b.key = KEY_V; break;
        case ShortcutAction::JudgeTool: b.key = KEY_B; break;
        case ShortcutAction::PreviousComponent: b.key = KEY_ONE; break;
        case ShortcutAction::PlaceComponent: b.key = KEY_TWO; break;
        case ShortcutAction::NextComponent: b.key = KEY_THREE; break;
        case ShortcutAction::RotateLeft: b.key = KEY_Q; break;
        case ShortcutAction::RotateRight: b.key = KEY_E; break;
        case ShortcutAction::DeleteSelection: b.key = KEY_DELETE; break;
        case ShortcutAction::FocusSelection: b.key = KEY_F; break;
        case ShortcutAction::ResetView: b.key = KEY_ZERO; break;
        case ShortcutAction::ToggleGrid: b.key = KEY_G; break;
        case ShortcutAction::ToggleJoints: b.key = KEY_J; break;
        case ShortcutAction::ToggleLabels: b.key = KEY_L; break;
        case ShortcutAction::ToggleContacts: b.key = KEY_C; break;
        case ShortcutAction::Undo: b.key = KEY_Z; b.ctrl = true; break;
        case ShortcutAction::Redo: b.key = KEY_Y; b.ctrl = true; break;
        case ShortcutAction::Save: b.key = KEY_S; b.ctrl = true; break;
        case ShortcutAction::SaveAs: b.key = KEY_S; b.ctrl = true; b.shift = true; break;
        case ShortcutAction::NewProject: b.key = KEY_N; b.ctrl = true; break;
        case ShortcutAction::SelectAll: b.key = KEY_A; b.ctrl = true; break;
        default: break;
    }
    return b;
}

std::string keyName(int key) {
    if (key == 0) return "未设置";
    if (key >= KEY_A && key <= KEY_Z) return std::string(1, (char)keyForLetter((char)key));
    if (key >= KEY_ZERO && key <= KEY_NINE) return std::string(1, (char)key);
    if (key >= KEY_F1 && key <= KEY_F12)
        return "F" + std::to_string(key - KEY_F1 + 1);
    if (key >= KEY_KP_0 && key <= KEY_KP_9)
        return "Num" + std::to_string(key - KEY_KP_0);
    switch (key) {
        case KEY_SPACE: return "Space";
        case KEY_ESCAPE: return "Esc";
        case KEY_ENTER: return "Enter";
        case KEY_KP_ENTER: return "NumEnter";
        case KEY_TAB: return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_INSERT: return "Insert";
        case KEY_DELETE: return "Delete";
        case KEY_RIGHT: return "Right";
        case KEY_LEFT: return "Left";
        case KEY_DOWN: return "Down";
        case KEY_UP: return "Up";
        case KEY_PAGE_UP: return "PageUp";
        case KEY_PAGE_DOWN: return "PageDown";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_CAPS_LOCK: return "Caps";
        case KEY_SCROLL_LOCK: return "Scroll";
        case KEY_NUM_LOCK: return "NumLock";
        case KEY_PRINT_SCREEN: return "Print";
        case KEY_PAUSE: return "Pause";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_LEFT_BRACKET: return "[";
        case KEY_RIGHT_BRACKET: return "]";
        case KEY_BACKSLASH: return "\\";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_GRAVE: return "`";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";
        case KEY_KP_DECIMAL: return "Num.";
        case KEY_KP_DIVIDE: return "Num/";
        case KEY_KP_MULTIPLY: return "Num*";
        case KEY_KP_SUBTRACT: return "Num-";
        case KEY_KP_ADD: return "Num+";
        default: break;
    }
    return "Key" + std::to_string(key);
}

std::string bindingLabel(const KeyBinding& binding) {
    if (binding.key == 0) return "未设置";
    std::string out;
    if (binding.ctrl) out += "Ctrl + ";
    if (binding.shift) out += "Shift + ";
    out += keyName(binding.key);
    return out;
}

const char* shortcutId(ShortcutAction action) {
    const int index = (int)action;
    if (index < 0 || index >= kShortcutCount) return "";
    return kShortcutIds[index];
}

const char* shortcutLabel(ShortcutAction action) {
    const int index = (int)action;
    if (index < 0 || index >= kShortcutCount) return "";
    return kShortcutLabels[index];
}

Settings::Settings() { resetShortcuts(); }

void Settings::resetShortcuts() {
    for (int i = 0; i < kShortcutCount; ++i)
        shortcuts[i] = defaultShortcut((ShortcutAction)i);
}

bool loadSettings(const fs::path& path, Settings& settings) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;

    Settings loaded;
    bool headerSeen = false;
    bool shortcutsSeen = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "[shortcuts]") {
            shortcutsSeen = true;
            continue;
        }
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        // Keep reading settings written by builds released under the old name.
        if (key == "PHIPHYSICE-SETTINGS" || key == "POLYNOTE-SETTINGS") {
            int version = 0;
            if (!(iss >> version) || version != 1) return false;
            headerSeen = true;
            continue;
        }
        if (!headerSeen) return false;
        if (key == "light") {
            int value = 0;
            if (iss >> value) loaded.lightMode = value != 0;
        } else if (key == "accent") {
            int value = 0;
            if (iss >> value) loaded.accent = value;
        } else if (key == "show_grid") {
            int value = 1;
            if (iss >> value) loaded.showGrid = value != 0;
        } else if (key == "show_joints") {
            int value = 1;
            if (iss >> value) loaded.showJoints = value != 0;
        } else if (key == "show_labels") {
            int value = 1;
            if (iss >> value) loaded.showLabels = value != 0;
        } else if (key == "show_wireframe") {
            int value = 0;
            if (iss >> value) loaded.showWireframe = value != 0;
        } else if (key == "show_contacts") {
            int value = 0;
            if (iss >> value) loaded.showContactPoints = value != 0;
        } else if (key == "show_grid_labels") {
            int value = 1;
            if (iss >> value) loaded.showGridLabels = value != 0;
        } else if (key == "gravity") {
            float value = 9.8f;
            if (iss >> value && std::isfinite(value)) loaded.gravity = clampf(value, 0.0f, 60.0f);
        } else if (key == "velocity_iterations") {
            int value = 10;
            if (iss >> value) loaded.velocityIterations = std::max(4, std::min(20, value));
        } else if (key == "position_iterations") {
            int value = 4;
            if (iss >> value) loaded.positionIterations = std::max(2, std::min(10, value));
        } else if (key == "slow_step_gear") {
            int value = 10;
            if (iss >> value) {
                value = std::max(0, std::min(50, value));
                value = (value + 2) / 5 * 5;
                loaded.slowStepGear = std::max(0, std::min(50, value));
            }
        } else if (key == "bpm") {
            float value = 180.0f;
            if (iss >> value && std::isfinite(value)) loaded.bpm = clampf(value, 1.0f, 1000.0f);
        } else if (key == "rivet_radius") {
            float value = 15.0f;
            if (iss >> value && std::isfinite(value)) loaded.rivetRadius = clampf(value, 2.0f, 500.0f);
        } else if (key == "export_directory") {
            std::string rest;
            std::getline(iss, rest);
            loaded.exportDirectory = trim(rest);
        } else if (shortcutsSeen) {
            int bindingKey = 0;
            int ctrl = 0;
            int shift = 0;
            if (!(iss >> bindingKey >> ctrl >> shift)) continue;
            // Unknown ids (including the retired F1 open_settings entry) are
            // ignored, so existing settings files keep their other bindings.
            for (int i = 0; i < kShortcutCount; ++i) {
                if (key != kShortcutIds[i]) continue;
                if (bindingKey < 0 || bindingKey > kMaxShortcutKey) bindingKey = 0;
                loaded.shortcuts[i] = KeyBinding{bindingKey, ctrl != 0, shift != 0};
                break;
            }
        }
    }
    if (!headerSeen) return false;
    if (loaded.accent < 0 || loaded.accent >= kAccentCount) loaded.accent = 0;
    settings = loaded;
    return true;
}

bool saveSettings(const fs::path& path, const Settings& settings, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "无法写入设置文件";
            return false;
        }
        out << "PHIPHYSICE-SETTINGS 1\n";
        out << "light " << (settings.lightMode ? 1 : 0) << "\n";
        out << "accent " << settings.accent << "\n";
        out << "show_grid " << (settings.showGrid ? 1 : 0) << "\n";
        out << "show_joints " << (settings.showJoints ? 1 : 0) << "\n";
        out << "show_labels " << (settings.showLabels ? 1 : 0) << "\n";
        out << "show_wireframe " << (settings.showWireframe ? 1 : 0) << "\n";
        out << "show_contacts " << (settings.showContactPoints ? 1 : 0) << "\n";
        out << "show_grid_labels " << (settings.showGridLabels ? 1 : 0) << "\n";
        out << "gravity " << settings.gravity << "\n";
        out << "velocity_iterations " << settings.velocityIterations << "\n";
        out << "position_iterations " << settings.positionIterations << "\n";
        out << "slow_step_gear " << settings.slowStepGear << "\n";
        out << "bpm " << settings.bpm << "\n";
        out << "rivet_radius " << settings.rivetRadius << "\n";
        out << "export_directory " << settings.exportDirectory << "\n";
        out << "[shortcuts]\n";
        for (int i = 0; i < kShortcutCount; ++i) {
            const KeyBinding& b = settings.shortcuts[i];
            out << kShortcutIds[i] << " " << b.key << " " << (b.ctrl ? 1 : 0) << " "
                << (b.shift ? 1 : 0) << "\n";
        }
        out.flush();
        if (!out.good()) {
            out.close();
            fs::remove(temp, ec);
            error = "写入设置文件失败";
            return false;
        }
    }
    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "保存设置失败: " + ec.message();
        return false;
    }
    return true;
}

} // namespace ppn
