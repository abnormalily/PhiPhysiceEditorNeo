#include "ui/Ui.h"

#include "rlgl.h"

#include "ui/Layout.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

using ppn::PI;
using ppn::clampf;

namespace {

// Glyphs are rasterised on demand from the bundled font. Pages are small RGBA
// atlases so supporting the full CJK range does not allocate one huge texture:
// only characters that are actually drawn consume memory.
constexpr int kAtlasSize = 1024;
constexpr int kAtlasPadding = 2;
// Doubled raster sizes (was 16 / 28 / 42): text is drawn at the same size but
// sampled from twice the resolution, which is the "font precision" upgrade.
// Higher window-scale tiers grow only mildly: over-supersampling (e.g. 112 px
// glyphs downscaled to 20 px) made thin CJK strokes disappear.
constexpr int kBaseFontSizes[3] = {32, 56, 84};
constexpr int kTierIncrement[3] = {6, 10, 14};

struct GlyphSlot {
    int page = -1;
    Rectangle rec{};
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float advanceX = 0.0f;
};

struct AtlasPage {
    Texture2D texture{};
    Image image{};
    int cursorX = kAtlasPadding;
    int cursorY = kAtlasPadding;
    int rowHeight = 0;
};

struct DynamicFont {
    int baseSize = 32;
    std::vector<AtlasPage> pages;
    std::unordered_map<int, GlyphSlot> glyphs;
};

struct State {
    DynamicFont dynFonts[3];
    bool fontsLoaded = false;
    int fontTier = 0;
    std::vector<unsigned char> fontData;
    std::string fontFileType;
    Vec2 mouse{-1000.0f, -1000.0f};
    Vec2 prevMouse{-1000.0f, -1000.0f};
    Vec2 mouseDelta{};
    bool valid = false;
    bool down[3] = {false, false, false};
    bool pressed[3] = {false, false, false};
    bool released[3] = {false, false, false};
    float wheel = 0.0f;
    bool consumed = false;
    uint32_t active = 0;
    uint32_t editing = 0;
    std::string editBuffer;
    int caret = 0;
    int selectionAnchor = 0;
    bool sawEditingField = false;
    bool backspaceHeld = false;
    double backspaceNextTime = 0.0;
    std::unordered_map<uint32_t, float> anim;
    std::unordered_map<std::string, float> scroll;
    std::string tooltip;
    Vec2 tooltipPos{};
    bool hasTooltip = false;
    float scale = 1.0f;       // logical pixels per design unit (input mapping)
    Vec2 offset{};
    float pixelScale = 1.0f;  // render-target pixels per design unit (drawing)
    Vec2 pixelOffset{};
    bool cursorSet = false;
    bool enabled = true;
};

State g;

Color textColor() { return theme::text; }

constexpr float kMinTextSize = 12.0f;

float effectiveSize(float size) { return size < kMinTextSize ? kMinTextSize : size; }

int activeFontIndex(float size) {
    // The thresholds follow the size ranges produced by layout::S (the UI text
    // scale); the atlas itself is rasterised at twice the old resolution.
    if (size <= 17.5f) return 0;
    if (size <= 31.0f) return 1;
    return 2;
}

DynamicFont* activeFont(float size = 15.0f) {
    if (!g.fontsLoaded) return nullptr;
    return &g.dynFonts[activeFontIndex(size)];
}

AtlasPage* allocateAtlasPage(DynamicFont& font, int width, int height) {
    for (auto& page : font.pages) {
        if (page.cursorY + height + kAtlasPadding > kAtlasSize) continue;
        if (page.cursorX + width + kAtlasPadding > kAtlasSize) {
            page.cursorY += page.rowHeight + kAtlasPadding;
            page.cursorX = kAtlasPadding;
            page.rowHeight = 0;
        }
        if (page.cursorY + height + kAtlasPadding <= kAtlasSize &&
            page.cursorX + width + kAtlasPadding <= kAtlasSize)
            return &page;
    }
    AtlasPage page;
    page.image = GenImageColor(kAtlasSize, kAtlasSize, BLANK);
    page.texture = LoadTextureFromImage(page.image);
    if (page.texture.id == 0) {
        UnloadImage(page.image);
        return nullptr;
    }
    SetTextureFilter(page.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(page.texture, TEXTURE_WRAP_CLAMP);
    font.pages.push_back(page);
    return &font.pages.back();
}

unsigned char glyphAlphaAt(const GlyphInfo& info, int x, int y) {
    const unsigned char* pixels = (const unsigned char*)info.image.data;
    if (!pixels) return 0;
    const int index = y * info.image.width + x;
    switch (info.image.format) {
        case PIXELFORMAT_UNCOMPRESSED_GRAYSCALE: return pixels[index];
        case PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA: return pixels[index * 2 + 1];
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8A8: return pixels[index * 4 + 3];
        case PIXELFORMAT_UNCOMPRESSED_R8G8B8: return 255;
        default: return 0;
    }
}

// Loads one codepoint into the page atlas and caches its metrics. Missing
// glyphs collapse to a blank advance instead of aborting the whole text run.
GlyphSlot* ensureGlyph(DynamicFont& font, int codepoint) {
    auto found = font.glyphs.find(codepoint);
    if (found != font.glyphs.end()) return &found->second;

    GlyphSlot slot;
    if (g.fontData.empty()) {
        slot.advanceX = (float)font.baseSize * (codepoint == ' ' ? 0.32f : 0.58f);
        auto inserted = font.glyphs.emplace(codepoint, slot);
        return &inserted.first->second;
    }

    GlyphInfo* data = LoadFontData(g.fontData.data(), (int)g.fontData.size(), font.baseSize,
                                   &codepoint, 1, FONT_DEFAULT);
    if (!data) {
        slot.advanceX = (float)font.baseSize * 0.5f;
        auto inserted = font.glyphs.emplace(codepoint, slot);
        return &inserted.first->second;
    }
    const GlyphInfo& info = data[0];
    slot.offsetX = (float)info.offsetX;
    slot.offsetY = (float)info.offsetY;
    slot.advanceX = (float)info.advanceX;

    const int width = info.image.width;
    const int height = info.image.height;
    if (width > 0 && height > 0 && info.image.data != nullptr) {
        AtlasPage* page = allocateAtlasPage(font, width, height);
        if (page) {
            const int x = page->cursorX;
            const int y = page->cursorY;
            unsigned char* atlasPixels = (unsigned char*)page->image.data;
            for (int row = 0; row < height; ++row) {
                for (int col = 0; col < width; ++col) {
                    const size_t offset =
                        ((size_t)(y + row) * page->image.width + (size_t)(x + col)) * 4;
                    atlasPixels[offset + 0] = 255;
                    atlasPixels[offset + 1] = 255;
                    atlasPixels[offset + 2] = 255;
                    atlasPixels[offset + 3] = glyphAlphaAt(info, col, row);
                }
            }
            UpdateTexture(page->texture, page->image.data);
            slot.page = (int)(page - font.pages.data());
            slot.rec = {(float)x, (float)y, (float)width, (float)height};
            page->cursorX += width + kAtlasPadding;
            page->rowHeight = std::max(page->rowHeight, height);
        }
    }
    if (slot.advanceX <= 0.0f) slot.advanceX = (float)font.baseSize * 0.25f;
    UnloadFontData(data, 1);
    auto inserted = font.glyphs.emplace(codepoint, slot);
    return &inserted.first->second;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

const char* formatStringFor(int decimals) {
    static const char* kFormats[] = {"%.0f", "%.1f", "%.2f", "%.3f"};
    if (decimals < 0) decimals = 0;
    if (decimals > 3) decimals = 3;
    return kFormats[decimals];
}

bool parseNumberText(const std::string& text, float lo, float hi, float& out) {
    try {
        size_t used = 0;
        const float value = std::stof(text, &used);
        if (used > 0 && used == text.size() && std::isfinite(value)) {
            out = clampf(value, lo, hi);
            return true;
        }
    } catch (...) {
    }
    return false;
}

// Backspace repeats while held: one immediate deletion, a short delay, then a
// steady stream of deletions for fast text clearing.
bool backspaceRepeat() {
    const double now = GetTime();
    if (IsKeyPressed(KEY_BACKSPACE)) {
        g.backspaceHeld = true;
        g.backspaceNextTime = now + 0.34;
        return true;
    }
    if (!IsKeyDown(KEY_BACKSPACE)) {
        g.backspaceHeld = false;
        return false;
    }
    if (g.backspaceHeld && now >= g.backspaceNextTime) {
        g.backspaceNextTime = now + 0.038;
        return true;
    }
    return false;
}

// A sub-pixel second pass gives the regular weight a little more body; bold is a
// single extra pass at one render pixel. More passes (or a larger offset) smear
// CJK strokes at small sizes and make text look blurry.
constexpr float kTextRegularPixels = 0.6f;
constexpr float kTextBoldPixels = 1.0f;

void drawTextRun(DynamicFont& font, const std::string& t, float x, float y,
                  float drawSize, float spacing, Color color) {
    const float scale = drawSize / (float)font.baseSize;
    float penX = x;
    size_t i = 0;
    while (i < t.size()) {
        int codepointSize = 0;
        const int codepoint = GetCodepointNext(&t[i], &codepointSize);
        i += (size_t)std::max(1, codepointSize);
        if (codepoint == '\n') {
            penX = x;
            y += drawSize;
            continue;
        }
        const GlyphSlot* glyph = ensureGlyph(font, codepoint);
        if (glyph->page >= 0 && glyph->rec.width > 0.0f && glyph->rec.height > 0.0f) {
            const Rectangle dest{penX + glyph->offsetX * scale,
                                 y + glyph->offsetY * scale,
                                 glyph->rec.width * scale,
                                 glyph->rec.height * scale};
            DrawTexturePro(font.pages[(size_t)glyph->page].texture, glyph->rec, dest,
                           {0.0f, 0.0f}, 0.0f, color);
        }
        penX += glyph->advanceX * scale + spacing;
    }
}

void drawTextImpl(const std::string& t, float x, float y, float size, Color color,
                  float spacing, float weightPixels = 0.0f) {
    if (t.empty()) return;
    // Every label is enlarged by layout::S, so all UI text got bigger at once.
    const float drawSize = effectiveSize(size * layout::S);
    DynamicFont* font = activeFont(drawSize);
    if (!font) {
        const Font fallback = GetFontDefault();
        DrawTextEx(fallback, t.c_str(), {x, y}, drawSize, spacing, color);
        if (weightPixels > 0.0f) {
            const float step = weightPixels / std::max(0.001f, g.pixelScale);
            DrawTextEx(fallback, t.c_str(), {x + step, y}, drawSize, spacing, color);
        }
        return;
    }
    drawTextRun(*font, t, x, y, drawSize, spacing, color);
    if (weightPixels > 0.0f) {
        // Offsets are in design units; divide by the canvas zoom so the weight
        // stays constant in render pixels at any window size.
        const float step = weightPixels / std::max(0.001f, g.pixelScale);
        drawTextRun(*font, t, x + step, y, drawSize, spacing, color);
    }
}

void maybeCursor(bool pointer) {
    if (!g.cursorSet && pointer) {
        SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        g.cursorSet = true;
    }
}

void drawArcArrow(Vec2 center, float radius, float startDeg, float endDeg, Color color,
                  float thickness) {
    DrawRing(center, radius - thickness * 0.5f, radius + thickness * 0.5f, startDeg, endDeg,
             32, color);
}

// Ear-clipping fill used by small icon silhouettes. The crescent outline is
// concave, so a triangle fan would paint over the "bite" of the moon.
void fillConcavePolygon(std::vector<Vec2> points, Color color) {
    if (points.size() < 3) return;
    // Drop zero-length edges (the crescent shares its two arc endpoints) so the
    // ear-clipping loop does not stall on a degenerate closing edge.
    std::vector<Vec2> cleaned;
    cleaned.reserve(points.size());
    for (const Vec2& p : points) {
        if (cleaned.empty() || lengthSq(p - cleaned.back()) > 1e-8f) cleaned.push_back(p);
    }
    while (cleaned.size() > 3 && lengthSq(cleaned.front() - cleaned.back()) <= 1e-8f)
        cleaned.pop_back();
    points = cleaned;
    if (points.size() < 3) return;
    std::vector<int> indices(points.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = (int)i;
    auto cross2 = [](Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; };
    float signedArea = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec2& a = points[i];
        const Vec2& b = points[(i + 1) % points.size()];
        signedArea += cross2(a, b);
    }
    if (signedArea < 0.0f) std::reverse(points.begin(), points.end());
    // Rebuild the index list after reversing.
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = (int)i;
    while (indices.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < indices.size(); ++i) {
            const int ia = indices[(i + indices.size() - 1) % indices.size()];
            const int ib = indices[i];
            const int ic = indices[(i + 1) % indices.size()];
            const Vec2 a = points[(size_t)ia];
            const Vec2 b = points[(size_t)ib];
            const Vec2 c = points[(size_t)ic];
            if (cross2(b - a, c - b) <= 1e-4f) continue;
            bool contains = false;
            for (int ip : indices) {
                if (ip == ia || ip == ib || ip == ic) continue;
                const Vec2 p = points[(size_t)ip];
                const float d1 = cross2(b - a, p - a);
                const float d2 = cross2(c - b, p - b);
                const float d3 = cross2(a - c, p - c);
                if (d1 >= 0.0f && d2 >= 0.0f && d3 >= 0.0f) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            // Emit both windings: the icon is drawn through a Y-up -> Y-down
            // projection, and the render batch may cull one winding.
            DrawTriangle(a, b, c, color);
            DrawTriangle(a, c, b, color);
            indices.erase(indices.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (indices.size() == 3) {
        DrawTriangle(points[(size_t)indices[0]], points[(size_t)indices[1]],
                     points[(size_t)indices[2]], color);
        DrawTriangle(points[(size_t)indices[0]], points[(size_t)indices[2]],
                     points[(size_t)indices[1]], color);
    }
}

// Filled crescent made from one outer and one offset inner circle. Drawing the
// difference polygon directly keeps the icon transparent on every button
// background.
void drawCrescent(Vec2 center, float radius, Color color) {
    const float outerR = radius * 0.86f;
    const float innerR = outerR * 0.82f;
    const float distance = outerR * 0.45f;
    const Vec2 outerC = center - Vec2{distance * 0.18f, 0.0f};
    const Vec2 innerC = outerC + Vec2{distance, 0.0f};

    const float x = (distance * distance + outerR * outerR - innerR * innerR) /
                    (2.0f * distance);
    const float y = std::sqrt(std::max(0.0f, outerR * outerR - x * x));
    const float outerAngle = std::atan2(y, x);
    const float innerAngle = std::atan2(y, x - distance);

    std::vector<Vec2> points;
    const int outerSteps = 30;
    for (int i = 0; i <= outerSteps; ++i) {
        const float a = outerAngle + (2.0f * PI - 2.0f * outerAngle) *
                                        ((float)i / (float)outerSteps);
        points.push_back({outerC.x + std::cos(a) * outerR,
                          outerC.y + std::sin(a) * outerR});
    }
    const int innerSteps = 20;
    // The final inner point coincides with the first outer point, so stop one
    // step early; the polygon closes back to it automatically.
    for (int i = 0; i < innerSteps; ++i) {
        // Follow the inner arc between the two intersections through the left
        // side (the part of the small circle that lies inside the big one).
        const float a = -innerAngle -
                        (2.0f * PI - 2.0f * innerAngle) *
                            ((float)i / (float)innerSteps);
        points.push_back({innerC.x + std::cos(a) * innerR,
                          innerC.y + std::sin(a) * innerR});
    }

    fillConcavePolygon(points, color);
}

void unloadFonts() {
    for (auto& font : g.dynFonts) {
        for (auto& page : font.pages) {
            if (page.texture.id != 0) UnloadTexture(page.texture);
            if (page.image.data != nullptr) UnloadImage(page.image);
        }
        font.pages.clear();
        font.glyphs.clear();
        font.baseSize = kBaseFontSizes[0];
    }
    g.fontsLoaded = false;
    g.fontTier = 0;
}

bool loadFontTier(int tier) {
    if (g.fontData.empty()) return false;
    unloadFonts();
    // Each tier doubles the old atlas precision: the glyphs are rasterised at
    // 2x the size they are drawn at, then sampled down with bilinear filtering.
    for (int i = 0; i < 3; ++i)
        g.dynFonts[i].baseSize =
            kBaseFontSizes[i] + (std::max(1, tier) - 1) * kTierIncrement[i];
    g.fontsLoaded = true;
    g.fontTier = tier;

    // Probe both an ASCII and a CJK glyph so a bad font file is reported early.
    GlyphSlot* latin = ensureGlyph(g.dynFonts[1], 'A');
    GlyphSlot* chinese = ensureGlyph(g.dynFonts[1], 0x4E2D); // 中
    if (!latin || !chinese || g.dynFonts[1].pages.empty()) {
        unloadFonts();
        return false;
    }
    return true;
}

} // namespace

void init() {
    g = State{};
}

void loadFont(const std::filesystem::path& path, const std::string& extraUtf8) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "[PhiPhysiceEditorNeo] warning: could not open UI font\n");
        return;
    }
    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        std::fprintf(stderr, "[PhiPhysiceEditorNeo] warning: empty UI font file\n");
        return;
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontData((size_t)fileSize);
    if (!file.read(reinterpret_cast<char*>(fontData.data()), fileSize)) {
        std::fprintf(stderr, "[PhiPhysiceEditorNeo] warning: could not read UI font\n");
        return;
    }
    std::string extension = path.extension().string();
    for (char& c : extension) c = (char)std::tolower((unsigned char)c);
    g.fontFileType = extension == ".otf" ? ".otf" : ".ttf";

    // The glyph set is no longer baked from a small hand-maintained string:
    // every codepoint in the bundled font is available and rasterised when it
    // is first drawn. extraUtf8 is kept for API compatibility.
    (void)extraUtf8;
    g.fontData = std::move(fontData);
    if (!loadFontTier(1))
        std::fprintf(stderr, "[PhiPhysiceEditorNeo] warning: could not load UI font\n");
}

void setFontScale(float scale) {
    if (!g.fontsLoaded) return;
    // Font atlases are regenerated in whole-number tiers so text remains sharp
    // when the canvas is magnified by a resized window. Hysteresis prevents
    // reloading the (large, CJK) atlases while the user drags the window edge.
    const int tier = g.fontTier;
    int desired = tier;
    if (tier <= 1) {
        // Switch to the denser atlases early enough that 125%/150% Windows
        // scaling never stretches the 16 px glyph atlas.
        if (scale > 1.95f) desired = 3;
        else if (scale > 1.10f) desired = 2;
    } else if (tier == 2) {
        if (scale < 1.00f) desired = 1;
        else if (scale > 2.05f) desired = 3;
    } else if (scale < 1.90f) {
        desired = 2;
    }
    if (desired != tier) loadFontTier(desired);
}

void beginFrame() {
    // Drawing and input must share one mapping. raylib's GetRenderWidth/Height
    // report the physical framebuffer (HiDPI aware), while GetMousePosition
    // returns DPI-independent window units. Older code mixed GetScreenWidth with
    // the render size; on Windows GetScreenWidth switches from logical to
    // physical units after the window is resized, which made every click land at
    // the wrong place until restart. Map everything in framebuffer pixels.
    float rw = (float)GetRenderWidth();
    float rh = (float)GetRenderHeight();
    if (rw <= 0.0f) rw = (float)GetScreenWidth();
    if (rh <= 0.0f) rh = (float)GetScreenHeight();
    if (rw <= 0.0f || rh <= 0.0f) rw = rh = 1.0f;

    const float previousScale = g.pixelScale;
    const Vec2 previousOffset = g.pixelOffset;

    g.pixelScale = std::min(rw / layout::W, rh / layout::H);
    if (g.pixelScale <= 0.0f) g.pixelScale = 1.0f;
    g.pixelOffset = {(rw - layout::W * g.pixelScale) * 0.5f,
                     (rh - layout::H * g.pixelScale) * 0.5f};
    // Kept in sync for any caller that still reads the legacy fields.
    g.scale = g.pixelScale;
    g.offset = g.pixelOffset;

    Vector2 dpi = GetWindowScaleDPI();
    if (dpi.x <= 0.01f) dpi.x = 1.0f;
    if (dpi.y <= 0.01f) dpi.y = 1.0f;
    const Vector2 m = GetMousePosition();
    const Vec2 mousePhysical{m.x * dpi.x, m.y * dpi.y};

    g.prevMouse = g.mouse;
    g.mouse = {(mousePhysical.x - g.pixelOffset.x) / g.pixelScale,
               (mousePhysical.y - g.pixelOffset.y) / g.pixelScale};
    // Resizing the window changes the mapping under a stationary cursor; do not
    // report that as mouse movement (it would jump a camera/note drag).
    const bool mappingChanged = std::fabs(previousScale - g.pixelScale) > 1e-4f ||
                                std::fabs(previousOffset.x - g.pixelOffset.x) > 0.5f ||
                                std::fabs(previousOffset.y - g.pixelOffset.y) > 0.5f;
    g.mouseDelta = mappingChanged ? Vec2{0.0f, 0.0f} : g.mouse - g.prevMouse;
    g.valid = g.mouse.x >= 0.0f && g.mouse.x <= layout::W &&
              g.mouse.y >= 0.0f && g.mouse.y <= layout::H;
    g.wheel = GetMouseWheelMove();
    g.consumed = false;
    g.hasTooltip = false;
    g.sawEditingField = false;
    g.cursorSet = false;
    g.enabled = true;
    for (int i = 0; i < 3; ++i) {
        g.down[i] = IsMouseButtonDown(i);
        g.pressed[i] = IsMouseButtonPressed(i);
        g.released[i] = IsMouseButtonReleased(i);
    }
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
}

float canvasScale() { return g.pixelScale; }
Vec2 canvasOffset() { return g.pixelOffset; }

void endFrame() {
    if (g.editing != 0 && !g.sawEditingField) {
        // The field vanished from the frame; drop editing state without committing.
        g.editing = 0;
        g.active = 0;
        g.editBuffer.clear();
        g.caret = 0;
        g.selectionAnchor = 0;
    }
    if (g.hasTooltip && !g.tooltip.empty()) {
        const float size = 13.0f;
        const float w = std::min(360.0f, textWidth(g.tooltip, size) + 26.0f);
        const float h = std::max(30.0f, textHeight(size) + 12.0f);
        float x = g.mouse.x + 16.0f;
        float y = g.mouse.y + 20.0f;
        if (x + w > layout::W - 8.0f) x = g.mouse.x - w - 10.0f;
        if (y + h > layout::H - 8.0f) y = g.mouse.y - h - 10.0f;
        DrawRectangleRounded({x + 2, y + 3, w, h}, 0.35f, 8, {0, 0, 0, 100});
        DrawRectangleRounded({x, y, w, h}, 0.35f, 8, theme::panelRaised);
        DrawRectangleRoundedLines({x, y, w, h}, 0.35f, 8, theme::stroke);
        textLeft(g.tooltip, {x, y, w, h}, size, theme::textSoft, 10.0f);
    }
    if (g.pressed[0]) {
        // A click that did not land on an active text field deactivates it next frame.
    }
}

void shutdown() { unloadFonts(); }

Font font() {
    // Kept for source compatibility with external callers; the editor draws
    // through the dynamic atlas below. A real Font would only expose the first
    // page, so this is a best-effort texture reference.
    if (!g.fontsLoaded) return GetFontDefault();
    DynamicFont& dynamic = g.dynFonts[1];
    Font result{};
    result.baseSize = dynamic.baseSize;
    if (!dynamic.pages.empty()) result.texture = dynamic.pages.front().texture;
    return result;
}
bool fontReady() { return g.fontsLoaded; }

float textWidth(const std::string& t, float size, float spacing) {
    if (t.empty()) return 0.0f;
    const float drawSize = effectiveSize(size * layout::S);
    DynamicFont* font = activeFont(drawSize);
    if (!font) return MeasureTextEx(GetFontDefault(), t.c_str(), drawSize, spacing).x;

    const float scale = drawSize / (float)font->baseSize;
    float width = 0.0f;
    size_t i = 0;
    while (i < t.size()) {
        int codepointSize = 0;
        const int codepoint = GetCodepointNext(&t[i], &codepointSize);
        i += (size_t)std::max(1, codepointSize);
        if (codepoint == '\n') continue;
        const GlyphSlot* glyph = ensureGlyph(*font, codepoint);
        width += glyph->advanceX * scale + spacing;
    }
    return std::max(0.0f, width - spacing);
}

float textHeight(float size) {
    // The dynamic atlas uses raylib's glyph metrics; line height is the scaled
    // draw size, matching the previous MeasureTextEx("Ag") result closely.
    return effectiveSize(size * layout::S);
}

void text(const std::string& t, float x, float y, float size, Color color, float spacing) {
    drawTextImpl(t, x, y, size, color, spacing, kTextRegularPixels);
}
void text(const std::string& t, Vec2 pos, float size, Color color, float spacing) {
    drawTextImpl(t, pos.x, pos.y, size, color, spacing, kTextRegularPixels);
}
void textCentered(const std::string& t, Rectangle r, float size, Color color, float spacing) {
    const float w = textWidth(t, size, spacing);
    const float h = textHeight(size);
    drawTextImpl(t, r.x + (r.width - w) * 0.5f, r.y + (r.height - h) * 0.5f, size, color,
                 spacing, kTextRegularPixels);
}
void textBold(const std::string& t, float x, float y, float size, Color color,
              float spacing) {
    // drawTextImpl applies layout::S exactly once; passing a pre-scaled size here
    // would enlarge bold labels twice.
    drawTextImpl(t, x, y, size, color, spacing, kTextBoldPixels);
}
void textLeftBold(const std::string& t, Rectangle r, float size, Color color, float pad,
                  float spacing) {
    const float h = textHeight(size);
    textBold(t, r.x + pad, r.y + (r.height - h) * 0.5f, size, color, spacing);
}
void textCenteredBold(const std::string& t, Rectangle r, float size, Color color,
                      float spacing) {
    const float w = textWidth(t, size, spacing);
    const float h = textHeight(size);
    textBold(t, r.x + (r.width - w) * 0.5f, r.y + (r.height - h) * 0.5f, size, color,
             spacing);
}

void textLeft(const std::string& t, Rectangle r, float size, Color color, float pad,
              float spacing) {
    const float h = textHeight(size);
    drawTextImpl(t, r.x + pad, r.y + (r.height - h) * 0.5f, size, color, spacing,
                 kTextRegularPixels);
}
void textRight(const std::string& t, Rectangle r, float size, Color color, float pad,
               float spacing) {
    const float w = textWidth(t, size, spacing);
    const float h = textHeight(size);
    drawTextImpl(t, r.x + r.width - w - pad, r.y + (r.height - h) * 0.5f, size, color,
                 spacing, kTextRegularPixels);
}

std::string ellipsize(const std::string& t, float size, float maxWidth) {
    if (textWidth(t, size) <= maxWidth) return t;
    std::string out = t;
    while (!out.empty() && textWidth(out + "...", size) > maxWidth)
        out = out.substr(0, utf8Prev(out, (int)out.size()));
    return out + "...";
}

std::string format(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

Vec2 mouse() { return g.mouse; }
Vec2 mouseDelta() { return g.mouseDelta; }
bool mouseValid() { return g.valid; }
bool mouseIn(Rectangle r) {
    return g.enabled && g.valid && g.mouse.x >= r.x && g.mouse.x <= r.x + r.width &&
           g.mouse.y >= r.y && g.mouse.y <= r.y + r.height;
}
void setInteractionEnabled(bool enabled) { g.enabled = enabled; }
bool interactionEnabled() { return g.enabled; }
bool mouseDown(int button) { return button >= 0 && button < 3 && g.down[button]; }
bool mousePressed(int button) { return button >= 0 && button < 3 && g.pressed[button]; }
bool mouseReleased(int button) { return button >= 0 && button < 3 && g.released[button]; }
bool mouseConsumed() { return g.consumed; }
void consumeMouse() { g.consumed = true; }
float wheel() { return g.wheel; }
bool isEditingText() { return g.editing != 0; }
void cancelTextEditing() {
    g.editing = 0;
    g.active = 0;
    g.editBuffer.clear();
    g.caret = 0;
    g.selectionAnchor = 0;
    g.backspaceHeld = false;
}
uint32_t activeId() { return g.active; }

uint32_t id(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 16777619u;
    }
    return h;
}

float animate(uint32_t key, bool hovered, float speed) {
    float& v = g.anim[key];
    const float target = hovered ? 1.0f : 0.0f;
    const float dt = std::min(0.05f, GetFrameTime());
    v += (target - v) * clamp01(dt * speed);
    return v;
}

void panel(Rectangle r, float radius, Color color, bool shadow) {
    if (shadow) {
        DrawRectangleRounded({r.x + 3, r.y + 5, r.width, r.height}, radius, 10,
                             theme::shadow);
    }
    DrawRectangleRounded(r, radius, 10, color);
}

void separator(float x1, float y1, float x2, float y2, Color color) {
    DrawLineEx({x1, y1}, {x2, y2}, 1.0f, color);
}

void icon(Icon ic, Rectangle r, Color color, float strokeScale) {
    if (ic == Icon::None) return;
    const float s = std::min(r.width, r.height) * 0.52f;
    const Vec2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float th = std::max(1.4f, s * 0.14f) * strokeScale;
    switch (ic) {
        case Icon::Play:
            DrawTriangle({c.x - s * 0.42f, c.y - s * 0.58f},
                         {c.x - s * 0.42f, c.y + s * 0.58f},
                         {c.x + s * 0.68f, c.y}, color);
            break;
        case Icon::Pause:
            DrawRectangleRounded({c.x - s * 0.5f, c.y - s * 0.55f, s * 0.34f, s * 1.1f}, 0.25f,
                                 4, color);
            DrawRectangleRounded({c.x + s * 0.16f, c.y - s * 0.55f, s * 0.34f, s * 1.1f}, 0.25f,
                                 4, color);
            break;
        case Icon::Step:
            DrawTriangle({c.x - s * 0.55f, c.y - s * 0.5f},
                         {c.x - s * 0.55f, c.y + s * 0.5f},
                         {c.x + s * 0.4f, c.y}, color);
            DrawRectangleRounded({c.x + s * 0.46f, c.y - s * 0.55f, s * 0.16f, s * 1.1f}, 0.4f,
                                 4, color);
            break;
        case Icon::Reset:
            DrawRing(c, s * 0.5f, s * 0.5f + th, 30.0f, 330.0f, 32, color);
            DrawTriangle({c.x + s * 0.28f, c.y - s * 0.74f},
                         {c.x + s * 0.82f, c.y - s * 0.5f},
                         {c.x + s * 0.3f, c.y - s * 0.18f}, color);
            break;
        case Icon::Undo:
        case Icon::Redo: {
            // Clean bent back-arrow: a vertical tail on the right (left for
            // redo) joins a horizontal shaft that ends in a solid arrow head.
            // The previous diagonal shaft made the head look detached at small
            // icon sizes.
            const bool flip = ic == Icon::Redo;
            const float dir = flip ? -1.0f : 1.0f;
            const Vec2 tail{c.x + dir * s * 0.56f, c.y + s * 0.58f};
            const Vec2 corner{c.x + dir * s * 0.56f, c.y - s * 0.30f};
            const Vec2 shaftEnd{c.x - dir * s * 0.24f, c.y - s * 0.30f};
            DrawLineEx(tail, corner, th, color);
            DrawLineEx(corner, shaftEnd, th, color);
            DrawCircleV(corner, th * 0.5f, color);
            const Vec2 tip{c.x - dir * s * 0.82f, c.y - s * 0.30f};
            const Vec2 baseTop{shaftEnd.x, c.y - s * 0.30f - s * 0.30f};
            const Vec2 baseBottom{shaftEnd.x, c.y - s * 0.30f + s * 0.30f};
            // Mirroring the arrow reverses its winding; submit both so the
            // undo head is never culled while the redo head stays visible.
            DrawTriangle(tip, baseTop, baseBottom, color);
            DrawTriangle(tip, baseBottom, baseTop, color);
            break;
        }
        case Icon::Save:
            DrawRectangleRounded({c.x - s * 0.58f, c.y - s * 0.65f, s * 1.16f, s * 1.3f}, 0.15f,
                                 6, color);
            DrawRectangleRounded({c.x - s * 0.34f, c.y - s * 0.65f, s * 0.68f, s * 0.44f},
                                 0.12f, 4, theme::panel);
            DrawRectangleRounded({c.x - s * 0.4f, c.y + s * 0.06f, s * 0.8f, s * 0.46f}, 0.12f,
                                 4, theme::panel);
            break;
        case Icon::Folder: {
            const Vector2 p1{c.x - s * 0.7f, c.y - s * 0.45f};
            const Vector2 p2{c.x - s * 0.1f, c.y - s * 0.45f};
            const Vector2 p3{c.x + s * 0.06f, c.y - s * 0.24f};
            const Vector2 p4{c.x + s * 0.7f, c.y - s * 0.24f};
            DrawLineEx(p1, p2, th, color);
            DrawLineEx(p2, p3, th, color);
            DrawLineEx(p3, p4, th, color);
            DrawRectangleRounded({c.x - s * 0.7f, c.y - s * 0.2f, s * 1.4f, s * 0.85f}, 0.12f,
                                 5, color);
            break;
        }
        case Icon::Plus:
            DrawLineEx({c.x - s * 0.6f, c.y}, {c.x + s * 0.6f, c.y}, th, color);
            DrawLineEx({c.x, c.y - s * 0.6f}, {c.x, c.y + s * 0.6f}, th, color);
            break;
        case Icon::Trash:
            DrawLineEx({c.x - s * 0.65f, c.y - s * 0.5f}, {c.x + s * 0.65f, c.y - s * 0.5f},
                       th, color);
            DrawLineEx({c.x - s * 0.2f, c.y - s * 0.72f}, {c.x + s * 0.2f, c.y - s * 0.72f},
                       th, color);
            DrawLineEx({c.x - s * 0.48f, c.y - s * 0.32f}, {c.x - s * 0.38f, c.y + s * 0.62f},
                       th, color);
            DrawLineEx({c.x + s * 0.48f, c.y - s * 0.32f}, {c.x + s * 0.38f, c.y + s * 0.62f},
                       th, color);
            DrawLineEx({c.x - s * 0.38f, c.y + s * 0.62f}, {c.x + s * 0.38f, c.y + s * 0.62f},
                       th, color);
            DrawLineEx({c.x - s * 0.12f, c.y - s * 0.22f}, {c.x - s * 0.08f, c.y + s * 0.42f},
                       th * 0.8f, color);
            DrawLineEx({c.x + s * 0.12f, c.y - s * 0.22f}, {c.x + s * 0.08f, c.y + s * 0.42f},
                       th * 0.8f, color);
            break;
        case Icon::Search:
            DrawRing({c.x - s * 0.16f, c.y - s * 0.16f}, s * 0.4f, s * 0.4f + th, 0, 360, 32,
                     color);
            DrawLineEx({c.x + s * 0.18f, c.y + s * 0.18f}, {c.x + s * 0.62f, c.y + s * 0.62f},
                       th, color);
            break;
        case Icon::Grid:
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    DrawRectangleRounded({c.x - s * 0.6f + i * s * 0.66f,
                                          c.y - s * 0.6f + j * s * 0.66f, s * 0.5f,
                                          s * 0.5f},
                                         0.25f, 4, color);
            break;
        case Icon::Pin:
            DrawRing({c.x, c.y - s * 0.25f}, s * 0.28f, s * 0.28f + th, 0, 360, 24, color);
            DrawLineEx({c.x, c.y + s * 0.08f}, {c.x, c.y + s * 0.7f}, th, color);
            DrawCircleV({c.x, c.y - s * 0.25f}, s * 0.13f, color);
            break;
        case Icon::Weld:
            DrawRing({c.x - s * 0.28f, c.y}, s * 0.32f, s * 0.32f + th, 0, 360, 24, color);
            DrawRing({c.x + s * 0.28f, c.y}, s * 0.32f, s * 0.32f + th, 0, 360, 24, color);
            DrawLineEx({c.x - s * 0.3f, c.y}, {c.x + s * 0.3f, c.y}, th * 0.8f, color);
            break;
        case Icon::World:
            DrawRing(c, s * 0.55f, s * 0.55f + th, 0, 360, 32, color);
            DrawEllipseLines((int)c.x, (int)c.y, s * 0.26f, s * 0.55f, color);
            DrawLineEx({c.x - s * 0.55f, c.y}, {c.x + s * 0.55f, c.y}, th * 0.8f, color);
            break;
        case Icon::Close:
            DrawLineEx({c.x - s * 0.45f, c.y - s * 0.45f}, {c.x + s * 0.45f, c.y + s * 0.45f},
                       th, color);
            DrawLineEx({c.x + s * 0.45f, c.y - s * 0.45f}, {c.x - s * 0.45f, c.y + s * 0.45f},
                       th, color);
            break;
        case Icon::Edit:
            DrawLineEx({c.x - s * 0.55f, c.y + s * 0.55f}, {c.x + s * 0.4f, c.y - s * 0.4f},
                       th, color);
            DrawTriangle({c.x + s * 0.32f, c.y - s * 0.35f},
                         {c.x + s * 0.66f, c.y - s * 0.66f},
                         {c.x + s * 0.48f, c.y - s * 0.2f}, color);
            DrawLineEx({c.x - s * 0.55f, c.y + s * 0.55f}, {c.x - s * 0.3f, c.y + s * 0.58f},
                       th, color);
            break;
        case Icon::Refresh:
            DrawRing(c, s * 0.48f, s * 0.48f + th, 40.0f, 320.0f, 32, color);
            DrawTriangle({c.x + s * 0.16f, c.y - s * 0.72f},
                         {c.x + s * 0.7f, c.y - s * 0.44f},
                         {c.x + s * 0.14f, c.y - s * 0.12f}, color);
            break;
        case Icon::ChevronDown:
            DrawLineEx({c.x - s * 0.45f, c.y - s * 0.2f}, {c.x, c.y + s * 0.25f}, th, color);
            DrawLineEx({c.x + s * 0.45f, c.y - s * 0.2f}, {c.x, c.y + s * 0.25f}, th, color);
            break;
        case Icon::Check:
            DrawLineEx({c.x - s * 0.5f, c.y}, {c.x - s * 0.1f, c.y + s * 0.4f}, th, color);
            DrawLineEx({c.x - s * 0.1f, c.y + s * 0.4f}, {c.x + s * 0.55f, c.y - s * 0.4f},
                       th, color);
            break;
        case Icon::Layers:
            DrawLineEx({c.x - s * 0.6f, c.y - s * 0.25f}, {c.x, c.y - s * 0.6f}, th, color);
            DrawLineEx({c.x, c.y - s * 0.6f}, {c.x + s * 0.6f, c.y - s * 0.25f}, th, color);
            DrawLineEx({c.x - s * 0.6f, c.y + s * 0.12f}, {c.x, c.y - s * 0.24f}, th, color);
            DrawLineEx({c.x, c.y - s * 0.24f}, {c.x + s * 0.6f, c.y + s * 0.12f}, th, color);
            DrawLineEx({c.x - s * 0.6f, c.y + s * 0.48f}, {c.x, c.y + s * 0.14f}, th, color);
            DrawLineEx({c.x, c.y + s * 0.14f}, {c.x + s * 0.6f, c.y + s * 0.48f}, th, color);
            break;
        case Icon::Note:
            DrawRectangleRounded({c.x - s * 0.62f, c.y - s * 0.45f, s * 1.24f, s * 0.9f}, 0.25f,
                                 5, color);
            DrawLineEx({c.x - s * 0.32f, c.y}, {c.x + s * 0.32f, c.y}, th, theme::panel);
            break;
        case Icon::Settings:
            DrawRing(c, s * 0.2f, s * 0.2f + th, 0, 360, 24, color);
            for (int i = 0; i < 6; ++i) {
                const float a = i * PI / 3.0f;
                const Vec2 d{std::cos(a), std::sin(a)};
                DrawLineEx({c.x + d.x * s * 0.35f, c.y + d.y * s * 0.35f},
                           {c.x + d.x * s * 0.6f, c.y + d.y * s * 0.6f}, th, color);
            }
            break;
        case Icon::Fit:
            DrawLineEx({c.x - s * 0.6f, c.y - s * 0.25f}, {c.x - s * 0.6f, c.y - s * 0.58f},
                       th, color);
            DrawLineEx({c.x - s * 0.6f, c.y - s * 0.58f}, {c.x - s * 0.27f, c.y - s * 0.58f},
                       th, color);
            DrawLineEx({c.x + s * 0.6f, c.y - s * 0.25f}, {c.x + s * 0.6f, c.y - s * 0.58f},
                       th, color);
            DrawLineEx({c.x + s * 0.6f, c.y - s * 0.58f}, {c.x + s * 0.27f, c.y - s * 0.58f},
                       th, color);
            DrawLineEx({c.x - s * 0.6f, c.y + s * 0.25f}, {c.x - s * 0.6f, c.y + s * 0.58f},
                       th, color);
            DrawLineEx({c.x - s * 0.6f, c.y + s * 0.58f}, {c.x - s * 0.27f, c.y + s * 0.58f},
                       th, color);
            DrawLineEx({c.x + s * 0.6f, c.y + s * 0.25f}, {c.x + s * 0.6f, c.y + s * 0.58f},
                       th, color);
            DrawLineEx({c.x + s * 0.6f, c.y + s * 0.58f}, {c.x + s * 0.27f, c.y + s * 0.58f},
                       th, color);
            break;
        case Icon::Dot:
            DrawCircleV(c, s * 0.35f, color);
            break;
        case Icon::Sun:
            DrawCircleV(c, s * 0.25f, color);
            for (int i = 0; i < 8; ++i) {
                const float a = i * PI / 4.0f;
                const Vec2 d{std::cos(a), std::sin(a)};
                DrawLineEx({c.x + d.x * s * 0.40f, c.y + d.y * s * 0.40f},
                           {c.x + d.x * s * 0.62f, c.y + d.y * s * 0.62f}, th, color);
            }
            break;
        case Icon::Moon:
            // Match the day-mode sun's visual footprint. The crescent outline
            // used to fill most of the 34 px button and looked oversized next
            // to the sun icon.
            drawCrescent(c, s * 0.74f, color);
            break;
        default:
            break;
    }
}

void tooltip(const std::string& t, Vec2 pos) {
    g.hasTooltip = true;
    g.tooltip = t;
    g.tooltipPos = pos;
}

bool button(const char* idStr, Rectangle r, const std::string& labelStr,
            ButtonStyle style, bool enabled, Icon ic) {
    const uint32_t key = id(idStr);
    const bool hovered = enabled && mouseIn(r) && !g.consumed;
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    const bool pressedNow = hovered && g.pressed[0];
    if (pressedNow) {
        g.active = key;
        g.consumed = true;
    }
    bool clicked = false;
    if (g.active == key) {
        if (g.released[0]) {
            if (hovered) clicked = true;
            g.active = 0;
        }
        if (!g.down[0]) g.active = 0;
    }

    Color bg;
    Color fg;
    Color border = BLANK;
    switch (style) {
        case ButtonStyle::Primary:
            bg = theme::mix(theme::primary, theme::primaryHover, h);
            fg = theme::onPrimary;
            break;
        case ButtonStyle::Secondary:
            bg = theme::mix(theme::card, theme::cardHover, h);
            fg = theme::text;
            border = theme::mix(theme::stroke, theme::accentDark, h * 0.6f);
            break;
        case ButtonStyle::Ghost:
            bg = theme::mix(Color{0, 0, 0, 0}, theme::alpha(theme::cardHover, 150), h);
            fg = theme::text;
            break;
        case ButtonStyle::Danger:
            bg = theme::mix(theme::alpha(theme::danger, 24), theme::alpha(theme::danger, 50), h);
            fg = theme::text;
            border = theme::alpha(theme::danger, 80);
            break;
        case ButtonStyle::Subtle:
            bg = theme::mix(theme::panelRaised, theme::card, h);
            fg = theme::text;
            break;
        case ButtonStyle::AccentLight:
            bg = theme::mix(theme::alpha(theme::accent, 62),
                            theme::alpha(theme::accent, 96), h);
            fg = theme::text;
            border = theme::alpha(theme::accent, 150);
            break;
    }
    if (!enabled) {
        bg = theme::alpha(theme::panelRaised, 160);
        fg = theme::dim;
        border = theme::strokeSoft;
    }

    if (style == ButtonStyle::Primary && enabled) {
        DrawRectangleRounded(
            {r.x, r.y + 4, r.width, r.height}, 0.32f, 8,
            theme::highlight(theme::accent, (unsigned char)(45 + 25 * h)));
    }
    DrawRectangleRounded(r, 0.32f, 8, bg);
    if (border.a > 0) DrawRectangleRoundedLines(r, 0.32f, 8, border);

    const float fontSize = 13.5f;
    const float iconW = ic != Icon::None ? 22.0f : 0.0f;
    const float tw = textWidth(labelStr, fontSize);
    const float contentW = tw + iconW;
    const float x0 = r.x + (r.width - contentW) * 0.5f;
    if (ic != Icon::None) {
        icon(ic, {x0, r.y + (r.height - 16) * 0.5f, 16, 16}, fg);
    }
    textLeftBold(labelStr, {x0 + iconW, r.y, tw + 4, r.height}, fontSize, fg, 0.0f);
    return clicked;
}

bool iconButton(const char* idStr, Rectangle r, Icon ic, const char* tip, bool active,
                bool enabled) {
    const uint32_t key = id(idStr);
    const bool hovered = enabled && mouseIn(r) && !g.consumed;
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    if (hovered && g.pressed[0]) {
        g.active = key;
        g.consumed = true;
    }
    bool clicked = false;
    if (g.active == key && g.released[0]) {
        if (hovered) clicked = true;
        g.active = 0;
    }
    Color bg = theme::mix(active ? theme::highlight(theme::accent, 40) : Color{0, 0, 0, 0},
                          theme::alpha(theme::cardHover, 210), h);
    if (active) bg = theme::mix(theme::highlight(theme::accent, 45),
                               theme::highlight(theme::accent, 70), h);
    DrawRectangleRounded(r, 0.28f, 8, bg);
    Color fg = theme::text;
    if (!enabled) fg = theme::dim;
    icon(ic, r, fg);
    if (hovered && tip) tooltip(tip, {g.mouse.x, g.mouse.y});
    return clicked;
}

bool chip(const char* idStr, Rectangle r, const std::string& label, bool selected,
          Color dotColor, bool enabled) {
    const uint32_t key = id(idStr);
    const bool hovered = enabled && mouseIn(r) && !g.consumed;
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    if (hovered && g.pressed[0]) {
        g.active = key;
        g.consumed = true;
    }
    bool clicked = false;
    if (g.active == key && g.released[0]) {
        if (hovered) clicked = true;
        g.active = 0;
    }
    Color bg = selected ? theme::highlight(theme::accent, 38)
                        : theme::mix(theme::panelRaised, theme::cardHover, h);
    DrawRectangleRounded(r, 0.4f, 8, bg);
    if (selected)
        DrawRectangleRoundedLines(r, 0.4f, 8, theme::highlight(theme::accent, 165));
    float x = r.x + 11.0f;
    if (dotColor.a > 0) {
        DrawCircleV({x + 3, r.y + r.height * 0.5f}, 3.5f, dotColor);
        x += 13.0f;
    }
    Color fg = selected ? theme::text : theme::textSoft;
    if (!enabled) fg = theme::dim;
    textLeftBold(label, {x, r.y, r.x + r.width - x - 8, r.height}, 12.5f, fg, 0.0f);
    if (hovered) maybeCursor(true);
    return clicked;
}

bool toggle(const char* idStr, Rectangle r, const std::string& label, bool& value) {
    const uint32_t key = id(idStr);
    const bool hovered = mouseIn(r) && !g.consumed && !isEditingText();
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    bool changed = false;
    if (hovered && g.pressed[0]) {
        value = !value;
        changed = true;
        g.consumed = true;
        g.active = key;
    }
    if (g.active == key && !g.down[0]) g.active = 0;
    textLeft(label, r, 12.5f, theme::text, 0.0f);
    Rectangle sw{r.x + r.width - 38, r.y + (r.height - 20) * 0.5f, 38, 20};
    DrawRectangleRounded(sw, 0.5f, 10, value ? theme::accent : theme::stroke);
    DrawCircleV({sw.x + (value ? 28.0f : 10.0f), sw.y + 10.0f}, 7.0f,
                value ? theme::onAccent : theme::muted);
    return changed;
}

bool checkbox(const char* idStr, Rectangle r, const std::string& label, bool& value) {
    const uint32_t key = id(idStr);
    const bool hovered = mouseIn(r) && !g.consumed && !isEditingText();
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    bool changed = false;
    if (hovered && g.pressed[0]) {
        value = !value;
        changed = true;
        g.consumed = true;
        g.active = key;
    }
    if (g.active == key && !g.down[0]) g.active = 0;
    Rectangle box{r.x, r.y + (r.height - 18) * 0.5f, 18, 18};
    DrawRectangleRounded(box, 0.28f, 6, value ? theme::accent : theme::alpha(theme::panelRaised, 220));
    DrawRectangleRoundedLines(box, 0.28f, 6,
                              value ? theme::accent : theme::mix(theme::stroke, theme::muted, h));
    if (value) icon(Icon::Check, {box.x + 1, box.y + 1, 16, 16}, theme::onAccent, 1.2f);
    textLeft(label, {r.x + 26, r.y, r.width - 26, r.height}, 12.5f, theme::text, 0.0f);
    return changed;
}

namespace {

int selectionBegin() { return std::min(g.caret, g.selectionAnchor); }
int selectionEnd() { return std::max(g.caret, g.selectionAnchor); }
bool hasSelection() { return g.caret != g.selectionAnchor; }

void deleteSelection(std::string& text) {
    const int begin = selectionBegin();
    const int end = selectionEnd();
    if (begin == end) return;
    text.erase((size_t)begin, (size_t)(end - begin));
    g.caret = begin;
    g.selectionAnchor = begin;
}

void moveCaret(int position, bool extend) {
    position = std::max(0, std::min((int)g.editBuffer.size(), position));
    g.caret = position;
    if (!extend) g.selectionAnchor = position;
}

int caretFromMouseX(const std::string& text, float x, float fontSize) {
    if (x <= 0.0f || text.empty()) return 0;
    int best = 0;
    float bestDistance = std::fabs(x);
    int pos = 0;
    while (pos < (int)text.size()) {
        const int next = utf8Next(text, pos);
        const float width = textWidth(text.substr(0, (size_t)next), fontSize);
        const float distance = std::fabs(x - width);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = next;
        }
        if (width >= x) break;
        pos = next;
    }
    return best;
}

void insertText(std::string& text, const std::string& insertion, int maxChars) {
    deleteSelection(text);
    const int room = maxChars - utf8Length(text);
    if (room <= 0) return;
    std::string filtered;
    int count = 0;
    for (int pos = 0; pos < (int)insertion.size() && count < room;) {
        const int next = utf8Next(insertion, pos);
        filtered.append(insertion, (size_t)pos, (size_t)(next - pos));
        pos = next;
        ++count;
    }
    text.insert((size_t)g.caret, filtered);
    g.caret += (int)filtered.size();
    g.selectionAnchor = g.caret;
}

bool numericCharAllowed(int codepoint) {
    return (codepoint >= '0' && codepoint <= '9') || codepoint == '.' ||
           codepoint == '-' || codepoint == '+' || codepoint == 'e' ||
           codepoint == 'E';
}

enum class TextEditResult { None, Commit, Cancel };

TextEditResult processTextEditing(std::string& text, bool numeric, int maxChars) {
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (ctrl && IsKeyPressed(KEY_A)) {
        g.selectionAnchor = 0;
        g.caret = (int)text.size();
        return TextEditResult::None;
    }
    if (ctrl && IsKeyPressed(KEY_C)) {
        if (hasSelection()) {
            const std::string selected =
                text.substr((size_t)selectionBegin(),
                            (size_t)(selectionEnd() - selectionBegin()));
            SetClipboardText(selected.c_str());
        }
        return TextEditResult::None;
    }
    if (ctrl && IsKeyPressed(KEY_X)) {
        if (hasSelection()) {
            const std::string selected =
                text.substr((size_t)selectionBegin(),
                            (size_t)(selectionEnd() - selectionBegin()));
            SetClipboardText(selected.c_str());
            deleteSelection(text);
        }
        return TextEditResult::None;
    }
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char* clipboard = GetClipboardText();
        if (clipboard && *clipboard) {
            std::string insertion = clipboard;
            if (numeric) {
                std::string filtered;
                for (int pos = 0; pos < (int)insertion.size();) {
                    const int next = utf8Next(insertion, pos);
                    const int cp = (unsigned char)insertion[(size_t)pos];
                    if (numericCharAllowed(cp))
                        filtered.append(insertion, (size_t)pos, (size_t)(next - pos));
                    pos = next;
                }
                insertion = std::move(filtered);
            }
            insertText(text, insertion, maxChars);
        }
        return TextEditResult::None;
    }

    if (IsKeyPressed(KEY_LEFT)) {
        if (ctrl) {
            moveCaret(0, shift);
        } else if (!shift && hasSelection()) {
            moveCaret(selectionBegin(), false);
        } else {
            int position = g.caret;
            if (position > 0) position = utf8Prev(text, position);
            moveCaret(position, shift);
        }
    }
    if (IsKeyPressed(KEY_RIGHT)) {
        if (ctrl) {
            moveCaret((int)text.size(), shift);
        } else if (!shift && hasSelection()) {
            moveCaret(selectionEnd(), false);
        } else {
            int position = g.caret;
            if (position < (int)text.size()) position = utf8Next(text, position);
            moveCaret(position, shift);
        }
    }
    if (IsKeyPressed(KEY_HOME)) moveCaret(0, shift);
    if (IsKeyPressed(KEY_END)) moveCaret((int)text.size(), shift);

    if (backspaceRepeat()) {
        if (hasSelection()) {
            deleteSelection(text);
        } else if (g.caret > 0) {
            const int prev = utf8Prev(text, g.caret);
            text.erase((size_t)prev, (size_t)(g.caret - prev));
            g.caret = prev;
            g.selectionAnchor = prev;
        }
    }
    if (IsKeyPressed(KEY_DELETE)) {
        if (hasSelection()) {
            deleteSelection(text);
        } else if (g.caret < (int)text.size()) {
            const int next = utf8Next(text, g.caret);
            text.erase((size_t)g.caret, (size_t)(next - g.caret));
        }
    }

    if (!ctrl) {
        int cp = 0;
        while ((cp = GetCharPressed()) != 0) {
            if (cp < 32) continue;
            if (numeric && !numericCharAllowed(cp)) continue;
            if (utf8Length(text) >= maxChars && !hasSelection()) continue;
            const std::string encoded = utf8Encode(cp);
            insertText(text, encoded, maxChars);
        }
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        return TextEditResult::Commit;
    if (IsKeyPressed(KEY_ESCAPE)) return TextEditResult::Cancel;
    return TextEditResult::None;
}

} // namespace

bool textField(const char* idStr, Rectangle r, std::string& value,
               const std::string& placeholder, int maxChars, float fontSize,
               const std::string& label) {
    const uint32_t key = id(idStr);
    const bool hovered = mouseIn(r) && !g.consumed;
    if (hovered) {
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
        g.cursorSet = true;
    }
    const bool isActive = g.editing == key;
    if (isActive) g.sawEditingField = true;
    const float labelWidth = label.empty() ? 0.0f : textWidth(label, fontSize) + 8.0f;
    const float textPad = 9.0f + labelWidth;

    bool committed = false;

    if (hovered && g.pressed[0]) {
        const bool extend = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool wasActive = g.editing == key;
        g.editing = key;
        g.editBuffer = value;
        const int clicked = caretFromMouseX(g.editBuffer, g.mouse.x - (r.x + textPad),
                                            fontSize);
        g.caret = clicked;
        if (!extend || !wasActive) g.selectionAnchor = clicked;
        g.consumed = true;
        g.active = key;
        g.sawEditingField = true;
    } else if (isActive && g.pressed[0] && !hovered) {
        value = g.editBuffer;
        g.editing = 0;
        g.active = 0;
        committed = true;
    }

    if (g.editing == key) {
        // Continue extending the selection while the mouse is held down.
        if (g.down[0] && g.active == key) {
            const int clicked = caretFromMouseX(g.editBuffer, g.mouse.x - (r.x + textPad),
                                                fontSize);
            g.caret = clicked;
        }
        const TextEditResult result = processTextEditing(g.editBuffer, false, maxChars);
        if (result == TextEditResult::Commit) {
            value = g.editBuffer;
            g.editing = 0;
            g.active = 0;
            committed = true;
        } else if (result == TextEditResult::Cancel) {
            g.editing = 0;
            g.active = 0;
        }
    }

    const bool activeNow = g.editing == key;
    Color bg = theme::mix(theme::panelRaised, theme::card, activeNow ? 1.0f : (hovered ? 0.5f : 0.0f));
    DrawRectangleRounded(r, 0.25f, 6, bg);
    DrawRectangleRoundedLines(r, 0.25f, 6,
                              activeNow ? theme::accent : theme::mix(theme::stroke, theme::muted, 0.3f));
    if (!label.empty())
        textLeft(label, r, fontSize, theme::muted, 9.0f);
    scissorPush({r.x + 8, r.y - 2, r.width - 16, r.height + 4});
    const std::string& shown = activeNow ? g.editBuffer : value;
    if (shown.empty() && !activeNow) {
        textLeft(placeholder, r, fontSize, theme::dim, textPad);
    } else {
        const std::string before = shown.substr(0, (size_t)g.caret);
        float offsetX = 0.0f;
        const float textWidthAvailable = r.width - 18.0f - labelWidth;
        if (activeNow) {
            const float caretX = textWidth(before, fontSize);
            if (caretX > textWidthAvailable) offsetX = caretX - textWidthAvailable;
        }
        if (activeNow && hasSelection()) {
            const int begin = selectionBegin();
            const int end = selectionEnd();
            const float x0 = r.x + textPad + textWidth(shown.substr(0, (size_t)begin), fontSize) -
                             offsetX;
            const float x1 = r.x + textPad + textWidth(shown.substr(0, (size_t)end), fontSize) -
                             offsetX;
            DrawRectangle((int)x0, (int)(r.y + 3), (int)std::max(1.0f, x1 - x0),
                          (int)(r.height - 6), theme::alpha(theme::accent, 75));
        }
        textLeft(shown, r, fontSize, theme::text, textPad - offsetX);
        if (activeNow && std::fmod((float)GetTime(), 1.0f) < 0.55f) {
            const float caretX = r.x + textPad + textWidth(before, fontSize) - offsetX;
            const float top = r.y + (r.height - textHeight(fontSize)) * 0.5f;
            DrawRectangle((int)caretX, (int)top, 1,
                          (int)textHeight(fontSize), theme::accentLight);
        }
    }
    scissorPop();
    return committed;
}

bool segmented(const char* idStr, Rectangle r, const std::vector<std::string>& labels,
               int& index) {
    const uint32_t key = id(idStr);
    bool changed = false;
    DrawRectangleRounded(r, 0.4f, 8, theme::panelRaised);
    const float segW = r.width / std::max<size_t>(1, labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        Rectangle seg{r.x + segW * i + 3, r.y + 3, segW - 6, r.height - 6};
        const bool hovered = mouseIn(seg) && !g.consumed;
        if (hovered) maybeCursor(true);
        const bool selected = (int)i == index;
        if (hovered && g.pressed[0]) {
            if (index != (int)i) {
                index = (int)i;
                changed = true;
            }
            g.consumed = true;
            g.active = key;
        }
        if (selected) {
            DrawRectangleRounded(seg, 0.32f, 6, theme::highlight(theme::accent, 55));
            DrawRectangleRoundedLines(seg, 0.32f, 6,
                                      theme::highlight(theme::accent, 100));
        } else if (hovered) {
            DrawRectangleRounded(seg, 0.32f, 6, theme::alpha(theme::cardHover, 120));
        }
        textCenteredBold(labels[i], seg, 12.5f, theme::text);
    }
    if (g.active == key && !g.down[0]) g.active = 0;
    return changed;
}

bool numberField(const char* idStr, Rectangle r, const std::string& label, float& value,
                 float lo, float hi, int decimals, const char* suffix) {
    if (hi <= lo) hi = lo + 1.0f;
    value = clampf(value, lo, hi);
    const uint32_t key = id(idStr);
    bool active = g.editing == key;
    if (active) g.sawEditingField = true;
    const bool hovered = mouseIn(r) && !g.consumed;
    if (hovered) {
        SetMouseCursor(MOUSE_CURSOR_IBEAM);
        g.cursorSet = true;
    }
    const float h = animate(key, hovered || active);
    bool changed = false;
    const float fontSize = 12.0f;
    const float textX = r.x + 8.0f +
                        (label.empty() ? 0.0f : textWidth(label, fontSize) + 6.0f);

    auto commitValue = [&] {
        float parsedValue = value;
        if (parseNumberText(g.editBuffer, lo, hi, parsedValue) &&
            std::fabs(parsedValue - value) > 1e-6f) {
            value = parsedValue;
            changed = true;
        }
    };

    if (hovered && g.pressed[0]) {
        const bool extend = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool wasActive = active;
        g.consumed = true;
        g.active = key;
        if (!active) {
            g.editing = key;
            g.editBuffer = format(formatStringFor(decimals), value);
            active = true;
        }
        g.sawEditingField = true;
        const int clicked = caretFromMouseX(g.editBuffer, g.mouse.x - textX, fontSize);
        g.caret = clicked;
        if (!extend || !wasActive) g.selectionAnchor = clicked;
    }

    if (g.editing == key) {
        if (g.down[0] && g.active == key) {
            const int clicked = caretFromMouseX(g.editBuffer, g.mouse.x - textX, fontSize);
            g.caret = clicked;
        }
        const TextEditResult result = processTextEditing(g.editBuffer, true, 16);
        if (result == TextEditResult::Commit) {
            commitValue();
            g.editing = 0;
            g.active = 0;
            active = false;
            while (GetCharPressed() != 0) {
            }
        } else if (result == TextEditResult::Cancel) {
            g.editing = 0;
            g.active = 0;
            active = false;
        } else if (g.pressed[0] && !hovered) {
            commitValue();
            g.editing = 0;
            g.active = 0;
            active = false;
            // Let the click reach whatever sits underneath.
            g.consumed = false;
        }
    }

    Color bg = theme::mix(theme::panelRaised, theme::card,
                          active ? 1.0f : (hovered ? 0.7f : 0.0f));
    DrawRectangleRounded(r, 0.24f, 6, bg);
    DrawRectangleRoundedLines(r, 0.24f, 6,
                              active ? theme::accent
                                     : theme::mix(theme::stroke, theme::muted, 0.25f + 0.25f * h));
    if (!label.empty())
        textLeft(label, {r.x, r.y, r.width, r.height}, fontSize, theme::text, 8.0f);
    const std::string shownText =
        active ? g.editBuffer : format(formatStringFor(decimals), value);
    const std::string shown = shownText + suffix;
    if (active && hasSelection()) {
        const int begin = selectionBegin();
        const int end = selectionEnd();
        const float x0 = textX + textWidth(shownText.substr(0, (size_t)begin), fontSize);
        const float x1 = textX + textWidth(shownText.substr(0, (size_t)end), fontSize);
        DrawRectangle((int)x0, (int)(r.y + 3.0f), (int)std::max(1.0f, x1 - x0),
                      (int)(r.height - 6.0f), theme::alpha(theme::accent, 75));
    }
    scissorPush({textX - 2, r.y - 8, std::max(1.0f, r.x + r.width - 8.0f - textX),
                 r.height + 16});
    textLeft(shown, {textX - 8.0f, r.y, r.width, r.height}, fontSize, theme::text, 8.0f);
    if (active && std::fmod((float)GetTime(), 1.0f) < 0.55f) {
        const std::string before = shownText.substr(0, (size_t)g.caret);
        const float caretX = textX + textWidth(before, fontSize);
        const float top = r.y + (r.height - textHeight(fontSize)) * 0.5f;
        DrawRectangle((int)caretX, (int)top, 1, (int)textHeight(fontSize), theme::accentLight);
    }
    scissorPop();
    return changed;
}

bool colorSwatch(const char* idStr, Rectangle r, Color color, bool selected) {
    const uint32_t key = id(idStr);
    const bool hovered = mouseIn(r) && !g.consumed && !isEditingText();
    if (hovered) maybeCursor(true);
    const float h = animate(key, hovered);
    bool clicked = false;
    if (hovered && g.pressed[0]) {
        clicked = true;
        g.consumed = true;
    }
    const Vec2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float radius = std::min(r.width, r.height) * 0.5f - 2.0f;
    if (selected || hovered)
        DrawCircleV(c, radius + 3.0f + h,
                    theme::highlight(theme::accent, selected ? 160 : 85));
    DrawCircleV(c, radius, color);
    if (selected)
        DrawCircleLines((int)c.x, (int)c.y, radius - 2.0f, theme::onAccent);
    return clicked;
}

float scrollOffset(const char* idStr, Rectangle r, float contentHeight) {
    float& off = g.scroll[idStr];
    const float maxOff = std::max(0.0f, contentHeight - r.height);
    if (mouseIn(r) && !g.consumed && std::fabs(g.wheel) > 0.001f && !isEditingText()) {
        off -= g.wheel * 46.0f;
        g.wheel = 0.0f;
    }
    off = clampf(off, 0.0f, maxOff);
    return off;
}

void drawScrollbar(Rectangle r, float contentHeight, float offset) {
    if (contentHeight <= r.height + 1.0f) return;
    const float trackH = r.height;
    const float barH = std::max(28.0f, trackH * (r.height / contentHeight));
    const float maxOff = contentHeight - r.height;
    const float t = maxOff > 0.0f ? offset / maxOff : 0.0f;
    const float x = r.x + r.width - 4.0f;
    DrawRectangleRounded({x, r.y, 4, trackH}, 0.5f, 6, theme::alpha(theme::stroke, 120));
    DrawRectangleRounded({x, r.y + (trackH - barH) * t, 4, barH}, 0.5f, 6,
                         theme::alpha(theme::muted, 170));
}

void scissorPush(Rectangle r) {
    // Scissor rectangles are framebuffer pixels, so map the design rectangle
    // through the camera transform first. Use rlScissor directly instead of
    // BeginScissorMode because raylib flips the rectangle with the logical
    // screen height, which is wrong on high-DPI windows where the framebuffer
    // is taller than the window's screen-space size.
    const float left = g.pixelOffset.x + r.x * g.pixelScale;
    const float top = g.pixelOffset.y + r.y * g.pixelScale;
    const float right = left + r.width * g.pixelScale;
    const float bottom = top + r.height * g.pixelScale;
    const int x0 = (int)std::floor(left);
    const int y1 = (int)std::ceil(bottom);
    const int width = std::max(0, (int)std::ceil(right) - x0);
    const int height = std::max(0, y1 - (int)std::floor(top));
    rlDrawRenderBatchActive();
    rlEnableScissorTest();
    rlScissor(x0, GetRenderHeight() - y1, width, height);
}
void scissorPop() {
    rlDrawRenderBatchActive();
    rlDisableScissorTest();
}

int utf8Length(const std::string& s) {
    int count = 0;
    int i = 0;
    while (i < (int)s.size()) {
        const unsigned char c = (unsigned char)s[(size_t)i];
        int size = 1;
        if ((c & 0x80) == 0) size = 1;
        else if ((c & 0xE0) == 0xC0) size = 2;
        else if ((c & 0xF0) == 0xE0) size = 3;
        else if ((c & 0xF8) == 0xF0) size = 4;
        i += size;
        ++count;
    }
    return count;
}

int utf8Prev(const std::string& s, int index) {
    if (index <= 0) return 0;
    int i = std::min(index, (int)s.size()) - 1;
    while (i > 0 && ((unsigned char)s[(size_t)i] & 0xC0) == 0x80) --i;
    return i;
}
int utf8Next(const std::string& s, int index) {
    if (index >= (int)s.size()) return (int)s.size();
    int i = index + 1;
    while (i < (int)s.size() && ((unsigned char)s[(size_t)i] & 0xC0) == 0x80) ++i;
    return i;
}
std::string utf8Encode(int cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return out;
}

} // namespace ui