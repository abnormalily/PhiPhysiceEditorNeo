#include "app/App.h"
#include "app/SelfTest.h"
#include "app/SingleInstance.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include "raylib.h"

#ifdef _WIN32
// Declared directly so windows.h does not clash with raylib's CloseWindow and
// ShowCursor symbols. ATTACH_PARENT_PROCESS is 0xFFFFFFFF.
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long processId);
extern "C" __declspec(dllimport) int __stdcall MessageBoxW(void* hwnd, const wchar_t* text,
                                                          const wchar_t* caption,
                                                          unsigned int type);
constexpr unsigned long kAttachParentProcess = 0xFFFFFFFFul;
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void printUsage() {
    std::printf("PhiPhysiceEditorNeo - physical note shape editor\n");
    std::printf("Usage: PhiPhysiceEditorNeo [options]\n");
    std::printf("  --selftest             Run physics / IO tests without opening a window\n");
    std::printf("  --screenshot <path>    Render a few frames, export a PNG and exit\n");
    std::printf("  --shape                Open the shape editor for the screenshot\n");
    std::printf("  --judge-selected       Select the judge line for the screenshot\n");
    std::printf("  --judge-place          Show the judge-line placement preview\n");
    std::printf("  --demo                 Place sample notes for the screenshot\n");
    std::printf("  --fields               Place sample force fields for the screenshot\n");
    std::printf("  --events               Open the event timeline for the selected note\n");
    std::printf("  --scenes               Place sample scene blocks for the screenshot\n");
    std::printf("  --place                Enter placement mode for the screenshot\n");
    std::printf("  --settings             Open the settings overlay for the screenshot\n");
    std::printf("  --settings-shortcuts   Open the shortcuts page of settings\n");
    std::printf("  --settings-appearance  Open the appearance page of settings\n");
    std::printf("  --help-overlay         Alias for --settings\n");
    std::printf("  --save-dialog          Open the save-project dialog for the screenshot\n");
    std::printf("  --projects             Show the project tab for the screenshot\n");
    std::printf("  --light                Use the light theme for the screenshot\n");
    std::printf("  --shape-vars           Open the variable/node tab of the shape editor\n");
    std::printf("  --frames <n>           Frames to render before the screenshot (default 4)\n");
    std::printf("  --help                 Show this help\n");
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The executable is built as a GUI application so double-clicking it never
    // opens a console window. When it is started from an existing terminal
    // (self tests / screenshots), attach to that console so stdout still works.
    if (AttachConsole(kAttachParentProcess)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONIN$", "r", stdin);
    }
#endif
    bool selftest = false;
    bool shapeShot = false;
    bool judgeShot = false;
    bool judgePlaceShot = false;
    bool demoShot = false;
    bool fieldsShot = false;
    bool eventsShot = false;
    bool scenesShot = false;
    bool placeShot = false;
    bool settingsShot = false;
    bool settingsShortcutsShot = false;
    bool settingsAppearanceShot = false;
    bool projectShot = false;
    bool projectsTab = false;
    bool lightShot = false;
    bool shapeVarsShot = false;
    std::string screenshotPath;
    int screenshotFrames = 4;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest") {
            selftest = true;
        } else if (arg == "--shape") {
            shapeShot = true;
        } else if (arg == "--judge-selected") {
            judgeShot = true;
        } else if (arg == "--judge-place") {
            judgePlaceShot = true;
        } else if (arg == "--demo") {
            demoShot = true;
        } else if (arg == "--fields") {
            fieldsShot = true;
        } else if (arg == "--events") {
            eventsShot = true;
        } else if (arg == "--scenes") {
            scenesShot = true;
        } else if (arg == "--place") {
            placeShot = true;
        } else if (arg == "--help-overlay" || arg == "--settings") {
            settingsShot = true;
        } else if (arg == "--settings-shortcuts") {
            settingsShot = true;
            settingsShortcutsShot = true;
        } else if (arg == "--settings-appearance") {
            settingsShot = true;
            settingsAppearanceShot = true;
        } else if (arg == "--save-dialog") {
            projectShot = true;
        } else if (arg == "--projects") {
            projectsTab = true;
        } else if (arg == "--light") {
            lightShot = true;
        } else if (arg == "--shape-vars") {
            shapeVarsShot = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            screenshotFrames = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            printUsage();
            return 1;
        }
    }

    if (selftest) return ppn::runSelfTests() ? 0 : 1;
    // Screenshot runs are short-lived and must be able to render while a normal
    // editor window is open (documentation automation).
    const bool singleInstance = ppn::ensureSingleInstance();
    if (!singleInstance && screenshotPath.empty()) return 0;

    SetTraceLogLevel(LOG_WARNING);
    // Draw straight to the window framebuffer so 4x MSAA applies to the editor
    // and so resizing/high-DPI screens do not pass through a scaled texture.
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    // The editor opens at a 16:9 aspect ratio; the UI scales to any later resize.
    InitWindow(1600, 900, "PhiPhysiceEditorNeo");
    if (!IsWindowReady()) {
        // Some remote-desktop / older OpenGL drivers cannot create a 4x MSAA
        // context. Retry with the simplest window configuration before
        // reporting a fatal error.
        CloseWindow();
        SetConfigFlags(FLAG_WINDOW_RESIZABLE);
        InitWindow(1600, 900, "PhiPhysiceEditorNeo");
    }
    if (!IsWindowReady()) {
        std::fprintf(stderr, "Failed to create window\n");
#ifdef _WIN32
        if (screenshotPath.empty())
            MessageBoxW(nullptr,
                        L"无法创建程序窗口。\n请确认显卡驱动正常，或不要通过远程桌面/无桌面环境启动。",
                        L"PhiPhysiceEditorNeo", 0x00000010u);
#endif
        return 1;
    }
    // Esc must exit note placement, never close the editor window.
    SetExitKey(KEY_NULL);
    // Vertical sync is intentionally off: the target FPS keeps the editor
    // responsive while resizing without the extra VSync latency.
    ClearWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(60);
    SetWindowMinSize(1280, 720);

    ui::init();
    ppn::App app;
    app.screenshotMode = !screenshotPath.empty();
    if (!app.init()) {
        std::fprintf(stderr, "Failed to initialise the editor\n");
#ifdef _WIN32
        if (screenshotPath.empty())
            MessageBoxW(nullptr,
                        L"程序初始化失败。\n请检查程序目录中的 components、assets 和设置文件是否完整。",
                        L"PhiPhysiceEditorNeo", 0x00000010u);
#endif
        return 1;
    }
    if (lightShot) app.setLightMode(true, false);
    std::string extraGlyphs;
    for (const auto& type : app.library.types)
        for (const auto& variable : type.variables) extraGlyphs += variable.displayName;
    for (const auto& type : app.sceneLibrary.types)
        for (const auto& variable : type.variables) extraGlyphs += variable.displayName;
    ui::loadFont(app.fontPath, extraGlyphs);

    if (!screenshotPath.empty()) {
        app.toasts.clear();
        app.statusMessage = "就绪";
    }
    if (settingsShot) app.showSettings = true;
    if (settingsShortcutsShot) app.settingsTab = 2;
    if (settingsAppearanceShot) app.settingsTab = 1;
    if (projectShot) app.openProjectDialog(ppn::ProjectDialogMode::SaveAs);
    if (projectsTab) app.leftTab = ppn::LeftTab::Projects;
    if (shapeShot) {
        // The library lists Tap / Drag / Flick / Hold in that order.
        int holdIndex = 0;
        for (size_t i = 0; i < app.library.types.size(); ++i)
            if (app.library.types[i].isHold()) holdIndex = (int)i;
        app.openShapeEditor(holdIndex);
        if (!app.shape.vertices.empty()) app.shape.selectedVertex = 1;
        if (shapeVarsShot) app.shape.formTab = 1;
    } else if (fieldsShot) {
        // Sample force fields for documentation screenshots only.
        app.librarySection = ppn::LibrarySection::Force;
        ppn::ForceField gravity;
        gravity.kind = ppn::ForceFieldKind::Gravity;
        gravity.shape = ppn::ForceFieldShape::Rectangle;
        gravity.position = {-380.0f, 170.0f};
        gravity.width = 360.0f;
        gravity.height = 250.0f;
        gravity.strength = 26.0f;
        app.doc.addForceField(gravity);
        ppn::ForceField repulsion;
        repulsion.kind = ppn::ForceFieldKind::Repulsion;
        repulsion.shape = ppn::ForceFieldShape::Circle;
        repulsion.position = {350.0f, 170.0f};
        repulsion.radius = 150.0f;
        repulsion.strength = 22.0f;
        app.doc.addForceField(repulsion);
        ppn::ForceField buoyancy;
        buoyancy.kind = ppn::ForceFieldKind::Buoyancy;
        buoyancy.shape = ppn::ForceFieldShape::Rectangle;
        buoyancy.position = {0.0f, -170.0f};
        buoyancy.width = 640.0f;
        buoyancy.height = 200.0f;
        buoyancy.buoyancy = 1.2f;
        app.doc.addForceField(buoyancy);
        ppn::ForceField drag;
        drag.kind = ppn::ForceFieldKind::Drag;
        drag.shape = ppn::ForceFieldShape::Rectangle;
        drag.position = {-360.0f, -170.0f};
        drag.width = 280.0f;
        drag.height = 160.0f;
        drag.strength = 12.0f;
        app.doc.addForceField(drag);
        ppn::ForceField change;
        change.kind = ppn::ForceFieldKind::GravityChange;
        change.shape = ppn::ForceFieldShape::Rectangle;
        change.position = {350.0f, -170.0f};
        change.width = 280.0f;
        change.height = 160.0f;
        change.buoyancyDirection = {1.0f, 0.0f};
        app.doc.addForceField(change);
        app.doc.addNote("Tap", {-380.0f, 170.0f}, 0.0f);
        app.doc.addNote("Drag", {350.0f, 170.0f}, 0.3f);
        app.doc.addNote("Hold", {0.0f, -170.0f}, 0.0f);
        app.rebuildWorld();
        // Select the buoyancy field so the documentation shot shows its
        // bottom-drag control alongside the other field volumes.
        app.selectedField = app.doc.fields.size() > 2 ? app.doc.fields[2].id
                                                       : app.doc.fields.back().id;
        app.toasts.clear();
    } else if (scenesShot) {
        // Scene blocks share the note geometry but stay fixed in the world.
        app.librarySection = ppn::LibrarySection::Scene;
        const int tapScene = app.doc.addNote("Tap", {-260.0f, 200.0f}, 0.0f, true);
        app.doc.addNote("Drag", {-40.0f, 120.0f}, 0.2f, true);
        app.doc.addNote("Flick", {180.0f, 220.0f}, -0.25f, true);
        const int holdScene = app.doc.addNote("Hold", {280.0f, -40.0f}, 0.0f, true);
        app.rebuildWorld();
        app.selectedNotes = {holdScene};
        (void)tapScene;
        app.toasts.clear();
    } else if (demoShot) {
        // Sample layout for documentation screenshots only.
        app.doc.addNote("Tap", {-260.0f, 160.0f}, 0.0f);
        app.doc.addNote("Drag", {-60.0f, 220.0f}, 0.2f);
        app.doc.addNote("Flick", {140.0f, 150.0f}, -0.25f);
        const int holdId = app.doc.addNote("Hold", {300.0f, 200.0f}, 0.0f);
        app.doc.addNote("Hold", {-160.0f, 40.0f}, 0.0f);
        app.rebuildWorld();
        app.selectedNotes = {holdId};
        app.toasts.clear();
    } else if (judgeShot) {
        if (!app.doc.judges.empty()) app.selectedJudge = app.doc.judges.front().id;
    } else if (judgePlaceShot) {
        app.librarySection = ppn::LibrarySection::Scene;
        if (!app.doc.judges.empty()) {
            ppn::JudgeLine& judge = app.doc.judges.front();
            judge.width = 620.0f;
            judge.height = 22.0f;
            judge.angle = 0.20f;
            app.selectedJudge = judge.id;
        }
        app.rebuildWorld();
        app.tool = ppn::Tool::JudgeLine;
        app.toasts.clear();
        SetMousePosition(900, 500);
    } else if (placeShot) {
        app.selectionBoxActive = false;
        app.tool = ppn::Tool::Place;
        app.librarySection = ppn::LibrarySection::Physics;
        app.placingScene = false;
        if (!app.library.types.empty()) app.selectedTypeName = "Flick";
        if (!app.doc.notes.empty()) app.selectedNotes = {app.doc.notes.front().id};
        SetMousePosition(900, 500);
    }
    if (eventsShot) {
        app.openEventEditor();
        if (std::vector<ppn::Event>* events = app.eventEditorEvents()) {
            ppn::Event move;
            move.track = 1;
            move.type = ppn::EventType::MoveX;
            move.startBeat = 1.0f;
            move.endBeat = 6.0f;
            move.startValue = "this";
            move.endValue = "this+180";
            move.easing = 6;
            events->push_back(move);
            ppn::Event rotate;
            rotate.track = 4;
            rotate.type = ppn::EventType::Rotate;
            rotate.startBeat = 2.0f;
            rotate.endBeat = 9.5f;
            rotate.startValue = "0";
            rotate.endValue = "90";
            rotate.easing = 20;
            events->push_back(rotate);
            ppn::Event momentum;
            momentum.track = 7;
            momentum.type = ppn::EventType::DirectionalMomentum;
            momentum.startBeat = 3.0f;
            momentum.endBeat = 8.0f;
            momentum.startValue = "25";
            momentum.endValue = "25";
            momentum.angleValue = "this";
            momentum.instant = true;
            events->push_back(momentum);
            app.eventEditor.selected = 2;
            app.eventEditor.selection = {2};
        }
    }

    int frameCount = 0;
    bool exported = false;
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        ui::beginFrame();
        ui::setFontScale(ui::canvasScale());
        app.update(dt);

        BeginDrawing();
        ClearBackground(theme::bg);

        // Map the fixed 1600x900 design space directly onto the window
        // framebuffer. No intermediate render texture means no extra resampling;
        // MSAA and high-DPI resolution are preserved.
        Camera2D canvas{};
        canvas.offset = {ui::canvasOffset().x, ui::canvasOffset().y};
        canvas.target = {0.0f, 0.0f};
        canvas.rotation = 0.0f;
        canvas.zoom = ui::canvasScale();
        BeginMode2D(canvas);
        app.draw();
        ui::endFrame();
        EndMode2D();
        EndDrawing();

        ++frameCount;
        if (!screenshotPath.empty() && frameCount >= screenshotFrames && !exported) {
            exported = true;
            // Read the resolved window buffer after presenting the frame.
            Image image = LoadImageFromScreen();
            std::error_code ec;
            const fs::path path(screenshotPath);
            if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
            if (ExportImage(image, screenshotPath.c_str()))
                std::printf("Screenshot saved: %s\n", screenshotPath.c_str());
            else
                std::fprintf(stderr, "Failed to save screenshot: %s\n", screenshotPath.c_str());
            UnloadImage(image);
            break;
        }
    }

    ui::shutdown();
    CloseWindow();
    return 0;
}