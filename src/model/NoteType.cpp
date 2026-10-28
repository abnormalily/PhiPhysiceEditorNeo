#include "model/NoteType.h"
#include "physics/Physics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace ppn {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (unsigned char)s[a] <= 32) ++a;
    while (b > a && (unsigned char)s[b - 1] <= 32) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool parseFloat(const std::string& token, float& out) {
    try {
        size_t used = 0;
        out = std::stof(token, &used);
        return used == token.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

void computeArea(NoteType& type) {
    type.area = std::fabs(polygonSignedArea(type.vertices));
}

} // namespace

NoteKind noteKindFromName(const std::string& name) {
    if (name == "Tap") return NoteKind::Tap;
    if (name == "Drag") return NoteKind::Drag;
    if (name == "Flick") return NoteKind::Flick;
    if (name == "Hold") return NoteKind::Hold;
    return NoteKind::Custom;
}

const char* noteKindName(NoteKind kind) {
    switch (kind) {
        case NoteKind::Tap: return "Tap";
        case NoteKind::Drag: return "Drag";
        case NoteKind::Flick: return "Flick";
        case NoteKind::Hold: return "Hold";
        default: return nullptr;
    }
}

bool NoteLibrary::supportedName(const std::string& name) {
    return noteKindFromName(name) != NoteKind::Custom;
}

// ---------------------------------------------------------------------------
// Hold geometry
// ---------------------------------------------------------------------------
namespace {

// Splits an eight-vertex hold outline into the four upper (height-driven)
// vertices and the four lower (fixed) vertices. Polygon order is preserved in
// the id arrays.
bool splitHoldVertexIds(const Poly& vertices, std::vector<int>& topIds,
                        std::vector<int>& bottomIds) {
    topIds.clear();
    bottomIds.clear();
    if (vertices.size() != 8) return false;
    std::vector<int> ids;
    ids.reserve(8);
    for (int i = 0; i < 8; ++i) ids.push_back(i);
    std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
        return vertices[(size_t)a].y > vertices[(size_t)b].y;
    });
    topIds.assign(ids.begin(), ids.begin() + 4);
    bottomIds.assign(ids.begin() + 4, ids.end());
    return true;
}

} // namespace

HoldGeometry extractHoldGeometry(const Poly& vertices) {
    HoldGeometry g;
    if (!splitHoldVertexIds(vertices, g.topIds, g.bottomIds)) return g;
    float loY = vertices[0].y;
    float hiY = vertices[0].y;
    for (const auto& v : vertices) {
        loY = std::min(loY, v.y);
        hiY = std::max(hiY, v.y);
    }
    g.valid = true;
    g.baseHeight = hiY - loY;
    for (int i : g.bottomIds) g.bottom.push_back(vertices[(size_t)i]);
    for (int i : g.topIds) g.top.push_back(vertices[(size_t)i]);
    return g;
}

Poly NoteType::renderVertices() const {
    return renderVertices(std::map<std::string, float>{});
}

Poly NoteType::renderVertices(const std::map<std::string, float>& values) const {
    std::vector<NoteVariable> resolved = variables;
    for (auto& variable : resolved) {
        const auto it = values.find(variable.name);
        if (it != values.end()) variable.value = it->second;
    }
    // Node bindings replace vertex coordinates around the authored origin. That
    // origin never moves: only the shape (and therefore its area centroid)
    // changes. Hold height depends on this, because its four upper nodes are
    // written as "this + height" while the bottom edge stays fixed. The
    // renderer and the physics body builder separately derive the centre of
    // gravity from the resulting outline, so the two concepts stay independent.
    return applyNodeBindings(vertices, bindings, resolved);
}

const NoteVariable* NoteType::findVariable(const std::string& variableName) const {
    for (const auto& v : variables)
        if (v.name == variableName) return &v;
    return nullptr;
}

NoteVariable* NoteType::findVariable(const std::string& variableName) {
    for (auto& v : variables)
        if (v.name == variableName) return &v;
    return nullptr;
}


// ---------------------------------------------------------------------------
// Variables, node bindings and expressions
// ---------------------------------------------------------------------------
namespace {

bool isIdentStart(char c) {
    return std::isalpha((unsigned char)c) || c == '_';
}
bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_';
}

class ExpressionParser {
public:
    ExpressionParser(const std::string& text, const std::vector<NoteVariable>& variables)
        : text_(text), variables_(variables) {}

    bool parse(float& out, std::string& error) {
        pos_ = 0;
        error_.clear();
        skipSpace();
        if (pos_ >= text_.size()) {
            error = "表达式为空";
            return false;
        }
        float value = 0.0f;
        if (!parseAdd(value)) {
            error = error_.empty() ? "表达式无效" : error_;
            return false;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            error = "第 " + std::to_string(pos_ + 1) + " 个字符无法解析";
            return false;
        }
        if (!std::isfinite(value)) {
            error = "表达式结果不是有限数值";
            return false;
        }
        out = value;
        return true;
    }

private:
    bool fail(const std::string& message) {
        if (error_.empty()) error_ = message;
        return false;
    }
    void skipSpace() {
        while (pos_ < text_.size() && (unsigned char)text_[pos_] <= 32) ++pos_;
    }
    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool parseAdd(float& out) {
        float left = 0.0f;
        if (!parseMul(left)) return false;
        for (;;) {
            skipSpace();
            const char op = peek();
            if (op != '+' && op != '-') break;
            ++pos_;
            float right = 0.0f;
            if (!parseMul(right)) return false;
            left = op == '+' ? left + right : left - right;
        }
        out = left;
        return true;
    }

    bool parseMul(float& out) {
        float left = 0.0f;
        if (!parseUnary(left)) return false;
        for (;;) {
            skipSpace();
            const char op = peek();
            if (op != '*' && op != '/' && op != '%') break;
            ++pos_;
            float right = 0.0f;
            if (!parseUnary(right)) return false;
            if ((op == '/' || op == '%') && std::fabs(right) < 1e-12f)
                return fail("除数不能为 0");
            if (op == '*') left *= right;
            else if (op == '/') left /= right;
            else left = std::fmod(left, right);
        }
        out = left;
        return true;
    }

    bool parseUnary(float& out) {
        skipSpace();
        const char op = peek();
        if (op == '+' || op == '-') {
            ++pos_;
            float value = 0.0f;
            if (!parseUnary(value)) return false;
            out = op == '-' ? -value : value;
            return true;
        }
        return parsePower(out);
    }

    bool parsePower(float& out) {
        float base = 0.0f;
        if (!parsePrimary(base)) return false;
        skipSpace();
        if (peek() == '^') {
            ++pos_;
            float exponent = 0.0f;
            if (!parseUnary(exponent)) return false;
            out = std::pow(base, exponent);
            return true;
        }
        out = base;
        return true;
    }

    bool parsePrimary(float& out) {
        skipSpace();
        if (pos_ >= text_.size()) return fail("表达式缺少数值");
        const char c = text_[pos_];
        if (c == '(') {
            ++pos_;
            if (!parseAdd(out)) return false;
            skipSpace();
            if (peek() != ')') return fail("缺少右括号");
            ++pos_;
            return true;
        }
        if (std::isdigit((unsigned char)c) || c == '.') {
            const char* begin = text_.c_str() + pos_;
            char* end = nullptr;
            const float value = std::strtof(begin, &end);
            if (end == begin) return fail("数值格式无效");
            pos_ = (size_t)(end - text_.c_str());
            out = value;
            return true;
        }
        if (isIdentStart(c)) {
            const size_t start = pos_;
            while (pos_ < text_.size() && isIdentChar(text_[pos_])) ++pos_;
            const std::string ident = text_.substr(start, pos_ - start);
            skipSpace();
            if (peek() == '(') {
                ++pos_;
                std::vector<float> args;
                skipSpace();
                if (peek() == ')') {
                    ++pos_;
                } else {
                    for (;;) {
                        float arg = 0.0f;
                        if (!parseAdd(arg)) return false;
                        args.push_back(arg);
                        skipSpace();
                        if (peek() == ',') {
                            ++pos_;
                            continue;
                        }
                        if (peek() == ')') {
                            ++pos_;
                            break;
                        }
                        return fail("函数参数缺少右括号");
                    }
                }
                return applyFunction(ident, args, out);
            }
            if (ident == "pi" || ident == "PI") {
                out = PI;
                return true;
            }
            if (ident == "e" || ident == "E") {
                out = 2.71828182845904523536f;
                return true;
            }
            for (const auto& variable : variables_) {
                if (variable.name == ident) {
                    out = variable.value;
                    return true;
                }
            }
            return fail("未知变量：" + ident);
        }
        return fail("无法识别的字符");
    }

    bool applyFunction(const std::string& name, const std::vector<float>& a, float& out) {
        auto one = [&](auto fn) {
            if (a.size() != 1) return fail("函数 " + name + " 需要 1 个参数");
            out = fn(a[0]);
            return true;
        };
        if (name == "abs") return one([](float v) { return std::fabs(v); });
        if (name == "sqrt") return one([](float v) { return std::sqrt(std::max(0.0f, v)); });
        if (name == "sin") return one([](float v) { return std::sin(v); });
        if (name == "cos") return one([](float v) { return std::cos(v); });
        if (name == "tan") return one([](float v) { return std::tan(v); });
        if (name == "floor") return one([](float v) { return std::floor(v); });
        if (name == "ceil") return one([](float v) { return std::ceil(v); });
        if (name == "round") return one([](float v) { return std::round(v); });
        if (name == "min" || name == "max") {
            if (a.size() < 2) return fail("函数 " + name + " 至少需要 2 个参数");
            float value = a[0];
            for (size_t i = 1; i < a.size(); ++i)
                value = name == "min" ? std::min(value, a[i]) : std::max(value, a[i]);
            out = value;
            return true;
        }
        if (name == "clamp") {
            if (a.size() != 3) return fail("函数 clamp 需要 3 个参数");
            out = clampf(a[0], std::min(a[1], a[2]), std::max(a[1], a[2]));
            return true;
        }
        return fail("未知函数：" + name);
    }

    const std::string& text_;
    const std::vector<NoteVariable>& variables_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

bool validVariableName(const std::string& name, std::string* reason) {
    if (name.empty()) {
        if (reason) *reason = "变量名不能为空";
        return false;
    }
    if (!isIdentStart(name[0])) {
        if (reason) *reason = "变量名以字母或下划线开头";
        return false;
    }
    for (char c : name) {
        if (!isIdentChar(c)) {
            if (reason) *reason = "变量名使用 ASCII 字母、数字和下划线";
            return false;
        }
    }
    return true;
}

std::string compactExpression(const std::string& expression) {
    std::string out;
    out.reserve(expression.size());
    for (char c : expression) {
        if (std::isspace((unsigned char)c)) continue;
        out.push_back(c);
    }
    return out;
}

bool evaluateNoteExpression(const std::string& expression,
                            const std::vector<NoteVariable>& variables, float& out,
                            std::string* error) {
    ExpressionParser parser(expression, variables);
    std::string message;
    if (!parser.parse(out, message)) {
        if (error) *error = message;
        return false;
    }
    return true;
}

// normalizePolygon() preserves the authored node list and only possibly
// reverses its winding. Node bindings refer to file vertex order, so translate
// their indices by index instead of matching positions (coincident nodes would
// otherwise collapse onto the first match).
void remapBindingIndices(const Poly& source, const Poly& normalized,
                         std::vector<NodeBinding>& bindings) {
    if (source.empty() || normalized.empty() || bindings.empty()) return;
    const size_t count = std::min(source.size(), normalized.size());
    bool sameOrder = source.size() == normalized.size();
    bool reversedOrder = source.size() == normalized.size();
    for (size_t i = 0; i < count; ++i) {
        if (lengthSq(source[i] - normalized[i]) > 1e-6f) sameOrder = false;
        if (lengthSq(source[i] - normalized[normalized.size() - 1 - i]) > 1e-6f)
            reversedOrder = false;
    }
    if (sameOrder) return;
    if (reversedOrder) {
        for (auto& binding : bindings) {
            if (binding.vertex >= 0 && binding.vertex < (int)normalized.size())
                binding.vertex = (int)normalized.size() - 1 - binding.vertex;
        }
        return;
    }
    // Fallback for unexpected transforms: match each source vertex to the
    // nearest normalized vertex.
    for (auto& binding : bindings) {
        if (binding.vertex < 0 || binding.vertex >= (int)source.size()) continue;
        const Vec2 target = source[(size_t)binding.vertex];
        int best = -1;
        float bestDistance = 1e30f;
        for (size_t i = 0; i < normalized.size(); ++i) {
            const float d = lengthSq(normalized[i] - target);
            if (d < bestDistance) {
                bestDistance = d;
                best = (int)i;
            }
        }
        if (best >= 0) binding.vertex = best;
    }
}

Poly applyNodeBindings(const Poly& vertices, const std::vector<NodeBinding>& bindings,
                       const std::vector<NoteVariable>& variables,
                       std::vector<std::string>* errors) {
    Poly result = vertices;
    for (const auto& binding : bindings) {
        if (binding.vertex < 0 || binding.vertex >= (int)result.size()) {
            if (errors)
                errors->push_back("节点 #" + std::to_string(binding.vertex) + " 超出顶点范围");
            continue;
        }
        // Bindings replace the node coordinate instead of adding an offset.
        // "this" is the node's authored default coordinate in the file.
        const Vec2 base = vertices[(size_t)binding.vertex];
        std::vector<NoteVariable> xVariables = variables;
        xVariables.push_back({"this", "this", base.x});
        std::vector<NoteVariable> yVariables = variables;
        yVariables.push_back({"this", "this", base.y});
        float x = base.x;
        float y = base.y;
        std::string error;
        if (!evaluateNoteExpression(binding.x, xVariables, x, &error)) {
            if (errors)
                errors->push_back("节点 #" + std::to_string(binding.vertex) + " X 表达式：" +
                                  error);
            continue;
        }
        if (!evaluateNoteExpression(binding.y, yVariables, y, &error)) {
            if (errors)
                errors->push_back("节点 #" + std::to_string(binding.vertex) + " Y 表达式：" +
                                  error);
            continue;
        }
        result[(size_t)binding.vertex] = {x, y};
    }
    return result;
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------
bool NoteLibrary::validName(const std::string& name, std::string* reason) {
    if (name.empty()) {
        if (reason) *reason = "名称不能为空";
        return false;
    }
    if (name.size() > 32) {
        if (reason) *reason = "名称最多 32 个字符";
        return false;
    }
    if (!std::isalpha((unsigned char)name[0])) {
        if (reason) *reason = "名称以字母开头";
        return false;
    }
    for (char c : name) {
        if (!std::isalnum((unsigned char)c) && c != '_' && c != '-') {
            if (reason) *reason = "名称使用 ASCII 字母、数字、_ 和 -";
            return false;
        }
    }
    return true;
}

bool parseNoteTypeFile(const fs::path& path, NoteType& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        error = "无法打开文件";
        return false;
    }

    NoteType type;
    type.sourcePath = path;
    int declaredCount = -1;
    bool hasName = false;
    int lineNo = 0;
    std::string line;
    // Files without a version/axis marker follow the legacy Y-down convention.
    bool legacyYDown = true;

    auto parseVariable = [&](const std::string& body) -> bool {
        std::string rest = trim(body);
        if (rest.empty()) {
            error = "第 " + std::to_string(lineNo) + " 行：变量名缺失";
            return false;
        }
        std::istringstream vss(rest);
        std::string varName;
        vss >> varName;
        if (!validVariableName(varName)) {
            error = "第 " + std::to_string(lineNo) + " 行：变量名无效（" + varName + "）";
            return false;
        }
        for (const auto& existing : type.variables) {
            if (existing.name == varName) {
                error = "第 " + std::to_string(lineNo) + " 行：变量 " + varName + " 重复";
                return false;
            }
        }
        std::string tail;
        std::getline(vss, tail);
        tail = trim(tail);
        float value = 0.0f;
        const size_t equals = tail.rfind('=');
        if (equals != std::string::npos) {
            const std::string valueText = trim(tail.substr(equals + 1));
            float parsed = 0.0f;
            if (parseFloat(valueText, parsed)) {
                value = parsed;
                tail = trim(tail.substr(0, equals));
            }
        }
        NoteVariable variable;
        variable.name = varName;
        variable.displayName = tail.empty() ? varName : tail;
        variable.value = value;
        type.variables.push_back(std::move(variable));
        return true;
    };

    auto parseBinding = [&](const std::string& body) -> bool {
        std::string rest = trim(body);
        std::istringstream bss(rest);
        int vertex = -1;
        if (!(bss >> vertex)) {
            error = "第 " + std::to_string(lineNo) + " 行：节点编号缺失";
            return false;
        }
        std::string tail;
        std::getline(bss, tail);
        tail = trim(tail);
        if (tail.empty()) {
            error = "第 " + std::to_string(lineNo) + " 行：节点表达式缺失";
            return false;
        }
        NodeBinding binding;
        binding.vertex = vertex;
        std::istringstream tss(tail);
        const bool axisForm = tail.rfind("x=", 0) == 0 || tail.rfind("y=", 0) == 0;
        if (axisForm) {
            std::string token;
            bool any = false;
            while (tss >> token) {
                if (token.rfind("x=", 0) == 0 && token.size() > 2) {
                    binding.x = token.substr(2);
                    any = true;
                } else if (token.rfind("y=", 0) == 0 && token.size() > 2) {
                    binding.y = token.substr(2);
                    any = true;
                } else {
                    error = "第 " + std::to_string(lineNo) + " 行：节点表达式应为 x=... y=...";
                    return false;
                }
            }
            if (!any) {
                error = "第 " + std::to_string(lineNo) + " 行：节点表达式缺失";
                return false;
            }
        } else {
            std::vector<std::string> tokens;
            std::string token;
            while (tss >> token) tokens.push_back(token);
            if (tokens.size() == 1) {
                binding.y = tokens[0];
            } else if (tokens.size() == 2) {
                binding.x = tokens[0];
                binding.y = tokens[1];
            } else {
                error = "第 " + std::to_string(lineNo) +
                        " 行：节点表达式应为 1 个（Y）或 2 个（X Y）";
                return false;
            }
        }
        type.bindings.push_back(std::move(binding));
        return true;
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string raw = trim(line);
        // Accept both the current header and the legacy PolyNote header so old
        // component files keep their Y-axis interpretation.
        if (raw.rfind("# PhiPhysiceEditorNeo shape v2", 0) == 0 ||
            raw.rfind("# PolyNote shape v2", 0) == 0)
            legacyYDown = false;
        else if (raw.rfind("# PhiPhysiceEditorNeo shape v1", 0) == 0 ||
                 raw.rfind("# PolyNote shape v1", 0) == 0)
            legacyYDown = true;

        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '&') {
            if (!parseVariable(line.substr(1))) return false;
            continue;
        }
        if (line[0] == '$') {
            if (!parseBinding(line.substr(1))) return false;
            continue;
        }

        std::istringstream iss(line);
        std::string key;
        iss >> key;
        const std::string lowerKey = lower(key);
        if (lowerKey == "phiphysice" || lowerKey == "phiphysiceeditorneo" ||
            lowerKey == "polynote" || lowerKey == "shape" || lowerKey == "format")
            continue;
        if (lowerKey == "yaxis") {
            std::string direction;
            if (iss >> direction) {
                const std::string dir = lower(direction);
                if (dir == "up") legacyYDown = false;
                else if (dir == "down") legacyYDown = true;
            }
            continue;
        }
        if (lowerKey == "name") {
            std::string rest;
            std::getline(iss, rest);
            rest = trim(rest);
            if (!rest.empty() && rest.front() == '"' && rest.back() == '"')
                rest = rest.substr(1, rest.size() - 2);
            type.name = rest;
            hasName = true;
        } else if (lowerKey == "vertices" || lowerKey == "count" || lowerKey == "vertexcount") {
            if (!(iss >> declaredCount)) {
                error = "第 " + std::to_string(lineNo) + " 行：顶点数量无效";
                return false;
            }
        } else if (lowerKey == "v" || lowerKey == "vertex" || lowerKey == "vert") {
            std::string xs, ys;
            float x = 0.0f, y = 0.0f;
            if (!(iss >> xs >> ys) || !parseFloat(xs, x) || !parseFloat(ys, y)) {
                error = "第 " + std::to_string(lineNo) + " 行：顶点坐标无效";
                return false;
            }
            type.vertices.push_back({x, y});
        } else if (lowerKey == "density") {
            float v = 0.0f;
            if (!(iss >> v) || !std::isfinite(v)) {
                error = "第 " + std::to_string(lineNo) + " 行：密度无效";
                return false;
            }
            type.density = v;
        } else if (lowerKey == "friction") {
            float v = 0.0f;
            if (!(iss >> v) || !std::isfinite(v)) {
                error = "第 " + std::to_string(lineNo) + " 行：摩擦系数无效";
                return false;
            }
            type.friction = v;
        } else if (lowerKey == "restitution") {
            float v = 0.0f;
            if (!(iss >> v) || !std::isfinite(v)) {
                error = "第 " + std::to_string(lineNo) + " 行：弹性系数无效";
                return false;
            }
            type.restitution = v;
        } else if (lowerKey == "color" || lowerKey == "colour") {
            int r = 0, g = 0, b = 0;
            if (!(iss >> r >> g >> b) || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 ||
                b > 255) {
                error = "第 " + std::to_string(lineNo) + " 行：颜色无效（应为 0-255 的 RGB）";
                return false;
            }
            type.color = {(unsigned char)r, (unsigned char)g, (unsigned char)b};
        }
        // Unknown keys are ignored for forward compatibility.
    }

    if (!hasName || type.name.empty()) {
        error = "缺少 name 字段";
        return false;
    }
    if (!NoteLibrary::validName(type.name)) {
        error = "音符名称无效（使用 ASCII 字母、数字、_、-）";
        return false;
    }
    if (declaredCount >= 0 && (int)type.vertices.size() != declaredCount) {
        error = "顶点数量不匹配：声明 " + std::to_string(declaredCount) +
                "，实际 " + std::to_string(type.vertices.size());
        return false;
    }
    // Legacy files used Y-down design coordinates; flip them so the rest of the
    // editor can consistently treat positive Y as up.
    if (legacyYDown && !type.vertices.empty()) {
        for (auto& v : type.vertices) v.y = -v.y;
    }
    const Poly sourceVertices = type.vertices;
    std::string geomError;
    if (!normalizePolygon(type.vertices, &geomError)) {
        error = geomError;
        return false;
    }
    remapBindingIndices(sourceVertices, type.vertices, type.bindings);
    if (type.vertices.size() > 64) {
        error = "顶点数量过多";
        return false;
    }
    for (size_t i = 0; i < type.bindings.size(); ++i) {
        const NodeBinding& binding = type.bindings[i];
        if (binding.vertex < 0 || binding.vertex >= (int)type.vertices.size()) {
            error = "节点绑定 #" + std::to_string(binding.vertex) + " 超出顶点范围";
            return false;
        }
    }
    type.needsRewrite = legacyYDown;
    type.density = clampf(type.density, 0.05f, 100.0f);
    type.friction = clampf(type.friction, 0.0f, 2.0f);
    type.restitution = clampf(type.restitution, 0.0f, 1.0f);
    computeArea(type);
    out = std::move(type);
    return true;
}

bool writeNoteTypeFile(const fs::path& path, const NoteType& input, std::string& error) {
    NoteType type = input;
    std::string reason;
    if (!NoteLibrary::validName(type.name, &reason)) {
        error = reason;
        return false;
    }
    const Poly sourceVertices = type.vertices;
    if (!normalizePolygon(type.vertices, &reason)) {
        error = reason;
        return false;
    }
    remapBindingIndices(sourceVertices, type.vertices, type.bindings);
    if (type.vertices.size() > 64) {
        error = "顶点数量过多（上限 64）";
        return false;
    }
    for (size_t i = 0; i < type.variables.size(); ++i) {
        const NoteVariable& variable = type.variables[i];
        if (!validVariableName(variable.name, &reason)) {
            error = "变量 " + variable.name + "：" + reason;
            return false;
        }
        if (!std::isfinite(variable.value)) {
            error = "变量 " + variable.name + " 的数值无效";
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (type.variables[j].name == variable.name) {
                error = "变量名重复：" + variable.name;
                return false;
            }
        }
    }
    for (const auto& binding : type.bindings) {
        if (binding.vertex < 0 || binding.vertex >= (int)type.vertices.size()) {
            error = "节点绑定 #" + std::to_string(binding.vertex) + " 超出顶点范围";
            return false;
        }
        const Vec2 base = type.vertices[(size_t)binding.vertex];
        std::vector<NoteVariable> xVariables = type.variables;
        xVariables.push_back({"this", "this", base.x});
        std::vector<NoteVariable> yVariables = type.variables;
        yVariables.push_back({"this", "this", base.y});
        float unused = 0.0f;
        if (!evaluateNoteExpression(binding.x, xVariables, unused, &reason)) {
            error = "节点 #" + std::to_string(binding.vertex) + " X 表达式：" + reason;
            return false;
        }
        if (!evaluateNoteExpression(binding.y, yVariables, unused, &reason)) {
            error = "节点 #" + std::to_string(binding.vertex) + " Y 表达式：" + reason;
            return false;
        }
    }
    type.density = clampf(type.density, 0.05f, 100.0f);
    type.friction = clampf(type.friction, 0.0f, 2.0f);
    type.restitution = clampf(type.restitution, 0.0f, 1.0f);

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "无法创建音符文件夹: " + ec.message();
        return false;
    }

    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "无法写入文件";
            return false;
        }
        out << "# PhiPhysiceEditorNeo shape v2\n";
        out << "# 设计坐标 Y 轴向上为正；每一行顶点遵循同一规则。\n";
        out << "# &<变量名> <显示名> = <默认值>   新增一个可编辑变量\n";
        out << "# $<节点编号> <X表达式> <Y表达式>  节点随变量偏移\n";
        out << "yaxis up\n";
        out << "name " << type.name << "\n";
        out << "vertices " << type.vertices.size() << "\n";
        out << std::fixed << std::setprecision(3);
        for (const auto& v : type.vertices) {
            out << "v " << v.x << " " << v.y << "\n";
        }
        out << std::defaultfloat;
        out << "density " << type.density << "\n";
        out << "friction " << type.friction << "\n";
        out << "restitution " << type.restitution << "\n";
        out << "color " << (int)type.color.r << " " << (int)type.color.g << " "
            << (int)type.color.b << "\n";
        for (const auto& variable : type.variables) {
            out << "&" << variable.name << " " << variable.displayName;
            if (std::fabs(variable.value) > 1e-6f) {
                out << " = " << std::fixed << std::setprecision(3) << variable.value
                    << std::defaultfloat;
            }
            out << "\n";
        }
        for (const auto& binding : type.bindings) {
            out << "$" << binding.vertex << " " << compactExpression(binding.x) << " "
                << compactExpression(binding.y) << "\n";
        }
        out.flush();
        if (!out.good()) {
            out.close();
            fs::remove(temp, ec);
            error = "写入文件失败";
            return false;
        }
    }

    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "保存文件失败: " + ec.message();
        return false;
    }
    return true;
}

bool NoteLibrary::load(const fs::path& folderPath) {
    folder = folderPath;
    types.clear();
    errors.clear();

    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        errors.push_back("无法创建音符文件夹: " + ec.message());
        return false;
    }

    std::vector<fs::path> files;
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string ext = lower(it->path().extension().string());
        if (ext != ".note") continue;
        files.push_back(it->path());
    }
    if (ec) errors.push_back("读取音符文件夹失败: " + ec.message());

    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().string() < b.filename().string();
    });

    for (const auto& file : files) {
        // Non-shape component files (rivet / judge / field definitions) share
        // the .note extension but are not note types.
        if (!NoteLibrary::supportedName(file.stem().string())) continue;
        NoteType type;
        std::string error;
        if (!parseNoteTypeFile(file, type, error)) {
            errors.push_back(file.filename().string() + ": " + error);
            continue;
        }
        // The file stem must match the declared name, so the folder stays tidy.
        if (file.stem().string() != type.name) {
            errors.push_back(file.filename().string() + ": 文件名与 name 字段不一致，已忽略");
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : types) {
            if (existing.name == type.name) duplicate = true;
        }
        if (duplicate) {
            errors.push_back(file.filename().string() + ": 音符名称重复，已忽略");
            continue;
        }
        // Loading is read-only: legacy Y-down data is converted in memory, but
        // the stored .note file is never rewritten behind the user's back.
        types.push_back(std::move(type));
    }

    // Only Tap / Drag / Flick / Hold are supported: anything else is kept on
    // disk but never listed, so the library always shows the four note kinds.
    std::stable_sort(types.begin(), types.end(), [](const NoteType& a, const NoteType& b) {
        const int ka = (int)a.kind();
        const int kb = (int)b.kind();
        if (ka != kb) return ka < kb;
        return a.name < b.name;
    });
    types.erase(std::remove_if(types.begin(), types.end(),
                               [](const NoteType& t) { return !NoteLibrary::supportedName(t.name); }),
                types.end());
    return true;
}

NoteType* NoteLibrary::find(const std::string& name) {
    for (auto& t : types)
        if (t.name == name) return &t;
    return nullptr;
}

const NoteType* NoteLibrary::find(const std::string& name) const {
    for (const auto& t : types)
        if (t.name == name) return &t;
    return nullptr;
}

bool NoteLibrary::saveType(NoteType type, std::string& error, bool overwrite) {
    std::string reason;
    if (!NoteLibrary::validName(type.name, &reason)) {
        error = reason;
        return false;
    }
    if (!NoteLibrary::supportedName(type.name)) {
        error = "支持的类型为 Tap、Drag、Flick、Hold";
        return false;
    }
    if (!overwrite && find(type.name)) {
        error = "已存在同名音符类型";
        return false;
    }
    // Do not allow saving over a different file that happens to share the name.
    const fs::path path = folder / (type.name + ".note");
    if (!writeNoteTypeFile(path, type, error)) return false;
    return true;
}

bool NoteLibrary::deleteType(const std::string& name, std::string& error) {
    for (const auto& t : types) {
        if (t.name != name) continue;
        std::error_code ec;
        if (!fs::remove(t.sourcePath, ec) || ec) {
            error = "删除失败: " + ec.message();
            return false;
        }
        return true;
    }
    error = "未找到该音符类型";
    return false;
}

} // namespace ppn