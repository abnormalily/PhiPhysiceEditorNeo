#pragma once
// Tiny immediate-mode UI toolkit built directly on raylib.
// The whole app renders at a fixed 1600x900 (16:9) virtual resolution.

#include "raylib.h"
#include "core/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

using ppn::Vec2;

enum class Icon {
    None, Play, Pause, Step, Reset, Undo, Redo, Save, Folder, Plus, Trash, Search,
    Grid, Pin, Weld, World, Close, Edit, Refresh, ChevronDown, Check, Layers,
    Note, Settings, Fit, Dot, Sun, Moon
};

enum class ButtonStyle { Primary, Secondary, Ghost, Danger, Subtle, AccentLight };

void init();
void loadFont(const std::filesystem::path& path, const std::string& extraUtf8 = {});
// Regenerate the font atlases when the canvas is rendered above its base
// resolution, so text stays sharp in enlarged windows.
void setFontScale(float scale);
void beginFrame();
void endFrame();
void shutdown();

// Mapping of the fixed 1600x900 design canvas into the current render target.
// Both values are in render-target pixels, so the canvas can be drawn at the
// native resolution of the window instead of being upscaled from a fixed
// 1600x900 texture.
float canvasScale();
Vec2 canvasOffset();

Font font();
bool fontReady();
float textWidth(const std::string& t, float size, float spacing = 0.4f);
float textHeight(float size);
void text(const std::string& t, float x, float y, float size, Color color,
          float spacing = 0.4f);
void text(const std::string& t, Vec2 pos, float size, Color color, float spacing = 0.4f);
void textCentered(const std::string& t, Rectangle r, float size, Color color,
                  float spacing = 0.4f);
void textBold(const std::string& t, float x, float y, float size, Color color,
              float spacing = 0.4f);
void textCenteredBold(const std::string& t, Rectangle r, float size, Color color,
                      float spacing = 0.4f);
void textLeftBold(const std::string& t, Rectangle r, float size, Color color,
                  float pad = 0.0f, float spacing = 0.4f);
void textLeft(const std::string& t, Rectangle r, float size, Color color, float pad = 0.0f,
              float spacing = 0.4f);
void textRight(const std::string& t, Rectangle r, float size, Color color, float pad = 0.0f,
               float spacing = 0.4f);
std::string ellipsize(const std::string& t, float size, float maxWidth);
std::string format(const char* fmt, ...);

Vec2 mouse();
Vec2 mouseDelta();
bool mouseValid();
bool mouseIn(Rectangle r);
bool mouseDown(int button = 0);
bool mousePressed(int button = 0);
bool mouseReleased(int button = 0);
bool mouseConsumed();
void consumeMouse();
float wheel();
bool isEditingText();
void setInteractionEnabled(bool enabled);
bool interactionEnabled();
void cancelTextEditing();
uint32_t activeId();

uint32_t id(const char* s);
float animate(uint32_t id, bool hovered, float speed = 14.0f);

void panel(Rectangle r, float radius, Color color, bool shadow = true);
void separator(float x1, float y1, float x2, float y2, Color color);
void icon(Icon icon, Rectangle r, Color color, float strokeScale = 1.0f);
void tooltip(const std::string& text, Vec2 pos);

bool button(const char* id, Rectangle r, const std::string& label,
            ButtonStyle style = ButtonStyle::Secondary, bool enabled = true,
            Icon icon = Icon::None);
bool iconButton(const char* id, Rectangle r, Icon icon, const char* tip = nullptr,
                bool active = false, bool enabled = true);
bool chip(const char* id, Rectangle r, const std::string& label, bool selected,
          Color dotColor = Color{0, 0, 0, 0}, bool enabled = true);
bool toggle(const char* id, Rectangle r, const std::string& label, bool& value);
bool checkbox(const char* id, Rectangle r, const std::string& label, bool& value);
bool textField(const char* id, Rectangle r, std::string& value,
               const std::string& placeholder = "", int maxChars = 32,
               float size = 15.0f, const std::string& label = {});

// Numeric input box. Clicking selects the current value for typing; Enter
// commits, Esc cancels and clicking outside also commits. There is deliberately
// no drag-to-scrub or wheel stepping: values are changed only by typing.
bool numberField(const char* id, Rectangle r, const std::string& label, float& value,
                 float lo, float hi, int decimals, const char* suffix = "");
bool segmented(const char* id, Rectangle r, const std::vector<std::string>& labels,
               int& index);
bool colorSwatch(const char* id, Rectangle r, Color color, bool selected);

float scrollOffset(const char* id, Rectangle r, float contentHeight);
void drawScrollbar(Rectangle r, float contentHeight, float offset);
void scissorPush(Rectangle r);
void scissorPop();

// UTF-8 helpers.
int utf8Length(const std::string& s);
int utf8Prev(const std::string& s, int index);
int utf8Next(const std::string& s, int index);
std::string utf8Encode(int codepoint);

struct ToastMessage {
    std::string text;
    Color color;
    float age = 0.0f;
    float duration = 3.6f;
};

} // namespace ui