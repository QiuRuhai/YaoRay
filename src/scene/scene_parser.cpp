#include <yaoray/scene/scene_parser.hpp>

#include <toml.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yr {
namespace {

SceneDiagnostic Error(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

std::filesystem::path NormalizeScenePath(const std::filesystem::path& scene_dir, const std::string& value) {
    std::filesystem::path path{value};
    if (path.is_relative()) {
        path = scene_dir / path;
    }
    return path.lexically_normal();
}

const toml::table* RequiredTable(
    const toml::table& root,
    std::string_view name,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::table* table = root[name].as_table();
    if (table == nullptr) {
        diagnostics.push_back(Error(file, std::string{name}, "missing required table"));
    }
    return table;
}

template <typename T>
std::optional<T> ReadValue(const toml::table& table, std::string_view key) {
    return table[key].value<T>();
}

std::optional<int> ReadInt(const toml::table& table, std::string_view key) {
    if (const auto value = table[key].value<int64_t>()) {
        return static_cast<int>(*value);
    }
    return std::nullopt;
}

std::optional<std::uint64_t> ReadUInt64(const toml::table& table, std::string_view key) {
    if (const auto value = table[key].value<int64_t>(); value && *value >= 0) {
        return static_cast<std::uint64_t>(*value);
    }
    return std::nullopt;
}

std::optional<float> ReadFloat(const toml::table& table, std::string_view key) {
    if (const auto value = table[key].value<double>()) {
        return static_cast<float>(*value);
    }
    if (const auto value = table[key].value<int64_t>()) {
        return static_cast<float>(*value);
    }
    return std::nullopt;
}

std::optional<Vec3f> ReadVec3(const toml::table& table, std::string_view key) {
    const toml::array* array = table[key].as_array();
    if (array == nullptr || array->size() != 3) {
        return std::nullopt;
    }

    Vec3f value;
    for (std::size_t i = 0; i < 3; ++i) {
        std::optional<double> element = (*array)[i].value<double>();
        if (!element) {
            if (const std::optional<int64_t> int_value = (*array)[i].value<int64_t>()) {
                element = static_cast<double>(*int_value);
            }
        }
        if (!element) {
            return std::nullopt;
        }

        if (i == 0) {
            value.x = static_cast<float>(*element);
        } else if (i == 1) {
            value.y = static_cast<float>(*element);
        } else {
            value.z = static_cast<float>(*element);
        }
    }
    return value;
}

std::optional<std::array<float, 2>> ReadVec2(const toml::table& table, std::string_view key) {
    const toml::array* array = table[key].as_array();
    if (array == nullptr || array->size() != 2) {
        return std::nullopt;
    }

    std::array<float, 2> value{};
    for (std::size_t i = 0; i < 2; ++i) {
        if (const std::optional<double> element = (*array)[i].value<double>()) {
            value[i] = static_cast<float>(*element);
        } else if (const std::optional<int64_t> element = (*array)[i].value<int64_t>()) {
            value[i] = static_cast<float>(*element);
        } else {
            return std::nullopt;
        }
    }
    return value;
}

void ParseRender(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (const auto backend = ReadValue<std::string>(table, "backend")) {
        if (const auto parsed = ParseRenderBackendName(*backend)) {
            scene.render.backend = *parsed;
        } else {
            diagnostics.push_back(Error(file, "render.backend", "unknown backend"));
        }
    }

    if (const auto width = ReadInt(table, "width")) {
        scene.render.width = *width;
    } else {
        diagnostics.push_back(Error(file, "render.width", "missing required field"));
    }

    if (const auto height = ReadInt(table, "height")) {
        scene.render.height = *height;
    } else {
        diagnostics.push_back(Error(file, "render.height", "missing required field"));
    }

    if (const auto spp = ReadInt(table, "spp")) {
        scene.render.spp = *spp;
    }
    if (const auto max_depth = ReadInt(table, "max_depth")) {
        scene.render.max_depth = *max_depth;
    }
    if (const auto seed = ReadUInt64(table, "seed")) {
        scene.render.seed = *seed;
    }

    if (scene.render.width <= 0) {
        diagnostics.push_back(Error(file, "render.width", "must be positive"));
    }
    if (scene.render.height <= 0) {
        diagnostics.push_back(Error(file, "render.height", "must be positive"));
    }
    if (scene.render.spp <= 0) {
        diagnostics.push_back(Error(file, "render.spp", "must be positive"));
    }
    if (scene.render.max_depth <= 0) {
        diagnostics.push_back(Error(file, "render.max_depth", "must be positive"));
    }
}

void ParseFilm(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& scene_dir,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (const auto output = ReadValue<std::string>(table, "output")) {
        if (output->empty()) {
            diagnostics.push_back(Error(file, "film.output", "must not be empty"));
        } else {
            scene.film.output = NormalizeScenePath(scene_dir, *output);
        }
    } else {
        diagnostics.push_back(Error(file, "film.output", "missing required field"));
    }

    if (const auto tone_mapper = ReadValue<std::string>(table, "tone_mapper")) {
        if (const auto parsed = ParseToneMapperName(*tone_mapper)) {
            scene.film.tone_mapper = *parsed;
        } else {
            diagnostics.push_back(Error(file, "film.tone_mapper", "unknown tone mapper"));
        }
    }
    if (const auto exposure = ReadFloat(table, "exposure")) {
        scene.film.exposure = *exposure;
    }
    if (const auto interval = ReadInt(table, "checkpoint_interval_s")) {
        scene.film.checkpoint_interval_s = *interval;
    }
    if (const auto checkpoint = ReadValue<std::string>(table, "checkpoint_path")) {
        scene.film.checkpoint_path = checkpoint->empty() ? std::filesystem::path{} : NormalizeScenePath(scene_dir, *checkpoint);
    }
}

void ParseCamera(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    CameraDescription camera;
    if (const auto type = ReadValue<std::string>(table, "type")) {
        if (const auto parsed = ParseCameraKindName(*type)) {
            camera.type = *parsed;
        } else {
            diagnostics.push_back(Error(file, "camera.type", "unknown camera type"));
        }
    }
    if (const auto position = ReadVec3(table, "position")) {
        camera.position = *position;
    } else {
        diagnostics.push_back(Error(file, "camera.position", "missing required field"));
    }
    if (const auto target = ReadVec3(table, "target")) {
        camera.target = *target;
    } else {
        diagnostics.push_back(Error(file, "camera.target", "missing required field"));
    }
    if (const auto fov_y = ReadFloat(table, "fov_y")) {
        camera.fov_y = *fov_y;
    } else {
        diagnostics.push_back(Error(file, "camera.fov_y", "missing required field"));
    }
    if (const auto aperture = ReadFloat(table, "aperture")) {
        camera.aperture = *aperture;
    }
    if (const auto focus_distance = ReadFloat(table, "focus_distance")) {
        camera.focus_distance = *focus_distance;
    }
    scene.camera = camera;
}

void ParseAssets(
    const toml::table& root,
    SceneDescription& scene,
    const std::filesystem::path& scene_dir,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* assets = root["assets"].as_array();
    if (assets == nullptr) {
        return;
    }

    std::unordered_set<std::string> names;
    for (const toml::node& node : *assets) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            diagnostics.push_back(Error(file, "assets", "asset entry must be a table"));
            continue;
        }

        AssetDescription asset;
        if (const auto name = ReadValue<std::string>(*table, "name")) {
            asset.name = *name;
        } else {
            diagnostics.push_back(Error(file, "assets.name", "missing required field"));
        }
        if (const auto path = ReadValue<std::string>(*table, "path")) {
            asset.path = NormalizeScenePath(scene_dir, *path);
        } else {
            diagnostics.push_back(Error(file, "assets.path", "missing required field"));
        }
        if (!asset.name.empty() && !names.insert(asset.name).second) {
            diagnostics.push_back(Error(file, "assets.name", "duplicate asset name"));
        }
        scene.assets.push_back(std::move(asset));
    }
}

void ParseInstances(
    const toml::table& root,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* instances = root["instances"].as_array();
    if (instances == nullptr) {
        return;
    }

    for (const toml::node& node : *instances) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            diagnostics.push_back(Error(file, "instances", "instance entry must be a table"));
            continue;
        }

        InstanceDescription instance;
        if (const auto asset = ReadValue<std::string>(*table, "asset")) {
            instance.asset = *asset;
        } else {
            diagnostics.push_back(Error(file, "instances.asset", "missing required field"));
        }
        if (const auto translate = ReadVec3(*table, "translate")) {
            instance.transform.translate = *translate;
        }
        if (const auto rotate = ReadVec3(*table, "rotate_degrees")) {
            instance.transform.rotate_degrees = *rotate;
        }
        if (const auto scale = ReadVec3(*table, "scale")) {
            instance.transform.scale = *scale;
        }
        scene.instances.push_back(std::move(instance));
    }
}

void ParseLights(
    const toml::table& root,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* lights = root["lights"].as_array();
    if (lights == nullptr) {
        return;
    }

    for (const toml::node& node : *lights) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            diagnostics.push_back(Error(file, "lights", "light entry must be a table"));
            continue;
        }

        LightDescription light;
        if (const auto type = ReadValue<std::string>(*table, "type")) {
            if (const auto parsed = ParseLightKindName(*type)) {
                light.type = *parsed;
            } else {
                diagnostics.push_back(Error(file, "lights.type", "unknown light type"));
            }
        }
        if (const auto position = ReadVec3(*table, "position")) {
            light.area.position = *position;
        }
        if (const auto size = ReadVec2(*table, "size")) {
            light.area.size = *size;
        }
        if (const auto radiance = ReadVec3(*table, "radiance")) {
            light.area.radiance = *radiance;
        }
        scene.lights.push_back(light);
    }
}

void ParseEnvironment(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& scene_dir,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (const auto type = ReadValue<std::string>(table, "type")) {
        if (const auto parsed = ParseEnvironmentKindName(*type)) {
            scene.environment.type = *parsed;
        } else {
            diagnostics.push_back(Error(file, "environment.type", "unknown environment type"));
        }
    }
    if (const auto radiance = ReadVec3(table, "radiance")) {
        scene.environment.radiance = *radiance;
    }
    if (const auto path = ReadValue<std::string>(table, "path")) {
        scene.environment.path = path->empty() ? std::filesystem::path{} : NormalizeScenePath(scene_dir, *path);
    }
    if (const auto strength = ReadFloat(table, "strength")) {
        scene.environment.strength = *strength;
    }
}

void ValidateReferences(
    const SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::unordered_set<std::string> assets;
    for (const AssetDescription& asset : scene.assets) {
        if (!asset.name.empty()) {
            assets.insert(asset.name);
        }
    }

    for (const InstanceDescription& instance : scene.instances) {
        if (!instance.asset.empty() && !assets.contains(instance.asset)) {
            diagnostics.push_back(Error(file, "instances.asset", "missing asset reference"));
        }
    }

    if (scene.instances.empty() && scene.lights.empty() && scene.environment.type == EnvironmentKind::None) {
        diagnostics.push_back(Error(file, "scene", "must contain at least one instance, light, or environment"));
    }
}

} // namespace

SceneLoadResult LoadSceneFile(const std::filesystem::path& path) {
    SceneLoadResult result;
    const std::filesystem::path file = path.lexically_normal();

    if (!std::filesystem::exists(path)) {
        result.diagnostics.push_back(Error(file, "", "scene file not found"));
        return result;
    }

    toml::table root;
    try {
        root = toml::parse_file(path.string());
    } catch (const toml::parse_error& error) {
        result.diagnostics.push_back(Error(file, "", std::string{"invalid TOML: "} + std::string{error.description()}));
        return result;
    }

    SceneDescription scene;
    scene.source_path = file;
    const std::filesystem::path scene_dir = file.parent_path();

    const toml::table* render = RequiredTable(root, "render", file, result.diagnostics);
    const toml::table* film = RequiredTable(root, "film", file, result.diagnostics);
    const toml::table* camera = RequiredTable(root, "camera", file, result.diagnostics);
    const toml::table* environment = root["environment"].as_table();

    if (render != nullptr) {
        ParseRender(*render, scene, file, result.diagnostics);
    }
    if (film != nullptr) {
        ParseFilm(*film, scene, scene_dir, file, result.diagnostics);
    }
    if (camera != nullptr) {
        ParseCamera(*camera, scene, file, result.diagnostics);
    }
    if (environment != nullptr) {
        ParseEnvironment(*environment, scene, scene_dir, file, result.diagnostics);
    }
    ParseAssets(root, scene, scene_dir, file, result.diagnostics);
    ParseInstances(root, scene, file, result.diagnostics);
    ParseLights(root, scene, file, result.diagnostics);
    ValidateReferences(scene, file, result.diagnostics);

    if (!HasSceneErrors(result.diagnostics)) {
        result.scene = std::move(scene);
    }
    return result;
}

void ApplyBackendOverride(SceneDescription& scene, RenderBackendKind backend) {
    scene.render.backend = backend;
}

} // namespace yr
