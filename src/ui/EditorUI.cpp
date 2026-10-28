#include "app/App.h"

#include "ui/Layout.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/EditorUIInternal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace ppn::editorui {
namespace {

using layout::S;

void drawForceFieldVolume(App& app, const ForceField& field, Color base, bool selected,
                          bool ghost, bool arrows, float fillAlpha, float borderAlpha);

Color withAlpha(Color c, unsigned char a) {
    c.a = a;
    return c;
}

// ---------------------------------------------------------------- scene frame
// The 1350 x 900 display frame marks the playable area. It is drawn only, never
// interactive: there is no body and nothing can pick it.
void drawSceneFrame(App& app) {
    const Rectangle c = app.canvasRect();
    const float hw = kSceneWidth * 0.5f;
    const float hh = kSceneHeight * 0.5f;
    // Y-up design space: the visual top-left is (-hw, +hh).
    const Vec2 a = app.designToScreen({-hw, hh});
    const Vec2 b = app.designToScreen({hw, -hh});
    const Vec2 tl{std::min(a.x, b.x), std::min(a.y, b.y)};
    const Vec2 br{std::max(a.x, b.x), std::max(a.y, b.y)};
    const Rectangle r{tl.x, tl.y, br.x - tl.x, br.y - tl.y};
    if (r.width < 4.0f || r.height < 4.0f) return;

    ui::scissorPush(c);
    // Dim everything outside of the frame so inside/outside reads clearly.
    const Color outside = withAlpha(theme::canvas, 150);
    if (tl.y > c.y) DrawRectangleRec({c.x, c.y, c.width, tl.y - c.y}, outside);
    if (br.y < c.y + c.height)
        DrawRectangleRec({c.x, br.y, c.width, c.y + c.height - br.y}, outside);
    if (tl.x > c.x)
        DrawRectangleRec({c.x, std::max(c.y, tl.y), tl.x - c.x,
                          std::min(br.y, c.y + c.height) - std::max(c.y, tl.y)}, outside);
    if (br.x < c.x + c.width)
        DrawRectangleRec({br.x, std::max(c.y, tl.y), c.x + c.width - br.x,
                          std::min(br.y, c.y + c.height) - std::max(c.y, tl.y)}, outside);

    DrawRectangleLinesEx(r, 1.5f, withAlpha(theme::accentLight, 150));
    // Corner ticks.
    const float len = 18.0f;
    const Color tick = withAlpha(theme::accentLight, 210);
    DrawLineEx({r.x, r.y}, {r.x + len, r.y}, 2.5f, tick);
    DrawLineEx({r.x, r.y}, {r.x, r.y + len}, 2.5f, tick);
    DrawLineEx({r.x + r.width, r.y}, {r.x + r.width - len, r.y}, 2.5f, tick);
    DrawLineEx({r.x + r.width, r.y}, {r.x + r.width, r.y + len}, 2.5f, tick);
    DrawLineEx({r.x, r.y + r.height}, {r.x + len, r.y + r.height}, 2.5f, tick);
    DrawLineEx({r.x, r.y + r.height}, {r.x, r.y + r.height - len}, 2.5f, tick);
    DrawLineEx({r.x + r.width, r.y + r.height}, {r.x + r.width - len, r.y + r.height}, 2.5f,
               tick);
    DrawLineEx({r.x + r.width, r.y + r.height}, {r.x + r.width, r.y + r.height - len}, 2.5f,
               tick);

    const std::string label =
        ui::format("场景 %.0f × %.0f  ·  参照框", kSceneWidth, kSceneHeight);
    const float labelH = ui::textHeight(10.0f);
    float labelY = r.y - labelH - 6.0f;
    // Keep the caption clear of the "编辑模式" pill when the frame extends past
    // the top of the canvas.
    labelY = std::max(labelY, c.y + 42.0f);
    const float labelX = std::max(r.x + 8.0f, c.x + 8.0f);
    ui::text(label, {labelX, labelY}, 10.0f, theme::muted);
    ui::scissorPop();
}

void drawGrid(App& app) {
    if (!app.showGrid) return;
    const Rectangle c = app.canvasRect();
    // Screen corners map to (high Y, low Y) because design space is Y-up.
    const Vec2 topLeft = app.screenToDesign({c.x, c.y});
    const Vec2 bottomRight = app.screenToDesign({c.x + c.width, c.y + c.height});
    const float minX = std::min(topLeft.x, bottomRight.x);
    const float maxX = std::max(topLeft.x, bottomRight.x);
    const float minY = std::min(topLeft.y, bottomRight.y);
    const float maxY = std::max(topLeft.y, bottomRight.y);

    const float candidates[] = {10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                2000.0f};
    float step = candidates[0];
    for (float candidate : candidates) {
        step = candidate;
        if (candidate * app.camera.zoom >= 26.0f) break;
    }

    const int firstX = (int)std::floor(minX / step);
    const int lastX = (int)std::ceil(maxX / step);
    const int firstY = (int)std::floor(minY / step);
    const int lastY = (int)std::ceil(maxY / step);
    if (lastX - firstX > 400 || lastY - firstY > 400) return;

    ui::scissorPush(c);
    for (int i = firstX; i <= lastX; ++i) {
        const float x = app.designToScreen({i * step, 0}).x;
        const bool major = (i % 5) == 0;
        const bool axis = i == 0;
        Color color = axis ? theme::alpha(theme::accent2, 70)
                           : theme::alpha(theme::stroke, major ? 105 : 58);
        DrawLineEx({x, c.y}, {x, c.y + c.height}, axis ? 1.4f : 1.0f, color);
    }
    for (int i = firstY; i <= lastY; ++i) {
        const float y = app.designToScreen({0, i * step}).y;
        const bool major = (i % 5) == 0;
        const bool axis = i == 0;
        Color color = axis ? theme::alpha(theme::accent2, 70)
                           : theme::alpha(theme::stroke, major ? 105 : 58);
        DrawLineEx({c.x, y}, {c.x + c.width, y}, axis ? 1.4f : 1.0f, color);
    }

    if (app.showGridLabels) {
        const float labelSize = 9.0f;
        int drawn = 0;
        for (int i = firstX; i <= lastX && drawn < 24; ++i) {
            if (i == 0 || (i % 5) != 0) continue;
            const float x = app.designToScreen({i * step, 0}).x;
            if (x < c.x + 8 || x > c.x + c.width - 30) continue;
            ui::text(ui::format("%.0f", i * step), {x + 3, c.y + c.height - 15}, labelSize,
                     theme::muted);
            ++drawn;
        }
        drawn = 0;
        for (int i = firstY; i <= lastY && drawn < 24; ++i) {
            if (i == 0 || (i % 5) != 0) continue;
            const float y = app.designToScreen({0, i * step}).y;
            if (y < c.y + 16 || y > c.y + c.height - 10) continue;
            ui::text(ui::format("%.0f", i * step), {c.x + 8, y - 12}, labelSize,
                     theme::muted);
            ++drawn;
        }
    }

    // Origin marker.
    const Vec2 origin = app.designToScreen({0.0f, 0.0f});
    if (c.x <= origin.x && origin.x <= c.x + c.width && c.y <= origin.y &&
        origin.y <= c.y + c.height) {
        DrawCircleV(origin, 3.0f, theme::alpha(theme::accent2, 140));
        DrawCircleLines((int)origin.x, (int)origin.y, 7.0f, theme::alpha(theme::accent2, 60));
    }
    ui::scissorPop();
}

// The judge line: a pale yellow static bar, draggable but not editable.
void drawJudgeLine(App& app, const JudgeLine& judge, bool selected) {
    Body* body = app.bodyForJudge(judge.id);
    std::vector<Vec2> screen;
    if (body) {
        screen.reserve(body->outline.size());
        for (const auto& v : body->outline)
            screen.push_back(app.designToScreen(body->worldPoint(v) * PPM));
    }
    const Color base{judge.color.r, judge.color.g, judge.color.b, 255};
    // Ghosted judge lines keep their exact colour but are drawn as dashed
    // strokes and no longer take part in collisions.
    const bool ghost = body ? body->ghost : judge.ghost;
    if (screen.size() < 4) {
        // Fallback box in case the body is temporarily missing.
        const float hw = judge.width * 0.5f;
        const float hh = judge.height * 0.5f;
        const Vec2 corners[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        for (const auto& v : corners)
            screen.push_back(app.designToScreen(judge.position + rotate(v, judge.angle)));
    }
    const float thickness = std::max(2.0f, judge.height * app.camera.zoom);
    const Vec2 a = app.designToScreen(judge.position -
                                      rotate(Vec2{judge.width * 0.5f, 0.0f}, judge.angle));
    const Vec2 b = app.designToScreen(judge.position +
                                      rotate(Vec2{judge.width * 0.5f, 0.0f}, judge.angle));
    if (ghost) {
        // Dashed outline and centre line make the non-colliding state obvious
        // while preserving the authored line colour.
        for (size_t i = 0; i < screen.size(); ++i)
            drawDashedLine(screen[i], screen[(i + 1) % screen.size()], 9.0f, 1.6f,
                           withAlpha(base, 235));
        drawDashedLine(a, b, 11.0f, std::max(2.0f, thickness), withAlpha(base, 235));
    } else {
        if (thickness >= 5.0f) {
            for (size_t i = 1; i + 1 < screen.size(); ++i)
                DrawTriangle(screen[0], screen[i], screen[i + 1], withAlpha(base, 235));
        }
        // A crisp center line keeps the bar readable when it is only a few px high.
        DrawLineEx(a, b, std::max(2.0f, thickness), withAlpha(base, 225));
        DrawLineEx(a, b, 1.2f, withAlpha(theme::bg, 70));
        // Soft glow so the line reads like a judgement bar.
        DrawLineEx(a, b, std::max(6.0f, thickness * 2.0f), withAlpha(base, 28));
    }

    const Vec2 center = app.designToScreen(judge.position);
    if (selected) {
        const Color selectColor = theme::highlight(theme::accent);
        // Build an oriented padded frame from the actual rotated outline. The
        // old axis-aligned bracket was wrong for rotated judge lines.
        const float pad = 6.0f / std::max(0.01f, app.camera.zoom);
        const float fhw = judge.width * 0.5f + pad;
        const float fhh = judge.height * 0.5f + pad;
        const Vec2 corners[4] = {{-fhw, -fhh}, {fhw, -fhh}, {fhw, fhh}, {-fhw, fhh}};
        std::vector<Vec2> frame;
        frame.reserve(4);
        for (const Vec2& c : corners)
            frame.push_back(app.designToScreen(judge.position + rotate(c, judge.angle)));
        if (frame.size() == 4) {
            for (int i = 0; i < 4; ++i)
                DrawLineEx(frame[(size_t)i], frame[(size_t)((i + 1) % 4)], 1.2f,
                           withAlpha(selectColor, 120));
            for (int i = 0; i < 4; ++i) {
                const Vec2 p = frame[(size_t)i];
                const Vec2 prev = frame[(size_t)((i + 3) % 4)];
                const Vec2 next = frame[(size_t)((i + 1) % 4)];
                DrawLineEx(p, p + normalize(prev - p) * 13.0f, 2.2f, selectColor);
                DrawLineEx(p, p + normalize(next - p) * 13.0f, 2.2f, selectColor);
            }
        }
        DrawCircleV(center, 3.5f, theme::highlight(theme::accentLight));
        Vec2 labelPos{1e9f, 1e9f};
        for (int i = 0; i < 4 && i < (int)screen.size(); ++i) {
            labelPos.x = std::min(labelPos.x, screen[(size_t)i].x);
            labelPos.y = std::min(labelPos.y, screen[(size_t)i].y);
        }
        if (labelPos.x > 1e8f) labelPos = center;
        ui::text(ui::format("判定线 #%d  %.0f × %.0f%s", judge.id, judge.width,
                             judge.height, ghost ? "  ·  虚化" : ""),
                 {labelPos.x, labelPos.y - 30.0f}, 9.5f, theme::text);
    }
}

void drawRivet(App& app, const Note& rivet, bool selected, bool hovered) {
    Body* body = app.bodyForNote(rivet.id);
    const Vec2 center = app.designToScreen(body ? body->position * PPM : rivet.position);
    const float radius = std::max(4.0f, rivet.radius * app.camera.zoom);
    const Color gray = typeColor(rivet.color);
    const Color white{255, 255, 255, 255};

    // A rivet is its own library component with an editable radius and colour.
    // It never follows a note. Scene rivets add a broad white ring so the
    // static variant is distinct.
    DrawCircleV(center, radius, withAlpha(gray, rivet.scene ? 82 : 66));
    if (rivet.scene) {
        DrawRing(center, radius * 0.42f, radius * 0.82f, 0.0f, 360.0f, 36,
                 withAlpha(white, 92));
    }
    if (selected) {
        DrawCircleV(center, radius + 4.0f, theme::highlight(theme::accent, 46));
        DrawCircleLines((int)center.x, (int)center.y, radius + 4.0f,
                        theme::highlight(theme::accent, 205));
    } else if (hovered) {
        DrawCircleLines((int)center.x, (int)center.y, radius + 3.0f,
                        theme::highlight(theme::accent, 120));
    }
    DrawCircleLines((int)center.x, (int)center.y, radius, withAlpha(gray, 225));
    DrawCircleLines((int)center.x, (int)center.y, std::max(2.0f, radius - 1.3f),
                    withAlpha(theme::bg, 65));

    if (app.showLabels || selected || hovered) {
        const std::string label = rivet.scene
                                      ? ui::format("#%d 场景铆钉", rivet.id)
                                      : ui::format("#%d 铆钉", rivet.id);
        const float top = center.y - radius - 18.0f;
        Rectangle tag{center.x - ui::textWidth(label, 9.5f) * 0.5f,
                      top, ui::textWidth(label, 9.5f) + 12, ui::textHeight(9.5f) + 5};
        if (selected)
            DrawRectangleRounded(tag, 0.4f, 6,
                                 theme::alpha(theme::highlight(theme::accent), 48));
        ui::text(label, {tag.x + 5, tag.y + 2}, 9.5f, theme::text);
    }
}

void drawNote(App& app, const Note& note, bool selected, bool hovered) {
    const NoteType* type = app.typeForNote(note);
    const bool showDetail = selected || hovered;
    if (!type) {
        const Vec2 p = app.designToScreen(note.position);
        DrawPoly(p, 4, 7.0f, 45.0f, theme::alpha(theme::danger, 200));
        DrawPolyLinesEx(p, 4, 7.0f, 45.0f, 1.5f, theme::danger);
        ui::text("?", {p.x - 4, p.y - 7}, 11.0f, theme::text);
        return;
    }

    // Notes are always painted at full opacity. While the simulation runs the
    // body is the source of truth for both the fill and the outline, so a moving
    // note never leaves a stale outline behind at its layout position.
    Body* body = app.bodyForNote(note.id);
    const bool bodyGeometry = body && body->outline.size() >= 3;
    Poly worldPoly;
    std::vector<Poly> worldTris;
    if (bodyGeometry) {
        worldPoly.reserve(body->outline.size());
        for (const auto& v : body->outline) worldPoly.push_back(body->worldPoint(v) * PPM);
        worldTris.reserve(body->pieces.size());
        for (const auto& tri : body->pieces) {
            if (tri.size() < 3) continue;
            worldTris.push_back(Poly{body->worldPoint(tri[0]) * PPM,
                                     body->worldPoint(tri[1]) * PPM,
                                     body->worldPoint(tri[2]) * PPM});
        }
    } else {
        // No body (missing/rebuilding world): fall back to the document outline
        // so a note can never silently disappear from the scene.
        const Poly local = app.noteVertices(note);
        worldPoly.reserve(local.size());
        for (const auto& v : local) worldPoly.push_back(note.position + rotate(v, note.angle));
    }
    if (worldPoly.size() < 3) {
        const Vec2 p = app.designToScreen(note.position);
        DrawPoly(p, 4, 7.0f, 45.0f, typeColor(type->color));
        DrawPolyLinesEx(p, 4, 7.0f, 45.0f, 1.5f, theme::text);
        return;
    }
    if (worldTris.empty()) worldTris = World::triangulate(worldPoly);

    const Color base = typeColor(type->color);
    // Ghosted notes turn semi-transparent. Their authored colour and outline
    // remain recognisable so they are still easy to select and edit.
    const bool ghost = body ? body->ghost : note.ghost;

    // Filled silhouette: the same polygon as the outline, triangulated only for
    // filling. No internal triangulation edges are drawn, so selecting a note
    // never shows spurious lines across its face.
    for (const auto& tri : worldTris) {
        const Vec2 a = app.designToScreen(tri[0]);
        const Vec2 b = app.designToScreen(tri[1]);
        const Vec2 c = app.designToScreen(tri[2]);
        DrawTriangle(a, b, c, withAlpha(base, ghost ? 105 : 255));
    }

    std::vector<Vec2> screen;
    screen.reserve(worldPoly.size());
    for (const auto& v : worldPoly) screen.push_back(app.designToScreen(v));

    // Scene blocks are static scenery: a translucent white wash plus a thick
    // white inner stroke makes them clearly distinct from physics notes.
    const Color white{255, 255, 255, 255};
    if (note.scene) {
        for (const auto& tri : worldTris) {
            DrawTriangle(app.designToScreen(tri[0]), app.designToScreen(tri[1]),
                         app.designToScreen(tri[2]), withAlpha(white, ghost ? 12 : 26));
        }
        for (size_t i = 0; i < screen.size(); ++i)
            DrawLineEx(screen[i], screen[(i + 1) % screen.size()], 6.0f,
                       withAlpha(white, ghost ? 34 : 78));
    }

    // The silhouette gets a darker stroke than the fill, so adjacent notes of
    // the same colour still read as separate shapes.
    const Color outline = theme::mix(base, Color{0, 0, 0, 255}, theme::light ? 0.46f : 0.34f);
    if (selected) {
        // Selection is a soft halo around the real silhouette, not a wireframe.
        for (size_t i = 0; i < screen.size(); ++i)
            DrawLineEx(screen[i], screen[(i + 1) % screen.size()], 7.0f,
                       theme::highlight(theme::accent, 70));
        for (size_t i = 0; i < screen.size(); ++i)
            DrawLineEx(screen[i], screen[(i + 1) % screen.size()], 3.4f,
                       theme::highlight(theme::accent, 185));
    } else if (hovered) {
        for (size_t i = 0; i < screen.size(); ++i)
            DrawLineEx(screen[i], screen[(i + 1) % screen.size()], 4.0f,
                       theme::highlight(theme::accent, 65));
    }
    for (size_t i = 0; i < screen.size(); ++i)
        DrawLineEx(screen[i], screen[(i + 1) % screen.size()], showDetail ? 2.1f : 1.4f,
                   withAlpha(outline, ghost ? 150 : 255));

    if (app.showWireframe && body) {
        for (const auto& tri : body->pieces) {
            const Vec2 a = app.designToScreen(body->worldPoint(tri[0]) * PPM);
            const Vec2 b = app.designToScreen(body->worldPoint(tri[1]) * PPM);
            const Vec2 c = app.designToScreen(body->worldPoint(tri[2]) * PPM);
            DrawTriangleLines(a, b, c, withAlpha(theme::accent2, 90));
        }
    }

    Vec2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    for (const auto& p : screen) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
    }
    const Vec2 center = app.designToScreen(bodyGeometry ? body->position * PPM
                                                        : note.position);
    if (selected) {
        drawSelectionBrackets({lo.x, lo.y, hi.x - lo.x, hi.y - lo.y},
                              theme::highlight(theme::accent));
    }


    if (app.showLabels || showDetail) {
        std::string label = note.scene
                                ? ui::format("#%d 场景 %s", note.id, type->name.c_str())
                                : ui::format("#%d %s", note.id, type->name.c_str());
        if (ghost) label += " · 虚化";
        Rectangle tag{lo.x + 1, lo.y - ui::textHeight(9.5f) - 3,
                      ui::textWidth(label, 9.5f) + 12, ui::textHeight(9.5f) + 5};
        if (showDetail) {
            DrawRectangleRounded(tag, 0.4f, 6,
                                 selected ? theme::alpha(theme::highlight(theme::accent), 48)
                                          : theme::alpha(theme::panelRaised, 235));
        }
        ui::text(label, {tag.x + 5, tag.y + 2}, 9.5f, theme::text);
    }

    if (note.group > 0) {
        const Color gc = groupColor(note.group);
        DrawCircleV({hi.x + 7, lo.y - 6}, 3.5f, gc);
    }
}

void drawForceFields(App& app) {
    const auto& fields = app.fieldsForDisplay();
    for (const auto& field : fields) {
        const bool selected = app.selectedField == field.id;
        drawForceFieldVolume(app, field, forceFieldColor(field.kind), selected, false, true,
                             field.kind == ForceFieldKind::Buoyancy ? 44.0f : 38.0f,
                             selected ? 235.0f : 190.0f);
    }
}

void drawJoints(App& app) {
    if (!app.showJoints) return;
    const Color link{156, 162, 173, 255};
    const Color linkOutline{74, 80, 91, 255};
    const Color worldAnchor{247, 205, 74, 255};
    const Color worldOutline{145, 103, 15, 255};

    auto drawEndpoint = [](Vec2 p, Color fill, Color outline) {
        DrawCircleV(p, 5.0f, fill);
        DrawCircleLines((int)p.x, (int)p.y, 5.0f, outline);
    };

    for (const auto& joint : app.doc.revJoints) {
        const Vec2 pa = app.designToScreen(app.jointAnchorWorld(joint, true));
        const Vec2 pb = app.designToScreen(app.jointAnchorWorld(joint, false));
        // Hinges read as a single grey dashed link with two attachment dots.
        drawDashedLine(pa, pb, 7.0f, 1.7f, withAlpha(link, 195));
        if (joint.noteA < 0) {
            drawEndpoint(pa, worldAnchor, worldOutline);
            drawEndpoint(pb, link, linkOutline);
        } else {
            drawEndpoint(pa, link, linkOutline);
            drawEndpoint(pb, link, linkOutline);
        }
    }

    // Welds keep their distinct amber rigging so hinges remain unambiguous.
    for (const auto& joint : app.world.joints) {
        if (joint.type != JointType::Weld || !joint.b) continue;
        const Vec2 pa = app.designToScreen(
            (joint.a ? joint.a->worldPoint(joint.localA) : joint.localA) * PPM);
        const Vec2 pb = app.designToScreen(joint.b->worldPoint(joint.localB) * PPM);
        const Vec2 pa2 = app.designToScreen(
            (joint.a ? joint.a->worldPoint(joint.localA2) : joint.localA2) * PPM);
        DrawLineEx(pa, pb, 1.2f, withAlpha(theme::warn, 110));
        DrawLineEx(pa, pa2, 2.0f, withAlpha(theme::warn, 130));
        DrawCircleV(pa2, 3.0f, withAlpha(theme::warn, 200));
        DrawCircleV(pa, 2.6f, withAlpha(theme::warn, 220));
        DrawCircleV(pb, 2.6f, withAlpha(theme::warn, 220));
        DrawCircleLines((int)pa.x, (int)pa.y, 7.0f, withAlpha(theme::warn, 160));
    }
}

void drawContacts(App& app) {
    if (!app.showContactPoints) return;
    for (const auto& contact : app.world.contacts) {
        for (int i = 0; i < contact.count; ++i) {
            const Vec2 p = app.designToScreen(
                contact.a->worldPoint(contact.points[i].localAnchorA) * PPM);
            DrawCircleV(p, 2.5f, theme::danger);
            const Vec2 n = contact.normal;
            DrawLineEx(p, {p.x + n.x * 22.0f, p.y + n.y * 22.0f}, 1.2f, theme::warn);
        }
    }
}

void drawGhost(App& app) {
    if (ui::isEditingText() || app.isModalOpen()) return;
    if (!ui::mouseValid() || app.tool == Tool::Select) return;
    // The placement ghost keeps tracking the cursor every frame. It is clipped
    // to the canvas so it never paints over the side panels.
    const Vec2 p = app.screenToDesign(ui::mouse());
    const Rectangle canvas = app.canvasRect();
    ui::scissorPush(canvas);
    const Vec2 sp = app.designToScreen(p);

    const std::string placeKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::PlaceComponent]);
    const std::string previousKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::PreviousComponent]);
    const std::string nextKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::NextComponent]);
    const std::string shortcutHint =
        previousKey + " / " + nextKey + " 切换 · " + placeKey + " 放置 · Esc 退出";

    auto drawHint = [&](const std::string& label, Color color) {
        const float labelW = ui::textWidth(label, 9.5f);
        const float hintW = ui::textWidth(shortcutHint, 8.5f);
        const float boxW = std::max(labelW, hintW) + 20.0f;
        float left = sp.x + 14.0f;
        if (left + boxW > canvas.x + canvas.width - 6.0f)
            left = canvas.x + canvas.width - boxW - 6.0f;
        float top = sp.y - 54.0f;
        if (top < canvas.y + 6.0f) top = sp.y + 18.0f;
        Rectangle box{left, top, boxW, 42.0f};
        DrawRectangleRounded(box, 0.22f, 8, theme::alpha(theme::panel, 225));
        DrawRectangleRoundedLines(box, 0.22f, 8, theme::alpha(color, 150));
        ui::text(label, {box.x + 10, box.y + 5}, 9.5f, theme::text);
        ui::text(shortcutHint, {box.x + 10, box.y + 23}, 8.5f, theme::muted);
    };

    if (app.tool == Tool::Place) {
        if (app.selectedTypeName == kRivetType) {
            const float radius = std::max(4.0f, app.settings.rivetRadius * app.camera.zoom);
            const Color color = typeColor(app.rivetPlacementColor);
            DrawCircleV(sp, radius, withAlpha(color, app.placingScene ? 90 : 72));
            if (app.placingScene) {
                DrawRing(sp, radius * 0.42f, radius * 0.82f, 0.0f, 360.0f, 36,
                         withAlpha(Color{255, 255, 255, 255}, 100));
            }
            DrawCircleLines((int)sp.x, (int)sp.y, radius, withAlpha(color, 225));
            DrawCircleLines((int)sp.x, (int)sp.y, radius + 2.0f, withAlpha(color, 70));
            const std::string label =
                app.placingScene
                    ? ui::format("场景铆钉 · 半径 %.0f · 不参与碰撞 · 固定在场景中",
                                 app.settings.rivetRadius)
                    : ui::format("铆钉 · 半径 %.0f · 不参与碰撞",
                                 app.settings.rivetRadius);
            drawHint(label, color);
            ui::scissorPop();
            return;
        }
        const NoteType* type = app.activeType();
        if (!type) {
            ui::scissorPop();
            return;
        }
        const Color color = typeColor(type->color);
        const bool sceneGhost = app.placingScene;
        const Poly local = type->renderVertices();
        std::vector<Vec2> screen;
        screen.reserve(local.size());
        for (const auto& v : local) screen.push_back(app.designToScreen(p + v));
        if (screen.size() >= 3) {
            // Triangulate instead of using a fan: concave shapes such as Flick
            // must not fill across their notches. The ghost uses the note's own
            // colour so the preview matches the placed block.
            for (const auto& tri : World::triangulate(local)) {
                DrawTriangle(app.designToScreen(p + tri[0]), app.designToScreen(p + tri[1]),
                             app.designToScreen(p + tri[2]), withAlpha(color, 42));
            }
            for (size_t i = 0; i < screen.size(); ++i)
                drawDashedLine(screen[i], screen[(i + 1) % screen.size()], 6.0f, 1.6f,
                               withAlpha(color, 225));
        }
        DrawLineEx({sp.x - 11, sp.y}, {sp.x + 11, sp.y}, 1.2f, withAlpha(color, 200));
        DrawLineEx({sp.x, sp.y - 11}, {sp.x, sp.y + 11}, 1.2f, withAlpha(color, 200));
        DrawCircleLines((int)sp.x, (int)sp.y, 4.0f, withAlpha(color, 180));
        const std::string label =
            sceneGhost ? ui::format("%s · 场景块 · 固定在场景中", type->name.c_str())
                       : ui::format("%s · 物理块", type->name.c_str());
        drawHint(label, color);
    } else if (app.tool == Tool::JudgeLine) {
        JudgeLine t = app.judgePlacementTemplate();
        t.position = p;
        const Color color = typeColor(t.color);
        const float hw = t.width * 0.5f;
        const float hh = t.height * 0.5f;
        const Vec2 corners[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
        Vec2 screen[4];
        for (int i = 0; i < 4; ++i)
            screen[i] = app.designToScreen(t.position + rotate(corners[i], t.angle));
        DrawTriangle(screen[0], screen[1], screen[2], withAlpha(color, 30));
        DrawTriangle(screen[0], screen[2], screen[3], withAlpha(color, 30));
        for (int i = 0; i < 4; ++i)
            drawDashedLine(screen[i], screen[(i + 1) % 4], 9.0f, 1.4f, withAlpha(color, 215));
        const Vec2 a = app.designToScreen(t.position - rotate({hw, 0.0f}, t.angle));
        const Vec2 b = app.designToScreen(t.position + rotate({hw, 0.0f}, t.angle));
        DrawLineEx(a, b, std::max(2.0f, t.height * app.camera.zoom), withAlpha(color, 215));
        drawHint(ui::format("判定线 · 固定在场景中 · %.0f × %.0f", t.width, t.height), color);
    } else if (app.tool == Tool::ForceField) {
        ForceField field;
        field.kind = app.placingFieldKind;
        field.shape = app.placingFieldShape;
        field.position = p;
        field.angle = 0.0f;
        if (field.kind == ForceFieldKind::Vortex) field.vortex = 20.0f;
        const Color color = forceFieldColor(field.kind);
        drawForceFieldVolume(app, field, color, false, true, true, 30, 160);
        drawHint(ui::format("%s · %s · 固定在世界中", forceFieldKindName(field.kind),
                            forceFieldShapeName(field.shape)),
                 color);
    }
    ui::scissorPop();
}

void drawCanvasOverlay(App& app) {
    const Rectangle c = app.canvasRect();

    Color stateDot = theme::muted;
    std::string stateText = "编辑模式";
    if (!app.paused || app.simTime > 1e-4f) {
        const float ms = app.simTime * 1000.0f;
        const float bpm = app.settings.bpm > 0.0f ? app.settings.bpm : 180.0f;
        const float beatLengthMs = 1000.0f / (bpm / 60.0f);
        const float beats = ms / beatLengthMs;
        const char* prefix = app.paused ? "模拟中途暂停" : "运行中";
        stateText = ui::format("%s  %.0f ms · %.2f beats", prefix, ms, beats);
        stateDot = app.paused ? theme::warn : theme::accent2;
    }
    if (app.doc.slowStepGear > 0)
        stateText += ui::format(" · 缓步 %d 档", app.doc.slowStepGear);
    const float stateWidth = std::max(150.0f, ui::textWidth(stateText, 10.0f) + 36.0f);
    drawPill({c.x + 14, c.y + 12, stateWidth, 26}, stateText, stateDot);

    const std::string zoomText = ui::format("缩放 %.0f%%", app.camera.zoom * 100.0f);
    drawPill({c.x + c.width - 106, c.y + 12, 92, 26}, zoomText, theme::accent, theme::muted);

    if (!app.missingTypes.empty()) {
        std::string names;
        for (size_t i = 0; i < app.missingTypes.size() && i < 3; ++i) {
            if (i) names += ", ";
            names += app.missingTypes[i];
        }
        drawPill({c.x + 24 + stateWidth, c.y + 12, 330, 26},
                 ui::format("%d 个音符引用了缺失类型: %s", (int)app.missingTypes.size(),
                            names.c_str()),
                 theme::danger, theme::danger);
    }

    if (app.paused && app.simTime > 1e-4f) {
        Rectangle banner{c.x + c.width * 0.5f - 175, c.y + 50, 350, 28};
        DrawRectangleRounded(banner, 0.5f, 8, theme::alpha(theme::warn, 24));
        DrawRectangleRoundedLines(banner, 0.5f, 8, theme::alpha(theme::warn, 90));
        ui::textCentered("按 R 重置到编辑布局", banner, 11.0f, theme::text);
    }

    const std::string placeKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::PlaceComponent]);
    const std::string previousKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::PreviousComponent]);
    const std::string nextKey =
        bindingLabel(app.settings.shortcuts[(int)ShortcutAction::NextComponent]);
    std::string hint;
    if (app.tool == Tool::Place && app.selectedTypeName == kRivetType) {
        hint = ui::format("%s 放置%s铆钉（不参与碰撞） · %s/%s 切换 · Esc 退出",
                          placeKey.c_str(), app.placingScene ? "场景" : "物理",
                          previousKey.c_str(), nextKey.c_str());
    } else if (app.tool == Tool::Place && app.placingScene) {
        const NoteType* t = app.activeType();
        hint = ui::format("%s 放置场景块 %s（固定） · %s/%s 切换 · Esc 退出",
                          placeKey.c_str(), t ? t->name.c_str() : "场景块",
                          previousKey.c_str(), nextKey.c_str());
    } else if (app.tool == Tool::Place) {
        const NoteType* t = app.activeType();
        hint = ui::format("%s 放置 %s · %s/%s 切换 · Esc 退出", placeKey.c_str(),
                          t ? t->name.c_str() : "音符", previousKey.c_str(),
                          nextKey.c_str());
    } else if (app.tool == Tool::JudgeLine) {
        hint = ui::format("%s 放置判定线 · 左键选择 · Esc 退出", placeKey.c_str());
    } else if (app.tool == Tool::ForceField) {
        hint = ui::format("%s 放置%s（%s） · 左键选择 · Esc 退出", placeKey.c_str(),
                          forceFieldKindName(app.placingFieldKind),
                          forceFieldShapeName(app.placingFieldShape));
    } else if (!app.selectedNotes.empty()) {
        hint = ui::format("已选择 %d 个对象", (int)app.selectedNotes.size());
    } else if (app.selectedField >= 0) {
        hint = "已选择力场";
    } else if (app.selectedJudge >= 0) {
        hint = "已选择判定线";
    }
    if (!hint.empty()) {
        Rectangle hintRect{c.x + 14, c.y + c.height - 36, ui::textWidth(hint, 10.0f) + 24,
                           26};
        DrawRectangleRounded(hintRect, 0.5f, 8, theme::alpha(theme::panel, 210));
        ui::text(hint, {hintRect.x + 12, hintRect.y + 3}, 10.0f, theme::muted);
    }

}

// Program mark: four note-shaped bars form a diamond with a downward arrow in
// the middle. The same shape is used for the window/executable icon.
void drawAppLogo(Rectangle area) {
    const Vec2 c{area.x + area.width * 0.5f, area.y + area.height * 0.5f};
    const float radius = std::min(area.width, area.height) * 0.31f;
    const Vec2 p[4] = {{c.x, c.y - radius}, {c.x + radius, c.y},
                       {c.x, c.y + radius}, {c.x - radius, c.y}};
    const Color colors[4] = {{125, 179, 247, 255}, {248, 160, 204, 255},
                             {140, 232, 194, 255}, {253, 214, 122, 255}};
    const float thickness = std::max(2.6f, radius * 0.33f);
    for (int i = 0; i < 4; ++i) {
        Vec2 a = p[i];
        Vec2 b = p[(i + 1) % 4];
        const Vec2 dir = normalize(b - a);
        a += dir * (radius * 0.17f);
        b -= dir * (radius * 0.17f);
        DrawLineEx(a, b, thickness, colors[i]);
        DrawCircleV(a, thickness * 0.5f, colors[i]);
        DrawCircleV(b, thickness * 0.5f, colors[i]);
    }
    const float arrowW = radius * 0.34f;
    DrawRectangle((int)(c.x - arrowW * 0.5f), (int)(c.y - radius * 0.42f),
                  (int)arrowW, (int)(radius * 0.55f), theme::text);
    const Vec2 headL{c.x - radius * 0.54f, c.y + radius * 0.18f};
    const Vec2 headR{c.x + radius * 0.54f, c.y + radius * 0.18f};
    const Vec2 headTip{c.x, c.y + radius * 0.78f};
    DrawTriangle(headL, headR, headTip, theme::text);
    DrawTriangle(headL, headTip, headR, theme::text);
}

void drawTopBarInternal(App& app) {
    DrawRectangleGradientH(0, 0, (int)layout::W, (int)layout::topH, theme::topBarTop,
                           theme::topBarBottom);
    DrawLineEx({0, layout::topH - 1}, {layout::W, layout::topH - 1}, 1.0f,
               theme::strokeSoft);

    // Logo.
    const Rectangle logoBg{16, 12, 38, 38};
    DrawRectangleRounded(logoBg, 0.32f, 8, theme::accentDark);
    DrawRectangleRounded({logoBg.x, logoBg.y, logoBg.width, logoBg.height * 0.55f}, 0.32f, 8,
                         theme::alpha(theme::accent, 110));
    drawAppLogo(logoBg);
    ui::text("PhiPhysice", {62, 11}, 15.0f, theme::text);
    ui::text("EDITOR NEO", {63, 32}, 7.5f, theme::dim, 2.0f);


    // Project chip (slightly wider to fit the bigger text).
    const Rectangle projectRect{164, 14, 176, 34};
    const uint32_t projectKey = ui::id("top.project");
    const bool projectHovered = ui::mouseIn(projectRect) && !ui::mouseConsumed();
    const float ph = ui::animate(projectKey, projectHovered);
    if (projectHovered) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    DrawRectangleRounded(projectRect, 0.38f, 8,
                         theme::mix(theme::panelRaised, theme::cardHover, ph));
    DrawRectangleRoundedLines(projectRect, 0.38f, 8, theme::strokeSoft);
    ui::icon(Icon::Folder, {projectRect.x + 7, projectRect.y + 7, 18, 18},
             theme::mix(theme::muted, theme::text, ph));
    const std::string projectName = ui::ellipsize(app.doc.name, 11.0f, 110.0f);
    ui::text(projectName, {projectRect.x + 30, projectRect.y + 8}, 11.0f, theme::textSoft);
    if (app.doc.dirty)
        DrawCircleV({projectRect.x + projectRect.width - 14, projectRect.y + 17}, 3.5f,
                    theme::warn);
    if (projectHovered && ui::mousePressed(0)) {
        app.leftTab = LeftTab::Projects;
        ui::consumeMouse();
    }

    DrawLineEx({352, 14}, {352, 50}, 1.0f, theme::strokeSoft);

    // Tools.
    if (ui::chip("top.tool.select", {366, 17, 68, 30}, "选择", app.tool == Tool::Select,
                 app.tool == Tool::Select ? theme::accent : Color{0, 0, 0, 0}))
        app.setTool(Tool::Select);

    DrawLineEx({444, 14}, {444, 50}, 1.0f, theme::strokeSoft);
    const std::string undoTip =
        "撤销 (" + bindingLabel(app.settings.shortcuts[(int)ShortcutAction::Undo]) + ")";
    const std::string redoTip =
        "重做 (" + bindingLabel(app.settings.shortcuts[(int)ShortcutAction::Redo]) + ")";
    if (ui::iconButton("top.undo", {456, 16, 32, 32}, Icon::Undo, undoTip.c_str(), false,
                       app.doc.canUndo()))
        app.undo();
    if (ui::iconButton("top.redo", {492, 16, 32, 32}, Icon::Redo, redoTip.c_str(), false,
                       app.doc.canRedo()))
        app.redo();

    // Simulation controls.
    const float simX = 640.0f;
    if (ui::iconButton("top.reset", {simX, 16, 34, 34}, Icon::Reset, "重置模拟 (R)")) {
        app.resetSimulation();
        app.notifyInfo("模拟已重置");
    }
    if (ui::iconButton("top.step", {simX + 40, 16, 34, 34}, Icon::Step, "单步模拟"))
        app.requestStep();
    const bool playing = !app.paused;
    if (ui::button("top.play", {simX + 84, 15, 92, 36}, playing ? "暂停" : "播放",
                   ButtonStyle::Primary, true, playing ? Icon::Pause : Icon::Play))
        app.togglePlay();

    const std::string slowLabel =
        app.doc.slowStepGear > 0
            ? ui::format(" · 缓步 %d 档", app.doc.slowStepGear)
            : std::string();
    ui::text(ui::format("重力 %.1f%s", app.doc.gravity, slowLabel.c_str()),
             {simX + 188, 24}, 9.5f, theme::dim);
    ui::text(ui::format("迭代 %d/%d", app.world.velocityIterations,
                        app.world.positionIterations),
             {simX + 188, 40}, 9.5f, theme::dim);
    ui::text(ui::format("场景 %.0f×%.0f", kSceneWidth, kSceneHeight), {simX + 266, 32},
             9.5f, theme::dim);

    // Right actions.
    if (ui::iconButton("top.theme", {1348, 16, 34, 34},
                       app.lightMode ? Icon::Moon : Icon::Sun,
                       app.lightMode ? "切换到深色模式" : "切换到浅色模式")) {
        app.setLightMode(!app.lightMode);
        app.notifyInfo(app.lightMode ? "已切换到浅色模式" : "已切换到深色模式");
    }
    if (ui::iconButton("top.export", {1392, 16, 34, 34}, Icon::Note,
                       "导出预制事件"))
        app.openExportDialog();
    const std::string saveTip =
        "保存工程 (" + bindingLabel(app.settings.shortcuts[(int)ShortcutAction::Save]) + ")";
    const std::string settingsTip = "设置（未选中对象时按 Esc）";
    if (ui::iconButton("top.save", {1436, 16, 34, 34}, Icon::Save, saveTip.c_str()))
        app.saveProjectInteractive();
    if (ui::iconButton("top.settings", {1478, 16, 34, 34}, Icon::Settings,
                       settingsTip.c_str(), app.showSettings))
        app.showSettings = !app.showSettings;
    if (ui::iconButton("top.projects", {1520, 16, 34, 34}, Icon::Layers, "工程列表")) {
        app.leftTab = LeftTab::Projects;
    }
}

} // namespace

Color typeColor(const Rgb& rgb, unsigned char alpha) {
    return Color{rgb.r, rgb.g, rgb.b, alpha};
}

Color groupColor(int group) {
    static const Color palette[] = {{246, 146, 160, 255}, {246, 186, 126, 255},
                                    {240, 222, 140, 255}, {146, 222, 172, 255},
                                    {136, 214, 220, 255}, {150, 178, 246, 255},
                                    {196, 160, 248, 255}};
    const int idx = group <= 0 ? 0 : (group - 1) % 7;
    return palette[idx];
}

Color forceFieldColor(ForceFieldKind kind, unsigned char alpha) {
    Color base{96, 226, 158, 255}; // green gravity
    if (kind == ForceFieldKind::Repulsion) base = {248, 110, 126, 255};
    if (kind == ForceFieldKind::Buoyancy) base = {92, 178, 255, 255};
    if (kind == ForceFieldKind::Drag) base = {156, 164, 178, 255};
    if (kind == ForceFieldKind::GravityChange) base = {250, 205, 72, 255};
    if (kind == ForceFieldKind::Vortex) base = {188, 140, 255, 255};
    return theme::highlight(base, alpha);
}

namespace {

std::vector<Vec2> forceFieldOutline(const ForceField& field) {
    std::vector<Vec2> out;
    if (field.shape == ForceFieldShape::Circle) {
        const int steps = 72;
        for (int i = 0; i < steps; ++i) {
            const float a = (float)i / (float)steps * TWO_PI;
            out.push_back({std::cos(a) * field.radius, std::sin(a) * field.radius});
        }
        return out;
    }
    const float hw = field.width * 0.5f;
    const float hh = field.height * 0.5f;
    out.push_back({-hw, -hh});
    out.push_back({hw, -hh});
    out.push_back({hw, hh});
    out.push_back({-hw, hh});
    return out;
}

void drawForceFieldVolume(App& app, const ForceField& field, Color base, bool selected,
                          bool ghost, bool arrows, float fillAlpha, float borderAlpha) {
    const std::vector<Vec2> local = forceFieldOutline(field);
    if (local.size() < 3) return;
    const Vec2 center = app.designToScreen(field.position);
    std::vector<Vec2> screen;
    screen.reserve(local.size());
    Vec2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    for (const auto& p : local) {
        const Vec2 s = app.designToScreen(field.position + rotate(p, field.angle));
        screen.push_back(s);
        lo.x = std::min(lo.x, s.x);
        lo.y = std::min(lo.y, s.y);
        hi.x = std::max(hi.x, s.x);
        hi.y = std::max(hi.y, s.y);
    }
    const Color fill = theme::alpha(base, (unsigned char)fillAlpha);
    for (size_t i = 0; i < screen.size(); ++i)
        DrawTriangle(center, screen[i], screen[(i + 1) % screen.size()], fill);

    const bool directionalField = field.kind == ForceFieldKind::Buoyancy ||
                                  field.kind == ForceFieldKind::GravityChange;
    if (directionalField && arrows) {
        // Both buoyancy and gravity-change fields keep an editable direction.
        Vec2 dir = normalize(field.buoyancyDirection);
        if (lengthSq(dir) < 0.5f) dir = {0.0f, 1.0f};
        const Vec2 screenDir{dir.x, -dir.y};
        const Color arrowColor =
            theme::alpha(theme::mix(base, Color{0, 0, 0, 255}, 0.34f), 120);
        const float hw = field.shape == ForceFieldShape::Circle ? field.radius
                                                                : field.width * 0.5f;
        const float hh = field.shape == ForceFieldShape::Circle ? field.radius
                                                                : field.height * 0.5f;
        // Keep the arrow grid light for very large fields: at most ~28 rows /
        // columns, while small fields keep the fixed 50 px spacing.
        const float spanX = field.shape == ForceFieldShape::Circle ? field.radius * 2.0f
                                                                   : field.width;
        const float spanY = field.shape == ForceFieldShape::Circle ? field.radius * 2.0f
                                                                   : field.height;
        const float spacing = std::max(50.0f, std::max(spanX, spanY) / 28.0f);
        const float limitSq = field.shape == ForceFieldShape::Circle
                                  ? std::max(0.0f, field.radius - 14.0f) *
                                        std::max(0.0f, field.radius - 14.0f)
                                  : 0.0f;
        for (float y = -hh + spacing * 0.5f; y <= hh; y += spacing) {
            for (float x = -hw + spacing * 0.5f; x <= hw; x += spacing) {
                if (field.shape == ForceFieldShape::Circle && x * x + y * y > limitSq)
                    continue;
                const Vec2 s =
                    app.designToScreen(field.position + rotate(Vec2{x, y}, field.angle));
                const Vec2 shaft = screenDir * (18.0f * app.camera.zoom);
                const Vec2 a = s - shaft * 0.55f;
                const Vec2 b = s + shaft * 0.55f;
                DrawLineEx(a, b, 1.8f, arrowColor);
                const Vec2 n = normalize(perp(shaft)) * (5.0f * app.camera.zoom);
                const Vec2 head = b - shaft * 0.60f;
                DrawLineEx(b, head + n, 1.8f, arrowColor);
                DrawLineEx(b, head - n, 1.8f, arrowColor);
            }
        }
    }

    if (field.kind == ForceFieldKind::Vortex && arrows &&
        std::fabs(field.vortex) > 1e-4f) {
        // Vortex arrows follow the local tangent so the grid itself shows the
        // orbit direction: positive strength turns clockwise, negative reverses
        // the arrows. The tangent is computed in design space (Y-up) and then
        // converted to screen space like every other overlay vector.
        const float sign = field.vortex < 0.0f ? -1.0f : 1.0f;
        const Color arrowColor =
            theme::alpha(theme::mix(base, Color{0, 0, 0, 255}, 0.34f), 130);
        const float hw = field.shape == ForceFieldShape::Circle ? field.radius
                                                                : field.width * 0.5f;
        const float hh = field.shape == ForceFieldShape::Circle ? field.radius
                                                                : field.height * 0.5f;
        const float spanX = field.shape == ForceFieldShape::Circle ? field.radius * 2.0f
                                                                   : field.width;
        const float spanY = field.shape == ForceFieldShape::Circle ? field.radius * 2.0f
                                                                   : field.height;
        const float spacing = std::max(50.0f, std::max(spanX, spanY) / 28.0f);
        const float limitSq = field.shape == ForceFieldShape::Circle
                                  ? std::max(0.0f, field.radius - 14.0f) *
                                        std::max(0.0f, field.radius - 14.0f)
                                  : 0.0f;
        for (float y = -hh + spacing * 0.5f; y <= hh; y += spacing) {
            for (float x = -hw + spacing * 0.5f; x <= hw; x += spacing) {
                if (field.shape == ForceFieldShape::Circle && x * x + y * y > limitSq)
                    continue;
                Vec2 tangent = normalize(Vec2{y, -x}) * sign;
                if (lengthSq(tangent) < 0.5f) continue;
                tangent = rotate(tangent, field.angle);
                const Vec2 screenDir{tangent.x, -tangent.y};
                const Vec2 s =
                    app.designToScreen(field.position + rotate(Vec2{x, y}, field.angle));
                const Vec2 shaft = screenDir * (18.0f * app.camera.zoom);
                const Vec2 a = s - shaft * 0.55f;
                const Vec2 b = s + shaft * 0.55f;
                DrawLineEx(a, b, 1.8f, arrowColor);
                const Vec2 n = normalize(perp(shaft)) * (5.0f * app.camera.zoom);
                const Vec2 head = b - shaft * 0.60f;
                DrawLineEx(b, head + n, 1.8f, arrowColor);
                DrawLineEx(b, head - n, 1.8f, arrowColor);
            }
        }
    }

    // Keep the field's semantic colour (green / red / blue) even while selected;
    // selection is shown separately with accent brackets.
    const Color border = theme::alpha(base, (unsigned char)borderAlpha);
    const float thickness = selected ? 2.6f : 1.6f;
    for (size_t i = 0; i < screen.size(); ++i)
        DrawLineEx(screen[i], screen[(i + 1) % screen.size()], thickness, border);

    if (!ghost) {
        const std::string label = ui::format("%s #%d", forceFieldKindName(field.kind), field.id);
        ui::text(label, {lo.x + 2, lo.y - 15.0f}, 9.0f, selected ? theme::text : theme::muted);
    }
    if (selected) {
        drawSelectionBrackets({lo.x, lo.y, hi.x - lo.x, hi.y - lo.y},
                              theme::highlight(theme::accent));
        DrawCircleV(center, 3.5f, theme::highlight(theme::accent));
    }
}

} // namespace

void drawForceFieldPreview(ForceFieldKind kind, ForceFieldShape shape, Rectangle area,
                           bool active) {
    const Color color = forceFieldColor(kind);
    const Vec2 c{area.x + area.width * 0.5f, area.y + area.height * 0.5f};
    const float pad = 10.0f;
    const bool directional = kind == ForceFieldKind::Buoyancy ||
                             kind == ForceFieldKind::GravityChange;
    const bool vortex = kind == ForceFieldKind::Vortex;
    DrawRectangleRounded(area, 0.16f, 8, theme::alpha(color, (unsigned char)(active ? 24 : 14)));
    auto drawSwirl = [&](float radius) {
        if (radius < 4.0f) return;
        const Color mark = theme::alpha(color, 160);
        const int count = 8;
        for (int i = 0; i < count; ++i) {
            const float a = (float)i / (float)count * TWO_PI;
            const Vec2 p{c.x + std::cos(a) * radius, c.y + std::sin(a) * radius};
            // Screen-space clockwise tangent (screen Y points down).
            const Vec2 tangent{-std::sin(a), std::cos(a)};
            const Vec2 shaft = tangent * (std::min(7.0f, radius * 0.35f));
            const Vec2 from = p - shaft * 0.5f;
            const Vec2 to = p + shaft * 0.5f;
            DrawLineEx(from, to, 1.6f, mark);
            const Vec2 n = perp(tangent) * 2.2f;
            DrawLineEx(to, to - shaft * 0.45f + n, 1.6f, mark);
            DrawLineEx(to, to - shaft * 0.45f - n, 1.6f, mark);
        }
    };
    if (shape == ForceFieldShape::Circle) {
        const float r = std::min(area.width, area.height) * 0.5f - pad;
        DrawCircleV(c, r, theme::alpha(color, 30));
        DrawCircleLines((int)c.x, (int)c.y, r, theme::alpha(color, 210));
        if (directional) {
            for (int i = -1; i <= 1; ++i)
                DrawLineEx({c.x - 9 + i * 3.0f, c.y + 8}, {c.x + i * 3.0f, c.y - 8}, 1.6f,
                           theme::alpha(color, 150));
        }
        if (vortex) drawSwirl(r * 0.62f);
    } else {
        const Rectangle box{c.x - (area.width * 0.5f - pad), c.y - (area.height * 0.34f),
                            area.width - pad * 2.0f, area.height * 0.68f};
        DrawRectangleRounded(box, 0.12f, 6, theme::alpha(color, 30));
        DrawRectangleRoundedLines(box, 0.12f, 6, theme::alpha(color, 210));
        if (directional) {
            for (int i = -1; i <= 1; ++i)
                DrawLineEx({c.x - 9 + i * 3.0f, c.y + 8}, {c.x + i * 3.0f, c.y - 8}, 1.6f,
                           theme::alpha(color, 150));
        }
        if (vortex) drawSwirl(std::min(area.width, area.height) * 0.26f);
    }
}

void drawPolyPreview(const Poly& vertices, Rectangle area, Color color) {
    if (vertices.size() < 3) return;
    float maxR = 1.0f;
    for (const auto& v : vertices) maxR = std::max(maxR, length(v));
    const float scale = std::min((area.width - 12.0f) / (2.0f * maxR),
                                 (area.height - 12.0f) / (2.0f * maxR));
    const Vec2 center{area.x + area.width * 0.5f, area.y + area.height * 0.5f};
    const std::vector<Poly> pieces = World::triangulate(vertices);
    // Preview widgets are Y-up too: screen Y is inverted while fitting.
    for (const auto& tri : pieces) {
        const Vec2 a{center.x + tri[0].x * scale, center.y - tri[0].y * scale};
        const Vec2 b{center.x + tri[1].x * scale, center.y - tri[1].y * scale};
        const Vec2 c{center.x + tri[2].x * scale, center.y - tri[2].y * scale};
        DrawTriangle(a, b, c, withAlpha(color, 85));
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        const Vec2 a{center.x + vertices[i].x * scale, center.y - vertices[i].y * scale};
        const Vec2 b{center.x + vertices[(i + 1) % vertices.size()].x * scale,
                     center.y - vertices[(i + 1) % vertices.size()].y * scale};
        DrawLineEx(a, b, 1.4f, withAlpha(color, 225));
    }
    DrawCircleV(center, 1.4f, withAlpha(color, 180));
}

void drawTypePreview(const NoteType& type, Rectangle area, Color color) {
    const Poly& verts = type.vertices;
    drawPolyPreview(verts, area, color);
}

void drawPill(Rectangle rect, const std::string& text, Color dotColor, Color textColor) {
    DrawRectangleRounded(rect, 0.5f, 8, theme::alpha(theme::panel, 225));
    DrawRectangleRoundedLines(rect, 0.5f, 8, theme::strokeSoft);
    DrawCircleV({rect.x + 12, rect.y + rect.height * 0.5f}, 3.2f, dotColor);
    ui::text(ui::ellipsize(text, 10.0f, rect.width - 30), {rect.x + 22, rect.y + 5}, 10.0f,
             theme::text);
}

void drawSectionTitle(const std::string& title, const std::string& tag, float x, float y,
                      float width) {
    DrawRectangleRounded({x, y + 4, 3, 11}, 0.5f, 4, theme::accent);
    // Regular weight keeps CJK section headings intact at every atlas tier;
    // a slightly brighter colour preserves the visual hierarchy.
    ui::text(title, x + 9, y, 11.5f, theme::text);
    if (!tag.empty())
        ui::text(tag, {x + width - ui::textWidth(tag, 8.0f, 1.0f), y + 3}, 8.0f, theme::dim,
                 1.0f);
}

void drawSelectionBrackets(Rectangle r, Color color) {
    const float pad = 5.0f;
    const float len = 10.0f;
    const float x0 = r.x - pad, y0 = r.y - pad;
    const float x1 = r.x + r.width + pad, y1 = r.y + r.height + pad;
    const float th = 1.8f;
    DrawLineEx({x0, y0}, {x0 + len, y0}, th, color);
    DrawLineEx({x0, y0}, {x0, y0 + len}, th, color);
    DrawLineEx({x1, y0}, {x1 - len, y0}, th, color);
    DrawLineEx({x1, y0}, {x1, y0 + len}, th, color);
    DrawLineEx({x0, y1}, {x0 + len, y1}, th, color);
    DrawLineEx({x0, y1}, {x0, y1 - len}, th, color);
    DrawLineEx({x1, y1}, {x1 - len, y1}, th, color);
    DrawLineEx({x1, y1}, {x1, y1 - len}, th, color);
}

void drawDashedLine(Vec2 a, Vec2 b, float dash, float thickness, Color color) {
    const Vec2 d = b - a;
    const float len = length(d);
    if (len <= 0.01f) return;
    const Vec2 dir = d / len;
    float t = 0.0f;
    while (t < len) {
        const float end = std::min(t + dash, len);
        DrawLineEx(a + dir * t, a + dir * end, thickness, color);
        t += dash * 1.8f;
    }
}

void drawDesignPolygon(App& app, const Poly& design, Vec2 origin, Color fill, Color outline,
                       float thickness) {
    if (design.size() < 3) return;
    std::vector<Vec2> screen;
    screen.reserve(design.size());
    for (const auto& v : design) screen.push_back(app.designToScreen(origin + v));
    for (size_t i = 1; i + 1 < screen.size(); ++i)
        DrawTriangle(screen[0], screen[i], screen[i + 1], fill);
    for (size_t i = 0; i < screen.size(); ++i)
        DrawLineEx(screen[i], screen[(i + 1) % screen.size()], thickness, outline);
}

void drawCopySourceDim(App& app) {
    const Color fill{104, 110, 122, 185};
    const Color outline{168, 174, 188, 220};
    if (app.isEventCopySource(EventEditorState::Target::Note,
                              app.eventCopySourceComponentId())) {
        if (const Note* note = app.doc.findNote(app.eventCopySourceComponentId())) {
            Poly local = app.noteVertices(*note);
            for (auto& v : local) v = rotate(v, note->angle);
            const std::vector<Poly> pieces = World::triangulate(local);
            for (const Poly& piece : pieces) {
                std::vector<Vec2> screen;
                screen.reserve(piece.size());
                for (const Vec2& v : piece)
                    screen.push_back(app.designToScreen(note->position + v));
                if (screen.size() >= 3) {
                    DrawTriangle(screen[0], screen[1], screen[2], fill);
                    for (size_t i = 0; i < screen.size(); ++i)
                        DrawLineEx(screen[i], screen[(i + 1) % screen.size()], 1.5f,
                                   outline);
                }
            }
        }
    } else if (app.isEventCopySource(EventEditorState::Target::Judge,
                                     app.eventCopySourceComponentId())) {
        if (const JudgeLine* judge = app.doc.findJudge(app.eventCopySourceComponentId())) {
            Poly local{{-judge->width * 0.5f, -judge->height * 0.5f},
                       {judge->width * 0.5f, -judge->height * 0.5f},
                       {judge->width * 0.5f, judge->height * 0.5f},
                       {-judge->width * 0.5f, judge->height * 0.5f}};
            for (auto& v : local) v = rotate(v, judge->angle) + judge->position;
            drawDesignPolygon(app, local, {0, 0}, fill, outline, 2.0f);
        }
    } else if (app.isEventCopySource(EventEditorState::Target::Field,
                                     app.eventCopySourceComponentId())) {
        if (const ForceField* field = app.doc.findField(app.eventCopySourceComponentId())) {
            if (field->shape == ForceFieldShape::Circle) {
                const Vec2 center = app.designToScreen(field->position);
                const float radius = field->radius * app.camera.zoom;
                DrawCircleV(center, radius, fill);
                DrawCircleLines((int)center.x, (int)center.y, radius, outline);
            } else {
                Poly local{{-field->width * 0.5f, -field->height * 0.5f},
                           {field->width * 0.5f, -field->height * 0.5f},
                           {field->width * 0.5f, field->height * 0.5f},
                           {-field->width * 0.5f, field->height * 0.5f}};
                for (auto& v : local) v = rotate(v, field->angle) + field->position;
                drawDesignPolygon(app, local, {0, 0}, fill, outline, 2.0f);
            }
        }
    }
}

void drawCanvas(App& app) {
    const Rectangle c = app.canvasRect();
    DrawRectangleRec(c, theme::canvas);
    DrawRectangleGradientV((int)c.x, (int)c.y, (int)c.width, (int)c.height,
                           theme::canvasTop, theme::canvasBottom);

    const Vec2 origin = app.designToScreen({0, 0});
    if (origin.x > c.x - 300 && origin.x < c.x + c.width + 300)
        DrawCircleGradient((int)origin.x, (int)origin.y, 360.0f,
                           theme::alpha(theme::accent, 12), Color{0, 0, 0, 0});

    drawGrid(app);
    // The read-only display frame goes under the content so notes stay visible.
    drawSceneFrame(app);

    // Paused scenes dim only the backdrop. Notes, judge lines and force fields
    // keep their full colours so the stage stays readable while paused.
    if (app.paused) {
        DrawRectangleRec(c, theme::alpha(theme::modalDim, theme::light ? 46 : 52));
    }

    drawForceFields(app);

    for (const auto& judge : app.doc.judges)
        drawJudgeLine(app, judge, app.selectedJudge == judge.id);

    for (const auto& note : app.doc.notes) {
        const bool selected =
            std::find(app.selectedNotes.begin(), app.selectedNotes.end(), note.id) !=
            app.selectedNotes.end();
        if (isRivetNote(note))
            drawRivet(app, note, selected, app.hoveredNote == note.id);
        else
            drawNote(app, note, selected, app.hoveredNote == note.id);
    }

    drawJoints(app);
    drawContacts(app);
    drawGhost(app);
    if (app.eventCopyOverlayActive()) drawCopySourceDim(app);

    if (app.selectionBoxActive) {
        DrawRectangleRec(app.selectionBoxScreen,
                         theme::alpha(theme::highlight(theme::accent), 25));
        DrawRectangleLinesEx(app.selectionBoxScreen, 1.2f,
                             theme::alpha(theme::highlight(theme::accent), 170));
    }

    drawCanvasOverlay(app);

    DrawLineEx({c.x, c.y}, {c.x, c.y + c.height}, 1.0f, theme::strokeSoft);
    DrawLineEx({c.x, c.y}, {c.x + c.width, c.y}, 1.0f, theme::strokeSoft);
    DrawLineEx({c.x + c.width, c.y}, {c.x + c.width, c.y + c.height}, 1.0f,
               theme::strokeSoft);
}

void drawTopBar(App& app) { drawTopBarInternal(app); }

void drawBottomBar(App& app) {
    const Rectangle bar = layout::bottom();
    DrawRectangleRec(bar, theme::panel);
    DrawLineEx({0, bar.y}, {layout::W, bar.y}, 1.0f, theme::strokeSoft);

    const Color dot = app.paused ? (app.simTime > 1e-4f ? theme::warn : theme::muted)
                                 : theme::accent2;
    DrawCircleV({16, bar.y + bar.height * 0.5f}, 3.0f, dot);
    std::string status = app.statusMessage;
    if (status.empty()) status = app.paused ? "就绪" : "模拟中";
    status = ui::ellipsize(status, 10.0f, 440.0f);
    ui::text(status, {25, bar.y + 7}, 10.0f, theme::muted);

    const std::string coords =
        app.mouseInCanvas ? ui::format("X %.0f   Y %.0f", app.mouseDesign.x, app.mouseDesign.y)
                          : "X --   Y --";
    ui::text(coords, {560, bar.y + 7}, 10.0f, theme::textSoft);
    ui::text(ui::format("缩放 %.0f%%", app.camera.zoom * 100.0f), {724, bar.y + 7}, 10.0f,
             theme::dim);

    const int jointCount = (int)app.doc.revJoints.size() + (int)app.doc.weldJoints.size();
    int noteCount = 0;
    int rivetCount = 0;
    for (const auto& note : app.doc.notes) {
        if (isRivetNote(note))
            ++rivetCount;
        else
            ++noteCount;
    }
    const std::string stats =
        ui::format("音符 %d   铆钉 %d   判定线 %d   力场 %d   关节 %d   接触 %d",
                   noteCount, rivetCount, (int)app.doc.judges.size(),
                   (int)app.doc.fields.size(), jointCount, (int)app.world.contacts.size());
    ui::text(ui::ellipsize(stats, 10.0f, 330.0f), {916, bar.y + 7}, 10.0f, theme::dim);
    ui::text(ui::format("%d FPS", GetFPS()), {1256, bar.y + 7}, 10.0f, theme::dim);
    ui::text("Esc 设置", {1508, bar.y + 7}, 10.0f, theme::dim);
}

void draw(App& app) {
    DrawRectangleGradientV(0, 0, (int)layout::W, (int)layout::H, theme::bg, theme::bgGlow);

    ui::setInteractionEnabled(!app.isModalOpen());

    drawCanvas(app);
    drawLeftPanel(app);
    drawRightPanel(app);
    drawTopBar(app);
    drawBottomBar(app);

    ui::setInteractionEnabled(true);
    if (app.eventEditor.open)
        drawEventEditor(app);
    else if (app.shape.open)
        drawShapeEditor(app);
    else if (app.showExportDialog)
        drawExportDialog(app);
    else if (app.showSettings)
        drawSettings(app);
    else if (app.projectDialog != ProjectDialogMode::None)
        drawProjectDialog(app);

    drawToasts(app);

    if (!ui::mouseDown(0)) {
        app.inspectorUndoPushed = false;
    }
}

} // namespace ppn::editorui