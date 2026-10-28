#pragma once
#include "raylib.h"

namespace layout {

constexpr float W = 1600.0f;
constexpr float H = 900.0f;

// Global UI text scale. The editor's text was previously drawn at 1.2x; 1.8 is
// exactly 1.5x that (and therefore 1.5x the interface the users had before) so
// every label, value and note tag is noticeably larger without changing the
// 1600x900 design canvas.
constexpr float S = 1.8f;

constexpr float topH = 64.0f;
constexpr float leftW = 280.0f;
constexpr float rightW = 312.0f;
constexpr float bottomH = 32.0f;

inline Rectangle top() { return {0, 0, W, topH}; }
inline Rectangle left() { return {0, topH, leftW, H - topH - bottomH}; }
inline Rectangle right() { return {W - rightW, topH, rightW, H - topH - bottomH}; }
inline Rectangle bottom() { return {0, H - bottomH, W, bottomH}; }
inline Rectangle canvas() {
    return {leftW, topH, W - leftW - rightW, H - topH - bottomH};
}

constexpr Rectangle shapeModal = {100, 68, 1400, 824};
inline Rectangle shapeHeader() { return {100, 68, 1400, 74}; }
inline Rectangle shapeCanvas() { return {124, 156, 800, 708}; }
inline Rectangle shapeForm() { return {944, 156, 532, 708}; }
inline Rectangle projectModal = {540, 306, 520, 348};
inline Rectangle settingsModal = {300, 96, 1000, 768};
inline Rectangle eventModal = {70, 40, 1460, 820};
inline Rectangle eventTracks() { return {150, 150, 860, 670}; }
inline Rectangle eventForm() { return {1040, 150, 450, 670}; }

} // namespace layout
