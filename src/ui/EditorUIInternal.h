#pragma once
// Shared drawing helpers for the editor UI implementation files.

#include "app/App.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <string>

namespace ppn::editorui {

using ui::ButtonStyle;
using ui::Icon;

Color typeColor(const Rgb& rgb, unsigned char alpha = 255);
Color groupColor(int group);
Color forceFieldColor(ForceFieldKind kind, unsigned char alpha = 255);
// Compact field badge used by the component library.
void drawForceFieldPreview(ForceFieldKind kind, ForceFieldShape shape, Rectangle area,
                           bool active);
void drawTypePreview(const NoteType& type, Rectangle area, Color color);
// Draws a polygon into a screen rectangle, scaled to fit.
void drawPolyPreview(const Poly& vertices, Rectangle area, Color color);
void drawPill(Rectangle rect, const std::string& text, Color dotColor,
              Color textColor = theme::textSoft);
void drawSectionTitle(const std::string& title, const std::string& tag, float x, float y,
                      float width);
void drawSelectionBrackets(Rectangle screenRect, Color color);
void drawDashedLine(Vec2 a, Vec2 b, float dash, float thickness, Color color);
// Filled polygon in design space, projected through the editor camera.
void drawDesignPolygon(App& app, const Poly& design, Vec2 origin, Color fill, Color outline,
                       float thickness);

void drawCanvas(App& app);
void drawTopBar(App& app);
void drawLeftPanel(App& app);
void drawRightPanel(App& app);
void drawBottomBar(App& app);
void drawShapeEditor(App& app);
void drawEventEditor(App& app);
void drawProjectDialog(App& app);
void drawExportDialog(App& app);
void drawSettings(App& app);
void drawToasts(App& app);

} // namespace ppn::editorui
