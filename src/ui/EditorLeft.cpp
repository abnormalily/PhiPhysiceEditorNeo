#include "app/App.h"

#include "core/PathUtil.h"
#include "ui/EditorUIInternal.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ppn::editorui {
namespace {

std::string lowerCopy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

void drawLibraryCard(App& app, const NoteType& type, Rectangle card, bool selected,
                     bool sceneMode) {
    const uint32_t key = ui::id(("lib.card." + type.name).c_str());
    const bool hovered = ui::mouseIn(card) && !ui::mouseConsumed() && !ui::isEditingText();
    const float h = ui::animate(key, hovered);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

    Color bg = selected ? theme::highlight(theme::accent, 26)
                        : theme::mix(theme::card, theme::cardHover, h);
    DrawRectangleRounded(card, 0.10f, 10, bg);
    if (selected)
        DrawRectangleRoundedLines(card, 0.10f, 10, theme::highlight(theme::accent, 170));
    else
        DrawRectangleRoundedLines(card, 0.10f, 10,
                                  theme::mix(theme::strokeSoft, theme::stroke, h));

    const Color color = typeColor(type.color);
    Rectangle preview{card.x + 8, card.y + 8, 54, 54};
    // Preview the type with its default variable values and node bindings.
    const Poly shown = type.renderVertices();
    DrawRectangleRounded(preview, 0.16f, 8, theme::alpha(color, 14));
    drawPolyPreview(shown, preview, color);

    const float textX = card.x + 70.0f;
    const std::string name = ui::ellipsize(type.name, 13.5f, 100.0f);
    ui::textBold(name, textX, card.y + 7, 13.5f, theme::text);
    if (selected)
        DrawCircleV({textX + ui::textWidth(name, 13.5f) + 7, card.y + 17}, 3.0f,
                    theme::highlight(theme::accent));

    ui::text(sceneMode ? type.label() + " · 固定在场景中" : type.label(),
             {textX, card.y + 29}, 9.5f, theme::muted);
    int pieces = 0;
    {
        Poly copy = shown;
        pieces = (int)World::triangulate(copy).size();
    }
    std::string detail = ui::format("%d 顶点 · %d 凸块", (int)shown.size(), pieces);
    if (!type.variables.empty())
        detail += ui::format(" · %d 变量", (int)type.variables.size());
    ui::text(ui::ellipsize(detail, 9.0f, 130.0f), {textX, card.y + 44}, 9.0f, theme::dim);

    const std::string editId = "lib.edit." + type.name;
    if (ui::iconButton(editId.c_str(), {card.x + card.width - 26, card.y + 6, 21, 21},
                       Icon::Edit, "编辑此原件类型")) {
        const NoteType* base = sceneMode ? app.sceneLibrary.types.data()
                                         : app.library.types.data();
        app.openShapeEditor((int)(&type - base), sceneMode);
    }

    if (ui::mouseIn(card) && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        if (selected && app.tool == Tool::Place) {
            app.cancelPlacement();
        } else if (sceneMode) {
            app.selectSceneNote(type.name);
        } else {
            app.setSelectedType(type.name);
        }
    }
}

void drawRivetCard(App& app, Rectangle card, bool scene) {
    const std::string idBase = scene ? "lib.rivet.scene" : "lib.rivet.physics";
    const uint32_t key = ui::id(idBase.c_str());
    const bool hovered = ui::mouseIn(card) && !ui::mouseConsumed() && !ui::isEditingText();
    const float h = ui::animate(key, hovered);
    const bool selected = app.tool == Tool::Place && app.selectedTypeName == kRivetType &&
                          app.placingScene == scene;
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

    DrawRectangleRounded(card, 0.10f, 10,
                         selected ? theme::highlight(theme::accent, 26)
                                  : theme::mix(theme::card, theme::cardHover, h));
    DrawRectangleRoundedLines(card, 0.10f, 10,
                              selected ? theme::highlight(theme::accent, 170)
                                       : theme::mix(theme::strokeSoft, theme::stroke, h));

    const Color gray = typeColor(app.rivetPlacementColor);
    Rectangle preview{card.x + 8, card.y + 8, 54, 54};
    DrawRectangleRounded(preview, 0.16f, 8, theme::alpha(gray, 16));
    const Vec2 center{preview.x + preview.width * 0.5f,
                      preview.y + preview.height * 0.5f};
    const float scale = std::min(1.0f, 16.0f / std::max(2.0f, app.settings.rivetRadius));
    const float radius = std::max(4.0f, app.settings.rivetRadius * scale);
    if (scene)
        DrawRing(center, radius * 0.42f, radius * 0.82f, 0.0f, 360.0f, 32,
                 theme::alpha(Color{255, 255, 255, 255}, 100));
    DrawCircleLines((int)center.x, (int)center.y, radius, theme::alpha(gray, 220));
    DrawCircleLines((int)center.x, (int)center.y, radius - 1.5f, theme::alpha(theme::bg, 60));

    ui::textBold(scene ? "场景铆钉" : "铆钉", card.x + 70.0f, card.y + 7, 13.5f,
                 theme::text);
    if (selected)
        DrawCircleV({card.x + 70.0f +
                         ui::textWidth(scene ? "场景铆钉" : "铆钉", 13.5f) + 7,
                     card.y + 17},
                    3.0f, theme::highlight(theme::accent));
    ui::text(scene ? "不参与碰撞 · 固定在场景中" : "不参与碰撞",
             {card.x + 70.0f, card.y + 29}, 9.5f, theme::muted);
    ui::text("可连接铰链", {card.x + 70.0f, card.y + 44}, 9.0f, theme::dim);

    if (ui::mouseIn(card) && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        if (selected) {
            app.cancelPlacement();
        } else {
            app.selectRivet(scene);
        }
    }
}

void drawSceneCard(App& app, Rectangle card, const JudgeLine& preview) {
    const uint32_t key = ui::id("lib.scene.judge");
    const bool hovered = ui::mouseIn(card) && !ui::mouseConsumed() && !ui::isEditingText();
    const float h = ui::animate(key, hovered);
    const bool selected = app.tool == Tool::JudgeLine;
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

    DrawRectangleRounded(card, 0.10f, 10,
                         selected ? theme::alpha(theme::highlight(theme::accent), 28)
                                  : theme::mix(theme::card, theme::cardHover, h));
    DrawRectangleRoundedLines(
        card, 0.10f, 10,
        selected ? theme::highlight(theme::accent, 175)
                 : theme::mix(theme::strokeSoft, theme::stroke, h));

    Rectangle previewRect{card.x + 8, card.y + 8, 54, 54};
    DrawRectangleRounded(previewRect, 0.16f, 8,
                         theme::alpha(theme::highlight(theme::judgeLine), 20));
    const Vec2 center{previewRect.x + previewRect.width * 0.5f,
                      previewRect.y + previewRect.height * 0.5f};
    DrawLineEx({previewRect.x + 7, center.y}, {previewRect.x + previewRect.width - 7, center.y},
               4.0f, theme::alpha(theme::highlight(theme::judgeLine), 200));
    DrawLineEx({previewRect.x + 7, center.y}, {previewRect.x + previewRect.width - 7, center.y},
               1.0f, theme::alpha(theme::bg, 80));

    ui::textBold("判定线", card.x + 70.0f, card.y + 7, 13.5f, theme::text);
    if (selected)
        DrawCircleV({card.x + 70.0f + ui::textWidth("判定线", 13.5f) + 7, card.y + 17}, 3.0f,
                    theme::highlight(theme::accent));
    ui::text("固定在场景中", {card.x + 70.0f, card.y + 29}, 9.5f, theme::muted);
    ui::text(ui::format("%.0f × %.0f px", preview.width, preview.height),
             {card.x + 70.0f, card.y + 44}, 9.0f, theme::dim);

    if (ui::mouseIn(card) && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        app.librarySection = LibrarySection::Scene;
        app.placingScene = false;
        app.tool = Tool::JudgeLine;
    }
}

void drawFieldCard(App& app, ForceFieldKind kind, ForceFieldShape shape, Rectangle card) {
    const std::string idBase = "lib.field." + std::to_string((int)kind) + "." +
                               std::to_string((int)shape);
    const uint32_t key = ui::id(idBase.c_str());
    const bool hovered = ui::mouseIn(card) && !ui::mouseConsumed() && !ui::isEditingText();
    const float h = ui::animate(key, hovered);
    const bool selected = app.tool == Tool::ForceField && app.placingFieldKind == kind;
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    const Color color = forceFieldColor(kind);

    DrawRectangleRounded(card, 0.10f, 10,
                         selected ? theme::alpha(theme::highlight(color), 26)
                                  : theme::mix(theme::card, theme::cardHover, h));
    DrawRectangleRoundedLines(card, 0.10f, 10,
                              selected ? theme::highlight(color, 185)
                                       : theme::mix(theme::strokeSoft, theme::stroke, h));

    Rectangle previewRect{card.x + 8, card.y + 8, 54, 54};
    DrawRectangleRounded(previewRect, 0.16f, 8, theme::alpha(color, 16));
    drawForceFieldPreview(kind, shape, previewRect, selected);

    const char* effect = kind == ForceFieldKind::Gravity    ? "吸引音符"
                         : kind == ForceFieldKind::Repulsion ? "推开音符"
                         : kind == ForceFieldKind::Buoyancy  ? "产生浮力"
                         : kind == ForceFieldKind::Drag      ? "削减动量"
                         : kind == ForceFieldKind::Vortex    ? "绕中心旋转"
                                                              : "改变重力方向";
    const ForceFieldTemplate* type = app.fieldLibrary.find(kind);
    const std::string title = type && !type->displayName.empty() ? type->displayName
                                                                 : forceFieldKindName(kind);
    ui::textBold(title, card.x + 70.0f, card.y + 7, 13.5f, theme::text);
    if (selected)
        DrawCircleV({card.x + 70.0f + ui::textWidth(title, 13.5f) + 7, card.y + 17}, 3.0f,
                    theme::highlight(theme::accent));
    ui::text(effect, {card.x + 70.0f, card.y + 29}, 9.0f, theme::muted);
    ui::text("固定在世界中", {card.x + 70.0f, card.y + 44}, 8.5f, theme::dim);

    if (hovered && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        app.selectForceFieldPlacement(kind, shape);
    }
}

void drawLibraryTab(App& app) {
    const float x = 12.0f;
    const float w = layout::leftW - 24.0f;
    const float buttonY = layout::H - layout::bottomH - 12.0f;
    const float listBottom = buttonY - 56.0f;
    const std::string needle = lowerCopy(app.search);
    auto matches = [&](const std::string& label) {
        return needle.empty() || lowerCopy(label).find(needle) != std::string::npos;
    };

    int section = (int)app.librarySection;
    if (ui::segmented("library.sections", {x, 118, w, 30}, {"物理块", "场景块", "力场"}, section))
        app.librarySection = (LibrarySection)section;

    // The search box belongs to the whole component library, not only the
    // physics section. Each section filters its own cards with it.
    ui::icon(Icon::Search, {x + 3, 162, 15, 15}, theme::dim);
    ui::textField("library.search", {x + 22, 154, w - 22, 32}, app.search, "搜索原件…",
                  32, 12.0f);

    if (app.librarySection == LibrarySection::Physics) {
        const int errorCount = (int)app.library.errors.size();
        ui::text(ui::format("%d 种物理块", (int)app.library.types.size()), {x, 194}, 9.5f,
                 theme::muted);
        if (errorCount > 0) {
            ui::text(ui::format("%d 个文件错误", errorCount), {x + w - 84, 194}, 9.5f,
                     theme::text);
            if (ui::mouseIn({x + w - 90, 190, 90, 18}) && !app.library.errors.empty())
                ui::tooltip(app.library.errors.front(), ui::mouse());
        }

        const Rectangle list{x, 214, w, listBottom - 214.0f};
        std::vector<const NoteType*> filtered;
        for (const auto& type : app.library.types) {
            if (matches(type.name)) filtered.push_back(&type);
        }
        const bool rivetMatches = matches("铆钉") || matches("rivet");
        const float cardH = 70.0f;
        const float gap = 8.0f;
        const int cardCount = (int)filtered.size() + (rivetMatches ? 1 : 0);
        const float contentH = cardCount > 0 ? cardCount * (cardH + gap) - gap : 0.0f;
        const float offset = ui::scrollOffset("library.scroll", list, contentH);
        ui::scissorPush(list);
        for (size_t i = 0; i < filtered.size(); ++i) {
            const float y = list.y - offset + (float)i * (cardH + gap);
            if (y + cardH < list.y - 4 || y > list.y + list.height) continue;
            const NoteType& type = *filtered[i];
            const bool selected = app.tool == Tool::Place && !app.placingScene &&
                                  type.name == app.selectedTypeName;
            drawLibraryCard(app, type, {list.x, y, w - 8, cardH}, selected, false);
        }
        if (rivetMatches) {
            const float rivetY = list.y - offset + (float)filtered.size() * (cardH + gap);
            if (!(rivetY + cardH < list.y - 4 || rivetY > list.y + list.height))
                drawRivetCard(app, {list.x, rivetY, w - 8, cardH}, false);
        }
        if (cardCount == 0)
            ui::text("没有匹配的原件", {list.x + 4, list.y + 10}, 9.5f, theme::dim);
        ui::scissorPop();
        ui::drawScrollbar(list, contentH, offset);
        float rivetRadius = app.settings.rivetRadius;
        if (ui::numberField("library.rivetRadius", {x, buttonY - 44.0f, w, 28},
                            "铆钉半径", rivetRadius, 2.0f, 500.0f, 1))
            app.setGlobalRivetRadius(rivetRadius);
        return;
    }

    if (app.librarySection == LibrarySection::Scene) {
        const Rectangle list{x, 214, w, listBottom - 214.0f};
        const float cardH = 70.0f;
        const float gap = 8.0f;
        std::vector<const NoteType*> filtered;
        for (const auto& type : app.sceneLibrary.types) {
            if (matches(type.name)) filtered.push_back(&type);
        }
        const bool rivetMatches = matches("铆钉") || matches("rivet");
        const bool judgeMatches = matches("判定线") || matches("judge");
        const int cardCount = (int)filtered.size() + (rivetMatches ? 1 : 0) +
                              (judgeMatches ? 1 : 0);
        const float contentH = cardCount > 0 ? cardCount * (cardH + gap) - gap : 0.0f;
        const float offset = ui::scrollOffset("library.sceneScroll", list, contentH);
        ui::scissorPush(list);
        int index = 0;
        for (const NoteType* type : filtered) {
            const float y = list.y - offset + (float)index * (cardH + gap);
            ++index;
            if (y + cardH < list.y - 4 || y > list.y + list.height) continue;
            const bool selected = app.tool == Tool::Place && app.placingScene &&
                                  type->name == app.selectedTypeName;
            drawLibraryCard(app, *type, {list.x, y, w - 8, cardH}, selected, true);
        }
        if (rivetMatches) {
            const float rivetY = list.y - offset + (float)index * (cardH + gap);
            ++index;
            if (!(rivetY + cardH < list.y - 4 || rivetY > list.y + list.height))
                drawRivetCard(app, {list.x, rivetY, w - 8, cardH}, true);
        }
        if (judgeMatches) {
            const float judgeY = list.y - offset + (float)index * (cardH + gap);
            if (!(judgeY + cardH < list.y - 4 || judgeY > list.y + list.height))
                drawSceneCard(app, {list.x, judgeY, w - 8, cardH},
                              app.judgePlacementTemplate());
        }
        if (cardCount == 0)
            ui::text("没有匹配的原件", {list.x + 4, list.y + 10}, 9.5f, theme::dim);
        ui::scissorPop();
        ui::drawScrollbar(list, contentH, offset);
        float rivetRadius = app.settings.rivetRadius;
        if (ui::numberField("library.rivetRadius.scene", {x, buttonY - 44.0f, w, 28},
                            "铆钉半径", rivetRadius, 2.0f, 500.0f, 1))
            app.setGlobalRivetRadius(rivetRadius);
        return;
    }

    const Rectangle list{x, 214, w, listBottom - 214.0f};
    const float cardH = 66.0f;
    const float gap = 8.0f;
    const ForceFieldKind kinds[6] = {ForceFieldKind::Gravity, ForceFieldKind::Repulsion,
                                     ForceFieldKind::Buoyancy, ForceFieldKind::Drag,
                                     ForceFieldKind::GravityChange, ForceFieldKind::Vortex};
    int cardCount = 0;
    for (ForceFieldKind kind : kinds)
        if (matches(forceFieldKindName(kind)) || matches("力场")) ++cardCount;
    const float contentH = cardCount > 0 ? cardCount * (cardH + gap) - gap : 0.0f;
    const float offset = ui::scrollOffset("library.fieldScroll", list, contentH);
    ui::scissorPush(list);
    int index = 0;
    for (ForceFieldKind kind : kinds) {
        if (!(matches(forceFieldKindName(kind)) || matches("力场"))) continue;
        const float y = list.y - offset + (float)index * (cardH + gap);
        ++index;
        if (y + cardH < list.y - 4 || y > list.y + list.height) continue;
        // The library only offers rectangles; circles are made in the inspector.
        drawFieldCard(app, kind, ForceFieldShape::Rectangle, {list.x, y, w - 8, cardH});
    }
    if (cardCount == 0)
        ui::text("没有匹配的原件", {list.x + 4, list.y + 10}, 9.5f, theme::dim);
    ui::scissorPop();
    ui::drawScrollbar(list, contentH, offset);
    float rivetRadius = app.settings.rivetRadius;
    if (ui::numberField("library.rivetRadius.force", {x, buttonY - 44.0f, w, 28},
                        "铆钉半径", rivetRadius, 2.0f, 500.0f, 1))
        app.setGlobalRivetRadius(rivetRadius);
}

void drawProjectRow(App& app, const ProjectInfo& info, Rectangle row) {
    const bool current = !app.projectPath.empty() && app.projectPath == info.path;
    const uint32_t key = ui::id(("proj.row." + info.name).c_str());
    const bool hovered = ui::mouseIn(row) && !ui::mouseConsumed() && !ui::isEditingText();
    const float h = ui::animate(key, hovered);
    if (hovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);

    DrawRectangleRounded(row, 0.2f, 8,
                         current ? theme::alpha(theme::accent, 22)
                                 : theme::mix(theme::panelRaised, theme::cardHover, h));
    if (current)
        DrawRectangleRoundedLines(row, 0.2f, 8, theme::alpha(theme::accent, 145));
    ui::icon(Icon::Save, {row.x + 8, row.y + 9, 16, 16},
             current ? theme::accentLight : theme::muted);
    ui::textBold(ui::ellipsize(info.name, 11.5f, 140.0f), row.x + 30, row.y + 5, 11.5f,
                 theme::textSoft);
    ui::text(info.modified, {row.x + 30, row.y + 25}, 9.0f, theme::dim);

    const std::string action = "project:" + pathUtf8(info.path);
    const bool armed = app.armedAction == action && GetTime() - app.armedActionTime < 3.0;
    const std::string trashId = "proj.del." + info.name;
    if (ui::iconButton(trashId.c_str(), {row.x + row.width - 32, row.y + 12, 24, 24},
                       Icon::Trash, armed ? "再次点击以删除" : "删除工程")) {
        if (armed) {
            app.armedAction.clear();
            app.deleteProjectFile(info.path);
        } else {
            app.armedAction = action;
            app.armedActionTime = GetTime();
            app.notifyInfo("再次点击垃圾桶以确认删除");
        }
    }

    if (ui::mouseIn(row) && ui::mousePressed(0) && !ui::mouseConsumed()) {
        ui::consumeMouse();
        app.loadProjectFile(info.path);
    }
}

void drawProjectsTab(App& app) {
    const float x = 12.0f;
    const float w = layout::leftW - 24.0f;

    DrawRectangleRounded({x, 126, w, 104}, 0.10f, 10, theme::panelRaised);
    DrawRectangleRoundedLines({x, 126, w, 104}, 0.10f, 10, theme::stroke);
    ui::text("当前工程", {x + 12, 134}, 9.0f, theme::dim, 1.2f);
    ui::text(ui::ellipsize(app.doc.name, 14.0f, 170.0f), {x + 12, 150}, 14.0f, theme::text);
    const bool saved = !app.projectPath.empty() && !app.doc.dirty;
    ui::text(saved ? "已保存" : "未保存", {x + w - 54, 154}, 9.0f, theme::text);
    ui::text(ui::format("%d 音符 · %d 判定线 · %d 力场 · %d 关节",
                        (int)app.doc.notes.size(), (int)app.doc.judges.size(),
                        (int)app.doc.fields.size(),
                        (int)app.doc.revJoints.size() + (int)app.doc.weldJoints.size()),
             {x + 12, 174}, 9.5f, theme::muted);
    const std::string pathText = app.projectPath.empty()
                                     ? "尚未保存到文件"
                                     : "projects/" + app.projectPath.filename().string();
    ui::text(ui::ellipsize(pathText, 9.0f, w - 24), {x + 12, 194}, 9.0f, theme::dim);

    if (ui::button("project.new", {x, 240, 78, 30}, "新建", ButtonStyle::Subtle)) {
        app.openProjectDialog(ProjectDialogMode::New);
    }
    if (ui::button("project.save", {x + 84, 240, 78, 30}, "保存", ButtonStyle::Primary)) {
        app.saveProjectInteractive();
    }
    if (ui::button("project.saveas", {x + 168, 240, 78, 30}, "另存为",
                   ButtonStyle::Subtle)) {
        app.openProjectDialog(ProjectDialogMode::SaveAs);
    }

    ui::text("工程文件", {x, 284}, 10.5f, theme::muted);
    ui::text(ui::format("%d 个", (int)app.projectList.size()), {x + w - 36, 284}, 9.5f,
             theme::dim);

    const float bottomY = layout::H - layout::bottomH - 12.0f;
    const Rectangle list{x, 304, w, bottomY - 44.0f - 304.0f};
    const float rowH = 52.0f;
    const float gap = 8.0f;
    const float contentH = app.projectList.empty()
                               ? 100.0f
                               : app.projectList.size() * (rowH + gap) - gap;
    const float offset = ui::scrollOffset("project.scroll", list, contentH);
    ui::scissorPush(list);
    if (app.projectList.empty()) {
        ui::text("还没有保存的工程", {list.x + 4, list.y + 18}, 10.5f, theme::muted);
        ui::text("点击“保存”或“另存为”创建工程文件。", {list.x + 4, list.y + 38}, 9.0f,
                 theme::dim);
    }
    for (size_t i = 0; i < app.projectList.size(); ++i) {
        const float y = list.y - offset + (float)i * (rowH + gap);
        if (y + rowH < list.y - 4 || y > list.y + list.height) continue;
        drawProjectRow(app, app.projectList[i], {list.x, y, w - 8, rowH});
    }
    ui::scissorPop();
    ui::drawScrollbar(list, contentH, offset);

    if (ui::button("project.openFolderHint", {x, bottomY - 18.0f, w, 18}, "数据目录",
                   ButtonStyle::Ghost, true, Icon::Folder))
        app.notifyInfo("数据目录：" + pathUtf8(app.rootDir));
}

} // namespace

void drawLeftPanel(App& app) {
    const Rectangle panel = layout::left();
    DrawRectangleRec(panel, theme::panel);
    DrawLineEx({layout::leftW - 1, panel.y}, {layout::leftW - 1, panel.y + panel.height}, 1.0f,
               theme::strokeSoft);

    int tab = (int)app.leftTab;
    if (ui::segmented("left.tabs", {12, 74, layout::leftW - 24, 34}, {"原件库", "工程"}, tab))
        app.leftTab = (LeftTab)tab;

    if (app.leftTab == LeftTab::Library)
        drawLibraryTab(app);
    else
        drawProjectsTab(app);
}

} // namespace ppn::editorui