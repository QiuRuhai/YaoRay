#include <yaoray/pbrt/pbrt_scene.hpp>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yr {
namespace {

SceneDiagnostic PbrtError(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

SceneDiagnostic PbrtWarning(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, file, std::move(field), std::move(message)};
}

struct PbrtParam {
    std::string type;
    std::string name;
    std::vector<std::string> values;
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

float FloatAt(const std::vector<std::string>& values, std::size_t index, float fallback = 0.0f) {
    return index < values.size() ? std::stof(values[index]) : fallback;
}

int IntAt(const std::vector<std::string>& values, std::size_t index, int fallback = 0) {
    return index < values.size() ? std::stoi(values[index]) : fallback;
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

MaterialKind MaterialKindFromPbrt(const std::vector<PbrtParam>& params) {
    const PbrtParam* type = FindParam(params, "type");
    if (type == nullptr || type->values.empty()) {
        return MaterialKind::Diffuse;
    }
    if (type->values[0] == "plastic") {
        return MaterialKind::Plastic;
    }
    if (type->values[0] == "metal") {
        return MaterialKind::Metal;
    }
    if (type->values[0] == "glass") {
        return MaterialKind::Dielectric;
    }
    return MaterialKind::Diffuse;
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
        world.film.output = path.parent_path() / filename->values[0];
    }
}

} // namespace

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path) {
    SceneWorldLoadResult result;
    if (!std::filesystem::exists(path)) {
        result.diagnostics.push_back(PbrtError(path, "", "PBRT file not found"));
        return result;
    }

    std::ifstream in{path};
    if (!in) {
        result.diagnostics.push_back(PbrtError(path, "", "failed to open PBRT file"));
        return result;
    }

    SceneWorld world;
    world.source_path = path;
    world.source_root = path.parent_path();
    world.render.backend = RenderBackendKind::Cpu;
    world.render.integrator = RenderIntegratorKind::Path;
    world.render.sampler = RenderSamplerKind::Independent;
    world.render.width = 1280;
    world.render.height = 720;
    world.render.spp = 1;
    world.render.max_depth = 5;
    world.film.output = path.parent_path() / "out" / (path.stem().string() + ".png");
    world.environment.type = EnvironmentKind::Constant;
    world.environment.radiance = Color3f{0.0f, 0.0f, 0.0f};

    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::vector<std::string> tokens = TokenizePbrt(text);
    std::string current_material;
    float current_fov = 45.0f;
    int next_asset_index = 0;

    for (std::size_t index = 0; index < tokens.size();) {
        const std::string command = tokens[index++];
        if (command == "Film") {
            if (index < tokens.size()) {
                ++index;
            }
            ParseFilm(path, ReadParams(tokens, index), world);
        } else if (command == "Camera") {
            if (index < tokens.size()) {
                ++index;
            }
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (const PbrtParam* fov = FindParam(params, "fov")) {
                current_fov = FloatAt(fov->values, 0, current_fov);
                if (world.camera.has_value()) {
                    world.camera->fov_y = current_fov;
                }
            }
        } else if (command == "LookAt") {
            if (index + 8 >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "LookAt", "expected nine numeric values"));
                return result;
            }
            CameraDescription camera;
            camera.type = CameraKind::Perspective;
            camera.position = Point3f{
                std::stof(tokens[index + 0]),
                std::stof(tokens[index + 1]),
                std::stof(tokens[index + 2])
            };
            camera.target = Point3f{
                std::stof(tokens[index + 3]),
                std::stof(tokens[index + 4]),
                std::stof(tokens[index + 5])
            };
            camera.fov_y = current_fov;
            world.camera = camera;
            index += 9;
        } else if (command == "WorldBegin" || command == "WorldEnd") {
            continue;
        } else if (command == "MakeNamedMaterial") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "MakeNamedMaterial", "missing material name"));
                return result;
            }
            MaterialDescription material;
            material.name = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            material.type = MaterialKindFromPbrt(params);
            material.albedo = ColorParam(params, "reflectance", material.albedo);
            world.materials.push_back(material);
        } else if (command == "NamedMaterial") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "NamedMaterial", "missing material name"));
                return result;
            }
            current_material = tokens[index++];
        } else if (command == "Shape") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "Shape", "missing shape type"));
                return result;
            }
            const std::string shape_type = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (shape_type != "trianglemesh") {
                result.diagnostics.push_back(PbrtError(path, "Shape", "unsupported PBRT shape: " + shape_type));
                return result;
            }
            const PbrtParam* p = FindParam(params, "P");
            const PbrtParam* indices = FindParam(params, "indices");
            if (p == nullptr || indices == nullptr) {
                result.diagnostics.push_back(PbrtError(path, "Shape", "trianglemesh requires P and indices parameters"));
                return result;
            }

            SceneWorldMesh mesh;
            mesh.material = current_material;
            for (std::size_t value = 0; value + 2 < p->values.size(); value += 3) {
                mesh.positions.push_back(Point3f{
                    FloatAt(p->values, value + 0),
                    FloatAt(p->values, value + 1),
                    FloatAt(p->values, value + 2)
                });
            }
            for (const std::string& value : indices->values) {
                mesh.indices.push_back(static_cast<std::uint32_t>(std::stoul(value)));
            }

            SceneWorldAsset asset;
            asset.name = "__pbrt_shape_" + std::to_string(next_asset_index++);
            asset.meshes.push_back(std::move(mesh));
            world.instances.push_back(SceneWorldInstance{asset.name, TransformDescription{}, ""});
            world.assets.push_back(std::move(asset));
        } else {
            result.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
        }
    }

    if (!world.camera.has_value()) {
        result.diagnostics.push_back(PbrtError(path, "Camera", "PBRT scene did not define a supported camera"));
    }
    if (world.assets.empty()) {
        result.diagnostics.push_back(PbrtError(path, "Shape", "PBRT scene did not define supported geometry"));
    }
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    result.scene = std::move(world);
    return result;
}

} // namespace yr
