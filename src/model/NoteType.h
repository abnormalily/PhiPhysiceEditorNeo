#pragma once
// File backed note types. Each .note file stores the note name, vertex count and
// every vertex position (plus optional material properties).

#include "core/Math.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ppn {

struct Rgb {
    unsigned char r = 96;
    unsigned char g = 165;
    unsigned char b = 250;
};

// The four note kinds the editor understands. All geometry is read from the
// .note files; the program never supplies or rewrites node defaults.
enum class NoteKind { Tap, Drag, Flick, Hold, Custom };

NoteKind noteKindFromName(const std::string& name);
const char* noteKindName(NoteKind kind); // nullptr for NoteKind::Custom
// Human readable description of a type, e.g. "Hold 音符" or "自定义音符".
inline std::string noteKindLabel(const std::string& name) {
    switch (noteKindFromName(name)) {
        case NoteKind::Tap: return "Tap 音符";
        case NoteKind::Drag: return "Drag 音符";
        case NoteKind::Flick: return "Flick 音符";
        case NoteKind::Hold: return "Hold 音符";
        default: break;
    }
    return "自定义音符";
}

// Hold geometry: the file stores the neutral eight-vertex outline. The helper
// is retained so the shape editor can mark the four upper (height-driven) and
// four lower (fixed) anchors; geometry changes come from variables/bindings.
struct HoldGeometry {
    bool valid = false;
    Poly bottom; // fixed vertices, in file space
    Poly top;    // moving vertices, in file space
    std::vector<int> bottomIds;
    std::vector<int> topIds;
    float baseHeight = 0.0f; // neutral outline thickness, design px
};

// A named numeric parameter declared in a .note file:
//     &height 高度 = 120
// The value is the type default. Every placed note can override it; the note
// inspector exposes those per-note inputs.
struct NoteVariable {
    std::string name;        // ASCII identifier used by expressions
    std::string displayName; // UTF-8 label shown in the editor
    float value = 0.0f;
};

// A vertex driven by one or more variables:
//     $4 this this+height   (node 4: keep default X, lift Y by height)
// The expressions replace the node's X/Y coordinates. Inside them "this" is the
// authored default value of that coordinate in the file, so additive behaviour
// is written explicitly as "this + ...".
struct NodeBinding {
    int vertex = 0;
    std::string x = "this";
    std::string y = "this";
};

struct NoteType {
    std::string name;
    Poly vertices; // design px, authored origin at (0,0), positive winding, Y-up
    float density = 1.0f;
    float friction = 0.45f;
    float restitution = 0.08f;
    Rgb color{96, 165, 250};
    std::filesystem::path sourcePath;
    float area = 0.0f; // design px^2
    std::vector<NoteVariable> variables;
    std::vector<NodeBinding> bindings;
    // Set while parsing a legacy Y-down file. The converted data lives only in
    // memory; loading never rewrites the stored .note file.
    bool needsRewrite = false;

    NoteKind kind() const { return noteKindFromName(name); }
    bool isHold() const { return kind() == NoteKind::Hold; }
    std::string label() const { return noteKindLabel(name); }

    // Vertices used on the canvas / by physics with the type's default variable
    // values. The second overload applies the per-note values on top of the
    // defaults before evaluating node bindings. The authored origin is kept
    // fixed; callers derive the area centroid when they need a centre of mass.
    Poly renderVertices() const;
    Poly renderVertices(const std::map<std::string, float>& values) const;

    const NoteVariable* findVariable(const std::string& name) const;
    NoteVariable* findVariable(const std::string& name);
};

class NoteLibrary {
public:
    std::filesystem::path folder;
    std::vector<NoteType> types;
    std::vector<std::string> errors;

    bool load(const std::filesystem::path& folderPath);
    bool reload() { return load(folder); }
    NoteType* find(const std::string& name);
    const NoteType* find(const std::string& name) const;
    // Only these four file types are supported by the editor.
    static bool supportedName(const std::string& name);
    bool saveType(NoteType type, std::string& error, bool overwrite = true);
    bool deleteType(const std::string& name, std::string& error);

    static bool validName(const std::string& name, std::string* reason = nullptr);
};

// Hold helpers shared by the renderer, physics and shape editor.
HoldGeometry extractHoldGeometry(const Poly& vertices);

// Variable names must start with an ASCII letter or underscore and contain only
// ASCII letters, digits and underscores so expressions stay unambiguous.
bool validVariableName(const std::string& name, std::string* reason = nullptr);
// Removes whitespace so an expression can be written as one .note token.
std::string compactExpression(const std::string& expression);
// Evaluates + - * / % ^, parentheses, unary signs and the functions
// abs/sqrt/sin/cos/tan/min/max/clamp/floor/ceil/round. Variables are looked up
// by name; "pi" and "e" are constants.
bool evaluateNoteExpression(const std::string& expression,
                            const std::vector<NoteVariable>& variables, float& out,
                            std::string* error = nullptr);
// Applies every node binding by replacing the node coordinate. Invalid bindings
// are skipped; when \p errors is provided, one message per failure is appended.
Poly applyNodeBindings(const Poly& vertices, const std::vector<NodeBinding>& bindings,
                       const std::vector<NoteVariable>& variables,
                       std::vector<std::string>* errors = nullptr);

// Parse / serialize the human readable .note format. Legacy Y-down files are
// converted to the Y-up convention while parsing.
bool parseNoteTypeFile(const std::filesystem::path& path, NoteType& out, std::string& error);
bool writeNoteTypeFile(const std::filesystem::path& path, const NoteType& type, std::string& error);

} // namespace ppn