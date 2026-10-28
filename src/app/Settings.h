#pragma once
// Persistent editor preferences: day/night mode, accent colour, display
// switches and the user-rebindable keyboard shortcuts stored in settings.ini.

#include <filesystem>
#include <string>

namespace ppn {

enum class ShortcutAction {
    PlayPause,
    ResetSimulation,
    SelectTool,
    JudgeTool,
    PreviousComponent,
    PlaceComponent,
    NextComponent,
    RotateLeft,
    RotateRight,
    DeleteSelection,
    FocusSelection,
    ResetView,
    ToggleGrid,
    ToggleJoints,
    ToggleLabels,
    ToggleContacts,
    Undo,
    Redo,
    Save,
    SaveAs,
    NewProject,
    SelectAll,
    Count
};

inline constexpr int kShortcutCount = (int)ShortcutAction::Count;

struct KeyBinding {
    int key = 0; // raylib key code; 0 means unassigned
    bool ctrl = false;
    bool shift = false;
};

// A human readable label for a key code ("Space", "Delete", "F1", "A").
std::string keyName(int key);
// "Ctrl + Shift + S", or "未设置" when the binding is empty.
std::string bindingLabel(const KeyBinding& binding);
// Stable ASCII id used by settings.ini.
const char* shortcutId(ShortcutAction action);
// Chinese label shown in the settings window.
const char* shortcutLabel(ShortcutAction action);
KeyBinding defaultShortcut(ShortcutAction action);

// Number of selectable accent colours (blue, pink, green, yellow, orange,
// sky, purple, grey).
inline constexpr int kAccentCount = 8;

struct Settings {
    bool lightMode = false;
    int accent = 0; // index into the theme colours
    // Display switches shown in Settings > Basic. They are saved together with
    // the rest of the settings so every toggle survives a restart.
    bool showGrid = true;
    bool showJoints = true;
    bool showLabels = true;
    bool showWireframe = false;
    bool showContactPoints = false;
    bool showGridLabels = true;
    // Scene/physics controls shown in Settings > Basic. They are global
    // preferences and are restored on the next launch.
    float gravity = 9.8f;
    int velocityIterations = 10;
    int positionIterations = 4;
    int slowStepGear = 10;
    // Playback tempo used to convert simulation milliseconds to beats.
    float bpm = 180.0f;
    // Global rivet radius shared by every placed rivet.
    float rivetRadius = 15.0f;
    // Directory used by "导出为预制事件". Empty means <program>/exports.
    std::string exportDirectory;
    KeyBinding shortcuts[kShortcutCount];

    Settings();
    void resetShortcuts();
};

// Missing or unreadable files leave the defaults untouched and return false.
bool loadSettings(const std::filesystem::path& path, Settings& settings);
bool saveSettings(const std::filesystem::path& path, const Settings& settings,
                  std::string& error);

} // namespace ppn
