#include "app/App.h"

#include "model/Easing.h"
#include "model/Events.h"
#include "ui/EditorUIInternal.h"
#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ppn::editorui {
namespace {

// Time runs from the bottom of the track area to the top. The ten vertical
// tracks are still laid out from left to right.
float beatToY(const App& app, float beat) {
    const Rectangle tracks = layout::eventTracks();
    return tracks.y + tracks.height - (beat - app.eventEditor.timeStart) /
                                           app.eventEditor.beatsVisible * tracks.height;
}

float yToBeat(const App& app, float y) {
    const Rectangle tracks = layout::eventTracks();
    return app.eventEditor.timeStart +
           (tracks.y + tracks.height - y) / tracks.height * app.eventEditor.beatsVisible;
}

float trackWidth() {
    return layout::eventTracks().width / (float)kEventTrackCount;
}

float trackToX(const App& app, int track) {
    (void)app;
    track = std::max(0, std::min(kEventTrackCount - 1, track));
    return layout::eventTracks().x + track * trackWidth();
}

Color eventColor(EventType type) {
    static const Color colors[kEventTypeCount] = {
        {125, 179, 247, 255}, {106, 196, 232, 255}, {253, 214, 122, 255},
        {248, 160, 204, 255}, {186, 168, 246, 255}, {140, 232, 194, 255},
        {244, 166, 178, 255}, {110, 210, 230, 255}, {215, 180, 255, 255},
        {255, 210, 120, 255}, {150, 226, 170, 255}, {255, 183, 77, 255},
        {186, 104, 200, 255}, {170, 178, 210, 255}, {188, 140, 255, 255}};
    const int index = (int)type;
    return index >= 0 && index < kEventTypeCount ? colors[index] : theme::accent;
}

std::string targetTitle(App& app) {
    switch (app.eventEditor.target) {
        case EventEditorState::Target::Note:
            if (const Note* note = app.doc.findNote(app.eventEditor.targetId)) {
                if (isRivetNote(*note)) return ui::format("铆钉 #%d", note->id);
                if (const NoteType* type = app.typeForNote(*note))
                    return ui::format("%s #%d", type->name.c_str(), note->id);
                return ui::format("音符 #%d", note->id);
            }
            break;
        case EventEditorState::Target::Judge:
            return ui::format("判定线 #%d", app.eventEditor.targetId);
        case EventEditorState::Target::Field:
            if (const ForceField* field = app.doc.findField(app.eventEditor.targetId))
                return ui::format("%s #%d", forceFieldKindName(field->kind), field->id);
            break;
        default: break;
    }
    return "未选择";
}

std::vector<EventType> allowedTypes(App& app) {
    if (app.eventEditor.target == EventEditorState::Target::Field) {
        if (const ForceField* field = app.doc.findField(app.eventEditor.targetId))
            return eventTypesForField(field->kind);
        return {};
    }
    if (app.eventEditor.target == EventEditorState::Target::Note) {
        const Note* note = app.doc.findNote(app.eventEditor.targetId);
        return note ? eventTypesForNote(note->scene, isRivetNote(*note))
                    : std::vector<EventType>{};
    }
    if (app.eventEditor.target == EventEditorState::Target::Judge)
        return eventTypesForJudge();
    return {};
}

void drawGrid(App& app) {
    const Rectangle tracks = layout::eventTracks();
    const EventEditorState& editor = app.eventEditor;
    const float lane = trackWidth();
    const float snap = eventSnapBeats(app.doc.eventSplit);

    DrawRectangleRounded(tracks, 0.015f, 6, theme::canvas);
    for (int i = 0; i < kEventTrackCount; ++i) {
        const float left = trackToX(app, i);
        Rectangle laneRect{left, tracks.y, lane, tracks.height};
        if (i == editor.activeTrack)
            DrawRectangleRec(laneRect, theme::alpha(theme::accent, 16));
        if ((i & 1) != 0)
            DrawRectangleRec(laneRect, theme::alpha(theme::stroke, 10));
    }

    ui::scissorPush({tracks.x, tracks.y, tracks.width, tracks.height});

    const float first = std::floor(editor.timeStart / snap) * snap;
    const float last = editor.timeStart + editor.beatsVisible;
    for (float beat = first; beat <= last + 1e-4f; beat += snap) {
        if (beat < 0.0f) continue;
        const float y = beatToY(app, beat);
        if (y < tracks.y - 1.0f || y > tracks.y + tracks.height + 1.0f) continue;
        const bool whole = std::fabs(beat - std::round(beat)) < 1e-3f;
        DrawLineEx({tracks.x, y}, {tracks.x + tracks.width, y}, whole ? 1.9f : 1.2f,
                   whole ? theme::alpha(theme::stroke, 205)
                         : theme::alpha(theme::stroke, 115));
    }
    for (int i = 0; i <= kEventTrackCount; ++i) {
        const float x = tracks.x + i * lane;
        DrawLineEx({x, tracks.y}, {x, tracks.y + tracks.height},
                   i == 0 || i == kEventTrackCount ? 1.4f : 1.0f,
                   theme::alpha(theme::stroke, i == 0 || i == kEventTrackCount ? 180 : 100));
    }

    const std::vector<Event>* events = app.eventEditorEvents();
    if (events) {
        for (size_t i = 0; i < events->size(); ++i) {
            const Event& event = (*events)[(size_t)i];
            const float y1 = beatToY(app, event.startBeat);
            const float y2 = beatToY(app, event.endBeat);
            const float top = std::min(y1, y2);
            const float height = std::max(5.0f, std::fabs(y2 - y1));
            if (top + height < tracks.y || top > tracks.y + tracks.height) continue;
            const Rectangle rect{trackToX(app, event.track) + 4.0f, top, lane - 8.0f,
                                 height};
            const Color color = eventColor(event.type);
            DrawRectangleRounded(rect, 0.22f, 8, theme::alpha(color, 195));
            DrawRectangleRoundedLines(rect, 0.22f, 8, theme::alpha(color, 255));
            if (std::find(app.eventEditor.selection.begin(),
                          app.eventEditor.selection.end(), (int)i) !=
                app.eventEditor.selection.end())
                DrawRectangleRoundedLines({rect.x - 2.0f, rect.y - 2.0f,
                                           rect.width + 4.0f, rect.height + 4.0f},
                                          0.22f, 8, theme::text);
            if (rect.height > 14.0f) {
                ui::text(ui::ellipsize(eventTypeName(event.type), 8.0f, rect.width - 8.0f),
                         {rect.x + 4, rect.y + 2}, 8.0f, theme::text);
            }
        }
    }

    // Ghost of the event clipboard at the prospective paste position.
    if (!app.eventClipboard.empty() && ui::mouseIn(tracks) &&
        !app.eventEditor.marqueeActive &&
        app.eventEditor.dragMode == EventEditorState::DragMode::None) {
        const float beat = std::max(
            0.0f, snapEventBeat(yToBeat(app, ui::mouse().y), app.doc.eventSplit));
        const int track = std::max(
            0, std::min(kEventTrackCount - 1,
                        (int)((ui::mouse().x - tracks.x) / trackWidth())));
        const std::vector<Event> preview = app.clipboardPreviewAt(beat, track);
        for (const Event& event : preview) {
            const float y1 = beatToY(app, event.startBeat);
            const float y2 = beatToY(app, event.endBeat);
            const float top = std::min(y1, y2);
            const float height = std::max(5.0f, std::fabs(y2 - y1));
            const Rectangle rect{trackToX(app, event.track) + 4.0f, top, lane - 8.0f,
                                 height};
            const Color color = eventColor(event.type);
            DrawRectangleRounded(rect, 0.22f, 8, theme::alpha(color, 55));
            DrawRectangleRoundedLines(rect, 0.22f, 8, theme::alpha(color, 190));
        }
    }

    if (app.eventEditor.marqueeActive) {
        const Vec2 a = app.eventEditor.marqueeStart;
        const Vec2 b = ui::mouse();
        const Rectangle box{std::min(a.x, b.x), std::min(a.y, b.y),
                            std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
        DrawRectangleRec(box, theme::alpha(theme::accent, 26));
        DrawRectangleLinesEx(box, 1.0f, theme::alpha(theme::accent, 190));
    }

    if (app.eventEditor.pendingStart >= 0.0f && ui::mouseIn(tracks)) {
        const int track = std::max(0, std::min(kEventTrackCount - 1,
                                               app.eventEditor.pendingTrack));
        const float startY = beatToY(app, app.eventEditor.pendingStart);
        const float endY = beatToY(
            app, std::max(0.0f, snapEventBeat(yToBeat(app, ui::mouse().y),
                                              app.doc.eventSplit)));
        const float top = std::min(startY, endY);
        const float height = std::max(3.0f, std::fabs(endY - startY));
        const Rectangle ghost{trackToX(app, track) + 4.0f, top, lane - 8.0f, height};
        const std::vector<EventType> types = allowedTypes(app);
        const EventType ghostType = types.empty() ? EventType::MoveX : types.front();
        const Color color = eventColor(ghostType);
        DrawRectangleRounded(ghost, 0.22f, 8, theme::alpha(color, 55));
        DrawRectangleRoundedLines(ghost, 0.22f, 8, theme::alpha(color, 190));
        drawDashedLine({ghost.x, top}, {ghost.x + ghost.width, top}, 5.0f, 1.6f,
                       theme::alpha(theme::warn, 220));
        drawDashedLine({ghost.x, top + height}, {ghost.x + ghost.width, top + height},
                       5.0f, 1.6f, theme::alpha(theme::warn, 220));
        ui::text(ui::format("%s", eventTypeName(ghostType)),
                 {ghost.x + 4, top + 3}, 8.0f, theme::text);
    }

    const float bpm = app.settings.bpm > 0.0f ? app.settings.bpm : 180.0f;
    const float playBeat = app.simTime * bpm / 60.0f;
    if (playBeat >= editor.timeStart && playBeat <= editor.timeStart + editor.beatsVisible) {
        const float y = beatToY(app, playBeat);
        DrawLineEx({tracks.x, y}, {tracks.x + tracks.width, y}, 1.8f,
                   theme::alpha(theme::danger, 230));
    }

    if (ui::mouseIn(tracks)) {
        const float beat = std::max(
            0.0f, snapEventBeat(yToBeat(app, ui::mouse().y), app.doc.eventSplit));
        const float y = beatToY(app, beat);
        DrawLineEx({tracks.x, y}, {tracks.x + tracks.width, y}, 1.0f,
                   theme::alpha(theme::accent, 190));
    }
    ui::scissorPop();

    for (int i = 0; i < kEventTrackCount; ++i) {
        const std::string label = std::to_string(i + 1);
        const float cx = trackToX(app, i) + lane * 0.5f;
        ui::text(label, {cx - ui::textWidth(label, 8.5f) * 0.5f, tracks.y - 22.0f},
                 8.5f, i == editor.activeTrack ? theme::text : theme::muted);
    }
    for (float beat = std::ceil(editor.timeStart); beat <= last; beat += 1.0f) {
        const float y = beatToY(app, beat);
        if (y < tracks.y || y > tracks.y + tracks.height) continue;
        const std::string label = ui::format("%.0f", beat);
        ui::textRight(label, {tracks.x - 46.0f, y - 8.0f, 36.0f, 16.0f}, 8.5f,
                      theme::dim);
    }
    ui::textRight(ui::format("%.2f beat", last),
                  {tracks.x + tracks.width - 100.0f, tracks.y + tracks.height + 6.0f,
                   94.0f, 16.0f},
                  8.0f, theme::dim);
}

void drawEasingGraph(const Event& event, Rectangle graph) {
    DrawRectangleRounded(graph, 0.05f, 8, theme::panelRaised);
    DrawRectangleRoundedLines(graph, 0.05f, 8, theme::strokeSoft);
    // Only the easing type is shown; the heading and numeric id are omitted.
    ui::text(ui::ellipsize(easingName(event.easing), 9.0f, graph.width - 20.0f),
             {graph.x + 10, graph.y + 8}, 9.0f, theme::textSoft);

    const Rectangle plot{graph.x + 20, graph.y + 30, graph.width - 40,
                         graph.height - 40};
    for (int i = 0; i <= 4; ++i) {
        const float x = plot.x + plot.width * (i / 4.0f);
        const float y = plot.y + plot.height * (i / 4.0f);
        DrawLineEx({x, plot.y}, {x, plot.y + plot.height}, 1.0f,
                   theme::alpha(theme::stroke, 50));
        DrawLineEx({plot.x, y}, {plot.x + plot.width, y}, 1.0f,
                   theme::alpha(theme::stroke, 50));
    }
    DrawLineEx({plot.x, plot.y + plot.height}, {plot.x + plot.width, plot.y + plot.height},
               1.2f, theme::alpha(theme::stroke, 150));
    DrawLineEx({plot.x, plot.y}, {plot.x, plot.y + plot.height}, 1.2f,
               theme::alpha(theme::stroke, 150));

    ui::scissorPush(plot);
    Vec2 previous{};
    const int samples = 72;
    for (int i = 0; i <= samples; ++i) {
        const float t = (float)i / (float)samples;
        const float eased = easingProgress(event.easing, t);
        const Vec2 point{plot.x + t * plot.width,
                         plot.y + (1.0f - eased) * plot.height};
        if (i > 0) DrawLineEx(previous, point, 2.2f, theme::accent);
        previous = point;
    }
    ui::scissorPop();
    ui::text("0", {plot.x - 2, plot.y + plot.height + 4}, 8.0f, theme::dim);
    ui::text("1", {plot.x + plot.width - 6, plot.y + plot.height + 4}, 8.0f,
             theme::dim);
}

void drawForm(App& app) {
    const Rectangle form = layout::eventForm();
    std::vector<Event>* events = app.eventEditorEvents();
    if (!events) return;

    ui::text("事件参数", {form.x, form.y}, 12.0f, theme::text);
    ui::text("EVENT", {form.x + 74, form.y + 3}, 8.0f, theme::dim, 1.4f);
    DrawLineEx({form.x, form.y + 24}, {form.x + form.width, form.y + 24}, 1.0f,
               theme::strokeSoft);

    if (app.eventEditor.selected < 0 ||
        app.eventEditor.selected >= (int)events->size()) {
        ui::text("选中一个事件，或在轨道上按 Q / E / R 放置。", {form.x, form.y + 44},
                 9.5f, theme::muted);
        return;
    }
    Event& event = (*events)[(size_t)app.eventEditor.selected];
    std::vector<EventType> types = allowedTypes(app);
    float y = form.y + 38.0f;
    const float w = form.width;
    const float half = (w - 8.0f) * 0.5f;

    ui::text("类型", {form.x, y}, 9.0f, theme::textSoft);
    y += 16.0f;
    const float typeW = (w - 6.0f) * 0.5f;
    for (size_t i = 0; i < types.size(); ++i) {
        const int col = (int)i % 2;
        const int row = (int)i / 2;
        const Rectangle cell{form.x + col * (typeW + 6.0f), y + row * 28.0f, typeW,
                             24.0f};
        const std::string id = "event.type." + std::to_string((int)types[i]);
        if (ui::chip(id.c_str(), cell, eventTypeName(types[i]), event.type == types[i])) {
            app.doc.pushUndo();
            event.type = types[i];
            if (eventTypeIsInstantOnly(event.type)) {
                event.instant = true;
                event.endBeat =
                    event.startBeat + eventSnapBeats(app.doc.eventSplit);
                // The switch reads naturally when it is created: end value 1
                // ghosts the component, 0 restores it.
                if (event.endValue.empty() || event.endValue == "this")
                    event.endValue = "1";
            }
            app.doc.dirty = true;
        }
    }
    y += std::max(1, (int)((types.size() + 1) / 2)) * 28.0f + 6.0f;

    const float snap = eventSnapBeats(app.doc.eventSplit);
    float start = event.startBeat;
    float end = event.endBeat;
    const bool cs = ui::numberField("event.start", {form.x, y, half, 28}, "起始时间",
                                    start, 0.0f, 100000.0f, 2);
    const bool ce = ui::numberField("event.end", {form.x + half + 8, y, half, 28},
                                    "结束时间", end, 0.0f, 100000.0f, 2);
    if (cs || ce) {
        app.doc.pushUndo();
        event.startBeat = std::max(0.0f, snapEventBeat(start, app.doc.eventSplit));
        event.endBeat = std::max(0.0f, snapEventBeat(end, app.doc.eventSplit));
        if (event.endBeat <= event.startBeat) event.endBeat = event.startBeat + snap;
        if (eventTypeIsInstantOnly(event.type)) {
            event.instant = true;
            event.endBeat = event.startBeat + snap;
        }
        app.doc.dirty = true;
    }
    y += 34.0f;

    if (eventTypeIsInstantOnly(event.type)) {
        // Ghost is a switch, so it has no meaningful duration or easing. Keep
        // the flag forced on instead of exposing an unusable checkbox.
        event.instant = true;
        ui::text("瞬时完成（虚化事件固定）", {form.x + 2, y + 5}, 9.5f, theme::accent);
    } else if (ui::checkbox("event.instant", {form.x, y, w, 24.0f}, "瞬时完成",
                            event.instant)) {
        app.doc.pushUndo();
        app.doc.dirty = true;
    }
    y += 30.0f;

    const bool momentum = eventTypeIsMomentum(event.type);
    if (ui::textField("event.startvalue", {form.x, y, w, 28}, event.startValue, "this",
                      96, 10.0f, momentum ? "起始动量" : "起始值")) {
        app.eventEditor.error.clear();
        if (event.startValue.empty()) event.startValue = "this";
        app.doc.dirty = true;
    }
    y += 34.0f;
    if (ui::textField("event.endvalue", {form.x, y, w, 28}, event.endValue, "this", 96,
                      10.0f, momentum ? "结束动量" : "结束值")) {
        app.eventEditor.error.clear();
        if (event.endValue.empty()) event.endValue = "this";
        app.doc.dirty = true;
    }
    y += 34.0f;

    if (event.type == EventType::DirectionalMomentum) {
        if (ui::textField("event.anglevalue", {form.x, y, w, 28}, event.angleValue,
                          "this", 96, 10.0f, "角度")) {
            app.eventEditor.error.clear();
            if (event.angleValue.empty()) event.angleValue = "this";
            app.doc.dirty = true;
        }
        y += 34.0f;
        ui::text("0 上 · 正数顺时针 · this 为当前角度", {form.x, y}, 8.0f,
                 theme::dim);
        y += 20.0f;
    } else if (event.type == EventType::RotationalMomentum) {
        ui::text("正数顺时针", {form.x, y}, 8.5f, theme::textSoft);
        y += 20.0f;
    } else if (event.type == EventType::Ghost) {
        ui::text(ui::ellipsize("1 = 虚化，0 = 恢复实体；在起始 beat 立即生效。", 8.0f, w),
                 {form.x, y}, 8.0f, theme::dim);
        y += 20.0f;
    } else if (event.type == EventType::VortexStrength) {
        ui::text("正数顺时针 · 负数逆时针", {form.x, y}, 8.5f, theme::textSoft);
        y += 20.0f;
    }

    float easingValue = (float)event.easing;
    if (ui::numberField("event.easingid", {form.x, y, w, 28}, "缓动", easingValue,
                        1.0f, (float)kEasingCount, 0)) {
        app.doc.pushUndo();
        event.easing =
            std::max(1, std::min(kEasingCount, (int)std::lround(easingValue)));
        app.doc.dirty = true;
    }
    y += 34.0f;

    if (event.type == EventType::GravityDirection) {
        ui::text(ui::ellipsize("0 上 · 1 下 · 2 左 · 3 右 · 4 左上 · 5 右上 · 6 左下 · "
                               "7 右下 · 8 无",
                               8.0f, w),
                 {form.x, y}, 8.0f, theme::dim);
        y += 20.0f;
    }

    const float graphSize = 180.0f;
    const Rectangle graph{form.x, y + 6.0f, graphSize, graphSize};
    drawEasingGraph(event, graph);
    const float cutY = graph.y + graphSize + 6.0f;
    if (ui::button("event.cut", {form.x, cutY, half, 28.0f}, "剪切",
                   ButtonStyle::Subtle))
        app.copySelectedEvents(true);
    if (ui::button("event.copy", {form.x + half + 8, cutY, half, 28.0f}, "复制",
                   ButtonStyle::Subtle))
        app.copySelectedEvents(false);
    const float copyOtherY = cutY + 34.0f;
    if (ui::button("event.copyother", {form.x, copyOtherY, w, 28.0f},
                   "复制到其他原件", ButtonStyle::AccentLight))
        app.beginCopyEventsToOther();
    const float deleteY = copyOtherY + 34.0f;
    if (ui::button("event.delete", {form.x, deleteY, w, 28.0f}, "删除事件",
                   ButtonStyle::Danger, true, Icon::Trash)) {
        app.deleteSelectedEvent();
        return;
    }
    if (!app.eventEditor.error.empty())
        ui::text(ui::ellipsize(app.eventEditor.error, 9.0f, w),
                 {form.x, deleteY + 34.0f}, 9.0f, theme::danger);
}

void drawEventCopyOverlay(App& app) {
    DrawRectangle(0, 0, (int)layout::W, (int)layout::H,
                  theme::alpha(theme::modalDim, 235));
    DrawRectangle(0, 0, (int)layout::W, 64, theme::panel);
    DrawLineEx({0, 64}, {layout::W, 64}, 1.0f, theme::stroke);
    ui::icon(Icon::Edit, {22, 18, 22, 22}, theme::accent);
    ui::text("复制事件到其他原件", {54, 14}, 17.0f, theme::text);
    ui::text("选择目标原件后点击确认复制", {54, 40}, 9.0f, theme::dim);

    const int selectedCount = (int)app.eventCopySelectedNotes.size() +
                              (app.eventCopySelectedJudge >= 0 ? 1 : 0) +
                              (app.eventCopySelectedField >= 0 ? 1 : 0);
    if (selectedCount > 0)
        ui::text(ui::format("已选 %d 个原件", selectedCount), {760, 22}, 10.0f,
                 theme::textSoft);

    if (ui::button("event.copy.cancel", {layout::W - 336, 16, 140, 32}, "取消",
                   ButtonStyle::Secondary))
        app.cancelEventCopyToTargets();
    if (ui::button("event.copy.confirm", {layout::W - 184, 16, 160, 32}, "确认复制",
                   ButtonStyle::Primary, selectedCount > 0, Icon::Check))
        app.confirmEventCopyToTargets();

    // Draw the scene through the overlay's own selection, then restore the
    // scene selection so the two interfaces stay independent.
    const std::vector<int> savedNotes = app.selectedNotes;
    const int savedJudge = app.selectedJudge;
    const int savedField = app.selectedField;
    app.selectedNotes = app.eventCopySelectedNotes;
    app.selectedJudge = app.eventCopySelectedJudge;
    app.selectedField = app.eventCopySelectedField;
    drawCanvas(app);
    app.selectedNotes = savedNotes;
    app.selectedJudge = savedJudge;
    app.selectedField = savedField;

    const Rectangle canvas = layout::canvas();
    const Rectangle footer{0, canvas.y + canvas.height, layout::W,
                           layout::H - (canvas.y + canvas.height)};
    DrawRectangleRec(footer, theme::panel);
    DrawLineEx({0, footer.y}, {layout::W, footer.y}, 1.0f, theme::strokeSoft);
    ui::text("右键 / 中键拖动平移 · 滚轮缩放 · 左键选择原件 · Shift / Ctrl 多选",
             {canvas.x + 8, footer.y + 7}, 8.5f, theme::dim);
}

void drawHeader(App& app) {
    const Rectangle modal = layout::eventModal;
    ui::icon(Icon::Edit, {modal.x + 24, modal.y + 16, 22, 22}, theme::accent);
    ui::text("事件编辑", {modal.x + 56, modal.y + 12}, 17.0f, theme::text);
    ui::text("EVENT TIMELINE", {modal.x + 56, modal.y + 42}, 8.0f, theme::dim, 1.4f);
    ui::text(targetTitle(app), {modal.x + 190, modal.y + 22}, 11.0f, theme::textSoft);
    float split = (float)app.doc.eventSplit;
    if (ui::numberField("event.split", {modal.x + 360, modal.y + 20, 150, 28}, "切分",
                        split, 1.0f, (float)kEventSplitMax, 0))
        app.setEventSplit((int)std::lround(split));
    if (ui::iconButton("event.close", {modal.x + modal.width - 46, modal.y + 20, 28, 28},
                       Icon::Close, "关闭 (Esc)"))
        app.closeEventEditor();
    DrawLineEx({modal.x + 24, modal.y + 68}, {modal.x + modal.width - 24, modal.y + 68},
               1.0f, theme::strokeSoft);
}

} // namespace

void drawEventEditor(App& app) {
    if (app.eventCopyOverlayActive()) {
        drawEventCopyOverlay(app);
        return;
    }
    DrawRectangle(0, 0, (int)layout::W, (int)layout::H,
                  theme::alpha(theme::modalDim, 185));
    const Rectangle modal = layout::eventModal;
    DrawRectangleRounded({modal.x + 5, modal.y + 8, modal.width, modal.height}, 0.035f, 12,
                         Color{0, 0, 0, 130});
    DrawRectangleRounded(modal, 0.035f, 12, theme::panel);
    DrawRectangleRoundedLines(modal, 0.035f, 12, theme::stroke);
    drawHeader(app);
    drawGrid(app);
    drawForm(app);
    ui::text("Q / E / R 放置 · 左键拖动 · 框选 / Shift / Ctrl 多选 · "
             "Z+左键改起点 · Z+右键改终点 · Ctrl+C/X/V 复制剪切粘贴",
             {layout::eventTracks().x,
              layout::eventModal.y + layout::eventModal.height - 18.0f},
             8.5f, theme::dim);
}

} // namespace ppn::editorui
