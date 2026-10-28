#include "app/App.h"

#include "ui/EditorUIInternal.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace ppn::editorui {
namespace {

constexpr float kPad = 12.0f;

int convexPieceCount(const NoteType& type) {
    Poly copy = type.vertices;
    return (int)World::triangulate(copy).size();
}

void drawActionRow(App& app, float x, float y, float w) {
    const bool canPair = app.selectedNotes.size() >= 2;
    const bool canAny = !app.selectedNotes.empty();
    const float bw = (w - 8.0f) * 0.5f;
    if (ui::button("insp.chain", {x, y, bw, 28}, "铰链", ButtonStyle::Subtle, canPair,
                   Icon::Pin))
        app.createChain();
    if (ui::button("insp.weld", {x + bw + 8, y, bw, 28}, "焊接组合", ButtonStyle::Subtle,
                   canPair, Icon::Weld))
        app.weldSelection();
    if (ui::button("insp.world", {x, y + 34, bw, 28}, "世界锚点", ButtonStyle::Subtle,
                   canAny, Icon::World))
        app.createWorldAnchor();
    if (ui::button("insp.removeJoints", {x + bw + 8, y + 34, bw, 28}, "移除关节",
                   ButtonStyle::Subtle, canAny, Icon::Close))
        app.removeSelectionJoints();
    if (ui::button("insp.ungroup", {x, y + 68, w, 26}, "解除焊接组", ButtonStyle::Ghost,
                   canAny))
        app.ungroupSelection();
    if (ui::button("insp.delete", {x, y + 100, w, 30}, "删除所选", ButtonStyle::Danger,
                   canAny, Icon::Trash))
        app.deleteSelection();
}

// 3 x 3 gravity picker shared by physics notes and physics rivets. Static scene
// blocks never show it because the world ignores their gravity. The multi
// selection version highlights a cell only when every selected physics
// component already uses that direction.
float drawGravityGrid(float x, float y, float w, const char* idPrefix,
                      bool hasCurrent, GravityDirection current,
                      const std::function<void(GravityDirection)>& onPick) {
    drawSectionTitle("重力方向", "GRAVITY", x, y, w);
    y += 24.0f;
    struct GravityCell {
        GravityDirection direction;
        const char* label;
    };
    static const GravityCell cells[3][3] = {
        {{GravityDirection::UpLeft, "左上"}, {GravityDirection::Up, "上"},
         {GravityDirection::UpRight, "右上"}},
        {{GravityDirection::Left, "左"}, {GravityDirection::None, "无"},
         {GravityDirection::Right, "右"}},
        {{GravityDirection::DownLeft, "左下"}, {GravityDirection::Down, "下"},
         {GravityDirection::DownRight, "右下"}}};
    const float cellW = (w - 8.0f) / 3.0f;
    const float cellH = 27.0f;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const GravityCell& cell = cells[row][col];
            const std::string cellId =
                idPrefix + std::to_string((int)cell.direction);
            const Rectangle rect{x + col * (cellW + 4.0f), y + row * (cellH + 4.0f),
                                 cellW, cellH};
            if (ui::chip(cellId.c_str(), rect, cell.label,
                         hasCurrent && current == cell.direction))
                onPick(cell.direction);
        }
    }
    return y + 3 * (cellH + 4.0f);
}

float drawRivetInspector(App& app, const Note& note, float x, float y, float w) {
    // Keep the placement colour in sync with the selected rivet.
    app.rivetPlacementColor = note.color;
    Rectangle card{x, y, w, 74};
    DrawRectangleRounded(card, 0.10f, 10, theme::card);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    const Color color = typeColor(note.color);
    DrawRectangleRounded({card.x, card.y + 8, 4, card.height - 16}, 0.5f, 4, color);
    DrawCircleV({card.x + w - 28, card.y + 34}, 13.0f, theme::alpha(color, 85));
    if (note.scene)
        DrawRing({card.x + w - 28, card.y + 34}, 5.0f, 10.0f, 0.0f, 360.0f, 28,
                 theme::alpha(Color{255, 255, 255, 255}, 100));
    DrawCircleLines((int)(card.x + w - 28), (int)(card.y + 34), 13.0f,
                    theme::alpha(color, 225));
    ui::text(note.scene ? "场景铆钉" : "物理铆钉", {card.x + 14, card.y + 7}, 14.0f,
             theme::text);
    ui::text(ui::format("#%d  ·  不参与碰撞%s", note.id,
                        note.scene ? "  ·  固定在场景中" : ""),
             {card.x + 14, card.y + 29}, 9.0f, theme::muted);

    y = card.y + card.height + 10.0f;
    drawSectionTitle("位置", "TRANSFORM", x, y, w);
    y += 24.0f;
    const float fieldW = (w - 8.0f) * 0.5f;
    float px = note.position.x;
    float py = note.position.y;
    const bool cx = ui::numberField("insp.rivet.x", {x, y, fieldW, 28}, "X", px,
                                    -6000.0f, 6000.0f, 0);
    const bool cy = ui::numberField("insp.rivet.y", {x + fieldW + 8, y, fieldW, 28}, "Y",
                                    py, -6000.0f, 6000.0f, 0);
    if (cx || cy) app.setNotePosition(note.id, {px, py});
    y += 38.0f;

    drawSectionTitle("外观", "APPEARANCE", x, y, w);
    y += 28.0f;
    static const Rgb palette[] = {{172, 178, 190}, {125, 179, 247}, {253, 214, 122},
                                  {248, 160, 204}, {140, 232, 194}, {186, 168, 246},
                                  {242, 226, 138}, {244, 166, 178}};
    for (int i = 0; i < 8; ++i) {
        const float cx = x + i * 34.0f;
        const bool selected = note.color.r == palette[i].r &&
                              note.color.g == palette[i].g &&
                              note.color.b == palette[i].b;
        if (ui::colorSwatch(ui::format("insp.rivet.color.%d", i).c_str(),
                            {cx, y, 26, 26}, typeColor(palette[i]), selected))
            app.setRivetColor(note.id, palette[i]);
    }
    y += 36.0f;

    // Physics rivets fall like notes, so they expose the same gravity picker.
    if (!note.scene)
        y = drawGravityGrid(x, y, w, "insp.gravity.", true, note.gravity,
                            [&](GravityDirection direction) {
                                app.setNoteGravity(note.id, direction);
                            });

    int jointCount = 0;
    for (const auto& joint : app.doc.revJoints)
        if (joint.noteA == note.id || joint.noteB == note.id) ++jointCount;
    ui::text(ui::format("铰链 %d", jointCount), {x, y}, 9.5f, theme::dim);
    y += 22.0f;
    drawSectionTitle("关节与编组", "RIG", x, y, w);
    y += 24.0f;
    drawActionRow(app, x, y, w);
    y += 134.0f;
    if (ui::button("insp.rivet.events", {x, y, w, 28}, "编辑事件", ButtonStyle::Subtle,
                   true, Icon::Edit))
        app.openEventEditor();
    y += 34.0f;
    return y;
}

float drawSingleNote(App& app, float x, float y, float w) {
    const int id = app.selectedNotes.front();
    Note* note = app.doc.findNote(id);
    if (!note) return y;
    if (isRivetNote(*note)) return drawRivetInspector(app, *note, x, y, w);
    const NoteType* type = app.typeForNote(*note);

    Rectangle card{x, y, w, 58};
    DrawRectangleRounded(card, 0.10f, 10, theme::card);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    const Color color = type ? typeColor(type->color) : theme::danger;
    DrawRectangleRounded({card.x, card.y + 8, 4, card.height - 16}, 0.5f, 4, color);
    const std::string title = type ? type->name : "缺失类型";
    ui::text(title, {card.x + 14, card.y + 7}, 14.0f, theme::text);
    ui::text(ui::format("#%d  ·  %s%s", note->id,
                        type ? type->label().c_str() : note->type.c_str(),
                        note->scene ? "  ·  固定在场景中" : ""),
             {card.x + 14, card.y + 29}, 9.0f, theme::muted);
    if (type)
        ui::text(ui::format("%d 顶点 · %d 凸块", (int)type->vertices.size(),
                            convexPieceCount(*type)),
                 {card.x + 14, card.y + 43}, 8.5f, theme::dim);

    const NoteLibrary& source = note->scene ? app.sceneLibrary : app.library;
    int typeIndex = -1;
    for (size_t i = 0; i < source.types.size(); ++i)
        if (source.types[i].name == note->type) typeIndex = (int)i;
    if (typeIndex >= 0) {
        if (ui::iconButton("insp.editType", {card.x + w - 30, card.y + 8, 22, 22},
                           Icon::Edit, "编辑原件形状"))
            app.openShapeEditor(typeIndex, note->scene);
    }

    y = card.y + card.height + 10.0f;
    drawSectionTitle("变换", "TRANSFORM", x, y, w);
    y += 24.0f;

    const float fieldW = (w - 8.0f) * 0.5f;
    float px = note->position.x;
    float py = note->position.y;
    const bool cx = ui::numberField("insp.posx", {x, y, fieldW, 28}, "X", px, -6000.0f,
                                    6000.0f, 0);
    const bool cy = ui::numberField("insp.posy", {x + fieldW + 8, y, fieldW, 28}, "Y", py,
                                    -6000.0f, 6000.0f, 0);
    if (cx || cy) app.setNotePosition(id, {px, py});
    y += 34.0f;

    float degrees = internalAngleToUserDegrees(note->angle);
    if (ui::numberField("insp.angle", {x, y, fieldW, 28}, "角度", degrees, -1000000.0f,
                        1000000.0f, 1, "°"))
        app.setNoteAngle(id, userDegreesToInternalAngle(degrees));
    if (ui::button("insp.rotQ", {x + fieldW + 8, y, (fieldW - 8) * 0.5f, 28}, "-15°",
                   ButtonStyle::Subtle))
        app.rotateSelection(15.0f * DEG2RAD);
    if (ui::button("insp.rotE", {x + fieldW + 8 + (fieldW - 8) * 0.5f + 8, y,
                                 (fieldW - 8) * 0.5f, 28}, "+15°", ButtonStyle::Subtle))
        app.rotateSelection(-15.0f * DEG2RAD);
    y += 38.0f;

    if (!note->scene)
        y = drawGravityGrid(x, y, w, "insp.gravity.", true, note->gravity,
                            [&](GravityDirection direction) {
                                app.setNoteGravity(note->id, direction);
                            });

    drawSectionTitle("虚化", "GHOST", x, y, w);
    y += 24.0f;
    bool ghostOn = note->ghost;
    if (ui::toggle("insp.ghost", {x, y, w, 28}, "虚化（半透明 · 不产生碰撞）",
                   ghostOn))
        app.setNoteGhost(note->id, ghostOn);
    y += 32.0f;
    ui::text("默认关闭 · 场景块与物理块都适用", {x, y}, 8.0f, theme::dim);
    y += 22.0f;

    if (type && !type->variables.empty()) {
        drawSectionTitle("变量", "VARIABLES", x, y, w);
        y += 24.0f;
        for (const auto& variable : type->variables) {
            float value = app.noteVariableValue(*note, variable);
            const std::string label =
                variable.displayName.empty() ? variable.name : variable.displayName;
            const std::string fieldId = "insp.var." + std::to_string(id) + "." + variable.name;
            if (ui::numberField(fieldId.c_str(), {x, y, w, 28}, label, value, -100000.0f,
                                100000.0f, 2))
                app.setNoteVariable(id, variable.name, value);
            y += 32.0f;
        }
        y += 6.0f;
    }

    int jointCount = 0;
    for (const auto& j : app.doc.revJoints)
        if (j.noteA == id || j.noteB == id) ++jointCount;
    for (const auto& j : app.doc.weldJoints)
        if (j.noteA == id || j.noteB == id) ++jointCount;
    const std::string groupText =
        note->group > 0 ? ui::format("#%d", note->group) : std::string("无");
    ui::text(ui::format("编组 %s · 关节 %d", groupText.c_str(), jointCount), {x, y}, 9.5f,
             theme::dim);
    y += 22.0f;

    drawSectionTitle("关节与编组", "RIG", x, y, w);
    y += 24.0f;
    drawActionRow(app, x, y, w);
    y += 134.0f;
    if (ui::button("insp.events", {x, y, w, 28}, "编辑事件", ButtonStyle::Subtle, true,
                   Icon::Edit))
        app.openEventEditor();
    y += 34.0f;
    return y;
}

float drawMultiNotes(App& app, float x, float y, float w) {
    Rectangle card{x, y, w, 60};
    DrawRectangleRounded(card, 0.10f, 10, theme::card);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    ui::text(ui::format("%d 个原件已选中", (int)app.selectedNotes.size()),
             {card.x + 14, card.y + 9}, 14.0f, theme::text);

    std::set<std::string> typeNames;
    std::vector<Color> colors;
    for (int id : app.selectedNotes) {
        const Note* n = app.doc.findNote(id);
        if (!n) continue;
        typeNames.insert(n->type);
        const NoteType* t = app.typeForNote(*n);
        if (t) colors.push_back(typeColor(t->color));
    }
    int dotX = (int)card.x + 14;
    for (size_t i = 0; i < colors.size() && i < 10; ++i) {
        DrawCircleV({(float)dotX, card.y + 38}, 4.0f, colors[i]);
        dotX += 12;
    }
    std::string names;
    for (const auto& n : typeNames) {
        if (!names.empty()) names += " · ";
        names += n;
    }
    ui::text(ui::ellipsize(names, 9.5f, w - 28), {card.x + 14, card.y + 43}, 9.5f,
             theme::muted);

    y = card.y + card.height + 10.0f;
    drawSectionTitle("批量操作", "BATCH", x, y, w);
    y += 24.0f;
    if (ui::button("insp.multi.rotl", {x, y, (w - 8) * 0.5f, 28}, "逆时针 15°",
                   ButtonStyle::Subtle))
        app.rotateSelection(15.0f * DEG2RAD);
    if (ui::button("insp.multi.rotr", {x + (w - 8) * 0.5f + 8, y, (w - 8) * 0.5f, 28},
                   "顺时针 15°", ButtonStyle::Subtle))
        app.rotateSelection(-15.0f * DEG2RAD);
    y += 36.0f;

    // Batch gravity: only physics components react to gravity. A mixed
    // selection still works, but scene blocks are left untouched.
    bool hasPhysics = false;
    bool sameGravity = true;
    GravityDirection commonGravity = GravityDirection::Down;
    for (int id : app.selectedNotes) {
        const Note* note = app.doc.findNote(id);
        if (!note || note->scene) continue;
        if (!hasPhysics) {
            hasPhysics = true;
            commonGravity = note->gravity;
        } else if (note->gravity != commonGravity) {
            sameGravity = false;
        }
    }
    if (hasPhysics) {
        y = drawGravityGrid(x, y, w, "insp.gravity.multi.", sameGravity, commonGravity,
                            [&](GravityDirection direction) {
                                app.setSelectionGravity(direction);
                            });
        y += 8.0f;
    }

    // Batch ghost state applies to every selected Tap / Drag / Flick / Hold
    // note, regardless of whether it is a physics or scene component.
    bool hasGhostable = false;
    bool allGhost = true;
    bool anyGhost = false;
    for (int id : app.selectedNotes) {
        const Note* note = app.doc.findNote(id);
        if (!note || isRivetNote(*note)) continue;
        hasGhostable = true;
        if (note->ghost) anyGhost = true;
        else allGhost = false;
    }
    if (hasGhostable) {
        drawSectionTitle("批量虚化", "GHOST", x, y, w);
        y += 24.0f;
        const float bw = (w - 8.0f) * 0.5f;
        if (ui::chip("insp.multi.ghost.on", {x, y, bw, 28}, "全部虚化",
                     anyGhost && allGhost))
            app.setSelectionGhost(true);
        if (ui::chip("insp.multi.ghost.off", {x + bw + 8, y, bw, 28}, "恢复实体",
                     !anyGhost))
            app.setSelectionGhost(false);
        y += 32.0f;
        ui::text("虚化后原件半透明且不再产生物理碰撞", {x, y}, 8.0f, theme::dim);
        y += 22.0f;
    }

    drawActionRow(app, x, y, w);
    y += 134.0f;
    return y;
}

void drawJudgeInspector(App& app, float x, float y, float w) {
    const int id = app.selectedJudge;
    JudgeLine* judge = app.doc.findJudge(id);
    if (!judge) return;

    Rectangle card{x, y, w, 46};
    DrawRectangleRounded(card, 0.10f, 10, theme::card);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    DrawRectangleRounded({card.x, card.y + 8, 4, card.height - 16}, 0.5f, 4,
                         typeColor(judge->color));
    ui::text(ui::format("判定线 #%d", judge->id), {card.x + 14, card.y + 7}, 14.0f,
             theme::text);
    ui::text(ui::format("固定在场景中 · %.0f × %.0f px", judge->width, judge->height),
             {card.x + 14, card.y + 29}, 9.0f, theme::muted);

    y = card.y + card.height + 10.0f;
    drawSectionTitle("判定线设置", "JUDGE LINE", x, y, w);
    y += 24.0f;

    const float fieldW = (w - 8.0f) * 0.5f;
    float px = judge->position.x;
    float py = judge->position.y;
    const bool cx = ui::numberField("insp.judgex", {x, y, fieldW, 28}, "X", px, -6000.0f,
                                    6000.0f, 0);
    const bool cy = ui::numberField("insp.judgey", {x + fieldW + 8, y, fieldW, 28}, "Y", py,
                                    -6000.0f, 6000.0f, 0);
    if (cx || cy) {
        app.inspectorEditBegin();
        if (JudgeLine* j = app.doc.findJudge(id)) {
            j->position = {px, py};
            app.syncBodyToJudge(id);
            app.doc.dirty = true;
        }
    }
    y += 34.0f;

    float degrees = internalAngleToUserDegrees(judge->angle);
    if (ui::numberField("insp.judgeangle", {x, y, fieldW, 28}, "角度", degrees, -1000000.0f,
                        1000000.0f, 1, "°"))
        app.setJudgeAngle(id, userDegreesToInternalAngle(degrees));
    y += 36.0f;

    float width = judge->width;
    float height = judge->height;
    const bool cw = ui::numberField("insp.judgew", {x, y, fieldW, 28}, "宽度", width, 20.0f,
                                    20000.0f, 0);
    const bool ch = ui::numberField("insp.judgeh", {x + fieldW + 8, y, fieldW, 28}, "高度",
                                    height, 1.0f, 400.0f, 0);
    if (cw || ch) app.setJudgeSize(id, width, height);
    y += 34.0f;

    if (ui::button("insp.judgeDefault", {x, y, fieldW, 28}, "默认尺寸", ButtonStyle::Subtle))
        app.setJudgeSize(id, 4000.0f, 5.0f);
    if (ui::button("insp.judgeColor", {x + fieldW + 8, y, fieldW, 28}, "淡黄色",
                   ButtonStyle::Subtle))
        app.setJudgeColor(id, {255, 238, 150});
    y += 38.0f;

    drawSectionTitle("虚化", "GHOST", x, y, w);
    y += 24.0f;
    bool ghostOn = judge->ghost;
    if (ui::toggle("insp.judge.ghost", {x, y, w, 28}, "虚化（虚线 · 不产生碰撞）",
                   ghostOn))
        app.setJudgeGhost(id, ghostOn);
    y += 32.0f;
    ui::text("颜色保持不变 · 默认关闭", {x, y}, 8.0f, theme::dim);
    y += 22.0f;

    if (ui::button("insp.judge.events", {x, y, w, 28}, "编辑事件", ButtonStyle::Subtle,
                   true, Icon::Edit))
        app.openEventEditor();
    y += 34.0f;
    if (ui::button("insp.judgeDelete", {x, y, w, 30}, "删除判定线", ButtonStyle::Danger, true,
                   Icon::Trash))
        app.deleteJudge(id);
    y += 40.0f;
}

void drawFieldInspector(App& app, float x, float y, float w) {
    const int id = app.selectedField;
    ForceField* field = app.doc.findField(id);
    if (!field) return;
    ForceField values = *field;
    const Color color = forceFieldColor(values.kind);

    Rectangle card{x, y, w, 50};
    DrawRectangleRounded(card, 0.10f, 10, theme::card);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    DrawRectangleRounded({card.x, card.y + 8, 4, card.height - 16}, 0.5f, 4, color);
    const ForceFieldTemplate* fieldType = app.fieldLibrary.find(values.kind);
    const std::string fieldTitle =
        fieldType && !fieldType->displayName.empty() ? fieldType->displayName
                                                     : forceFieldKindName(values.kind);
    ui::text(ui::format("%s #%d", fieldTitle.c_str(), values.id),
             {card.x + 14, card.y + 7}, 14.0f, theme::text);
    ui::text(ui::format("%s · 固定在世界中", forceFieldShapeName(values.shape)),
             {card.x + 14, card.y + 29}, 9.0f, theme::muted);

    y = card.y + card.height + 10.0f;
    drawSectionTitle("力场设置", "FORCE FIELD", x, y, w);
    y += 24.0f;

    const float fieldW = (w - 8.0f) * 0.5f;
    bool changed = false;
    float px = values.position.x;
    float py = values.position.y;
    const bool cx = ui::numberField("insp.fieldx", {x, y, fieldW, 28}, "X", px, -6000.0f,
                                    6000.0f, 0);
    const bool cy = ui::numberField("insp.fieldy", {x + fieldW + 8, y, fieldW, 28}, "Y", py,
                                    -6000.0f, 6000.0f, 0);
    if (cx || cy) {
        values.position = {px, py};
        changed = true;
    }
    y += 34.0f;

    float degrees = internalAngleToUserDegrees(values.angle);
    if (ui::numberField("insp.fieldangle", {x, y, fieldW, 28}, "角度", degrees, -1000000.0f,
                        1000000.0f, 1, "°")) {
        values.angle = userDegreesToInternalAngle(degrees);
        changed = true;
    }
    int shape = (int)values.shape;
    if (ui::segmented("insp.fieldshape", {x + fieldW + 8, y, fieldW, 28},
                      {"矩形", "圆形"}, shape)) {
        app.setFieldShape(id, (ForceFieldShape)shape);
        values.shape = (ForceFieldShape)shape;
        field = app.doc.findField(id);
        if (!field) return;
    }
    y += 36.0f;

    if (values.shape == ForceFieldShape::Rectangle) {
        float width = values.width;
        float height = values.height;
        const bool cw = ui::numberField("insp.fieldw", {x, y, fieldW, 28}, "宽度", width,
                                        20.0f, 20000.0f, 0);
        const bool ch = ui::numberField("insp.fieldh", {x + fieldW + 8, y, fieldW, 28},
                                        "高度", height, 20.0f, 20000.0f, 0);
        if (cw || ch) {
            values.width = width;
            values.height = height;
            changed = true;
        }
    } else {
        float radius = values.radius;
        if (ui::numberField("insp.fieldr", {x, y, w, 28}, "半径", radius, 10.0f,
                            20000.0f, 0)) {
            values.radius = radius;
            changed = true;
        }
    }
    y += 36.0f;

    if (values.kind == ForceFieldKind::Vortex) {
        float vortex = values.vortex;
        if (ui::numberField("insp.fieldvortex", {x, y, w, 28}, "涡流强度", vortex,
                            -500.0f, 500.0f, 2)) {
            values.vortex = vortex;
            changed = true;
        }
        y += 34.0f;
        ui::text("正值顺时针 · 负值逆时针", {x, y}, 9.0f, theme::muted);
        y += 22.0f;
    } else if (values.kind == ForceFieldKind::Buoyancy) {
        float directionDeg = directionToUserDegrees(values.buoyancyDirection);
        if (ui::numberField("insp.fieldbuoydir", {x, y, w, 28}, "浮力角度",
                            directionDeg, -360.0f, 360.0f, 1, "°")) {
            values.buoyancyDirection = userDegreesToDirection(directionDeg);
            changed = true;
        }
        y += 36.0f;
        float fluidDensity = values.buoyancy;
        if (ui::numberField("insp.fieldbuoy", {x, y, w, 28}, "流体密度", fluidDensity,
                            0.0f, 500.0f, 2)) {
            values.buoyancy = fluidDensity;
            changed = true;
        }
        y += 36.0f;
    } else if (values.kind == ForceFieldKind::GravityChange) {
        float directionDeg = directionToUserDegrees(values.buoyancyDirection);
        if (ui::numberField("insp.fieldgravitydir", {x, y, w, 28}, "引力方向",
                            directionDeg, -360.0f, 360.0f, 1, "°")) {
            values.buoyancyDirection = userDegreesToDirection(directionDeg);
            changed = true;
        }
        y += 36.0f;
    } else if (values.kind == ForceFieldKind::Drag) {
        float drag = values.strength;
        if (ui::numberField("insp.fielddrag", {x, y, w, 28}, "阻力强度", drag, 0.0f,
                            500.0f, 2)) {
            values.strength = drag;
            changed = true;
        }
        y += 36.0f;
    } else {
        float strength = values.strength;
        if (ui::numberField("insp.fieldstrength", {x, y, w, 28},
                            values.kind == ForceFieldKind::Gravity ? "最大强度"
                                                                   : "最大强度",
                            strength, 0.0f, 500.0f, 2)) {
            values.strength = strength;
            changed = true;
        }
        y += 36.0f;
        if (values.kind == ForceFieldKind::Gravity) {
            bool dampingOn = values.damping > 0.0f;
            if (ui::toggle("insp.fielddamping", {x, y, w, 28}, "阻尼（削减音符动力）",
                           dampingOn)) {
                values.damping = dampingOn ? 8.0f : 0.0f;
                changed = true;
            }
            y += 34.0f;
            if (values.damping > 0.0f) {
                float damping = values.damping;
                if (ui::numberField("insp.fielddampingvalue", {x, y, w, 28}, "阻尼强度",
                                    damping, 0.0f, 500.0f, 2)) {
                    values.damping = damping;
                    changed = true;
                }
                y += 36.0f;
            }
        }
    }

    if (ui::button("insp.field.events", {x, y, w, 28}, "编辑事件", ButtonStyle::Subtle,
                   true, Icon::Edit))
        app.openEventEditor();
    y += 34.0f;
    if (ui::button("insp.fieldDelete", {x, y, w, 30}, "删除力场", ButtonStyle::Danger, true,
                   Icon::Trash))
        app.deleteField(id);
    y += 40.0f;
    if (changed) app.setFieldParameters(id, values);
}

void drawStats(App& app, float x, float y, float w) {
    Rectangle card{x, y, w, 66};
    DrawRectangleRounded(card, 0.10f, 10, theme::panelRaised);
    DrawRectangleRoundedLines(card, 0.10f, 10, theme::strokeSoft);
    int dynamicNotes = 0;
    int sceneNotes = 0;
    int rivets = 0;
    for (const auto& note : app.doc.notes) {
        if (isRivetNote(note)) {
            ++rivets;
            continue;
        }
        (note.scene ? sceneNotes : dynamicNotes) += 1;
    }
    const int values[] = {dynamicNotes,          sceneNotes,       (int)app.doc.judges.size(),
                          (int)app.doc.fields.size(), (int)app.world.contacts.size()};
    const char* labels[] = {"音符", "场景块", "判定线", "力场", "接触"};
    const float cell = w / 5.0f;
    for (int i = 0; i < 5; ++i) {
        const float cx = x + cell * i + cell * 0.5f;
        if (i > 0)
            DrawLineEx({x + cell * i, y + 10}, {x + cell * i, y + card.height - 10}, 1.0f,
                       theme::strokeSoft);
        const std::string v = std::to_string(values[i]);
        ui::text(v, {cx - ui::textWidth(v, 13.0f) * 0.5f, y + 6}, 13.0f, theme::text);
        ui::text(labels[i], {cx - ui::textWidth(labels[i], 8.5f) * 0.5f, y + 30}, 8.5f,
                 theme::dim);
    }
    int awake = 0;
    for (const Body* b : app.world.bodies)
        if (b->awake && b->type == BodyType::Dynamic) ++awake;
    ui::text(ui::format("醒着的动态刚体 %d · 铆钉 %d · 固定步长 120 Hz", awake, rivets),
             {x + 10, y + 48}, 8.0f, theme::dim);
}

} // namespace

void drawRightPanel(App& app) {
    const Rectangle panel = layout::right();
    DrawRectangleRec(panel, theme::panel);
    DrawLineEx({panel.x, panel.y}, {panel.x, panel.y + panel.height}, 1.0f,
               theme::strokeSoft);

    const float x = panel.x + kPad;
    const float w = panel.width - kPad * 2.0f;
    ui::text("检查器", {x, 74}, 12.5f, theme::text);
    ui::text("INSPECTOR", {x + 48, 77}, 8.0f, theme::dim, 1.4f);

    int totalSelected = (int)app.selectedNotes.size() + (app.selectedJudge >= 0 ? 1 : 0) +
                        (app.selectedField >= 0 ? 1 : 0);
    if (totalSelected > 0) {
        const std::string badge = std::to_string(totalSelected);
        DrawRectangleRounded({x + w - 24, 74, 24, 18}, 0.5f, 6,
                             theme::alpha(theme::accent, 45));
        ui::textCentered(badge, {x + w - 24, 74, 24, 18}, 9.0f, theme::text);
    }
    DrawLineEx({x, 98}, {x + w, 98}, 1.0f, theme::strokeSoft);

    // The inspector scrolls when a note type exposes several variables; the
    // header and project footer stay fixed.
    size_t variableCount = 0;
    if (app.selectedNotes.size() == 1) {
        const Note* note = app.doc.findNote(app.selectedNotes.front());
        if (const NoteType* type = note ? app.typeForNote(*note) : nullptr)
            variableCount = type->variables.size();
    }
    float contentHeight = 560.0f;
    if (app.selectedNotes.size() == 1) {
        const Note* note = app.doc.findNote(app.selectedNotes.front());
        contentHeight = (note && isRivetNote(*note))
                            ? 800.0f
                            : 1080.0f + (float)variableCount * 32.0f;
    } else if (app.selectedField >= 0)
        contentHeight = 1080.0f;
    else if (app.selectedJudge >= 0 || app.selectedNotes.size() > 1)
        contentHeight = 1040.0f;
    const Rectangle scrollRect{panel.x, 102.0f, panel.width,
                               layout::H - layout::bottomH - 104.0f};
    const float offset = ui::scrollOffset("inspector.scroll", scrollRect, contentHeight);

    ui::scissorPush(scrollRect);
    float y = 110.0f - offset;
    if (app.selectedJudge >= 0) {
        drawJudgeInspector(app, x, y, w);
        y += 300.0f;
    } else if (app.selectedField >= 0) {
        drawFieldInspector(app, x, y, w);
        y += 440.0f;
    } else if (!app.selectedNotes.empty()) {
        if (app.selectedNotes.size() == 1)
            y = drawSingleNote(app, x, y, w);
        else
            y = drawMultiNotes(app, x, y, w);
    } else {
        ui::text("未选择对象", {x, y + 2}, 11.0f, theme::muted);
        y += 46.0f;
    }

    y += 4.0f;
    drawStats(app, x, y, w);
    y += 78.0f;

    if (!app.missingTypes.empty()) {
        ui::text(ui::format("警告：%d 个音符缺少类型文件", (int)app.missingTypes.size()),
                 {x, y}, 9.0f, theme::text);
        y += 16.0f;
    }
    if (!app.library.errors.empty()) {
        ui::text("音符文件有错误，详见左侧提示。", {x, y}, 9.0f, theme::text);
        y += 16.0f;
    }
    ui::scissorPop();
    ui::drawScrollbar(scrollRect, contentHeight, offset);

    const std::string footer =
        app.projectPath.empty() ? "尚未保存工程" : app.projectPath.filename().string();
    ui::text(ui::ellipsize(footer, 8.5f, w), {x, layout::H - layout::bottomH - 22.0f},
             8.5f, theme::dim);
}

} // namespace ppn::editorui