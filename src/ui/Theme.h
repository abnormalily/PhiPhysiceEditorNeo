#pragma once
// PhiPhysiceEditorNeo visual language. Two palettes share the same semantic colour
// names: the default dark "laboratory" theme and a light paper theme. UI code
// always reads theme::* at draw time, so switching modes or accent colours takes
// effect on the next frame without rebuilding cached colours.

#include "raylib.h"

namespace theme {

inline bool light = false;
inline int accentIndex = 0;

// Surfaces.
inline Color bg = {7, 10, 20, 255};
inline Color bgGlow = {14, 22, 44, 255};
inline Color canvas = {9, 13, 26, 255};
inline Color canvasGlow = {18, 29, 56, 255};
inline Color canvasTop = {11, 17, 32, 255};
inline Color canvasBottom = {8, 12, 23, 255};
inline Color topBarTop = {11, 17, 32, 255};
inline Color topBarBottom = {14, 22, 42, 255};
inline Color panel = {14, 21, 38, 255};
inline Color panelRaised = {19, 28, 49, 255};
inline Color card = {23, 34, 58, 255};
inline Color cardHover = {30, 45, 75, 255};
inline Color cardActive = {35, 53, 90, 255};
inline Color stroke = {39, 55, 88, 255};
inline Color strokeSoft = {28, 40, 65, 255};
inline Color modalDim = {3, 6, 14, 255};

// Text tiers.
inline Color text = {255, 255, 255, 255};
inline Color textSoft = {248, 251, 255, 255};
inline Color muted = {240, 246, 255, 255};
inline Color dim = {232, 240, 255, 255};
inline Color onPrimary = {255, 255, 255, 255};
inline Color onAccent = {255, 255, 255, 255};
inline Color accent = {136, 176, 255, 255};
inline Color accentLight = {190, 214, 255, 255};
inline Color accentDark = {84, 122, 196, 255};
inline Color primary = {58, 92, 164, 255};
inline Color primaryHover = {72, 110, 188, 255};
inline Color accent2 = {122, 224, 198, 255};
inline Color good = {134, 216, 176, 255};
inline Color warn = {242, 202, 138, 255};
inline Color danger = {242, 150, 165, 255};
inline Color shadow = {0, 0, 0, 46};
// Default judge line colour (pale yellow).
inline Color judgeLine = {255, 238, 150, 255};

inline Color alpha(Color c, unsigned char a) {
    c.a = a;
    return c;
}

inline Color mix(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return Color{(unsigned char)(a.r + (b.r - a.r) * t),
                 (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t),
                 (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Tint used by every interaction highlight (selection halos, hover outlines,
// placement ghosts, field borders). Dark mode lifts every hue towards white so
// highlights stay clearly visible on the dark canvas.
inline Color highlight(Color c, unsigned char alpha = 255) {
    if (!light) c = mix(c, Color{255, 255, 255, 255}, 0.30f);
    c.a = alpha;
    return c;
}

inline constexpr int kAccentCount = 8;

inline const char* accentName(int index) {
    static const char* names[] = {"蓝色", "粉色", "绿色", "黄色",
                                  "橙色", "天蓝色", "紫色", "灰色"};
    if (index < 0 || index >= kAccentCount) index = 0;
    return names[index];
}

inline Color accentPreview(int index) {
    // Macaron palette. Yellow keeps its red channel lower than orange so the
    // two hues stay clearly separated.
    static const Color dark[] = {{158, 196, 255, 255}, {255, 176, 206, 255},
                                 {166, 232, 196, 255}, {246, 230, 148, 255},
                                 {255, 176, 124, 255}, {160, 224, 250, 255},
                                 {206, 186, 255, 255}, {206, 208, 214, 255}};
    static const Color paper[] = {{96, 142, 220, 255},  {220, 108, 152, 255},
                                  {64, 174, 132, 255}, {206, 174, 54, 255},
                                  {228, 124, 62, 255}, {56, 166, 220, 255},
                                  {144, 114, 214, 255}, {124, 130, 142, 255}};
    if (index < 0 || index >= kAccentCount) index = 0;
    return light ? paper[index] : dark[index];
}

inline void setAccentColor(int index) {
    static const Color dark[] = {{158, 196, 255, 255}, {255, 176, 206, 255},
                                 {166, 232, 196, 255}, {246, 230, 148, 255},
                                 {255, 176, 124, 255}, {160, 224, 250, 255},
                                 {206, 186, 255, 255}, {206, 208, 214, 255}};
    static const Color paper[] = {{96, 142, 220, 255},  {220, 108, 152, 255},
                                  {64, 174, 132, 255}, {206, 174, 54, 255},
                                  {228, 124, 62, 255}, {56, 166, 220, 255},
                                  {144, 114, 214, 255}, {124, 130, 142, 255}};
    if (index < 0 || index >= kAccentCount) index = 0;
    accentIndex = index;
    const Color base = light ? paper[index] : dark[index];
    accent = base;

    // The application background follows the active accent. Tint from fixed
    // base colours so switching themes or accents never accumulates.
    const Color bgBase = light ? Color{235, 240, 248, 255} : Color{7, 10, 20, 255};
    const Color bgGlowBase = light ? Color{246, 249, 255, 255} : Color{14, 22, 44, 255};
    const Color canvasBase = light ? Color{247, 250, 255, 255} : Color{9, 13, 26, 255};
    const Color canvasGlowBase = light ? Color{226, 234, 247, 255} : Color{18, 29, 56, 255};
    const Color canvasTopBase = light ? Color{246, 249, 255, 255} : Color{11, 17, 32, 255};
    const Color canvasBottomBase = light ? Color{231, 238, 250, 255} : Color{8, 12, 23, 255};
    const Color topBarTopBase = light ? Color{252, 253, 255, 255} : Color{11, 17, 32, 255};
    const Color topBarBottomBase = light ? Color{238, 243, 251, 255} : Color{14, 22, 42, 255};
    const float bgTint = light ? 0.055f : 0.14f;
    const float surfaceTint = light ? 0.040f : 0.085f;
    bg = mix(bgBase, base, bgTint);
    bgGlow = mix(bgGlowBase, base, bgTint);
    canvas = mix(canvasBase, base, surfaceTint);
    canvasGlow = mix(canvasGlowBase, base, surfaceTint);
    canvasTop = mix(canvasTopBase, base, surfaceTint);
    canvasBottom = mix(canvasBottomBase, base, surfaceTint);
    topBarTop = mix(topBarTopBase, base, surfaceTint);
    topBarBottom = mix(topBarBottomBase, base, surfaceTint);

    if (light) {
        accentLight = mix(base, Color{0, 0, 0, 255}, 0.18f);
        accentDark = mix(base, Color{0, 0, 0, 255}, 0.10f);
        primary = base;
        primaryHover = mix(base, Color{0, 0, 0, 255}, 0.12f);
    } else {
        accentLight = mix(base, Color{255, 255, 255, 255}, 0.48f);
        accentDark = mix(base, Color{0, 0, 0, 255}, 0.30f);
        primary = mix(base, bg, 0.55f);
        primaryHover = mix(base, bg, 0.40f);
    }
}

inline void setLightMode(bool useLight) {
    light = useLight;
    if (useLight) {
        // Light paper palette. Text tiers stay dark enough for at least 7:1 on
        // the white panels; accent colours are deepened for readable borders.
        bg = {235, 240, 248, 255};
        bgGlow = {246, 249, 255, 255};
        canvas = {247, 250, 255, 255};
        canvasGlow = {226, 234, 247, 255};
        canvasTop = {246, 249, 255, 255};
        canvasBottom = {231, 238, 250, 255};
        topBarTop = {252, 253, 255, 255};
        topBarBottom = {238, 243, 251, 255};
        panel = {255, 255, 255, 255};
        panelRaised = {242, 246, 253, 255};
        card = {233, 239, 249, 255};
        cardHover = {223, 232, 246, 255};
        cardActive = {211, 224, 242, 255};
        stroke = {177, 192, 216, 255};
        strokeSoft = {208, 219, 235, 255};
        modalDim = {30, 42, 64, 255};

        text = {20, 28, 43, 255};
        textSoft = {34, 45, 66, 255};
        muted = {61, 75, 99, 255};
        dim = {92, 106, 130, 255};
        onPrimary = {255, 255, 255, 255};
        onAccent = {255, 255, 255, 255};
        accent2 = {13, 122, 102, 255};
        good = {25, 123, 77, 255};
        warn = {150, 95, 16, 255};
        danger = {183, 43, 66, 255};
        shadow = {20, 34, 58, 38};
        judgeLine = {206, 148, 12, 255};
    } else {
        bg = {7, 10, 20, 255};
        bgGlow = {14, 22, 44, 255};
        canvas = {9, 13, 26, 255};
        canvasGlow = {18, 29, 56, 255};
        canvasTop = {11, 17, 32, 255};
        canvasBottom = {8, 12, 23, 255};
        topBarTop = {11, 17, 32, 255};
        topBarBottom = {14, 22, 42, 255};
        panel = {14, 21, 38, 255};
        panelRaised = {19, 28, 49, 255};
        card = {23, 34, 58, 255};
        cardHover = {40, 59, 97, 255};
        cardActive = {52, 76, 120, 255};
        stroke = {49, 68, 106, 255};
        strokeSoft = {31, 44, 71, 255};
        modalDim = {3, 6, 14, 255};

        text = {255, 255, 255, 255};
        textSoft = {248, 251, 255, 255};
        muted = {240, 246, 255, 255};
        dim = {232, 240, 255, 255};
        onPrimary = {255, 255, 255, 255};
        onAccent = {255, 255, 255, 255};
        accent2 = {122, 224, 198, 255};
        good = {134, 216, 176, 255};
        warn = {242, 202, 138, 255};
        danger = {242, 150, 165, 255};
        shadow = {0, 0, 0, 46};
        judgeLine = {255, 238, 150, 255};
    }
    setAccentColor(accentIndex);
}

} // namespace theme
