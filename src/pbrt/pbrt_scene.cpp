#include <yaoray/pbrt/pbrt_scene.hpp>

#include <yaoray/assets/ply_loader.hpp>

#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr int MaxIncludeDepth = 32;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic PbrtError(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

SceneDiagnostic PbrtWarning(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, file, std::move(field), std::move(message)};
}

struct Mat4 {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

Mat4 Multiply(Mat4 a, Mat4 b) {
    Mat4 result;
    result.m.fill(0.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[static_cast<std::size_t>(column * 4 + row)] +=
                    a.m[static_cast<std::size_t>(k * 4 + row)] *
                    b.m[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Point3f TransformPoint(Mat4 transform, Point3f point) {
    return Point3f{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8] * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9] * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]
    };
}

Vec3f TransformVector(Mat4 transform, Vec3f value) {
    return Vec3f{
        transform.m[0] * value.x + transform.m[4] * value.y + transform.m[8] * value.z,
        transform.m[1] * value.x + transform.m[5] * value.y + transform.m[9] * value.z,
        transform.m[2] * value.x + transform.m[6] * value.y + transform.m[10] * value.z
    };
}

Vec3f TransformNormal(Mat4 transform, Vec3f normal) {
    const float a = transform.m[0];
    const float b = transform.m[4];
    const float c = transform.m[8];
    const float d = transform.m[1];
    const float e = transform.m[5];
    const float f = transform.m[9];
    const float g = transform.m[2];
    const float h = transform.m[6];
    const float i = transform.m[10];

    const float determinant =
        a * (e * i - f * h) -
        b * (d * i - f * g) +
        c * (d * h - e * g);
    if (std::fabs(determinant) <= 1.0e-12f) {
        const Vec3f fallback = Normalize(TransformVector(transform, normal));
        return LengthSquared(fallback) > 0.0f ? fallback : Normalize(normal);
    }

    const float inv_det = 1.0f / determinant;
    return Normalize(Vec3f{
        ((e * i - f * h) * normal.x + (f * g - d * i) * normal.y + (d * h - e * g) * normal.z) * inv_det,
        ((c * h - b * i) * normal.x + (a * i - c * g) * normal.y + (b * g - a * h) * normal.z) * inv_det,
        ((b * f - c * e) * normal.x + (c * d - a * f) * normal.y + (a * e - b * d) * normal.z) * inv_det
    });
}

Mat4 TranslationMatrix(Vec3f translation) {
    Mat4 result;
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

Mat4 ScaleMatrix(Vec3f scale) {
    Mat4 result;
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

Mat4 RotationAxisMatrix(float degrees, Vec3f axis) {
    const Vec3f unit_axis = Normalize(axis);
    if (LengthSquared(unit_axis) == 0.0f) {
        return Mat4{};
    }

    const float radians = DegreesToRadians(degrees);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0f - c;
    const float x = unit_axis.x;
    const float y = unit_axis.y;
    const float z = unit_axis.z;

    Mat4 result;
    result.m[0] = t * x * x + c;
    result.m[1] = t * x * y + s * z;
    result.m[2] = t * x * z - s * y;

    result.m[4] = t * x * y - s * z;
    result.m[5] = t * y * y + c;
    result.m[6] = t * y * z + s * x;

    result.m[8] = t * x * z + s * y;
    result.m[9] = t * y * z - s * x;
    result.m[10] = t * z * z + c;
    return result;
}

Mat4 MatrixFromPbrtValues(const std::vector<float>& values) {
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[static_cast<std::size_t>(column * 4 + row)] =
                values[static_cast<std::size_t>(row * 4 + column)];
        }
    }
    return result;
}

struct PbrtParam {
    std::string type;
    std::string name;
    std::vector<std::string> values;
};

struct ScopedPbrtState {
    std::string material;
    Mat4 transform;
};

struct PbrtParserState {
    SceneWorld world;
    std::vector<SceneDiagnostic> diagnostics;
    std::string current_material;
    Mat4 current_transform;
    std::vector<ScopedPbrtState> attribute_stack;
    std::vector<Mat4> transform_stack;
    std::vector<std::filesystem::path> include_stack;
    float current_fov = 45.0f;
    int next_asset_index = 0;
    int next_material_index = 0;
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

std::vector<PbrtParam> ReadParams(const std::vector<std::string>& tokens, std::size_t& index) {
    std::vector<PbrtParam> params;
    while (index < tokens.size()) {
        std::string type;
        std::string name;
        if (!SplitParamName(tokens[index], type, name)) {
            break;
        }
        ++index;
        PbrtParam param;
        param.type = std::move(type);
        param.name = std::move(name);
        param.values = ReadValueList(tokens, index);
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

float FloatAt(const std::vector<std::string>& values, std::size_t index, float fallback = 0.0f) {
    if (index >= values.size()) {
        return fallback;
    }
    return ParseFloatToken(values[index]).value_or(fallback);
}

int IntAt(const std::vector<std::string>& values, std::size_t index, int fallback = 0) {
    if (index >= values.size()) {
        return fallback;
    }
    return ParseIntToken(values[index]).value_or(fallback);
}

std::optional<float> FloatParam(const std::vector<PbrtParam>& params, std::string_view name) {
    const PbrtParam* param = FindParam(params, name);
    if (param == nullptr || param->values.empty()) {
        return std::nullopt;
    }
    return ParseFloatToken(param->values[0]);
}

Color3f ColorParam(const std::vector<PbrtParam>& params, std::string_view name, Color3f fallback) {
    const PbrtParam* param = FindParam(params, name);
    if (param == nullptr || param->values.size() < 3) {
        return fallback;
    }
    return Color3f{
        FloatAt(param->values, 0, fallback.x),
        FloatAt(param->values, 1, fallback.y),
        FloatAt(param->values, 2, fallback.z)
    };
}

float Average(Color3f color) {
    return (color.x + color.y + color.z) / 3.0f;
}

MaterialKind MaterialKindFromPbrt(std::string_view type) {
    if (type == "plastic" || type == "uber" || type == "substrate") {
        return MaterialKind::Plastic;
    }
    if (type == "metal" || type == "conductor") {
        return MaterialKind::Metal;
    }
    if (type == "glass" || type == "dielectric" || type == "thindielectric" || type == "roughdielectric") {
        return MaterialKind::Dielectric;
    }
    return MaterialKind::Diffuse;
}

std::string MaterialTypeParam(const std::vector<PbrtParam>& params, std::string_view fallback) {
    const PbrtParam* type = FindParam(params, "type");
    if (type == nullptr || type->values.empty()) {
        return std::string{fallback};
    }
    return type->values[0];
}

void PopulateMaterialFromPbrt(
    MaterialDescription& material,
    std::string_view pbrt_type,
    const std::vector<PbrtParam>& params
) {
    material.type = MaterialKindFromPbrt(pbrt_type);
    material.albedo = ColorParam(params, "reflectance", material.albedo);
    material.albedo = ColorParam(params, "Kd", material.albedo);
    material.albedo = ColorParam(params, "color", material.albedo);
    if (const PbrtParam* kr = FindParam(params, "Kr");
        kr != nullptr && material.type == MaterialKind::Dielectric) {
        material.albedo = ColorParam(params, "Kr", material.albedo);
    }
    if (const PbrtParam* ks = FindParam(params, "Ks"); ks != nullptr && ks->values.size() >= 3) {
        material.specular = Average(ColorParam(params, "Ks", Color3f{material.specular, material.specular, material.specular}));
    }
    if (std::optional<float> roughness = FloatParam(params, "roughness")) {
        material.roughness = *roughness;
    }
    if (std::optional<float> uroughness = FloatParam(params, "uroughness")) {
        material.roughness = *uroughness;
    }
    if (std::optional<float> vroughness = FloatParam(params, "vroughness")) {
        material.roughness = (material.roughness + *vroughness) * 0.5f;
    }
    if (std::optional<float> eta = FloatParam(params, "eta")) {
        material.ior = *eta;
    }
    if (std::optional<float> ior = FloatParam(params, "ior")) {
        material.ior = *ior;
    }
    if (std::optional<float> index = FloatParam(params, "index")) {
        material.ior = *index;
    }
    if (pbrt_type == "thindielectric") {
        material.thin = true;
    }
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

void ParseFilm(
    const std::filesystem::path& path,
    const std::vector<PbrtParam>& params,
    SceneWorld& world
) {
    if (const PbrtParam* x = FindParam(params, "xresolution")) {
        world.render.width = IntAt(x->values, 0, world.render.width);
    }
    if (const PbrtParam* y = FindParam(params, "yresolution")) {
        world.render.height = IntAt(y->values, 0, world.render.height);
    }
    if (const PbrtParam* filename = FindParam(params, "filename");
        filename != nullptr && !filename->values.empty()) {
        world.film.output = ResolvePath(path.parent_path(), filename->values[0]);
    }
}

void AddShapeAsset(PbrtParserState& state, std::vector<SceneWorldMesh> meshes) {
    SceneWorldAsset asset;
    asset.name = "__pbrt_shape_" + std::to_string(state.next_asset_index++);
    asset.meshes = std::move(meshes);
    state.world.instances.push_back(SceneWorldInstance{asset.name, TransformDescription{}, ""});
    state.world.assets.push_back(std::move(asset));
}

bool AppendTriangleMeshShape(
    PbrtParserState& state,
    const std::filesystem::path& path,
    const std::vector<PbrtParam>& params
) {
    const PbrtParam* p = FindParam(params, "P");
    const PbrtParam* indices = FindParam(params, "indices");
    if (p == nullptr || indices == nullptr) {
        state.diagnostics.push_back(PbrtError(path, "Shape", "trianglemesh requires P and indices parameters"));
        return false;
    }
    if (p->values.size() % 3 != 0) {
        state.diagnostics.push_back(PbrtError(path, "Shape.P", "trianglemesh P count must be divisible by three"));
        return false;
    }
    if (indices->values.size() % 3 != 0) {
        state.diagnostics.push_back(PbrtError(path, "Shape.indices", "trianglemesh index count must be divisible by three"));
        return false;
    }

    SceneWorldMesh mesh;
    mesh.material = state.current_material;
    mesh.positions.reserve(p->values.size() / 3);
    for (std::size_t value = 0; value + 2 < p->values.size(); value += 3) {
        mesh.positions.push_back(TransformPoint(state.current_transform, Point3f{
            FloatAt(p->values, value + 0),
            FloatAt(p->values, value + 1),
            FloatAt(p->values, value + 2)
        }));
    }

    if (const PbrtParam* normals = FindParam(params, "N");
        normals != nullptr && normals->values.size() == mesh.positions.size() * 3) {
        mesh.normals.reserve(mesh.positions.size());
        for (std::size_t value = 0; value + 2 < normals->values.size(); value += 3) {
            mesh.normals.push_back(TransformNormal(state.current_transform, Vec3f{
                FloatAt(normals->values, value + 0),
                FloatAt(normals->values, value + 1),
                FloatAt(normals->values, value + 2)
            }));
        }
    }

    const PbrtParam* uv = FindParam(params, "uv");
    if (uv == nullptr) {
        uv = FindParam(params, "st");
    }
    if (uv != nullptr && uv->values.size() == mesh.positions.size() * 2) {
        mesh.texcoords0.reserve(mesh.positions.size());
        for (std::size_t value = 0; value + 1 < uv->values.size(); value += 2) {
            mesh.texcoords0.push_back(Vec2f{
                FloatAt(uv->values, value + 0),
                FloatAt(uv->values, value + 1)
            });
        }
    }

    mesh.indices.reserve(indices->values.size());
    for (const std::string& value : indices->values) {
        const std::optional<std::uint32_t> index = ParseUintToken(value);
        if (!index.has_value()) {
            state.diagnostics.push_back(PbrtError(path, "Shape.indices", "trianglemesh index must be a non-negative integer"));
            return false;
        }
        mesh.indices.push_back(*index);
    }

    AddShapeAsset(state, std::vector<SceneWorldMesh>{std::move(mesh)});
    return true;
}

bool AppendPlyMeshShape(
    PbrtParserState& state,
    const std::filesystem::path& path,
    const std::vector<PbrtParam>& params
) {
    const PbrtParam* filename = FindParam(params, "filename");
    if (filename == nullptr || filename->values.empty()) {
        state.diagnostics.push_back(PbrtError(path, "Shape.filename", "plymesh requires string filename parameter"));
        return false;
    }

    const std::filesystem::path ply_path = ResolvePath(path.parent_path(), filename->values[0]);
    AssetLoadResult load = LoadPlyResource(ply_path);
    for (const std::string& warning : load.warnings) {
        state.diagnostics.push_back(PbrtWarning(path, "Shape.filename", warning));
    }
    for (const std::string& error : load.errors) {
        state.diagnostics.push_back(PbrtError(path, "Shape.filename", error));
    }
    if (!load.errors.empty()) {
        return false;
    }
    if (!load.resource.has_value()) {
        state.diagnostics.push_back(PbrtError(path, "Shape.filename", "PLY loader returned no resource"));
        return false;
    }

    std::vector<SceneWorldMesh> meshes;
    for (const AssetMesh& asset_mesh : load.resource->meshes) {
        for (const AssetPrimitive& primitive : asset_mesh.primitives) {
            if (primitive.topology != AssetPrimitiveTopology::Triangles) {
                state.diagnostics.push_back(PbrtError(path, "Shape.filename", "PLY primitive topology is not triangles"));
                return false;
            }

            SceneWorldMesh mesh;
            mesh.material = state.current_material;
            mesh.positions.reserve(primitive.positions.size());
            for (Point3f position : primitive.positions) {
                mesh.positions.push_back(TransformPoint(state.current_transform, position));
            }
            if (primitive.normals.size() == primitive.positions.size()) {
                mesh.normals.reserve(primitive.normals.size());
                for (Vec3f normal : primitive.normals) {
                    mesh.normals.push_back(TransformNormal(state.current_transform, normal));
                }
            }
            if (primitive.texcoords0.size() == primitive.positions.size()) {
                mesh.texcoords0 = primitive.texcoords0;
            }
            mesh.indices = primitive.indices;
            meshes.push_back(std::move(mesh));
        }
    }

    if (meshes.empty()) {
        state.diagnostics.push_back(PbrtError(path, "Shape.filename", "PLY resource did not contain mesh primitives"));
        return false;
    }

    AddShapeAsset(state, std::move(meshes));
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
            if (index < tokens.size()) {
                ++index;
            }
            ParseFilm(path, ReadParams(tokens, index), state.world);
        } else if (command == "Integrator") {
            std::string integrator = index < tokens.size() ? tokens[index++] : "";
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (integrator == "path") {
                state.world.render.integrator = RenderIntegratorKind::Path;
            }
            if (std::optional<float> max_depth = FloatParam(params, "maxdepth")) {
                state.world.render.max_depth = static_cast<int>(*max_depth);
            }
        } else if (command == "Sampler") {
            std::string sampler = index < tokens.size() ? tokens[index++] : "";
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            state.world.render.sampler = sampler == "stratified"
                ? RenderSamplerKind::Stratified
                : RenderSamplerKind::Independent;
            if (std::optional<float> pixelsamples = FloatParam(params, "pixelsamples")) {
                state.world.render.spp = static_cast<int>(*pixelsamples);
            } else if (std::optional<float> xsamples = FloatParam(params, "xsamples")) {
                const int x = static_cast<int>(*xsamples);
                const int y = static_cast<int>(FloatParam(params, "ysamples").value_or(1.0f));
                state.world.render.spp = x * y;
            }
        } else if (command == "Camera") {
            if (index < tokens.size()) {
                ++index;
            }
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (const PbrtParam* fov = FindParam(params, "fov")) {
                state.current_fov = FloatAt(fov->values, 0, state.current_fov);
                if (state.world.camera.has_value()) {
                    state.world.camera->fov_y = state.current_fov;
                }
            }
        } else if (command == "LookAt") {
            std::vector<float> values;
            if (!ReadFloatSequence(tokens, index, 9, path, "LookAt", state.diagnostics, values)) {
                return false;
            }
            CameraDescription camera;
            camera.type = CameraKind::Perspective;
            camera.position = Point3f{values[0], values[1], values[2]};
            camera.target = Point3f{values[3], values[4], values[5]};
            camera.fov_y = state.current_fov;
            state.world.camera = camera;
        } else if (command == "WorldBegin" || command == "WorldEnd") {
            continue;
        } else if (command == "AttributeBegin") {
            state.attribute_stack.push_back(ScopedPbrtState{state.current_material, state.current_transform});
        } else if (command == "AttributeEnd") {
            if (state.attribute_stack.empty()) {
                state.diagnostics.push_back(PbrtWarning(path, "AttributeEnd", "unmatched AttributeEnd ignored"));
            } else {
                const ScopedPbrtState scoped = state.attribute_stack.back();
                state.attribute_stack.pop_back();
                state.current_material = scoped.material;
                state.current_transform = scoped.transform;
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
            state.current_transform = Mat4{};
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
            MaterialDescription material;
            material.name = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            PopulateMaterialFromPbrt(material, MaterialTypeParam(params, "matte"), params);
            state.world.materials.push_back(material);
        } else if (command == "Material") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "Material", "missing material type"));
                return false;
            }
            const std::string pbrt_type = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            MaterialDescription material;
            material.name = "__pbrt_material_" + std::to_string(state.next_material_index++);
            PopulateMaterialFromPbrt(material, pbrt_type, params);
            state.current_material = material.name;
            state.world.materials.push_back(material);
        } else if (command == "NamedMaterial") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "NamedMaterial", "missing material name"));
                return false;
            }
            state.current_material = tokens[index++];
        } else if (command == "Shape") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(path, "Shape", "missing shape type"));
                return false;
            }
            const std::string shape_type = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (shape_type == "trianglemesh") {
                if (!AppendTriangleMeshShape(state, path, params)) {
                    return false;
                }
            } else if (shape_type == "plymesh") {
                if (!AppendPlyMeshShape(state, path, params)) {
                    return false;
                }
            } else {
                state.diagnostics.push_back(PbrtError(path, "Shape", "unsupported PBRT shape: " + shape_type));
                return false;
            }
        } else if (command == "Include") {
            if (!ParseInclude(tokens, index, path, state, include_depth)) {
                return false;
            }
        } else if (command == "PixelFilter" || command == "Accelerator" || command == "LightSource" ||
                   command == "AreaLightSource" || command == "MakeNamedMedium" || command == "MediumInterface") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 1);
        } else if (command == "Texture") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 3);
        } else if (command == "ObjectBegin" || command == "ObjectInstance" || command == "CoordinateSystem" ||
                   command == "CoordSysTransform" || command == "ActiveTransform") {
            state.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
            SkipUnsupportedWithParams(tokens, index, 1);
        } else if (command == "ObjectEnd" || command == "ReverseOrientation") {
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

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path) {
    PbrtParserState state;
    state.world.source_path = path;
    state.world.source_root = path.parent_path();
    state.world.render.backend = RenderBackendKind::Cpu;
    state.world.render.integrator = RenderIntegratorKind::Path;
    state.world.render.sampler = RenderSamplerKind::Independent;
    state.world.render.width = 1280;
    state.world.render.height = 720;
    state.world.render.spp = 1;
    state.world.render.max_depth = 5;
    state.world.film.output = path.parent_path() / "out" / (path.stem().string() + ".png");
    state.world.environment.type = EnvironmentKind::Constant;
    state.world.environment.radiance = Color3f{0.0f, 0.0f, 0.0f};

    (void)ParsePbrtFileIntoState(path, state, 0);

    if (!state.world.camera.has_value()) {
        state.diagnostics.push_back(PbrtError(path, "Camera", "PBRT scene did not define a supported camera"));
    }
    if (state.world.assets.empty()) {
        state.diagnostics.push_back(PbrtError(path, "Shape", "PBRT scene did not define supported geometry"));
    }

    SceneWorldLoadResult result;
    result.diagnostics = std::move(state.diagnostics);
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    result.scene = std::move(state.world);
    return result;
}

} // namespace yr
