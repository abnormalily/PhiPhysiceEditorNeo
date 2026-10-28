#include "app/App.h"

#include "ui/EditorUIInternal.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ppn::editorui {
namespace {

void dimBackground(float alpha = 185.0f) {
    DrawRectangle(0, 0, (int)layout::W, (int)layout::H,
                  theme::alpha(theme::modalDim, (unsigned char)alpha));
}

void drawShapeGrid(App& app) {
    const Rectangle c = layout::shapeCanvas();
    const float step = 50.0f;
    float scale = app.shape.zoom;
    float spacing = step;
    while (spacing * scale < 32.0f) spacing *= 2.0f;

    const Vec2 center{app.shape.center.x, app.shape.center.y};
    const float left = center.x - c.width * 0.5f / scale;
    const float right = center.x + c.width * 0.5f / scale;
    const float minY = center.y - c.height * 0.5f / scale;
    const float maxY = center.y + c.height * 0.5f / scale;

    for (float x = std::floor(left / spacing) * spacing; x <= right; x += spacing) {
        const float sx = app.shapeToScreen({x, 0}).x;
        DrawLineEx({sx, c.y}, {sx, c.y + c.height}, 1.0f,
                   theme::alpha(theme::stroke, std::fabs(x) < 0.1f ? 150 : 60));
    }
    for (float y = std::floor(minY / spacing) * spacing; y <= maxY; y += spacing) {
        const float sy = app.shapeToScreen({0, y}).y;
        DrawLineEx({c.x, sy}, {c.x + c.width, sy}, 1.0f,
                   theme::alpha(theme::stroke, std::fabs(y) < 0.1f ? 150 : 60));
    }
    const Vec2 axis = app.shapeToScreen({0, 0});
    DrawLineEx({axis.x, c.y}, {axis.x, c.y + c.height}, 1.2f, theme::alpha(theme::accent2, 80));
    DrawLineEx({c.x, axis.y}, {c.x + c.width, axis.y}, 1.2f, theme::alpha(theme::accent2, 80));
}

// Shape coordinates are Y-up. The polygon drawn here includes every node
// binding so the canvas shows exactly what will be saved.
Poly shapeDisplayVertices(const ShapeDraft& s) {
    return applyNodeBindings(s.vertices, s.bindings, s.variables);
}

void drawShapeCanvas(App& app) {
    const Rectangle c = layout::shapeCanvas();
    DrawRectangleRounded(c, 0.025f, 10, theme::canvas);
    DrawRectangleRoundedLines(c, 0.025f, 10, theme::strokeSoft);

    ui::scissorPush({c.x + 1, c.y + 1, c.width - 2, c.height - 2});
    drawShapeGrid(app);

    const ShapeDraft& s = app.shape;
    const Poly shown = shapeDisplayVertices(s);
    if (shown.size() >= 3) {
        const std::vector<Poly> pieces = World::triangulate(shown);
        const Color color = typeColor(s.color);
        for (const auto& tri : pieces) {
            const Vec2 a = app.shapeToScreen(tri[0]);
            const Vec2 b = app.shapeToScreen(tri[1]);
            const Vec2 cc = app.shapeToScreen(tri[2]);
            DrawTriangle(a, b, cc, theme::alpha(color, 80));
        }
        for (size_t i = 0; i < shown.size(); ++i) {
            const Vec2 a = app.shapeToScreen(shown[i]);
            const Vec2 b = app.shapeToScreen(shown[(i + 1) % shown.size()]);
            DrawLineEx(a, b, 2.0f, theme::alpha(color, 235));
        }
        const Vec2 centroid = polygonCentroid(shown);
        const Vec2 centerScreen = app.shapeToScreen(centroid);
        DrawCircleV(centerScreen, 3.0f, theme::alpha(color, 200));
        DrawCircleLines((int)centerScreen.x, (int)centerScreen.y, 8.0f,
                        theme::alpha(color, 80));
    }

    // Hold notes: mark the four upper and four lower anchors for reference.
    if (s.holdOriginal) {
        const HoldGeometry geometry = extractHoldGeometry(s.vertices);
        if (geometry.valid) {
            for (int i : geometry.bottomIds) {
                const Vec2 p = app.shapeToScreen(s.vertices[(size_t)i]);
                DrawCircleLines((int)p.x, (int)p.y, 10.0f, theme::accent2);
            }
            for (int i : geometry.topIds) {
                const Vec2 p = app.shapeToScreen(s.vertices[(size_t)i]);
                DrawCircleLines((int)p.x, (int)p.y, 10.0f, theme::warn);
            }
        }
    }

    // Show the coordinate overrides produced by variable bindings.
    for (const auto& binding : s.bindings) {
        if (binding.vertex < 0 || binding.vertex >= (int)s.vertices.size() ||
            binding.vertex >= (int)shown.size())
            continue;
        const Vec2 from = app.shapeToScreen(s.vertices[(size_t)binding.vertex]);
        const Vec2 to = app.shapeToScreen(shown[(size_t)binding.vertex]);
        if (length(to - from) < 0.5f) continue;
        drawDashedLine(from, to, 5.0f, 1.2f, theme::alpha(theme::warn, 190));
        DrawCircleV(to, 2.6f, theme::warn);
    }

    for (size_t i = 0; i < s.vertices.size(); ++i) {
        const Vec2 p = app.shapeToScreen(s.vertices[i]);
        const bool selected = (int)i == s.selectedVertex;
        const float radius = selected ? 7.0f : 5.0f;
        DrawCircleV(p, radius + 3.0f, theme::alpha(theme::panel, 200));
        DrawCircleV(p, radius, selected ? theme::accent : theme::card);
        DrawCircleLines((int)p.x, (int)p.y, radius,
                        selected ? theme::accentLight : theme::alpha(theme::textSoft, 200));
        ui::text(std::to_string(i), {p.x + 9, p.y - 12}, 8.5f, theme::text);
    }
    ui::scissorPop();

    const int pieces = shown.size() >= 3 ? (int)World::triangulate(shown).size() : 0;
    const float area = std::fabs(polygonSignedArea(shown));
    const std::string info = ui::format("%d 顶点 · %d 凸块 · 面积 %.0f px²",
                                        (int)s.vertices.size(), pieces, area);
    Rectangle infoRect{c.x + 12, c.y + 12, ui::textWidth(info, 9.5f) + 22, 24};
    DrawRectangleRounded(infoRect, 0.5f, 8, theme::alpha(theme::panel, 230));
    ui::text(info, {infoRect.x + 11, infoRect.y + 5}, 9.5f, theme::textSoft);

}

void drawVertexList(App& app, float x, float y, float w, float h) {
    DrawRectangleRounded({x, y, w, h}, 0.06f, 8, theme::panelRaised);
    DrawRectangleRoundedLines({x, y, w, h}, 0.06f, 8, theme::strokeSoft);

    const float rowH = 24.0f;
    const float contentH = std::max(h, app.shape.vertices.size() * rowH);
    const float offset = ui::scrollOffset("shape.vertexScroll", {x, y, w, h}, contentH);
    ui::scissorPush({x + 2, y + 2, w - 4, h - 4});
    for (size_t i = 0; i < app.shape.vertices.size(); ++i) {
        Rectangle row{x + 4, y + 4 - offset + i * rowH, w - 8, rowH - 2};
        if (row.y + row.height < y || row.y > y + h) continue;
        const bool selected = (int)i == app.shape.selectedVertex;
        const bool hovered = ui::mouseIn(row) && !ui::mouseConsumed();
        if (selected)
            DrawRectangleRounded(row, 0.25f, 6, theme::alpha(theme::accent, 35));
        else if (hovered)
            DrawRectangleRounded(row, 0.25f, 6, theme::alpha(theme::cardHover, 120));
        ui::text(ui::format("#%d", (int)i), {row.x + 8, row.y + 4}, 9.5f, theme::text);
        const Vec2 v = app.shape.vertices[i];
        ui::text(ui::format("%.1f, %.1f", v.x, v.y), {row.x + 44, row.y + 4}, 9.5f,
                 theme::text);
        if (hovered && ui::mousePressed(0)) {
            app.shape.selectedVertex = (int)i;
            ui::consumeMouse();
        }
    }
    ui::scissorPop();
    ui::drawScrollbar({x, y, w, h}, contentH, offset);
}

void drawPalette(App& app, float x, float y) {
    static const Rgb palette[] = {{125, 179, 247}, {253, 214, 122}, {248, 160, 204},
                                  {140, 232, 194}, {186, 168, 246}, {242, 226, 138},
                                  {244, 166, 178}, {178, 192, 214}};
    for (int i = 0; i < 8; ++i) {
        const float cx = x + i * 38.0f;
        const bool selected = app.shape.color.r == palette[i].r &&
                              app.shape.color.g == palette[i].g &&
                              app.shape.color.b == palette[i].b;
        if (ui::colorSwatch(ui::format("shape.color.%d", i).c_str(), {cx, y, 28, 28},
                            typeColor(palette[i]), selected))
            app.shape.color = palette[i];
    }
}

void drawShapeProperties(App& app, float x, float y, float w) {
    drawSectionTitle("标识", "IDENTITY", x, y, w);
    y += 20.0f;
    const bool locked = app.shape.editingExisting;
    if (locked) {
        ui::text(ui::format("%s.note", app.shape.name.c_str()), {x, y + 6}, 13.0f,
                 theme::textSoft);
    } else {
        ui::textField("shape.nameField", {x, y, w, 30}, app.shape.name, "音符名称", 32, 12.0f);
    }
    y += 36.0f;

    drawSectionTitle("物理材质", "MATERIAL", x, y, w);
    y += 20.0f;
    ui::numberField("shape.density", {x, y, w, 28}, "密度", app.shape.density, 0.1f, 6.0f, 2);
    y += 32.0f;
    ui::numberField("shape.friction", {x, y, w, 28}, "摩擦", app.shape.friction, 0.0f, 1.2f, 2);
    y += 32.0f;
    ui::numberField("shape.restitution", {x, y, w, 28}, "弹性", app.shape.restitution, 0.0f,
                    0.95f, 2);
    y += 34.0f;

    drawSectionTitle("颜色", "COLOR", x, y, w);
    y += 20.0f;
    drawPalette(app, x, y);
    y += 34.0f;

    drawSectionTitle("顶点", "VERTICES", x, y, w);
    y += 20.0f;
    const int sel = app.shape.selectedVertex;
    if (sel >= 0 && sel < (int)app.shape.vertices.size()) {
        ui::text(ui::format("选中顶点 #%d", sel), {x, y + 2}, 9.5f, theme::textSoft);
        float vx = app.shape.vertices[sel].x;
        float vy = app.shape.vertices[sel].y;
        const float fw = (w - 8.0f) * 0.5f;
        const bool cx = ui::numberField("shape.vx", {x, y + 16, fw, 26}, "X", vx, -4000.0f,
                                        4000.0f, 1);
        const bool cy = ui::numberField("shape.vy", {x + fw + 8, y + 16, fw, 26}, "Y", vy,
                                        -4000.0f, 4000.0f, 1);
        if (cx) app.shape.vertices[sel].x = vx;
        if (cy) app.shape.vertices[sel].y = vy;
    } else {
        ui::text("未选择顶点", {x, y + 2}, 9.0f, theme::dim);
    }
    y += 48.0f;
    if (ui::button("shape.addVertex", {x, y, 130, 28}, "添加顶点", ButtonStyle::Subtle,
                   app.shape.vertices.size() < 64, Icon::Plus))
        app.addShapeVertex();
    if (ui::button("shape.delVertex", {x + 138, y, 130, 28}, "删除顶点",
                   ButtonStyle::Subtle, app.shape.vertices.size() > 3, Icon::Trash))
        app.deleteSelectedShapeVertex();
    y += 34.0f;
    drawVertexList(app, x, y, w, 82.0f);
    y += 88.0f;
}

void drawShapeVariables(App& app, float x, float y, float w) {
    ShapeDraft& s = app.shape;

    drawSectionTitle("变量", "VARIABLES", x, y, w);
    y += 20.0f;

    const Rectangle varList{x, y, w, 140};
    DrawRectangleRounded(varList, 0.05f, 8, theme::panelRaised);
    DrawRectangleRoundedLines(varList, 0.05f, 8, theme::strokeSoft);
    const float rowH = 34.0f;
    const float varContent =
        std::max(varList.height, std::max<size_t>(1, s.variables.size()) * rowH + 8.0f);
    const float varOffset = ui::scrollOffset("shape.variables", varList, varContent);
    ui::scissorPush({varList.x + 2, varList.y + 2, varList.width - 4, varList.height - 4});
    if (s.variables.empty()) {
        ui::text("还没有变量。在下方添加一个，例如 height / 高度。", {varList.x + 10,
                                                                  varList.y + 10},
                 9.0f, theme::muted);
    }
    for (size_t i = 0; i < s.variables.size(); ++i) {
        Rectangle row{varList.x + 4, varList.y + 4 - varOffset + i * rowH,
                      varList.width - 8, rowH - 2};
        if (row.y + row.height < varList.y || row.y > varList.y + varList.height) continue;
        DrawRectangleRounded(row, 0.2f, 6, theme::alpha(theme::card, 150));
        ui::textBold(s.variables[i].name, row.x + 8, row.y + 3, 9.5f, theme::text);
        ui::text(ui::ellipsize(s.variables[i].displayName, 8.5f, 84.0f), {row.x + 8,
                                                                         row.y + 17},
                 8.5f, theme::dim);
        float value = s.variables[i].value;
        if (ui::numberField(ui::format("shape.varvalue.%d", (int)i).c_str(),
                            {row.x + 108, row.y + 3, 92, 25}, "值", value, -100000.0f,
                            100000.0f, 2))
            s.variables[i].value = value;
        if (ui::iconButton(ui::format("shape.vardel.%d", (int)i).c_str(),
                           {row.x + row.width - 28, row.y + 5, 22, 22}, Icon::Trash,
                           "删除变量"))
            app.deleteShapeVariable((int)i);
    }
    ui::scissorPop();
    ui::drawScrollbar(varList, varContent, varOffset);
    y = varList.y + varList.height + 8.0f;

    const float fw = (w - 16.0f) / 3.0f;
    ui::textField("shape.newVarName", {x, y, fw, 28}, s.newVariableName, "变量名", 24, 10.5f);
    ui::textField("shape.newVarDisplay", {x + fw + 8, y, fw, 28}, s.newVariableDisplay,
                  "显示名", 24, 10.5f);
    ui::numberField("shape.newVarValue", {x + (fw + 8) * 2, y, fw, 28}, "默认",
                    s.newVariableValue, -100000.0f, 100000.0f, 2);
    y += 32.0f;
    if (ui::button("shape.addVar", {x, y, 148, 28}, "添加变量", ButtonStyle::Subtle, true,
                   Icon::Plus))
        app.addShapeVariable();
    y += 38.0f;

    drawSectionTitle("节点绑定", "NODES", x, y, w);
    y += 20.0f;

    const Rectangle nodeList{x, y, w, 160};
    DrawRectangleRounded(nodeList, 0.05f, 8, theme::panelRaised);
    DrawRectangleRoundedLines(nodeList, 0.05f, 8, theme::strokeSoft);
    const float nodeContent =
        std::max(nodeList.height, std::max<size_t>(1, s.bindings.size()) * rowH + 8.0f);
    const float nodeOffset = ui::scrollOffset("shape.bindings", nodeList, nodeContent);
    ui::scissorPush({nodeList.x + 2, nodeList.y + 2, nodeList.width - 4, nodeList.height - 4});
    if (s.bindings.empty()) {
        ui::text("还没有节点绑定。例如 $6 this this+height 让节点 6 的 Y 坐标随 height 偏移。",
                 {nodeList.x + 10, nodeList.y + 10}, 9.0f, theme::muted);
    }
    for (size_t i = 0; i < s.bindings.size(); ++i) {
        Rectangle row{nodeList.x + 4, nodeList.y + 4 - nodeOffset + i * rowH,
                      nodeList.width - 8, rowH - 2};
        if (row.y + row.height < nodeList.y || row.y > nodeList.y + nodeList.height) continue;
        DrawRectangleRounded(row, 0.2f, 6, theme::alpha(theme::card, 150));
        ui::text(ui::format("#%d", s.bindings[i].vertex), {row.x + 8, row.y + 10}, 9.5f,
                 theme::text);
        ui::textField(ui::format("shape.bindx.%d", (int)i).c_str(),
                      {row.x + 42, row.y + 4, 140, 25}, s.bindings[i].x, "X 表达式", 32,
                      9.5f);
        ui::textField(ui::format("shape.bindy.%d", (int)i).c_str(),
                      {row.x + 188, row.y + 4, 140, 25}, s.bindings[i].y, "Y 表达式", 32,
                      9.5f);
        if (ui::iconButton(ui::format("shape.binddel.%d", (int)i).c_str(),
                           {row.x + row.width - 28, row.y + 5, 22, 22}, Icon::Trash,
                           "删除节点绑定"))
            app.deleteShapeBinding((int)i);
    }
    ui::scissorPop();
    ui::drawScrollbar(nodeList, nodeContent, nodeOffset);
    y = nodeList.y + nodeList.height + 8.0f;

    float bindVertex = (float)s.newBindingVertex;
    if (ui::numberField("shape.newBindVertex", {x, y, fw, 28}, "节点", bindVertex, 0.0f,
                        63.0f, 0))
        s.newBindingVertex = (int)std::lround(bindVertex);
    ui::textField("shape.newBindX", {x + fw + 8, y, fw, 28}, s.newBindingX, "X 表达式", 32,
                  10.0f);
    ui::textField("shape.newBindY", {x + (fw + 8) * 2, y, fw, 28}, s.newBindingY,
                  "Y 表达式", 32, 10.0f);
    y += 32.0f;
    if (ui::button("shape.addBind", {x, y, 160, 28}, "添加节点绑定", ButtonStyle::Subtle,
                   true, Icon::Plus))
        app.addShapeBinding();
}

void drawShapeFooter(App& app, float x, float y, float w) {
    if (ui::button("shape.save", {x, y, 190, 36}, "保存到文件夹", ButtonStyle::Primary, true,
                   Icon::Save))
        app.saveShapeDraft();
    if (ui::button("shape.cancel", {x + 202, y, 84, 36}, "取消", ButtonStyle::Subtle)) {
        app.shape.open = false;
        app.shape.error.clear();
    }
    const bool canDelete = app.shape.editingExisting;
    if (ui::button("shape.deleteFile", {x + 298, y, w - 298, 36},
                   app.shape.deleteArmed ? "再次点击确认删除" : "删除文件", ButtonStyle::Danger,
                   canDelete, Icon::Trash))
        app.deleteShapeType();
}

} // namespace

void drawShapeEditor(App& app) {
    dimBackground(190.0f);
    const Rectangle modal = layout::shapeModal;
    DrawRectangleRounded({modal.x + 6, modal.y + 10, modal.width, modal.height}, 0.03f, 12,
                         Color{0, 0, 0, 120});
    DrawRectangleRounded(modal, 0.03f, 12, theme::panel);
    DrawRectangleRoundedLines(modal, 0.03f, 12, theme::stroke);

    // Header.
    DrawRectangleRounded({modal.x, modal.y, modal.width, 74}, 0.03f, 12, theme::panelRaised);
    DrawRectangle((int)modal.x, (int)(modal.y + 60), (int)modal.width, 1, theme::strokeSoft);
    DrawPoly({modal.x + 34, modal.y + 37}, 6, 13.0f, 0.0f, theme::accent);
    ui::text("音符形状编辑器", {modal.x + 58, modal.y + 12}, 16.0f, theme::text);
    const std::string subtitle =
        (app.shape.scene ? "SCENE  " : "PHYSICS  ") +
        (app.shape.editingExisting ? app.shape.originalName + ".note" : "NEW .note");
    ui::text(subtitle, {modal.x + 58, modal.y + 40}, 9.0f, theme::dim);
    if (ui::iconButton("shape.fit", {modal.x + modal.width - 92, modal.y + 20, 30, 30},
                       Icon::Fit, "适应视图"))
        app.fitShapeView();
    if (ui::iconButton("shape.close", {modal.x + modal.width - 50, modal.y + 20, 30, 30},
                       Icon::Close, "关闭 (Esc)"))
        app.shape.open = false;

    drawShapeCanvas(app);

    const float x = layout::shapeForm().x + 22;
    const float w = layout::shapeForm().width - 44;
    float y = 164.0f;

    ShapeDraft& s = app.shape;
    int tab = s.formTab;
    if (ui::segmented("shape.formTab", {x, y, w, 30}, {"形状 / 材质", "变量 / 节点"}, tab))
        s.formTab = tab;
    y += 40.0f;

    if (s.formTab == 0) {
        drawShapeProperties(app, x, y, w);
    } else {
        drawShapeVariables(app, x, y, w);
    }

    if (!s.error.empty()) {
        Rectangle err{x, 768.0f, w, 28};
        DrawRectangleRounded(err, 0.3f, 6, theme::alpha(theme::danger, 28));
        ui::text(ui::ellipsize(s.error, 9.5f, w - 20), {x + 10, 775.0f}, 9.5f,
                 theme::text);
    }

    drawShapeFooter(app, x, 812.0f, w);
}

void drawProjectDialog(App& app) {
    dimBackground(170.0f);
    const Rectangle m = layout::projectModal;
    DrawRectangleRounded({m.x + 5, m.y + 8, m.width, m.height}, 0.04f, 12,
                         Color{0, 0, 0, 120});
    DrawRectangleRounded(m, 0.04f, 12, theme::panel);
    DrawRectangleRoundedLines(m, 0.04f, 12, theme::stroke);

    const bool isNew = app.projectDialog == ProjectDialogMode::New;
    ui::icon(isNew ? Icon::Plus : Icon::Save, {m.x + 22, m.y + 22, 20, 20}, theme::accent);
    ui::text(isNew ? "新建工程" : "保存工程", {m.x + 52, m.y + 15}, 15.0f, theme::text);
    ui::text(isNew ? "NEW PROJECT" : "SAVE AS", {m.x + 52, m.y + 38}, 8.0f, theme::dim, 1.4f);
    if (ui::iconButton("project.close", {m.x + m.width - 40, m.y + 18, 26, 26}, Icon::Close,
                       "关闭"))
        app.setProjectDialogResult(false);

    ui::text("工程名称", {m.x + 22, m.y + 72}, 9.5f, theme::muted);
    ui::textField("project.nameField", {m.x + 22, m.y + 90, m.width - 44, 34},
                  app.projectNameDraft, "ProjectName", 48, 12.0f);
    ui::text("保存位置  projects/" + app.projectNameDraft + ".pnp", {m.x + 22, m.y + 132},
             8.5f, theme::dim);

    if (isNew && app.doc.dirty) {
        ui::text("当前工程有未保存更改", {m.x + 22, m.y + 152}, 9.0f, theme::text);
    }
    if (!app.projectDialogError.empty()) {
        DrawRectangleRounded({m.x + 22, m.y + 188, m.width - 44, 28}, 0.3f, 6,
                             theme::alpha(theme::danger, 25));
        ui::text(ui::ellipsize(app.projectDialogError, 9.5f, m.width - 64),
                 {m.x + 32, m.y + 196}, 9.5f, theme::text);
    }

    const float by = m.y + m.height - 56.0f;
    if (ui::button("project.cancel", {m.x + m.width - 222, by, 96, 36}, "取消",
                   ButtonStyle::Subtle))
        app.setProjectDialogResult(false);
    if (ui::button("project.confirm", {m.x + m.width - 116, by, 94, 36},
                   isNew ? "创建" : "保存", ButtonStyle::Primary))
        app.setProjectDialogResult(true);
}

void drawExportDialog(App& app) {
    dimBackground(175.0f);
    const Rectangle m{480, 300, 640, 340};
    DrawRectangleRounded({m.x + 5, m.y + 8, m.width, m.height}, 0.04f, 12,
                         Color{0, 0, 0, 120});
    DrawRectangleRounded(m, 0.04f, 12, theme::panel);
    DrawRectangleRoundedLines(m, 0.04f, 12, theme::stroke);

    ui::icon(Icon::Note, {m.x + 22, m.y + 18, 22, 22}, theme::accent);
    ui::text("导出预制事件", {m.x + 54, m.y + 14}, 16.0f, theme::text);
    ui::text("EXPORT PREFAB EVENTS", {m.x + 54, m.y + 38}, 8.0f, theme::dim, 1.3f);
    if (ui::iconButton("export.close", {m.x + m.width - 40, m.y + 16, 26, 26},
                       Icon::Close, "关闭"))
        app.closeExportDialog();

    float y = m.y + 76.0f;
    ui::textField("export.split", {m.x + 22, y, m.width - 44, 30}, app.exportSplitInput,
                  "", 12, 10.0f, "切分");
    y += 42.0f;
    ui::textField("export.end", {m.x + 22, y, m.width - 44, 30}, app.exportEndInput,
                  "", 24, 10.0f, "结束时间(beats)");
    y += 46.0f;

    const std::string dir = app.settings.exportDirectory.empty()
                                ? (app.rootDir / "exports").string()
                                : app.settings.exportDirectory;
    ui::text(ui::ellipsize("输出目录：" + dir, 8.5f, m.width - 44),
             {m.x + 24, y}, 8.5f, theme::dim);
    y += 20.0f;
    ui::text("文件名：NotePhysics.json · UTF-8 · 每次覆盖旧文件", {m.x + 24, y}, 8.5f,
             theme::dim);

    if (!app.exportDialogError.empty()) {
        DrawRectangleRounded({m.x + 22, m.y + m.height - 96.0f, m.width - 44, 26}, 0.3f,
                             6, theme::alpha(theme::danger, 28));
        ui::text(ui::ellipsize(app.exportDialogError, 9.0f, m.width - 64),
                 {m.x + 32, m.y + m.height - 89.0f}, 9.0f, theme::text);
    }

    const float by = m.y + m.height - 54.0f;
    if (ui::button("export.cancel", {m.x + m.width - 222, by, 96, 34}, "取消",
                   ButtonStyle::Subtle))
        app.closeExportDialog();
    if (ui::button("export.run", {m.x + m.width - 116, by, 94, 34}, "导出",
                   ButtonStyle::AccentLight, true, Icon::Save))
        app.confirmExportDialog();
}

bool slowGearSlider(const char* idStr, Rectangle r, int& value) {
    const uint32_t key = ui::id(idStr);
    static uint32_t activeSlider = 0;
    const bool hovered = ui::mouseIn(r);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (hovered && ui::mousePressed(0) && !ui::mouseConsumed()) {
        activeSlider = key;
        ui::consumeMouse();
    }
    bool changed = false;
    if (activeSlider == key) {
        if (ui::mouseDown(0)) {
            const float t = clampf((ui::mouse().x - r.x) / std::max(1.0f, r.width), 0.0f,
                                   1.0f);
            const int next = (int)std::lround(t * 50.0f / 5.0f) * 5;
            if (next != value) {
                value = next;
                changed = true;
            }
            ui::consumeMouse();
        } else {
            activeSlider = 0;
        }
    }

    const float cy = r.y + r.height * 0.5f;
    DrawLineEx({r.x, cy}, {r.x + r.width, cy}, 4.0f, theme::alpha(theme::stroke, 190));
    const float t = clampf((float)value / 50.0f, 0.0f, 1.0f);
    DrawLineEx({r.x, cy}, {r.x + r.width * t, cy}, 4.0f, theme::accent);
    for (int i = 0; i <= 10; ++i) {
        const float tx = r.x + r.width * ((float)i / 10.0f);
        DrawLineEx({tx, cy - 5}, {tx, cy + 5}, 1.0f, theme::alpha(theme::stroke, 200));
    }
    const Vec2 knob{r.x + r.width * t, cy};
    DrawCircleV(knob, 8.0f, theme::accentLight);
    DrawCircleLines((int)knob.x, (int)knob.y, 8.0f, theme::accentDark);
    return changed;
}

void drawSettingsBasic(App& app, float x, float y, float w) {
    drawSectionTitle("显示", "VIEW", x, y, w);
    y += 24.0f;
    struct DisplayRow {
        const char* id;
        const char* label;
        const char* description;
        bool* value;
    };
    DisplayRow rows[] = {
        {"settings.view.grid", "网格", "显示画布网格", &app.showGrid},
        {"settings.view.joints", "关节", "显示铰链与焊接连接", &app.showJoints},
        {"settings.view.labels", "标签", "显示物件编号与类型", &app.showLabels},
        {"settings.view.wire", "线框", "显示物理凸块", &app.showWireframe},
        {"settings.view.contacts", "接触点", "显示碰撞接触点", &app.showContactPoints},
        {"settings.view.gridLabels", "坐标", "显示网格坐标", &app.showGridLabels},
    };
    bool displayChanged = false;
    for (const DisplayRow& row : rows) {
        displayChanged |= ui::toggle(row.id, {x, y, w, 26}, row.label, *row.value);
        ui::text(row.description, {x + 2, y + 27}, 8.5f, theme::dim);
        y += 45.0f;
    }
    if (displayChanged) app.persistDisplaySettings();
    y += 4.0f;

    drawSectionTitle("场景", "SCENE", x, y, w);
    y += 24.0f;
    float gravity = app.settings.gravity;
    if (ui::numberField("settings.scene.gravity", {x, y, w, 28}, "重力", gravity, 0.0f,
                        60.0f, 2))
        app.setSceneGravity(gravity);
    y += 34.0f;
    float velocity = (float)app.settings.velocityIterations;
    if (ui::numberField("settings.scene.velocity", {x, y, w, 28}, "速度迭代", velocity,
                        4.0f, 20.0f, 0)) {
        app.setSolverIterations((int)std::lround(velocity),
                                app.settings.positionIterations);
    }
    y += 34.0f;
    float position = (float)app.settings.positionIterations;
    if (ui::numberField("settings.scene.position", {x, y, w, 28}, "位置迭代", position,
                        2.0f, 10.0f, 0)) {
        app.setSolverIterations(app.settings.velocityIterations,
                                (int)std::lround(position));
    }
    y += 38.0f;
    ui::text("缓步模拟", {x, y}, 9.5f, theme::textSoft);
    const std::string gearText = std::to_string(app.settings.slowStepGear);
    ui::text(gearText, {x + w - ui::textWidth(gearText, 9.5f), y}, 9.5f, theme::text);
    y += 22.0f;
    int gear = app.settings.slowStepGear;
    if (slowGearSlider("settings.scene.slowgear", {x, y, w, 26}, gear))
        app.setSlowStepGear(gear);
    y += 42.0f;

    drawSectionTitle("播放", "PLAYBACK", x, y, w);
    y += 24.0f;
    float bpm = app.settings.bpm;
    if (ui::numberField("settings.playback.bpm", {x, y, w, 28}, "BPM", bpm, 1.0f,
                        1000.0f, 2))
        app.setBpm(bpm);
}

void drawAccentCircle(App& app, int index, float centerX, float centerY) {
    const Color base = theme::accentPreview(index);
    const Color dark = theme::mix(base, Color{0, 0, 0, 255}, 0.22f);
    const Color light = theme::mix(base, Color{255, 255, 255, 255}, 0.62f);
    const float r = 26.0f;
    DrawCircleV({centerX, centerY}, r, light);
    DrawCircleSector({centerX, centerY}, r, 45.0f, 225.0f, 48, dark);
    if (app.themeColor == index) {
        DrawCircleLines((int)centerX, (int)centerY, r + 4.0f, theme::text);
        DrawCircleLines((int)centerX, (int)centerY, r + 5.0f, theme::text);
    } else {
        DrawCircleLines((int)centerX, (int)centerY, r, theme::stroke);
    }
    const Rectangle hit{centerX - r, centerY - r, r * 2.0f, r * 2.0f};
    const bool hovered = ui::mouseIn(hit);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    if (hovered && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        app.setThemeColor(index);
    }
}

void drawSettingsAppearance(App& app, float x, float y, float w) {
    const int count = theme::kAccentCount;
    const float step = 108.0f;
    const float firstCenter = x + (w - step * (count - 1)) * 0.5f;
    const float centerY = y + 54.0f;
    for (int i = 0; i < count; ++i) {
        const float centerX = firstCenter + step * i;
        drawAccentCircle(app, i, centerX, centerY);
        ui::text(theme::accentName(i),
                 {centerX - ui::textWidth(theme::accentName(i), 9.0f) * 0.5f,
                  centerY + 36.0f},
                 9.0f, app.themeColor == i ? theme::text : theme::dim);
    }
}

void drawSettingsShortcuts(App& app, float x, float y, float w) {
    drawSectionTitle("自定义快捷键", "SHORTCUTS", x, y, w);
    y += 24.0f;

    const float gap = 24.0f;
    const float colW = (w - gap) * 0.5f;
    const float rowH = 38.0f;
    const int perColumn = (kShortcutCount + 1) / 2;
    for (int i = 0; i < kShortcutCount; ++i) {
        const int col = i / perColumn;
        const int row = i % perColumn;
        const float rowX = x + col * (colW + gap);
        const float rowY = y + row * rowH;
        const ShortcutAction action = (ShortcutAction)i;
        const bool capturing = app.shortcutCapture == i;
        Rectangle rowRect{rowX, rowY, colW, rowH - 4.0f};
        DrawRectangleRounded(rowRect, 0.25f, 6,
                             capturing ? theme::alpha(theme::accent, 30)
                                       : theme::alpha(theme::panelRaised, 150));
        if (capturing)
            DrawRectangleRoundedLines(rowRect, 0.25f, 6, theme::alpha(theme::accent, 150));
        ui::text(shortcutLabel(action), {rowX + 10, rowY + 8}, 9.5f, theme::textSoft);

        const std::string binding = capturing
                                        ? "按下按键…"
                                        : bindingLabel(app.settings.shortcuts[i]);
        const std::string buttonId = "settings.bind." + std::to_string(i);
        if (ui::button(buttonId.c_str(), {rowX + colW - 150, rowY + 5, 140, 24}, binding,
                       capturing ? ButtonStyle::Primary : ButtonStyle::Secondary)) {
            app.shortcutCapture = capturing ? -1 : i;
        }
    }

    const float footerY = y + perColumn * rowH + 8.0f;
    if (ui::button("settings.resetShortcuts", {x, footerY, 180, 30}, "恢复默认快捷键",
                   ButtonStyle::Subtle, true, Icon::Refresh))
        app.resetShortcuts();
    if (app.shortcutCapture >= 0)
        ui::text("等待按键… 按 Esc 取消。", {x + 196, footerY + 8}, 9.0f, theme::text);
}

void drawSettingsExport(App& app, float x, float y, float w) {
    drawSectionTitle("预制事件", "PREFAB EVENTS", x, y, w);
    y += 24.0f;
    const float browseW = 142.0f;
    const bool changed = ui::textField(
        "settings.export.dir", {x, y, w - browseW - 8.0f, 28.0f},
        app.settings.exportDirectory, "exports", 512, 9.5f, "目录");
    if (changed) app.saveSettings();
    if (ui::button("settings.export.browse", {x + w - browseW, y, browseW, 28.0f},
                   "选择文件夹", ButtonStyle::Secondary))
        app.browseExportDirectory();
    y += 38.0f;
    ui::text("留空时使用程序目录下的 exports。", {x + 2, y}, 8.5f, theme::dim);
    y += 28.0f;

    drawSectionTitle("导出", "EXPORT", x, y, w);
    y += 26.0f;
    ui::text("导出按钮位于顶部栏的保存按钮旁；点击后填写切分和结束时间。",
             {x + 2, y}, 9.0f, theme::textSoft);
    y += 22.0f;
    ui::text("输出文件：NotePhysics.json，UTF-8，每次覆盖旧文件。", {x + 2, y}, 9.0f,
             theme::textSoft);
    y += 22.0f;
    ui::text("规则文件：" + (app.rootDir / "export_rules.txt").string(), {x + 2, y}, 9.0f,
             theme::textSoft);
}

void drawSettings(App& app) {
    dimBackground(175.0f);
    const Rectangle m = layout::settingsModal;
    DrawRectangleRounded({m.x + 5, m.y + 8, m.width, m.height}, 0.035f, 12,
                         Color{0, 0, 0, 120});
    DrawRectangleRounded(m, 0.035f, 12, theme::panel);
    DrawRectangleRoundedLines(m, 0.035f, 12, theme::stroke);

    ui::icon(Icon::Settings, {m.x + 28, m.y + 16, 22, 22}, theme::accent);
    ui::text("设置", {m.x + 60, m.y + 12}, 17.0f, theme::text);
    ui::text("PHIPHYSICE EDITOR NEO · SETTINGS", {m.x + 60, m.y + 42}, 8.0f, theme::dim, 1.2f);
    if (ui::iconButton("settings.close", {m.x + m.width - 46, m.y + 20, 28, 28},
                       Icon::Close, "关闭 (Esc)"))
        app.showSettings = false;
    DrawLineEx({m.x + 28, m.y + 68}, {m.x + m.width - 28, m.y + 68}, 1.0f, theme::strokeSoft);

    const float x = m.x + 28.0f;
    const float w = m.width - 56.0f;
    int tab = app.settingsTab;
    if (ui::segmented("settings.tabs", {x, m.y + 80, 480, 32},
                      {"基础", "外观", "快捷键", "预制事件"}, tab))
        app.settingsTab = tab;

    if (app.settingsTab == 0)
        drawSettingsBasic(app, x, m.y + 128, w);
    else if (app.settingsTab == 1)
        drawSettingsAppearance(app, x, m.y + 128, w);
    else if (app.settingsTab == 2)
        drawSettingsShortcuts(app, x, m.y + 128, w);
    else
        drawSettingsExport(app, x, m.y + 128, w);

}

void drawToasts(App& app) {
    const float width = 340.0f;
    const float height = 40.0f;
    // Notifications live above the bottom-left corner, clear of the inspector
    // and of the library's bottom action button.
    const float left = 16.0f;
    float y = layout::H - layout::bottomH - 60.0f;
    for (int i = (int)app.toasts.size() - 1; i >= 0; --i) {
        const Toast& t = app.toasts[i];
        float alpha = 1.0f;
        if (t.age < 0.18f) alpha = t.age / 0.18f;
        if (t.age > t.duration - 0.45f)
            alpha = std::max(0.0f, (t.duration - t.age) / 0.45f);
        const unsigned char a = (unsigned char)(alpha * 255.0f);
        const float slide = (1.0f - alpha) * 18.0f;
        Rectangle r{left + slide, y - height, width, height};
        DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.3f, 8,
                             theme::alpha(Color{0, 0, 0, 130}, a));
        DrawRectangleRounded(r, 0.3f, 8, theme::alpha(theme::panelRaised, a));
        DrawRectangleRoundedLines(r, 0.3f, 8, theme::alpha(theme::stroke, a));
        DrawRectangleRounded({r.x, r.y + 8, 3, r.height - 16}, 0.5f, 4,
                             theme::alpha(t.color, a));
        ui::text(ui::ellipsize(t.text, 10.0f, width - 32), {r.x + 15, r.y + 12}, 10.0f,
                 theme::textSoft);
        y -= height + 7.0f;
    }
}

} // namespace ppn::editorui