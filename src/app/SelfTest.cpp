#include "app/SelfTest.h"

#include "app/App.h"
#include "app/Settings.h"
#include "io/ProjectIO.h"
#include "model/Document.h"
#include "model/Easing.h"
#include "model/Events.h"
#include "model/NoteType.h"
#include "physics/Physics.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace ppn {
namespace {

int passCount = 0;
int failCount = 0;

void check(bool condition, const std::string& name, const std::string& detail = "") {
    if (condition) {
        ++passCount;
        std::printf("  [PASS] %s\n", name.c_str());
    } else {
        ++failCount;
        std::printf("  [FAIL] %s%s%s\n", name.c_str(), detail.empty() ? "" : " - ",
                    detail.c_str());
    }
}

fs::path componentsRoot() {
    fs::path path = fs::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        std::error_code ec;
        if (fs::exists(path / "components" / "physics" / "Tap.note", ec))
            return path / "components";
        if (!path.has_parent_path()) break;
        const fs::path parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return {};
}

bool loadStoredNote(const char* folder, const char* name, NoteType& out,
                    std::string& error) {
    const fs::path root = componentsRoot();
    if (root.empty()) {
        error = "找不到 components 目录";
        return false;
    }
    return parseNoteTypeFile(root / folder / (std::string(name) + ".note"), out, error);
}

Body* addBox(World& world, BodyType type, Vec2 position, Vec2 halfExtents,
             float density = 1.0f) {
    Body* body = world.createBody();
    body->type = type;
    body->position = position;
    Poly box{{-halfExtents.x, -halfExtents.y}, {halfExtents.x, -halfExtents.y},
             {halfExtents.x, halfExtents.y}, {-halfExtents.x, halfExtents.y}};
    body->setOutline(box, density);
    if (type == BodyType::Static) {
        body->mass = body->inertia = 0.0f;
        body->invMass = body->invInertia = 0.0f;
        body->awake = false;
    }
    body->updateAABB();
    return body;
}

void testPolygon() {
    std::printf("Polygon geometry\n");
    Poly star{{80, 0}, {26, 24}, {0, 78}, {-26, 24}, {-80, 0},
              {-26, -24}, {0, -78}, {26, -24}};
    std::string error;
    const bool normalized = normalizePolygon(star, &error);
    check(normalized, "normalize concave star", error);
    if (!normalized) return;
    const float area = std::fabs(polygonSignedArea(star));
    check(area > 1000.0f, "star area positive");
    check(isSimplePolygon(star), "star is simple");
    const auto pieces = World::triangulate(star);
    float pieceArea = 0.0f;
    for (const auto& tri : pieces) pieceArea += std::fabs(polygonSignedArea(tri));
    check(!pieces.empty(), "star triangulates");
    check(std::fabs(pieceArea - area) < area * 0.005f + 0.01f,
          "triangle areas sum to polygon area",
          std::to_string(pieceArea) + " vs " + std::to_string(area));

    Poly selfIntersecting{{0, 0}, {100, 100}, {100, 0}, {0, 100}};
    check(!normalizePolygon(selfIntersecting), "self intersecting polygon rejected");

    Poly coincident{{0, 0}, {100, 0}, {100, 0}, {100, 40}, {0, 40}};
    check(normalizePolygon(coincident) && coincident.size() == 5,
          "coincident note nodes are preserved");
}

void testHoldGeometry() {
    std::printf("Hold geometry\n");
    NoteType hold;
    NoteType tap;
    std::string loadError;
    const bool loaded = loadStoredNote("physics", "Hold", hold, loadError) &&
                        loadStoredNote("physics", "Tap", tap, loadError);
    check(loaded, "load stored hold and tap notes", loadError);
    if (!loaded) return;
    check(hold.isHold(), "stored hold type is a hold note");
    check(hold.vertices.size() == 8, "hold has eight vertices");

    auto bounds = [](const Poly& poly) {
        float loX = poly[0].x, hiX = poly[0].x, loY = poly[0].y, hiY = poly[0].y;
        for (const auto& v : poly) {
            loX = std::min(loX, v.x);
            hiX = std::max(hiX, v.x);
            loY = std::min(loY, v.y);
            hiY = std::max(hiY, v.y);
        }
        return Vec2{hiX - loX, hiY - loY};
    };
    const Poly rendered = hold.renderVertices();
    const HoldGeometry g = extractHoldGeometry(hold.vertices);
    check(rendered.size() == 8, "hold render keeps eight vertices");
    check(g.valid && g.topIds.size() == 4 && g.bottomIds.size() == 4,
          "hold splits into four top and four bottom anchors");
    const Poly stretched = hold.renderVertices({{"height", 120.0f}});
    const Vec2 stretchedCentroid = polygonCentroid(stretched);
    float renderedMinY = rendered[0].y;
    float stretchedMinY = stretched[0].y;
    float stretchedMaxY = stretched[0].y;
    for (const auto& v : rendered) renderedMinY = std::min(renderedMinY, v.y);
    for (const auto& v : stretched) {
        stretchedMinY = std::min(stretchedMinY, v.y);
        stretchedMaxY = std::max(stretchedMaxY, v.y);
    }
    check(std::fabs(stretchedMinY - renderedMinY) < 0.01f,
          "hold bottom edge stays fixed when height changes");
    check(stretchedCentroid.y > polygonCentroid(rendered).y,
          "hold height raises the centre of gravity");
    check(std::fabs(stretchedCentroid.y - (stretchedMinY + stretchedMaxY) * 0.5f) < 0.01f,
          "hold centre of gravity stays in the middle of the stretched shape");
    check(std::fabs(stretched[0].y - rendered[0].y - 120.0f) < 0.01f &&
              std::fabs(stretched[5].y - rendered[5].y - 120.0f) < 0.01f &&
              std::fabs(stretched[6].y - rendered[6].y - 120.0f) < 0.01f &&
              std::fabs(stretched[7].y - rendered[7].y - 120.0f) < 0.01f,
          "hold nodes 0/5/6/7 move up by the height value");
    check(std::fabs(stretched[2].y - rendered[2].y) < 0.01f &&
              std::fabs(stretched[3].y - rendered[3].y) < 0.01f,
          "hold bottom nodes do not move");

    const Vec2 holdSize = bounds(rendered);
    const Vec2 tapSize = bounds(tap.vertices);
    check(std::fabs(holdSize.x - tapSize.x) < 0.5f &&
              std::fabs(holdSize.y - tapSize.y) < 0.5f,
          "stored hold silhouette matches a tap note",
          std::to_string(holdSize.x) + "x" + std::to_string(holdSize.y));

    Poly normalized = hold.vertices;
    normalizePolygon(normalized);
    check(normalized.size() == 8, "stored hold vertices are valid");
}

void testExpressions() {
    std::printf("Variables and expressions\n");
    std::vector<NoteVariable> variables{
        {"height", "高度", 120.0f}, {"width", "宽度", 200.0f}};
    float value = 0.0f;
    std::string error;
    check(evaluateNoteExpression("height*0.5+10", variables, value, &error) &&
              std::fabs(value - 70.0f) < 0.01f,
          "expression uses variables and precedence", error);
    check(evaluateNoteExpression("clamp(width, 0, 100)", variables, value, &error) &&
              std::fabs(value - 100.0f) < 0.01f,
          "expression supports clamp()", error);
    check(evaluateNoteExpression("sqrt(16)+abs(-2)", variables, value, &error) &&
              std::fabs(value - 6.0f) < 0.01f,
          "expression supports functions", error);
    check(!evaluateNoteExpression("missing+1", variables, value, &error),
          "unknown variable is rejected");
    check(validVariableName("height") && validVariableName("_h2") &&
              !validVariableName("2h") && !validVariableName("高度"),
          "variable name validation");

    Poly base{{0, 0}, {100, 0}, {100, 40}, {0, 40}};
    std::vector<NodeBinding> bindings{{2, "this+width/2", "this+height"},
                                      {3, "this", "this-height"}};
    std::vector<std::string> errors;
    const Poly moved = applyNodeBindings(base, bindings, variables, &errors);
    check(errors.empty() && std::fabs(moved[2].x - 200.0f) < 0.01f &&
              std::fabs(moved[2].y - 160.0f) < 0.01f,
          "node bindings override coordinates and expose this");
    check(std::fabs(moved[3].y + 80.0f) < 0.01f,
          "negative expressions move nodes down");

    NoteType type;
    type.vertices = {{0, 0}, {100, 0}, {100, 40}, {0, 40}};
    type.variables = {{"height", "高度", 10.0f}};
    type.bindings = {{2, "this", "height"}};
    const Poly withDefault = type.renderVertices();
    const Poly withOverride = type.renderVertices({{"height", 30.0f}});
    check(std::fabs(withDefault[2].y - withDefault[0].y - 10.0f) < 0.01f &&
              std::fabs(withDefault[2].x - 100.0f) < 0.01f,
          "type default variable overrides the node coordinate");
    check(std::fabs(withOverride[2].y - withOverride[0].y - 30.0f) < 0.01f,
          "per-note variable override drives geometry");
    const Vec2 defaultCentroid = polygonCentroid(withDefault);
    const Vec2 overrideCentroid = polygonCentroid(withOverride);
    check(lengthSq(withDefault[0]) < 0.0001f && lengthSq(withOverride[0]) < 0.0001f,
          "node bindings leave the authored origin fixed");
    check(defaultCentroid.y > 0.0f && overrideCentroid.y > defaultCentroid.y,
          "changing a variable moves the centre of gravity independently");
}

void testNoteFiles(const fs::path& dir) {
    std::printf("Note file format\n");
    NoteType type;
    type.name = "TestShape";
    type.vertices = {{-50, -10}, {50, -10}, {60, 0}, {50, 10}, {-50, 10}, {-60, 0}};
    type.density = 1.25f;
    type.friction = 0.4f;
    type.restitution = 0.2f;
    type.color = {10, 20, 30};
    std::string error;
    const fs::path file = dir / "TestShape.note";
    check(writeNoteTypeFile(file, type, error), "write .note file", error);
    check(fs::exists(file), "file exists after write");

    NoteType loaded;
    check(parseNoteTypeFile(file, loaded, error), "parse .note file", error);
    check(loaded.name == type.name, "name round trip");
    check(loaded.vertices.size() == type.vertices.size(), "vertex count round trip");
    check(std::fabs(loaded.density - type.density) < 0.001f, "density round trip");
    check(loaded.color.r == 10 && loaded.color.g == 20 && loaded.color.b == 30,
          "color round trip");
    // Hold files may carry variables and bindings; start from the stored file.
    NoteType hold;
    check(loadStoredNote("physics", "Hold", hold, error),
          "load stored hold before writing", error);
    check(writeNoteTypeFile(dir / "HoldThis.note", hold, error),
          "write stored hold bindings that use this", error);
    hold.variables = {{"height", "高度", 120.0f}, {"width", "宽度", 200.0f}};
    hold.bindings = {{4, "0", "height"}, {5, "width/2", "0"}};
    const fs::path holdFile = dir / "Hold.note";
    check(writeNoteTypeFile(holdFile, hold, error), "write hold .note file", error);
    NoteType holdLoaded;
    check(parseNoteTypeFile(holdFile, holdLoaded, error), "parse hold .note file", error);
    check(holdLoaded.isHold(), "hold type round trip");
    check(holdLoaded.variables.size() == 2 && holdLoaded.variables[0].name == "height" &&
              std::fabs(holdLoaded.variables[0].value - 120.0f) < 0.01f,
          "note variables round trip");
    check(holdLoaded.bindings.size() == 2 && holdLoaded.bindings[1].vertex == 5 &&
              holdLoaded.bindings[1].x == "width/2",
          "node bindings round trip");

    // Only the four supported note kinds are listed by the library.
    check(NoteLibrary::supportedName("Tap") && NoteLibrary::supportedName("Drag") &&
              NoteLibrary::supportedName("Flick") && NoteLibrary::supportedName("Hold"),
          "four supported note kinds");
    check(!NoteLibrary::supportedName("Bar") && !NoteLibrary::supportedName("Star") &&
              !NoteLibrary::supportedName("TestShape"),
          "other note kinds are rejected");
    NoteLibrary library;
    library.load(dir);
    for (const auto& t : library.types)
        check(t.name == "Hold", "library only lists supported kinds", t.name);
}

void testLegacyNoteMigration(const fs::path& dir) {
    std::printf("Legacy Y-down note migration\n");
    const fs::path tapFile = dir / "Tap.note";
    {
        std::ofstream out(tapFile);
        out << "# PhiPhysiceEditorNeo shape v1\n";
        out << "name Tap\nvertices 6\n";
        out << "v -95 -10\nv 95 -10\nv 100 0\nv 95 10\nv -95 10\nv -100 0\n";
        out << "density 1\nfriction 0.42\nrestitution 0.1\ncolor 125 179 247\n";
    }
    NoteType tap;
    std::string error;
    check(parseNoteTypeFile(tapFile, tap, error), "parse legacy Y-down note", error);
    bool flipped = tap.vertices.size() == 6;
    for (const auto& v : tap.vertices) {
        if (v.y < -12.0f || v.y > 12.0f) flipped = false;
    }
    check(tap.needsRewrite, "legacy note is marked for rewrite");
    check(flipped, "legacy note vertices are converted to Y-up");

    // Loading must be read-only even for legacy Y-down files.
    const fs::path readOnlyDir = dir / "readonly_legacy";
    std::error_code ec;
    fs::create_directories(readOnlyDir, ec);
    const fs::path readOnlyFile = readOnlyDir / "Tap.note";
    fs::copy_file(tapFile, readOnlyFile, fs::copy_options::overwrite_existing, ec);
    NoteLibrary legacyLibrary;
    legacyLibrary.folder = readOnlyDir;
    check(!ec && legacyLibrary.load(readOnlyDir), "load legacy note library");
    std::ifstream legacyIn(readOnlyFile);
    std::string legacyHeader;
    std::getline(legacyIn, legacyHeader);
    check(legacyHeader.find("v1") != std::string::npos,
          "loading does not rewrite legacy note file", legacyHeader);

    const fs::path holdFile = dir / "HoldLegacy.note";
    {
        std::ofstream out(holdFile);
        out << "# PhiPhysiceEditorNeo shape v1\n";
        out << "name Hold\nvertices 6\n";
        out << "v -95 -10\nv 95 -10\nv 100 0\nv 95 10\nv -95 10\nv -100 0\n";
        out << "height 30\ncolor 140 232 194\n";
    }
    NoteType hold;
    check(parseNoteTypeFile(holdFile, hold, error), "parse legacy six-vertex Hold", error);
    check(hold.isHold() && hold.vertices.size() == 6,
          "legacy Hold keeps its custom six-vertex outline");
    check(hold.variables.empty(), "legacy height field is ignored");
}

void testPhysics() {
    std::printf("Physics engine\n");
    {
        World world;
        world.gravity = {0.0f, 9.8f};
        Body* floor = addBox(world, BodyType::Static, {0, 5}, {8, 0.5f});
        Body* box = addBox(world, BodyType::Dynamic, {0, 0}, {0.5f, 0.5f}, 1.0f);
        box->friction = 0.6f;
        box->restitution = 0.05f;
        for (int i = 0; i < 900; ++i) world.step(1.0f / 120.0f);
        check(std::fabs(box->position.y - 4.0f) < 0.08f, "box settles on static floor",
              "y=" + std::to_string(box->position.y));
        check(length(box->velocity) < 0.15f, "box comes to rest",
              "v=" + std::to_string(length(box->velocity)));
        check(floor->type == BodyType::Static, "floor remains static");
    }
    {
        // Slow-step runs ten sub-steps per frame; contacts must stay stable.
        World world;
        world.gravity = {0.0f, 9.8f};
        world.slowMotionGear = 10;
        addBox(world, BodyType::Static, {0, 5}, {8, 0.5f});
        Body* box = addBox(world, BodyType::Dynamic, {0, 0}, {0.5f, 0.5f}, 1.0f);
        box->friction = 0.6f;
        box->restitution = 0.05f;
        for (int i = 0; i < 600; ++i) world.step(1.0f / 120.0f);
        check(std::isfinite(box->position.y) && std::fabs(box->position.y - 4.0f) < 0.12f,
              "slow-step box settles on static floor",
              "y=" + std::to_string(box->position.y));
        check(length(box->velocity) < 0.3f, "slow-step box comes to rest",
              "v=" + std::to_string(length(box->velocity)));
    }
    {
        // A ghosted body is decorative: it falls straight through a static
        // floor that would otherwise stop it.
        World world;
        world.gravity = {0.0f, 9.8f};
        addBox(world, BodyType::Static, {0, 5}, {8, 0.5f});
        Body* box = addBox(world, BodyType::Dynamic, {0, 0}, {0.5f, 0.5f}, 1.0f);
        box->ghost = true;
        for (int i = 0; i < 300; ++i) world.step(1.0f / 120.0f);
        check(box->position.y > 6.0f, "ghost body passes through static collisions",
              "y=" + std::to_string(box->position.y));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* a = addBox(world, BodyType::Dynamic, {-1.0f, 0.0f}, {0.5f, 0.5f});
        Body* b = addBox(world, BodyType::Dynamic, {1.0f, 0.0f}, {0.5f, 0.5f});
        Joint joint;
        joint.type = JointType::Revolute;
        joint.a = a;
        joint.b = b;
        joint.localA = {0.5f, 0.0f};
        joint.localB = {-0.5f, 0.0f};
        world.joints.push_back(joint);
        b->applyImpulse({0.0f, 4.0f}, {1.0f, 0.0f});
        for (int i = 0; i < 300; ++i) world.step(1.0f / 120.0f);
        const Vec2 anchorA = a->worldPoint(joint.localA);
        const Vec2 anchorB = b->worldPoint(joint.localB);
        check(length(anchorA - anchorB) < 0.03f, "revolute joint keeps anchors together",
              std::to_string(length(anchorA - anchorB)));
    }
    {
        // A damped world-anchored hinge loses its swing and comes to rest.
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {2.0f, 0.0f}, {0.5f, 0.5f});
        body->linearDamping = 0.0f;
        body->angularDamping = 0.0f;
        Joint joint;
        joint.type = JointType::Revolute;
        joint.a = nullptr;
        joint.b = body;
        joint.localA = {0.0f, 0.0f};
        joint.localB = {-2.0f, 0.0f};
        joint.damping = 6.0f;
        world.joints.push_back(joint);
        body->angularVelocity = 4.0f;
        for (int i = 0; i < 240; ++i) world.step(1.0f / 120.0f);
        check(std::fabs(body->angularVelocity) < 0.2f,
              "revolute joint damping stops the swing",
              std::to_string(body->angularVelocity));
    }
    {
        // A stack of bodies should settle without exploding.
        World world;
        world.gravity = {0.0f, 9.8f};
        addBox(world, BodyType::Static, {0, 5}, {6, 0.5f});
        std::vector<Body*> stack;
        for (int i = 0; i < 5; ++i)
            stack.push_back(addBox(world, BodyType::Dynamic, {0.0f, 4.0f - i * 1.0f},
                                   {0.5f, 0.5f}));
        for (int i = 0; i < 900; ++i) world.step(1.0f / 120.0f);
        bool finite = true;
        for (const Body* body : stack) {
            if (!std::isfinite(body->position.x) || !std::isfinite(body->position.y))
                finite = false;
        }
        check(finite, "stacked bodies stay finite");
        const float topY = stack.front()->position.y;
        check(topY > 3.0f && topY < 5.5f, "stack settles near the floor",
              "topY=" + std::to_string(topY));
    }
    {
        // Concave (triangulated) star shape falls and comes to rest.
        World world;
        world.gravity = {0.0f, 9.8f};
        addBox(world, BodyType::Static, {0, 5}, {8, 0.5f});
        Body* star = world.createBody();
        star->type = BodyType::Dynamic;
        star->position = {0, 0};
        Poly starShape{{0.8f, 0}, {0.26f, 0.24f}, {0, 0.78f}, {-0.26f, 0.24f},
                       {-0.8f, 0}, {-0.26f, -0.24f}, {0, -0.78f}, {0.26f, -0.24f}};
        star->setOutline(starShape, 1.0f);
        star->friction = 0.5f;
        star->restitution = 0.05f;
        for (int i = 0; i < 900; ++i) world.step(1.0f / 120.0f);
        check(std::isfinite(star->position.y) && star->position.y > 2.0f &&
                  star->position.y < 5.0f,
              "concave note collides and settles",
              "y=" + std::to_string(star->position.y));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* a = addBox(world, BodyType::Dynamic, {-1.0f, 0.0f}, {0.5f, 0.5f});
        Body* b = addBox(world, BodyType::Dynamic, {1.0f, 0.0f}, {0.5f, 0.5f});
        Joint joint;
        joint.type = JointType::Weld;
        joint.a = a;
        joint.b = b;
        joint.localA = {0.0f, 0.0f};
        joint.localB = {-2.0f, 0.0f};
        joint.localA2 = {0.5f, 0.0f};
        joint.localB2 = {-1.5f, 0.0f};
        world.joints.push_back(joint);
        a->angularVelocity = 3.0f;
        for (int i = 0; i < 240; ++i) world.step(1.0f / 120.0f);
        check(std::fabs(a->angle - b->angle) < 0.05f, "weld joint locks rotation",
              std::to_string(a->angle - b->angle));
    }
}

void testPerBodyGravity() {
    std::printf("Per-body gravity\n");
    {
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* right = addBox(world, BodyType::Dynamic, {0, 0}, {0.5f, 0.5f});
        right->useGravityDirection = true;
        right->gravityDirection = gravityDirectionVector(GravityDirection::Right);
        for (int i = 0; i < 60; ++i) world.step(1.0f / 120.0f);
        check(right->position.x > 1.0f && std::fabs(right->position.y) < 0.01f,
              "right gravity moves a note sideways",
              std::to_string(right->position.x) + "," + std::to_string(right->position.y));
    }
    {
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* none = addBox(world, BodyType::Dynamic, {0, 0}, {0.5f, 0.5f});
        none->useGravityDirection = true;
        none->gravityDirection = gravityDirectionVector(GravityDirection::None);
        for (int i = 0; i < 240; ++i) world.step(1.0f / 120.0f);
        check(length(none->position) < 0.001f, "none gravity keeps a note in place");
    }
}

void testNoteContactStability() {
    std::printf("Note contact stability\n");
    World world;
    world.gravity = {0.0f, -9.8f};
    addBox(world, BodyType::Static, {0, -0.5f}, {12, 0.5f});
    NoteType tap;
    std::string loadError;
    check(loadStoredNote("physics", "Tap", tap, loadError),
          "load stored tap for contact test", loadError);
    if (tap.vertices.empty()) return;
    Poly noteShape = tap.vertices;
    for (auto& v : noteShape) v = v / PPM;
    std::vector<Body*> notes;
    for (int i = 0; i < 3; ++i) {
        Body* body = world.createBody();
        body->type = BodyType::Dynamic;
        body->position = {i == 1 ? 0.02f : (i == 2 ? -0.015f : 0.0f), 1.2f + i * 0.5f};
        body->angle = i == 1 ? 0.04f : (i == 2 ? -0.03f : 0.0f);
        body->setOutline(noteShape, 1.0f);
        body->friction = 0.55f;
        body->restitution = 0.05f;
        body->angularDamping = 0.6f;
        notes.push_back(body);
    }
    for (int i = 0; i < 1200; ++i) world.step(1.0f / 120.0f);
    float maxSpeed = 0.0f;
    float maxAngular = 0.0f;
    bool finite = true;
    for (const Body* body : notes) {
        if (!std::isfinite(body->position.x) || !std::isfinite(body->position.y) ||
            !std::isfinite(body->angle))
            finite = false;
        maxSpeed = std::max(maxSpeed, length(body->velocity));
        maxAngular = std::max(maxAngular, std::fabs(body->angularVelocity));
    }
    check(finite, "stacked notes stay finite");
    check(maxSpeed < 0.2f, "stacked notes stop drifting",
          "speed=" + std::to_string(maxSpeed));
    check(maxAngular < 0.2f, "stacked notes stop rotating",
          "angular=" + std::to_string(maxAngular));
    bool stacked = true;
    for (size_t i = 1; i < notes.size(); ++i) {
        if (notes[i]->position.y <= notes[i - 1]->position.y + 0.05f) stacked = false;
    }
    check(stacked, "notes remain stacked in order");
}

void testSettingsRoundTrip(const fs::path& dir) {
    std::printf("Settings persistence\n");
    Settings settings;
    settings.lightMode = true;
    settings.accent = 7;
    settings.showGrid = false;
    settings.showJoints = false;
    settings.showLabels = false;
    settings.showWireframe = true;
    settings.showContactPoints = true;
    settings.showGridLabels = false;
    settings.gravity = 12.5f;
    settings.velocityIterations = 12;
    settings.positionIterations = 5;
    settings.slowStepGear = 25;
    settings.bpm = 174.5f;
    settings.rivetRadius = 22.5f;
    settings.exportDirectory = "D:/PhiPhysiceEditorNeo/exports";
    settings.shortcuts[(int)ShortcutAction::PlayPause].key = KEY_P;
    settings.shortcuts[(int)ShortcutAction::Undo].ctrl = false;
    settings.shortcuts[(int)ShortcutAction::Undo].shift = true;
    const fs::path file = dir / "settings.ini";
    std::string error;
    check(saveSettings(file, settings, error), "save settings", error);
    Settings loaded;
    check(loadSettings(file, loaded), "load settings");
    check(loaded.lightMode, "day/night mode round trip");
    check(loaded.accent == 7, "accent colour round trip");
    check(!loaded.showGrid && !loaded.showJoints && !loaded.showLabels &&
              loaded.showWireframe && loaded.showContactPoints && !loaded.showGridLabels,
          "display switch round trip");
    check(std::fabs(loaded.gravity - 12.5f) < 0.01f, "gravity setting round trip");
    check(loaded.velocityIterations == 12 && loaded.positionIterations == 5,
          "solver setting round trip");
    check(loaded.slowStepGear == 25, "slow-step setting round trip");
    check(std::fabs(loaded.bpm - 174.5f) < 0.01f, "BPM setting round trip");
    check(std::fabs(loaded.rivetRadius - 22.5f) < 0.01f,
          "global rivet radius round trip");
    check(loaded.exportDirectory == "D:/PhiPhysiceEditorNeo/exports",
          "prefab export directory round trip");
    check(loaded.shortcuts[(int)ShortcutAction::PlayPause].key == KEY_P,
          "custom shortcut round trip");
    check(!loaded.shortcuts[(int)ShortcutAction::Undo].ctrl &&
              loaded.shortcuts[(int)ShortcutAction::Undo].shift,
          "shortcut modifiers round trip");
    check(bindingLabel(KeyBinding{KEY_S, true, true}) == "Ctrl + Shift + S",
          "shortcut label formatting");
}

void testProjectMigration(const fs::path& dir) {
    std::printf("Legacy Y-down project migration\n");
    const fs::path file = dir / "legacy-v1.pnp";
    {
        std::ofstream out(file);
        out << "PHIPHYSICE-PROJECT 1\n";
        out << "name Legacy\n";
        out << "gravity 9.8\n";
        out << "nextids 3 2 2 1 1\n";
        out << "[notes]\n";
        out << "note 1 Tap 100.000 -200.000 30.000 0\n";
        out << "note 2 Hold -50.000 120.000 -10.000 0 80.000\n";
        out << "[judges]\n";
        out << "judge 1 0.000 -270.000 0.000 4000.000 5.000 255 238 150\n";
        out << "[rev]\n";
        out << "rev 1 -1 1 100.000 -200.000 0.000 0.000 0.000 0 0\n";
        out << "[weld]\n";
        out << "[end]\n";
    }
    Document doc;
    std::string error;
    check(loadProject(file, doc, error), "load legacy v1 project", error);
    if (const Note* tap = doc.findNote(1)) {
        check(std::fabs(tap->position.y - 200.0f) < 0.01f &&
                  std::fabs(tap->angle + 30.0f * DEG2RAD) < 0.001f,
              "legacy note Y and angle are flipped to Y-up");
    } else {
        check(false, "legacy tap note present");
    }
    if (const Note* hold = doc.findNote(2)) {
        check(std::fabs(hold->position.y + 120.0f) < 0.01f,
              "legacy hold position is flipped");
    } else {
        check(false, "legacy hold note present");
    }
    if (!doc.judges.empty()) {
        check(std::fabs(doc.judges.front().position.y - 270.0f) < 0.01f,
              "legacy judge line Y is flipped");
    } else {
        check(false, "legacy judge line present");
    }
    if (!doc.revJoints.empty()) {
        check(std::fabs(doc.revJoints.front().localA.y - 200.0f) < 0.01f,
              "legacy joint anchor Y is flipped");
    } else {
        check(false, "legacy joint present");
    }
}

void testForceFields() {
    std::printf("Force fields\n");
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {1.5f, 0.0f}, {0.2f, 0.2f});
        ForceFieldData field;
        field.kind = 0; // gravity
        field.shape = 1; // circle
        field.position = {0.0f, 0.0f};
        field.radius = 3.0f;
        field.strength = 24.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 180; ++i) world.step(1.0f / 120.0f);
        check(body->position.x < 1.0f, "gravity field pulls notes towards its centre",
              "x=" + std::to_string(body->position.x));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {1.0f, 0.0f}, {0.2f, 0.2f});
        ForceFieldData field;
        field.kind = 1; // repulsion
        field.shape = 1;
        field.position = {0.0f, 0.0f};
        field.radius = 3.0f;
        field.strength = 24.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 180; ++i) world.step(1.0f / 120.0f);
        check(body->position.x > 1.5f, "repulsion field pushes notes outwards",
              "x=" + std::to_string(body->position.x));
    }
    {
        // Real Archimedes buoyancy: a fluid denser than the body lifts it, and
        // the force is opposite gravity rather than an editable direction.
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f}, 1.0f);
        ForceFieldData field;
        field.kind = 2; // buoyancy
        field.shape = 0; // rectangle
        field.position = {0.0f, 0.0f};
        field.halfWidth = 4.0f;
        field.halfHeight = 4.0f;
        field.buoyancy = 1.2f; // fluid density
        world.forceFields.push_back(field);
        for (int i = 0; i < 180; ++i) world.step(1.0f / 120.0f);
        check(body->position.y > 0.3f, "real buoyancy lifts a body lighter than the fluid",
              "y=" + std::to_string(body->position.y));
    }
    {
        // A body heavier than the fluid sinks, proving the force is mass-aware.
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f}, 2.0f);
        ForceFieldData field;
        field.kind = 2;
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 4.0f;
        field.halfHeight = 4.0f;
        field.buoyancy = 1.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 180; ++i) world.step(1.0f / 120.0f);
        check(body->position.y < -0.3f, "real buoyancy lets a denser body sink",
              "y=" + std::to_string(body->position.y));
    }
    {
        ForceField field;
        check(field.width == 100.0f && field.height == 100.0f && field.radius == 50.0f,
              "new force fields default to 100x100 or radius 50");
        field.kind = ForceFieldKind::Buoyancy;
        field.shape = ForceFieldShape::Circle;
        field.position = {120.0f, 80.0f};
        field.radius = 95.0f;
        check(forceFieldContainsPoint(field, {120.0f, 80.0f}), "field contains its centre");
        check(!forceFieldContainsPoint(field, {120.0f, 300.0f}),
              "field rejects points outside");
    }
    {
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.4f, 0.1f}, 1.0f);
        ForceFieldData field;
        field.kind = 2;    // buoyancy
        field.shape = 0;   // rectangle
        field.position = {0.35f, 0.0f};
        field.halfWidth = 0.6f;
        field.halfHeight = 1.0f;
        field.buoyancy = 2.0f; // dense fluid so the partly covered body still rises
        world.forceFields.push_back(field);
        for (int i = 0; i < 30; ++i) world.step(1.0f / 120.0f);
        check(body->position.y > 0.01f,
              "real buoyancy lifts a partially covered note");
        check(std::fabs(body->angularVelocity) > 0.01f,
              "real buoyancy creates torque from uneven coverage");
    }
    {
        // The restored buoyancy angle controls the lift direction.
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f}, 1.0f);
        ForceFieldData field;
        field.kind = 2;
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 4.0f;
        field.halfHeight = 4.0f;
        field.buoyancy = 2.0f;
        field.buoyancyDirection = {1.0f, 0.0f};
        world.forceFields.push_back(field);
        for (int i = 0; i < 90; ++i) world.step(1.0f / 120.0f);
        check(body->position.x > 0.02f,
              "buoyancy angle directs the lift force",
              "x=" + std::to_string(body->position.x));
    }
    {
        // Buoyancy damping settles a rising note on the fluid surface instead
        // of letting it keep overshooting.
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, -1.0f}, {0.3f, 0.3f}, 1.0f);
        body->linearDamping = 0.0f;
        body->angularDamping = 0.0f;
        ForceFieldData field;
        field.kind = 2;
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 5.0f;
        field.halfHeight = 1.5f;
        field.buoyancy = 2.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 1500; ++i) world.step(1.0f / 120.0f);
        check(std::fabs(body->velocity.y) < 0.15f &&
                  body->position.y > 1.2f && body->position.y < 1.8f,
              "buoyancy damping settles the note on the surface",
              "y=" + std::to_string(body->position.y) +
                  " vy=" + std::to_string(body->velocity.y));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f});
        body->velocity = {4.0f, 0.0f};
        ForceFieldData field;
        field.kind = 0; // gravity field with damping enabled
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 5.0f;
        field.halfHeight = 5.0f;
        field.strength = 0.0f;
        field.damping = 10.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 60; ++i) world.step(1.0f / 120.0f);
        check(body->velocity.x < 3.0f,
              "gravity field damping reduces note momentum",
              "vx=" + std::to_string(body->velocity.x));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f});
        body->velocity = {4.0f, 0.0f};
        ForceFieldData field;
        field.kind = 3; // drag
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 5.0f;
        field.halfHeight = 5.0f;
        field.strength = 10.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 60; ++i) world.step(1.0f / 120.0f);
        check(body->velocity.x < 3.0f && std::fabs(body->velocity.y) < 0.01f,
              "drag field removes momentum without adding force",
              "vx=" + std::to_string(body->velocity.x));
    }
    {
        World normal;
        normal.gravity = {0.0f, -9.8f};
        Body* normalBody = addBox(normal, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f});
        World slow;
        slow.gravity = {0.0f, -9.8f};
        slow.slowMotionGear = 10;
        Body* slowBody = addBox(slow, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f});
        for (int i = 0; i < 120; ++i) {
            normal.step(1.0f / 120.0f);
            slow.step(1.0f / 120.0f);
        }
        check(std::fabs(normalBody->position.y - slowBody->position.y) < 0.08f,
              "slow-step simulation keeps the normal trajectory",
              "normal=" + std::to_string(normalBody->position.y) +
                  " slow=" + std::to_string(slowBody->position.y));
        check(std::fabs(normalBody->velocity.y - slowBody->velocity.y) < 0.15f,
              "slow-step simulation keeps the normal velocity",
              "normal=" + std::to_string(normalBody->velocity.y) +
                  " slow=" + std::to_string(slowBody->velocity.y));
    }
    {
        World world;
        world.gravity = {0.0f, -9.8f};
        Body* body = addBox(world, BodyType::Dynamic, {0.0f, 0.0f}, {0.2f, 0.2f});
        ForceFieldData field;
        field.kind = 4; // gravity change
        field.shape = 0;
        field.position = {0.0f, 0.0f};
        field.halfWidth = 5.0f;
        field.halfHeight = 5.0f;
        field.buoyancyDirection = {1.0f, 0.0f};
        world.forceFields.push_back(field);
        for (int i = 0; i < 120; ++i) world.step(1.0f / 120.0f);
        check(body->position.x > 1.0f && std::fabs(body->position.y) < 0.2f,
              "gravity change field redirects gravity to its arrow",
              std::to_string(body->position.x) + "," + std::to_string(body->position.y));
    }
    {
        // A positive vortex strength orbits the note clockwise around the
        // field centre; starting to the right of the centre means moving down.
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {1.0f, 0.0f}, {0.2f, 0.2f});
        ForceFieldData field;
        field.kind = 5; // vortex
        field.shape = 1;
        field.position = {0.0f, 0.0f};
        field.radius = 3.0f;
        field.vortex = 24.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 120; ++i) world.step(1.0f / 120.0f);
        check(body->position.y < -0.05f,
              "positive vortex strength orbits notes clockwise",
              "x=" + std::to_string(body->position.x) +
                  " y=" + std::to_string(body->position.y));
    }
    {
        World world;
        world.gravity = {0.0f, 0.0f};
        Body* body = addBox(world, BodyType::Dynamic, {1.0f, 0.0f}, {0.2f, 0.2f});
        ForceFieldData field;
        field.kind = 5;
        field.shape = 1;
        field.position = {0.0f, 0.0f};
        field.radius = 3.0f;
        field.vortex = -24.0f;
        world.forceFields.push_back(field);
        for (int i = 0; i < 120; ++i) world.step(1.0f / 120.0f);
        check(body->position.y > 0.05f,
              "negative vortex strength orbits notes counter-clockwise",
              "x=" + std::to_string(body->position.x) +
                  " y=" + std::to_string(body->position.y));
    }
}

void testEvents() {
    std::printf("Component events\n");
    check(kEasingCount == 28, "easing curve list has 28 entries");
    check(std::fabs(eventSnapBeats(4) - 0.25f) < 0.001f,
          "default event split is quarter beat");
    check(std::fabs(snapEventBeat(1.13f, 4) - 1.25f) < 0.001f,
          "event snapping uses the project split");
    check(std::fabs(internalAngleToUserDegrees(0.0f)) < 0.01f &&
              std::fabs(userDegreesToInternalAngle(0.0f)) < 0.001f,
          "user angle 0 is the default rotation");
    check(std::fabs(internalAngleToUserDegrees(userDegreesToInternalAngle(720.0f)) -
                    720.0f) < 0.01f &&
              std::fabs(internalAngleToUserDegrees(userDegreesToInternalAngle(-725.0f)) +
                        725.0f) < 0.01f,
          "rotation angles above 360 and below 0 are preserved");
    check(std::fabs(directionToUserDegrees({0.0f, 1.0f})) < 0.01f &&
              std::fabs(directionToUserDegrees({1.0f, 0.0f}) - 90.0f) < 0.01f &&
              std::fabs(directionToUserDegrees({-1.0f, 0.0f}) + 90.0f) < 0.01f,
          "positive user angle turns clockwise from up");
    check(std::fabs(easingProgress(1, 0.5f) - 0.5f) < 0.001f, "linear easing");
    check(std::fabs(easingProgress(5, 0.5f) - 0.25f) < 0.001f, "quadratic ease-in");

    Note note;
    note.position = {10.0f, 20.0f};
    Event moveX;
    moveX.track = 2;
    moveX.type = EventType::MoveX;
    moveX.startBeat = 0.0f;
    moveX.endBeat = 2.0f;
    moveX.startValue = "this";
    moveX.endValue = "this+100";
    moveX.easing = 1;
    note.events = {moveX};
    applyEventsToNote(note, 1.0f);
    check(std::fabs(note.position.x - 60.0f) < 0.01f,
          "event lerps between start and end expressions",
          std::to_string(note.position.x));

    Note expressionNote;
    expressionNote.position = {10.0f, 0.0f};
    Event expression = moveX;
    expression.endValue = "this + 100 * et + 0 * pi + 0 * t";
    expressionNote.events = {expression};
    applyEventsToNote(expressionNote, 1.0f);
    check(std::fabs(expressionNote.position.x - 35.0f) < 0.01f,
          "event expressions expose this / pi / t / et",
          std::to_string(expressionNote.position.x));

    Note layered;
    layered.position = {10.0f, 0.0f};
    Event first = moveX;
    first.startBeat = 0.0f;
    first.endBeat = 10.0f;
    first.startValue = "this";
    first.endValue = "this+100";
    Event second = first;
    second.startBeat = 2.0f;
    second.endBeat = 4.0f;
    layered.events = {first, second};
    applyEventsToNote(layered, 3.0f);
    check(std::fabs(layered.position.x - 80.0f) < 0.01f,
          "this captures the value at the event start instant",
          std::to_string(layered.position.x));

    ForceField field;
    field.kind = ForceFieldKind::Buoyancy;
    field.buoyancyDirection = {0.0f, 1.0f};
    Event direction;
    direction.type = EventType::Direction;
    direction.startBeat = 0.0f;
    direction.endBeat = 1.0f;
    direction.startValue = "90";
    direction.endValue = "0";
    field.events = {direction};
    applyEventsToField(field, 0.5f);
    check(std::fabs(std::atan2(field.buoyancyDirection.y, field.buoyancyDirection.x) *
                        RAD2DEG -
                    45.0f) < 0.1f,
          "field direction event rotates the buoyancy angle");

    const auto vortexTypes = eventTypesForField(ForceFieldKind::Vortex);
    check(std::find(vortexTypes.begin(), vortexTypes.end(),
                    EventType::VortexStrength) != vortexTypes.end(),
          "vortex fields expose the signed vortex strength variable");
    ForceField vortexField;
    vortexField.kind = ForceFieldKind::Vortex;
    Event vortexEvent;
    vortexEvent.type = EventType::VortexStrength;
    vortexEvent.startBeat = 0.0f;
    vortexEvent.endBeat = 1.0f;
    vortexEvent.startValue = "30";
    vortexEvent.endValue = "-30";
    vortexField.events = {vortexEvent};
    applyEventsToField(vortexField, 1.0f);
    check(std::fabs(vortexField.vortex + 30.0f) < 0.01f,
          "vortex strength event keeps negative counter-clockwise values",
          std::to_string(vortexField.vortex));

    const auto physicsTypes = eventTypesForNote(false);
    check(std::find(physicsTypes.begin(), physicsTypes.end(),
                    EventType::GravityDirection) != physicsTypes.end(),
          "physics notes expose the gravity direction event");
    check(std::find(physicsTypes.begin(), physicsTypes.end(),
                    EventType::DirectionalMomentum) != physicsTypes.end() &&
              std::find(physicsTypes.begin(), physicsTypes.end(),
                        EventType::RotationalMomentum) != physicsTypes.end(),
          "physics notes expose directional and rotational momentum events");
    const auto rivetTypes = eventTypesForNote(false, true);
    check(std::find(rivetTypes.begin(), rivetTypes.end(),
                    EventType::DirectionalMomentum) != rivetTypes.end() &&
              std::find(rivetTypes.begin(), rivetTypes.end(),
                        EventType::RotationalMomentum) == rivetTypes.end(),
          "physics rivets expose directional momentum but not rotation");
    const auto sceneTypes = eventTypesForNote(true);
    check(std::find(sceneTypes.begin(), sceneTypes.end(),
                    EventType::GravityDirection) == sceneTypes.end() &&
              std::find(sceneTypes.begin(), sceneTypes.end(),
                        EventType::DirectionalMomentum) == sceneTypes.end() &&
              std::find(sceneTypes.begin(), sceneTypes.end(),
                        EventType::RotationalMomentum) == sceneTypes.end(),
          "scene blocks do not expose physics momentum events");
    const auto judgeTypes = eventTypesForJudge();
    check(std::find(judgeTypes.begin(), judgeTypes.end(),
                    EventType::GravityDirection) == judgeTypes.end(),
          "judge lines do not expose the gravity direction event");
    check(std::find(physicsTypes.begin(), physicsTypes.end(), EventType::Ghost) !=
                  physicsTypes.end() &&
              std::find(sceneTypes.begin(), sceneTypes.end(), EventType::Ghost) !=
                  sceneTypes.end() &&
              std::find(judgeTypes.begin(), judgeTypes.end(), EventType::Ghost) !=
                  judgeTypes.end() &&
              std::find(rivetTypes.begin(), rivetTypes.end(), EventType::Ghost) ==
                  rivetTypes.end(),
          "tap/drag/flick/hold and judge lines expose the ghost event");

    Note ghostNote;
    Event ghostEvent;
    ghostEvent.type = EventType::Ghost;
    ghostEvent.startBeat = 1.0f;
    ghostEvent.endBeat = 2.0f;
    ghostEvent.startValue = "this";
    ghostEvent.endValue = "1";
    ghostEvent.instant = true;
    ghostNote.events = {ghostEvent};
    applyEventsToNote(ghostNote, 0.5f);
    check(!ghostNote.ghost, "ghost event is off before its start beat");
    applyEventsToNote(ghostNote, 1.0f);
    check(ghostNote.ghost, "ghost event switches the note to ghost at its start beat");

    JudgeLine ghostJudge;
    ghostJudge.events = {ghostEvent};
    applyEventsToJudge(ghostJudge, 1.0f);
    check(ghostJudge.ghost, "ghost event switches judge lines too");

    Note gravityNote;
    gravityNote.gravity = GravityDirection::Down;
    Event gravityEvent;
    gravityEvent.type = EventType::GravityDirection;
    gravityEvent.startBeat = 0.0f;
    gravityEvent.endBeat = 1.0f;
    gravityEvent.startValue = "this";
    gravityEvent.endValue = "3";
    gravityNote.events = {gravityEvent};
    applyEventsToNote(gravityNote, 0.5f);
    check(gravityNote.gravity == GravityDirection::Left,
          "gravity direction event rounds to the nearest direction",
          std::to_string((int)gravityNote.gravity));
    applyEventsToNote(gravityNote, 1.0f);
    check(gravityNote.gravity == GravityDirection::Right,
          "gravity direction event reaches its end value",
          std::to_string((int)gravityNote.gravity));

    Note instantNote;
    instantNote.position = {10.0f, 0.0f};
    Event instantMove = moveX;
    instantMove.instant = true;
    instantNote.events = {instantMove};
    applyEventsToNote(instantNote, 0.5f);
    check(std::fabs(instantNote.position.x - 110.0f) < 0.01f,
          "instant event jumps to its end value at the start beat",
          std::to_string(instantNote.position.x));

    {
        App app;
        Note note;
        note.id = 1;
        note.type = "Tap";
        Event event;
        event.type = EventType::DirectionalMomentum;
        event.startBeat = 2.0f;
        event.endBeat = 4.0f;
        event.startValue = "2";
        event.endValue = "2";
        event.angleValue = "this";
        note.events = {event};
        app.doc.notes.push_back(note);
        Body* body = app.world.createBody();
        body->type = BodyType::Dynamic;
        body->mass = body->inertia = 1.0f;
        body->invMass = body->invInertia = 1.0f;
        body->angle = userDegreesToInternalAngle(90.0f);
        app.noteBodies[note.id] = body;
        app.settings.bpm = 60.0f;
        app.simTime = 3.0f;
        app.applyEventsAtTime();
        app.simTime = 4.0f;
        app.applyEventsAtTime();
        check(std::fabs(body->velocity.x - 2.0f) < 0.01f &&
                  std::fabs(body->velocity.y) < 0.01f,
              "directional momentum spreads its total and angle this is the note angle",
              std::to_string(body->velocity.x) + "," +
                  std::to_string(body->velocity.y));
    }

    {
        App app;
        Note note;
        note.id = 2;
        note.type = "Tap";
        Event event;
        event.type = EventType::DirectionalMomentum;
        event.startBeat = 2.0f;
        event.endBeat = 4.0f;
        event.startValue = "5";
        event.endValue = "5";
        event.angleValue = "0";
        event.instant = true;
        note.events = {event};
        app.doc.notes.push_back(note);
        Body* body = app.world.createBody();
        body->type = BodyType::Dynamic;
        body->mass = body->inertia = 1.0f;
        body->invMass = body->invInertia = 1.0f;
        app.noteBodies[note.id] = body;
        app.settings.bpm = 60.0f;
        app.simTime = 1.0f;
        app.applyEventsAtTime();
        app.simTime = 3.0f;
        app.applyEventsAtTime();
        app.simTime = 4.0f;
        app.applyEventsAtTime();
        check(std::fabs(body->velocity.y - 5.0f) < 0.01f &&
                  std::fabs(body->velocity.x) < 0.01f,
              "instant directional momentum fires once at the start beat",
              std::to_string(body->velocity.x) + "," +
                  std::to_string(body->velocity.y));
    }

    {
        App app;
        Note note;
        note.id = 3;
        note.type = "Tap";
        Event event;
        event.type = EventType::RotationalMomentum;
        event.startBeat = 0.0f;
        event.endBeat = 2.0f;
        event.startValue = "4";
        event.endValue = "4";
        event.instant = true;
        note.events = {event};
        app.doc.notes.push_back(note);
        Body* body = app.world.createBody();
        body->type = BodyType::Dynamic;
        body->mass = body->inertia = 1.0f;
        body->invMass = body->invInertia = 1.0f;
        app.noteBodies[note.id] = body;
        app.settings.bpm = 60.0f;
        app.simTime = 0.5f;
        app.applyEventsAtTime();
        app.simTime = 1.0f;
        app.applyEventsAtTime();
        check(std::fabs(body->angularVelocity + 4.0f) < 0.01f,
              "rotational momentum turns clockwise and fires once",
              std::to_string(body->angularVelocity));
    }
}

void testEventEditing() {
    std::printf("Event editing\n");
    {
        App app;
        Event first;
        first.track = 2;
        first.startBeat = 1.0f;
        first.endBeat = 3.0f;
        Event second;
        second.track = 4;
        second.startBeat = 5.0f;
        second.endBeat = 6.0f;
        app.eventClipboard = {first, second};
        app.eventClipboardAnchorBeat = 1.0f;
        app.eventClipboardAnchorTrack = 2;
        const std::vector<Event> preview = app.clipboardPreviewAt(10.0f, 5);
        check(preview.size() == 2 &&
                  std::fabs(preview[0].startBeat - 10.0f) < 0.01f &&
                  preview[0].track == 5 &&
                  std::fabs(preview[1].startBeat - 14.0f) < 0.01f &&
                  preview[1].track == 7,
              "clipboard preview keeps relative time and tracks from the anchor");
        const std::vector<Event> clamped = app.clipboardPreviewAt(0.0f, 0);
        check(clamped.size() == 2 &&
                  std::fabs(clamped[0].startBeat) < 0.01f &&
                  clamped[0].track == 0 && clamped[1].track == 2,
              "clipboard preview clamps early paste times and tracks");
    }
    {
        App app;
        const int sourceId = app.doc.addNote("Tap", {0.0f, 0.0f});
        const int targetId = app.doc.addNote("Tap", {100.0f, 0.0f});
        Note* source = app.doc.findNote(sourceId);
        Note* target = app.doc.findNote(targetId);
        Event move;
        move.type = EventType::MoveX;
        move.track = 0;
        move.startBeat = 0.0f;
        move.endBeat = 1.0f;
        Event rotate;
        rotate.type = EventType::Rotate;
        rotate.track = 1;
        rotate.startBeat = 2.0f;
        rotate.endBeat = 3.0f;
        source->events = {move, rotate};
        Event occupiedMove = move;
        Event occupiedRotate = rotate;
        target->events = {occupiedMove, occupiedRotate};
        app.eventEditor.target = EventEditorState::Target::Note;
        app.eventEditor.targetId = sourceId;
        app.eventEditor.selection = {0, 1};
        app.eventEditor.selected = 1;
        app.beginCopyEventsToOther();
        app.eventCopySelectedNotes = {targetId};
        app.confirmEventCopyToTargets();
        check(target->events.size() == 4 &&
                  target->events[2].type == EventType::MoveX &&
                  target->events[2].track == 1 &&
                  target->events[3].type == EventType::Rotate &&
                  target->events[3].track == 0,
              "copy to another note picks adjacent free tracks");
    }
}

void testRenumbering() {
    std::printf("Creation-order renumbering\n");
    Document doc;
    const int a = doc.addNote("Tap", {0.0f, 0.0f});
    const int b = doc.addNote("Drag", {10.0f, 0.0f});
    doc.removeNote(a);
    const int c = doc.addNote("Flick", {20.0f, 0.0f});
    const auto mapping = doc.renumberNotes();
    check(doc.notes.size() == 2 && doc.notes[0].id == 1 && doc.notes[1].id == 2,
          "notes are renumbered in creation order");
    check(mapping.size() == 2 && doc.findNote(1) != nullptr &&
              doc.findNote(1)->type == "Drag" && doc.findNote(2) != nullptr &&
              doc.findNote(2)->type == "Flick",
          "renumbering keeps the surviving notes addressable");
}

void testPrefabExport(const fs::path& dir) {
    std::printf("Prefab export\n");
    App app;
    app.rootDir = dir / "prefab_root";
    app.settings.exportDirectory = (dir / "prefab_out").string();
    std::error_code exportEc;
    fs::create_directories(app.rootDir, exportEc);
    fs::create_directories(dir / "prefab_out", exportEc);
    fs::copy_file(componentsRoot().parent_path() / "export_rules.txt",
                  app.rootDir / "export_rules.txt", fs::copy_options::overwrite_existing,
                  exportEc);
    app.library.folder = componentsRoot() / "physics";
    app.library.load(app.library.folder);
    app.sceneLibrary.folder = componentsRoot() / "scene";
    app.sceneLibrary.load(app.sceneLibrary.folder);
    app.exportEndBeat = 0.5f;
    const int a = app.doc.addNote("Tap", {0.0f, 0.0f});
    app.doc.addNote("Drag", {10.0f, 20.0f}, 0.2f, true);
    app.doc.addNote(kRivetType, {30.0f, 0.0f});
    Note* note = app.doc.findNote(a);
    Event move;
    move.type = EventType::MoveX;
    move.startBeat = 0.0f;
    move.endBeat = 1.0f;
    move.startValue = "this";
    move.endValue = "this+100";
    Event rotate;
    rotate.type = EventType::Rotate;
    rotate.startBeat = 0.0f;
    rotate.endBeat = 1.0f;
    rotate.startValue = "this";
    rotate.endValue = "this+720";
    note->events = {move, rotate};
    check(app.exportPrefabEvents(), "prefab export runs");
    std::ifstream in(dir / "prefab_out" / "NotePhysics.json");
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    check(text.find("\"Events\" : [") != std::string::npos &&
              text.find("\"intro\" : \"Note:2,Events:12\"") != std::string::npos &&
              text.find("\"line\" : 0") != std::string::npos &&
              text.find("\"line\" : 1") != std::string::npos,
          "prefab export follows the external note3d rules");
    check(text.find("\"t1\" : [0, 0, 4]") != std::string::npos &&
              text.find("\"t2\" : [0, 1, 4]") != std::string::npos &&
              text.find("\"x1\" : 0.000") != std::string::npos &&
              text.find("\"x2\" : 25.000") != std::string::npos &&
              text.find("\"x1\" : 180.000") != std::string::npos &&
              text.find("\"x2\" : 360.000") != std::string::npos,
          "prefab export preserves rotation beyond 360");
    check(fs::exists(dir / "prefab_out" / "NotePhysics.json"),
          "prefab export creates missing NotePhysics.json");
    check(fs::exists(dir / "prefab_root" / "export_rules.txt"),
          "prefab export keeps the editable rules file");
}

void testBatchGravity() {
    std::printf("Batch gravity\n");
    App app;
    const int noteId = app.doc.addNote("Tap", {0.0f, 0.0f});
    const int rivetId = app.doc.addNote(kRivetType, {20.0f, 0.0f});
    const int sceneId = app.doc.addNote("Tap", {40.0f, 0.0f}, 0.0f, true);
    app.selectedNotes = {noteId, rivetId, sceneId};
    app.setSelectionGravity(GravityDirection::Left);
    const Note* note = app.doc.findNote(noteId);
    const Note* rivet = app.doc.findNote(rivetId);
    const Note* scene = app.doc.findNote(sceneId);
    check(note && note->gravity == GravityDirection::Left &&
              rivet && rivet->gravity == GravityDirection::Left &&
              scene && scene->gravity == GravityDirection::Down,
          "batch gravity updates physics notes and rivets but not scene blocks");
    app.setSelectionGravity(GravityDirection::None);
    check(note->gravity == GravityDirection::None &&
              rivet->gravity == GravityDirection::None,
          "batch gravity applies a second direction");
}

void testBatchGhost() {
    std::printf("Batch ghost\n");
    App app;
    const int noteId = app.doc.addNote("Tap", {0.0f, 0.0f});
    const int rivetId = app.doc.addNote(kRivetType, {20.0f, 0.0f});
    const int sceneId = app.doc.addNote("Hold", {40.0f, 0.0f}, 0.0f, true);
    app.selectedNotes = {noteId, rivetId, sceneId};
    app.setSelectionGhost(true);
    const Note* note = app.doc.findNote(noteId);
    const Note* rivet = app.doc.findNote(rivetId);
    const Note* scene = app.doc.findNote(sceneId);
    check(note && note->ghost && scene && scene->ghost,
          "batch ghost updates physics and scene notes");
    check(rivet && !rivet->ghost, "batch ghost leaves rivets unchanged");
    app.setSelectionGhost(false);
    check(note && !note->ghost && scene && !scene->ghost,
          "batch ghost restores solid components");
}

void testProject(const fs::path& dir) {
    std::printf("Project persistence\n");
    Document doc;
    doc.name = "SelfTestProject";
    doc.addJudge({0, 300}, 4000.0f, 5.0f, 0.1f);
    const int a = doc.addNote("Tap", {-100, 0}, 0.2f);
    const int b = doc.addNote("Hold", {100, 0}, -0.1f);
    const int sceneId = doc.addNote("Hold", {300, -200}, 0.0f, true);
    const int rivetId = doc.addNote(kRivetType, {-300, -200}, 0.0f, false);
    if (Note* rivet = doc.findNote(rivetId)) {
        rivet->radius = 27.5f;
        rivet->color = {12, 34, 56};
        rivet->gravity = GravityDirection::Left;
        Event rivetGravity;
        rivetGravity.track = 4;
        rivetGravity.type = EventType::GravityDirection;
        rivetGravity.startBeat = 0.0f;
        rivetGravity.endBeat = 2.0f;
        rivetGravity.startValue = "8";
        rivetGravity.endValue = "this";
        rivet->events = {rivetGravity};
    }
    if (Note* sceneNote = doc.findNote(sceneId)) sceneNote->ghost = true;
    if (Note* tapNote = doc.findNote(a)) {
        tapNote->variables["width"] = 42.0f;
        tapNote->gravity = GravityDirection::UpRight;
    }
    if (Note* holdNote = doc.findNote(b)) {
        holdNote->variables["height"] = 150.0f;
        holdNote->gravity = GravityDirection::None;
    }
    doc.addRevJoint(a, b, {0, 0}, false, 1);
    const int g = doc.nextGroupId++;
    doc.setGroupForNote(a, g);
    doc.setGroupForNote(b, g);
    doc.addWeldJoint(a, b, g, {-100, 0}, {-50, 0});
    doc.gravity = 12.5f;
    doc.velocityIterations = 12;
    doc.positionIterations = 5;
    doc.slowStepGear = 25;
    doc.eventSplit = 8;
    ForceField field;
    field.kind = ForceFieldKind::Buoyancy;
    field.shape = ForceFieldShape::Circle;
    field.position = {120.0f, 80.0f};
    field.radius = 95.0f;
    field.buoyancy = 1.4f;
    field.buoyancyDirection = {0.6f, 0.8f};
    field.damping = 4.5f;
    doc.addForceField(field);
    ForceField dragField;
    dragField.kind = ForceFieldKind::Drag;
    dragField.shape = ForceFieldShape::Rectangle;
    dragField.position = {-200.0f, 30.0f};
    dragField.strength = 13.0f;
    doc.addForceField(dragField);
    ForceField changeField;
    changeField.kind = ForceFieldKind::GravityChange;
    changeField.shape = ForceFieldShape::Rectangle;
    changeField.position = {250.0f, -30.0f};
    changeField.buoyancyDirection = {0.0f, 1.0f};
    doc.addForceField(changeField);
    ForceField vortexField;
    vortexField.kind = ForceFieldKind::Vortex;
    vortexField.shape = ForceFieldShape::Rectangle;
    vortexField.position = {0.0f, 200.0f};
    vortexField.vortex = -27.5f;
    doc.addForceField(vortexField);
    Event noteEvent;
    noteEvent.track = 3;
    noteEvent.type = EventType::MoveY;
    noteEvent.startBeat = 1.0f;
    noteEvent.endBeat = 5.0f;
    noteEvent.startValue = "this";
    noteEvent.endValue = "this+80";
    noteEvent.easing = 7;
    noteEvent.instant = true;
    noteEvent.angleValue = "this+15";
    Event momentumEvent;
    momentumEvent.track = 5;
    momentumEvent.type = EventType::DirectionalMomentum;
    momentumEvent.startBeat = 6.0f;
    momentumEvent.endBeat = 7.0f;
    momentumEvent.startValue = "3";
    momentumEvent.endValue = "5";
    momentumEvent.angleValue = "this+45";
    momentumEvent.instant = true;
    if (Note* tapNote = doc.findNote(a)) tapNote->events = {noteEvent, momentumEvent};
    Event judgeEvent;
    judgeEvent.track = 0;
    judgeEvent.type = EventType::Rotate;
    judgeEvent.startBeat = 0.0f;
    judgeEvent.endBeat = 2.0f;
    judgeEvent.startValue = "0";
    judgeEvent.endValue = "pi*10";
    if (!doc.judges.empty()) {
        doc.judges.front().ghost = true;
        Event judgeGhost;
        judgeGhost.track = 1;
        judgeGhost.type = EventType::Ghost;
        judgeGhost.startBeat = 4.0f;
        judgeGhost.endBeat = 5.0f;
        judgeGhost.startValue = "this";
        judgeGhost.endValue = "1";
        judgeGhost.instant = true;
        doc.judges.front().events = {judgeEvent, judgeGhost};
    }
    Event fieldEvent;
    fieldEvent.track = 9;
    fieldEvent.type = EventType::Direction;
    fieldEvent.startBeat = 0.0f;
    fieldEvent.endBeat = 4.0f;
    fieldEvent.startValue = "90";
    fieldEvent.endValue = "180";
    if (!doc.fields.empty()) doc.fields.front().events = {fieldEvent};

    const fs::path file = dir / "selftest.pnp";
    std::string error;
    check(saveProject(file, doc, error), "save project", error);
    Document loaded;
    check(loadProject(file, loaded, error), "load project", error);
    check(loaded.notes.size() == 4, "note count round trip");
    if (const Note* scene = loaded.findNote(sceneId)) {
        check(scene->scene, "scene block flag round trip");
        check(scene->ghost, "scene block ghost state round trip");
    } else {
        check(false, "scene block present after load");
    }
    if (const Note* normal = loaded.findNote(a))
        check(!normal->scene, "regular notes are not scene blocks");
    if (const Note* rivet = loaded.findNote(rivetId)) {
        check(isRivetNote(*rivet), "rivet type round trip");
        check(std::fabs(rivet->radius - 27.5f) < 0.01f,
              "rivet radius round trip");
        check(rivet->color.r == 12 && rivet->color.g == 34 && rivet->color.b == 56,
              "rivet colour round trip");
        check(rivet->gravity == GravityDirection::Left,
              "rivet gravity round trip");
        check(rivet->events.size() == 1 &&
                  rivet->events.front().type == EventType::GravityDirection &&
                  rivet->events.front().startValue == "8",
              "rivet gravity event round trip");
    } else {
        check(false, "rivet present after load");
    }
    check(loaded.judges.size() == 1, "judge line count round trip");
    if (!loaded.judges.empty()) {
        check(std::fabs(loaded.judges.front().width - 4000.0f) < 0.01f &&
                  std::fabs(loaded.judges.front().height - 5.0f) < 0.01f,
              "judge line size round trip");
        check(loaded.judges.front().color.r == 255 && loaded.judges.front().color.g == 238 &&
                  loaded.judges.front().color.b == 150,
              "judge line colour round trip");
        check(loaded.judges.front().ghost, "judge line ghost state round trip");
    }
    if (const Note* holdNote = loaded.findNote(b)) {
        const auto it = holdNote->variables.find("height");
        check(it != holdNote->variables.end() && std::fabs(it->second - 150.0f) < 0.01f,
              "per-note hold variable round trip");
    }
    if (const Note* tapNote = loaded.findNote(a)) {
        const auto it = tapNote->variables.find("width");
        check(it != tapNote->variables.end() && std::fabs(it->second - 42.0f) < 0.01f,
              "per-note variable round trip");
        check(tapNote->gravity == GravityDirection::UpRight,
              "per-note gravity round trip");
    }
    if (const Note* holdNote = loaded.findNote(b))
        check(holdNote->gravity == GravityDirection::None,
              "gravity none round trip");
    check(loaded.revJoints.size() == 1, "revolute count round trip");
    check(loaded.weldJoints.size() == 1, "weld count round trip");
    check(loaded.findNote(a) != nullptr && loaded.findNote(b) != nullptr,
          "note ids preserved");
    check(std::fabs(loaded.gravity - 12.5f) < 0.01f, "gravity round trip");
    check(loaded.velocityIterations == 12 && loaded.positionIterations == 5,
          "solver iteration round trip");
    check(loaded.slowStepGear == 25, "slow-step gear round trip");
    check(loaded.eventSplit == 8, "event split round trip");
    check(loaded.fields.size() == 4, "force field count round trip");
    if (!loaded.fields.empty()) {
        const ForceField& f = loaded.fields.front();
        check(f.kind == ForceFieldKind::Buoyancy &&
                  f.shape == ForceFieldShape::Circle,
              "force field kind and shape round trip");
        check(std::fabs(f.position.x - 120.0f) < 0.01f &&
                  std::fabs(f.position.y - 80.0f) < 0.01f &&
                  std::fabs(f.radius - 95.0f) < 0.01f,
              "force field geometry round trip");
        check(std::fabs(f.buoyancy - 1.4f) < 0.01f,
              "buoyancy fluid density round trip");
        check(std::fabs(f.damping - 4.5f) < 0.01f, "gravity damping round trip");
        check(f.events.size() == 1 && f.events.front().track == 9 &&
                  f.events.front().type == EventType::Direction &&
                  std::fabs(f.events.front().endBeat - 4.0f) < 0.01f,
              "field event round trip");
    }
    if (const Note* tapEvent = loaded.findNote(a)) {
        check(tapEvent->events.size() == 2 &&
                  tapEvent->events.front().type == EventType::MoveY &&
                  tapEvent->events.front().easing == 7 &&
                  tapEvent->events.front().endValue == "this+80" &&
                  tapEvent->events.front().instant &&
                  tapEvent->events.front().angleValue == "this+15",
              "note event instant and angle round trip");
        check(tapEvent->events[1].type == EventType::DirectionalMomentum &&
                  tapEvent->events[1].startValue == "3" &&
                  tapEvent->events[1].endValue == "5" &&
                  tapEvent->events[1].angleValue == "this+45" &&
                  tapEvent->events[1].instant,
              "momentum event round trip");
    }
    if (!loaded.judges.empty())
        check(loaded.judges.front().events.size() == 2 &&
                  loaded.judges.front().events.front().type == EventType::Rotate &&
                  loaded.judges.front().events[1].type == EventType::Ghost &&
                  loaded.judges.front().events[1].instant,
              "judge event and ghost switch round trip");
    if (loaded.fields.size() >= 3) {
        check(loaded.fields[1].kind == ForceFieldKind::Drag &&
                  std::fabs(loaded.fields[1].strength - 13.0f) < 0.01f,
              "drag field kind and strength round trip");
        check(loaded.fields[2].kind == ForceFieldKind::GravityChange &&
                  std::fabs(loaded.fields[2].buoyancyDirection.y - 1.0f) < 0.01f,
              "gravity change field direction round trip");
    }
    if (loaded.fields.size() >= 4) {
        check(loaded.fields[3].kind == ForceFieldKind::Vortex &&
                  std::fabs(loaded.fields[3].vortex + 27.5f) < 0.01f,
              "vortex field signed strength round trip");
    }
}

} // namespace

bool runSelfTests() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    passCount = 0;
    failCount = 0;
    std::printf("PhiPhysiceEditorNeo self test\n=========================\n");

    std::error_code ec;
    // Keep test data inside the working directory: some sandboxed Windows
    // environments deny writes to the OS temp folder.
    fs::path temp = fs::current_path() / "phiphysice_selftest";
    fs::remove_all(temp, ec);
    fs::create_directories(temp, ec);

    testPolygon();
    testHoldGeometry();
    testExpressions();
    testNoteFiles(temp);
    testLegacyNoteMigration(temp);
    testPhysics();
    testPerBodyGravity();
    testNoteContactStability();
    testForceFields();
    testEvents();
    testEventEditing();
    testRenumbering();
    testPrefabExport(temp);
    testBatchGravity();
    testBatchGhost();
    testProject(temp);
    testProjectMigration(temp);
    testSettingsRoundTrip(temp);

    fs::remove_all(temp, ec);
    std::printf("=========================\n%d passed, %d failed\n", passCount, failCount);
    return failCount == 0;
}

} // namespace ppn