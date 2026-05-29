#include <yaoray/pbrt/pbrt_scene.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yr {
namespace {

constexpr int MaxIncludeDepth = 32;

SceneDiagnostic PbrtError(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

SceneDiagnostic PbrtWarning(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, file, std::move(field), std::move(message)};
}

Mat4f MatrixFromPbrtValues(const std::vector<float>& values) {
    Mat4f result;
    for (std::size_t i = 0; i < result.m.size(); ++i) {
        result.m[i] = values[i];
    }
    return result;
}

struct ScopedPbrtState {
    std::string material_name;
    std::optional<PbrtEntity> inline_material;
    std::optional<PbrtEntity> current_area_light;
    Mat4f transform;
};

struct PbrtParserState {
    PbrtScene scene;
    std::vector<SceneDiagnostic> diagnostics;
    std::string current_material_name;
    std::optional<PbrtEntity> current_inline_material;
    std::optional<PbrtEntity> current_area_light;
    Mat4f current_transform;
    std::vector<ScopedPbrtState> attribute_stack;
    std::vector<Mat4f> transform_stack;
    std::vector<std::filesystem::path> include_stack;
    // For ObjectBegin/End tracking
    std::string current_object_name;
    bool inside_object = false;
};

std::vector<std::string> TokenizePbrt(std::string_view text) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '#') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '[' || c == ']') {
            tokens.push_back(std::string(1, c));
            ++i;
            continue;
        }
        if (c == '"') {
            ++i;
            std::string value;
            while (i < text.size() && text[i] != '"') {
                value.push_back(text[i++]);
            }
            if (i < text.size() && text[i] == '"') {
                ++i;
            }
            tokens.push_back(std::move(value));
            continue;
        }

        std::string value;
        while (i < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[i])) &&
               text[i] != '[' &&
               text[i] != ']' &&
               text[i] != '#') {
            value.push_back(text[i++]);
        }
        tokens.push_back(std::move(value));
    }
    return tokens;
}

bool SplitParamName(std::string_view token, std::string& type, std::string& name) {
    const std::size_t space = token.find(' ');
    if (space == std::string_view::npos || space + 1 >= token.size()) {
        return false;
    }
    type = std::string(token.substr(0, space));
    name = std::string(token.substr(space + 1));
    return true;
}

std::vector<std::string> ReadValueList(const std::vector<std::string>& tokens, std::size_t& index) {
    std::vector<std::string> values;
    if (index < tokens.size() && tokens[index] == "[") {
        ++index;
        while (index < tokens.size() && tokens[index] != "]") {
            values.push_back(tokens[index++]);
        }
        if (index < tokens.size() && tokens[index] == "]") {
            ++index;
        }
    } else if (index < tokens.size()) {
        values.push_back(tokens[index++]);
    }
    return values;
}

std::optional<float> ParseFloatToken(std::string_view token) {
    try {
        const std::string value{token};
        std::size_t consumed = 0;
        const float parsed = std::stof(value, &consumed);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> ParseIntToken(std::string_view token) {
    try {
        const std::string value{token};
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::uint32_t> ParseUintToken(std::string_view token) {
    try {
        const std::string value{token};
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(value, &consumed);
        if (consumed != value.size()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<PbrtParam> ReadParams(const std::vector<std::string>& tokens, std::size_t& index) {
    std::vector<PbrtParam> params;
    while (index < tokens.size()) {
        std::string type;
        std::string name;
        if (!SplitParamName(tokens[index], type, name)) {
            break;
        }
        ++index;
        std::vector<std::string> raw_values = ReadValueList(tokens, index);

        PbrtParam param;
        param.type = type;
        param.name = name;

        // Populate typed fields based on type
        if (type == "float" || type == "rgb" || type == "color" || type == "point3" ||
            type == "point2" || type == "vector3" || type == "normal" || type == "blackbody") {
            param.floats.reserve(raw_values.size());
            for (const std::string& v : raw_values) {
                param.floats.push_back(ParseFloatToken(v).value_or(0.0f));
            }
        } else if (type == "spectrum") {
            // A "spectrum" param may be either:
            //   (a) inline numeric values: "spectrum foo" [0.4 0.5 0.6]  -> stored in floats[]
            //   (b) an SPD filename:       "spectrum foo" ["spds/Au.eta.spd"] -> stored in strings[]
            // Distinguish by trying to parse the first value as a float.
            // If it fails (non-numeric string), treat the whole value list as strings
            // so the compiler can detect and warn about unsupported SPD references.
            if (!raw_values.empty() && !ParseFloatToken(raw_values[0]).has_value()) {
                // Non-numeric: SPD filename reference.
                param.strings = std::move(raw_values);
            } else {
                // Numeric inline spectrum values.
                param.floats.reserve(raw_values.size());
                for (const std::string& v : raw_values) {
                    param.floats.push_back(ParseFloatToken(v).value_or(0.0f));
                }
            }
        } else if (type == "integer") {
            param.ints.reserve(raw_values.size());
            for (const std::string& v : raw_values) {
                param.ints.push_back(ParseIntToken(v).value_or(0));
            }
        } else if (type == "bool") {
            param.bools.reserve(raw_values.size());
            for (const std::string& v : raw_values) {
                param.bools.push_back(v == "true" || v == "1");
            }
        } else {
            // string, texture, or unknown type
            param.strings = std::move(raw_values);
        }

        params.push_back(std::move(param));
    }
    return params;
}

const PbrtParam* FindParam(const std::vector<PbrtParam>& params, std::string_view name) {
    for (const PbrtParam& param : params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

std::string StringParam(const std::vector<PbrtParam>& params, std::string_view name, const std::string& fallback = "") {
    const PbrtParam* param = FindParam(params, name);
    if (param == nullptr || param->strings.empty()) {
        return fallback;
    }
    return param->strings[0];
}

std::filesystem::path ResolvePath(const std::filesystem::path& base, std::string_view value) {
    const std::filesystem::path candidate{std::string{value}};
    if (candidate.is_absolute()) {
        return candidate.lexically_normal();
    }
    return (base / candidate).lexically_normal();
}

std::filesystem::path NormalizeExistingOrAbsolute(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.lexically_normal();
    }
    const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal();
}

bool ReadFloatSequence(
    const std::vector<std::string>& tokens,
    std::size_t& index,
    std::size_t expected_count,
    const std::filesystem::path& path,
    std::string_view field,
    std::vector<SceneDiagnostic>& diagnostics,
    std::vector<float>& values
) {
    values.clear();
    const bool bracketed = index < tokens.size() && tokens[index] == "[";
    if (bracketed) {
        ++index;
    }

    while (index < tokens.size() && values.size() < expected_count && (!bracketed || tokens[index] != "]")) {
        const std::optional<float> parsed = ParseFloatToken(tokens[index]);
        if (!parsed.has_value()) {
            diagnostics.push_back(PbrtError(path, std::string{field}, "expected numeric value"));
            return false;
        }
        values.push_back(*parsed);
        ++index;
    }

    if (values.size() != expected_count) {
        diagnostics.push_back(PbrtError(path, std::string{field}, "expected " + std::to_string(expected_count) + " numeric values"));
        return false;
    }
    if (bracketed) {
        if (index >= tokens.size() || tokens[index] != "]") {
            diagnostics.push_back(PbrtError(path, std::string{field}, "expected closing bracket"));
            return false;
        }
        ++index;
    }
    return true;
}

void SkipUnsupportedWithParams(const std::vector<std::string>& tokens, std::size_t& index, int positional_count) {
    for (int i = 0; i < positional_count && index < tokens.size(); ++i) {
        ++index;
    }
    (void)ReadParams(tokens, index);
}

bool ParsePbrtFileIntoState(const std::filesystem::path& path, PbrtParserState& state, int include_depth);

bool ParseInclude(
    const std::vector<std::string>& tokens,
    std::size_t& index,
    const std::filesystem::path& path,
    PbrtParserState& state,
    int include_depth
) {
    if (index >= tokens.size()) {
        state.diagnostics.push_back(PbrtError(path, "Include", "missing include path"));
        return false;
    }
    const std::filesystem::path include_path = ResolvePath(path.parent_path(), tokens[index++]);
    return ParsePbrtFileIntoState(include_path, state, include_depth + 1);
}

bool ParseTokens(
    const std::filesystem::path& path,
    const std::vector<std::string>& tokens,
    PbrtParserState& state,
    int include_depth
) {
    for (std::size_t index = 0; index < tokens.size();) {
        const std::string command = tokens[index++];
        if (command == "Film") {
            std::string film_type = index < tokens.size() ? tokens[index++] : "rgb";
            state.scene.film = PbrtEntity{film_type, ReadParams(tokens, index)};
        } else if (command == "Integrator") {
            std::string type = index < tokens.size() ? tokens[index++] : "";
            state.scene.integrator = PbrtEntity{type, ReadParams(tokens, index)};
        } else if (command == "Sampler") {
            std::string type = index < tokens.size() ? tokens[index++] : "";
            state.scene.sampler = PbrtEntity{type, ReadParams(tokens, index)};
        } else if (command == "Camera") {
            std::string type = index < tokens.size() ? tokens[index++] : "perspective";
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            state.scene.camera = PbrtEntity{type, std::move(params)};
            // camera_transform is captured at WorldBegin, not here (PBRT v4 semantics)
        } else if (command == "LookAt") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 9, path, "LookAt", state.diagnostics, values)) {
                return false;
            }
            Point3f eye{values[0], values[1], values[2]};
            Point3f target{values[3], values[4], values[5]};
            Vec3f up{values[6], values[7], values[8]};
            state.current_transform = Multiply(state.current_transform, LookAtMatrix(eye, target, up));
        } else if (command == "WorldBegin") {
            // PBRT v4: camera transform is the CTM at WorldBegin
            state.scene.camera_transform = state.current_transform;
            state.current_transform = Mat4f{};
            state.attribute_stack.clear();
            state.transform_stack.clear();
        } else if (command == "WorldEnd") {
            continue;
        } else if (command == "AttributeBegin") {
            state.attribute_stack.push_back(ScopedPbrtState{
                state.current_material_name, state.current_inline_material,
                state.current_area_light, state.current_transform});
        } else if (command == "AttributeEnd") {
            if (state.attribute_stack.empty()) {
                state.diagnostics.push_back(PbrtWarning(path, "AttributeEnd", "unmatched AttributeEnd ignored"));
            } else {
                const ScopedPbrtState& scoped = state.attribute_stack.back();
                state.current_material_name = scoped.material_name;
                state.current_inline_material = scoped.inline_material;
                state.current_area_light = scoped.current_area_light;
                state.current_transform = scoped.transform;
                state.attribute_stack.pop_back();
            }
        } else if (command == "TransformBegin") {
            state.transform_stack.push_back(state.current_transform);
        } else if (command == "TransformEnd") {
            if (state.transform_stack.empty()) {
                state.diagnostics.push_back(PbrtWarning(path, "TransformEnd", "unmatched TransformEnd ignored"));
            } else {
                state.current_transform = state.transform_stack.back();
                state.transform_stack.pop_back();
            }
        } else if (command == "Identity") {
            state.current_transform = Mat4f{};
        } else if (command == "Translate") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 3, path, "Translate", state.diagnostics, values)) {
                return false;
            }
            state.current_transform = Multiply(state.current_transform, TranslationMatrix(Vec3f{values[0], values[1], values[2]}));
        } else if (command == "Scale") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 3, path, "Scale", state.diagnostics, values)) {
                return false;
            }
            state.current_transform = Multiply(state.current_transform, ScaleMatrix(Vec3f{values[0], values[1], values[2]}));
        } else if (command == "Rotate") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 4, path, "Rotate", state.diagnostics, values)) {
                return false;
            }
            state.current_transform = Multiply(
                state.current_transform,
                RotationAxisMatrix(values[0], Vec3f{values[1], values[2], values[3]})
            );
        } else if (command == "Transform") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 16, path, "Transform", state.diagnostics, values)) {
                return false;
            }
            state.current_transform = MatrixFromPbrtValues(values);
        } else if (command == "ConcatTransform") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 16, path, "ConcatTransform", state.diagnostics, values)) {
                return false;
            }
            state.current_transform = Multiply(state.current_transform, MatrixFromPbrtValues(values));
        } else if (command == "MakeNamedMaterial") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "MakeNamedMaterial", "missing material name"));
                return false;
            }
            std::string name = tokens[index++];
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            std::string mat_type = StringParam(params, "type", "matte");
            state.scene.named_materials[name] = PbrtEntity{mat_type, std::move(params)};
        } else if (command == "Material") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "Material", "missing material type"));
                return false;
            }
            std::string type = tokens[index++];
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            state.current_inline_material = PbrtEntity{type, std::move(params)};
            state.current_material_name.clear();
        } else if (command == "NamedMaterial") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "NamedMaterial", "missing material name"));
                return false;
            }
            state.current_material_name = tokens[index++];
            state.current_inline_material.reset();
        } else if (command == "AreaLightSource") {
            std::string type = index < tokens.size() ? tokens[index++] : "diffuse";
            state.current_area_light = PbrtEntity{type, ReadParams(tokens, index)};
        } else if (command == "Shape") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "Shape", "missing shape type"));
                return false;
            }
            std::string shape_type = tokens[index++];
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            PbrtShapeRecord record;
            record.shape = PbrtEntity{shape_type, std::move(params)};
            record.material_name = state.current_material_name;
            record.inline_material = state.current_inline_material;
            record.area_light = state.current_area_light;
            record.object_to_world = state.current_transform;
            if (state.inside_object) {
                state.scene.object_definitions[state.current_object_name].push_back(std::move(record));
            } else {
                state.scene.shapes.push_back(std::move(record));
            }
        } else if (command == "Texture") {
            if (index + 2 >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "Texture", "missing texture parameters"));
                return false;
            }
            std::string tex_name = tokens[index++];
            std::string tex_value_type = tokens[index++];  // "float" or "spectrum"/"color"
            std::string tex_class = tokens[index++];  // "imagemap", "constant", etc.
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            state.scene.named_textures[tex_name] = PbrtEntity{tex_class, std::move(params)};
        } else if (command == "LightSource") {
            std::string type = index < tokens.size() ? tokens[index++] : "";
            std::vector<PbrtParam> params = ReadParams(tokens, index);
            state.scene.lights.push_back(PbrtLightRecord{PbrtEntity{type, std::move(params)}, state.current_transform});
        } else if (command == "PixelFilter") {
            std::string type = index < tokens.size() ? tokens[index++] : "";
            state.scene.filter = PbrtEntity{type, ReadParams(tokens, index)};
        } else if (command == "ObjectBegin") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "ObjectBegin", "missing object name"));
                return false;
            }
            state.current_object_name = tokens[index++];
            state.inside_object = true;
            state.scene.object_definitions[state.current_object_name]; // ensure entry exists
        } else if (command == "ObjectEnd") {
            state.inside_object = false;
            state.current_object_name.clear();
        } else if (command == "ObjectInstance") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "ObjectInstance", "missing object name"));
                return false;
            }
            std::string name = tokens[index++];
            state.scene.instances.push_back(PbrtObjectInstance{std::move(name), state.current_transform});
        } else if (command == "Include") {
            if (!ParseInclude(tokens, index, path, state, include_depth)) {
                return false;
            }
        } else if (command == "Accelerator" || command == "MakeNamedMedium" || command == "MediumInterface") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 1);
        } else if (command == "CoordinateSystem" || command == "CoordSysTransform" || command == "ActiveTransform") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 1);
        } else if (command == "ReverseOrientation") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
        } else if (command == "TransformTimes") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 2);
        } else {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            (void)ReadParams(tokens, index);
        }
    }

    return true;
}

bool ParsePbrtFileIntoState(const std::filesystem::path& path, PbrtParserState& state, int include_depth) {
    if (include_depth > MaxIncludeDepth) {
        state.diagnostics.push_back(PbrtError(path, "Include", "PBRT include depth exceeded"));
        return false;
    }
    if (!std::filesystem::exists(path)) {
        state.diagnostics.push_back(PbrtError(path, "", "PBRT file not found"));
        return false;
    }

    const std::filesystem::path normalized_path = NormalizeExistingOrAbsolute(path);
    for (const std::filesystem::path& active_path : state.include_stack) {
        if (active_path == normalized_path) {
            state.diagnostics.push_back(PbrtError(path, "Include", "PBRT include cycle detected"));
            return false;
        }
    }

    state.include_stack.push_back(normalized_path);
    struct StackPop {
        std::vector<std::filesystem::path>& values;
        ~StackPop() {
            values.pop_back();
        }
    } stack_pop{state.include_stack};

    std::ifstream in{path};
    if (!in) {
        state.diagnostics.push_back(PbrtError(path, "", "failed to open PBRT file"));
        return false;
    }

    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    return ParseTokens(path, TokenizePbrt(text), state, include_depth);
}

} // namespace

PbrtSceneLoadResult LoadPbrtScene(const std::filesystem::path& path) {
    PbrtParserState state;
    state.scene.source_path = path;
    state.scene.source_root = path.parent_path();

    (void)ParsePbrtFileIntoState(path, state, 0);

    PbrtSceneLoadResult result;
    result.diagnostics = std::move(state.diagnostics);
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    result.scene = std::move(state.scene);
    return result;
}

} // namespace yr
