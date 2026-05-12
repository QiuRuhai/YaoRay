#include <yaoray/render/scene_compiler.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

RenderCamera CompileCamera(const CameraDescription& camera) {
    RenderCamera compiled;
    compiled.origin = camera.position;
    compiled.forward = Normalize(camera.target - camera.position);
    if (LengthSquared(compiled.forward) == 0.0f) {
        compiled.forward = Vec3f{0.0f, 0.0f, -1.0f};
    }
    const Vec3f world_up{0.0f, 1.0f, 0.0f};
    compiled.right = Normalize(Cross(compiled.forward, world_up));
    if (LengthSquared(compiled.right) == 0.0f) {
        compiled.right = Vec3f{1.0f, 0.0f, 0.0f};
    }
    compiled.up = Normalize(Cross(compiled.right, compiled.forward));
    compiled.fov_y_radians = DegreesToRadians(camera.fov_y);
    compiled.aperture = camera.aperture;
    compiled.focus_distance = camera.focus_distance;
    return compiled;
}

void CopyAreaLights(const SceneDescription& scene, RenderScene& compiled) {
    for (const LightDescription& light : scene.lights) {
        if (light.type != LightKind::Area) {
            continue;
        }
        compiled.area_lights.push_back(RenderAreaLight{
            light.area.position,
            light.area.size[0],
            light.area.size[1],
            light.area.radiance
        });
    }
}

Vec3f RotateX(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x, value.y * c - value.z * s, value.y * s + value.z * c};
}

Vec3f RotateY(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

Vec3f RotateZ(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x * c - value.y * s, value.x * s + value.y * c, value.z};
}

Point3f ApplyTransform(Point3f point, const TransformDescription& transform) {
    Vec3f value{
        point.x * transform.scale.x,
        point.y * transform.scale.y,
        point.z * transform.scale.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Point3f{
        value.x + transform.translate.x,
        value.y + transform.translate.y,
        value.z + transform.translate.z
    };
}

std::unordered_map<std::string, std::filesystem::path> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, std::filesystem::path> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, asset.path);
    }
    return assets;
}

void AppendBuiltinTriangle(RenderScene& compiled, const TransformDescription& transform) {
    constexpr Point3f p0{-0.5f, 0.0f, 0.0f};
    constexpr Point3f p1{0.5f, 0.0f, 0.0f};
    constexpr Point3f p2{0.0f, 1.0f, 0.0f};

    const Point3f world_p0 = ApplyTransform(p0, transform);
    const Point3f world_p1 = ApplyTransform(p1, transform);
    const Point3f world_p2 = ApplyTransform(p2, transform);

    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
    compiled.triangles.push_back(RenderTriangle{
        world_p0,
        world_p1,
        world_p2,
        Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
        material_index
    });
}

} // namespace

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderScene compiled;
    compiled.backend = scene.render.backend;
    compiled.width = scene.render.width;
    compiled.height = scene.render.height;
    compiled.spp = scene.render.spp;
    compiled.max_depth = scene.render.max_depth;
    compiled.seed = scene.render.seed;

    if (!scene.camera.has_value()) {
        result.diagnostics.push_back(Error(scene, "camera", "missing camera"));
    } else {
        compiled.camera = CompileCamera(scene.camera.value());
    }

    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        compiled.environment.type = scene.environment.type;
        compiled.environment.radiance = scene.environment.radiance;
        compiled.environment.strength = scene.environment.strength;
    }

    CopyAreaLights(scene, compiled);

    const std::unordered_map<std::string, std::filesystem::path> assets = BuildAssetMap(scene);
    for (const InstanceDescription& instance : scene.instances) {
        const auto asset = assets.find(instance.asset);
        if (asset == assets.end()) {
            result.diagnostics.push_back(Error(scene, "instances.asset", "references unknown asset"));
            continue;
        }

        const std::string asset_path = asset->second.generic_string();
        if (asset_path == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform);
        }
    }

    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }
    result.scene = compiled;
    return result;
}

} // namespace yr
