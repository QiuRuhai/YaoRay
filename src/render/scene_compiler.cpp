#include <yaoray/render/scene_compiler.hpp>

#include <yaoray/assets/obj_loader.hpp>
#include <yaoray/render/bvh.hpp>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic Warning(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, scene.source_path, std::move(field), std::move(message)};
}

bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
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

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

bool AppendTriangle(
    const SceneDescription& scene,
    RenderScene& compiled,
    Point3f p0,
    Point3f p1,
    Point3f p2,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const Vec3f normal = Cross(p1 - p0, p2 - p0);
    if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
        diagnostics.push_back(Error(scene, "assets.quads", "quad produces degenerate triangle"));
        return false;
    }

    compiled.triangles.push_back(RenderTriangle{
        p0,
        p1,
        p2,
        Normalize(normal),
        material_index
    });
    return true;
}

std::unordered_map<std::string, const AssetDescription*> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, const AssetDescription*> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, &asset);
    }
    return assets;
}

std::unordered_map<std::string, int> BuildMaterialMap(const SceneDescription& scene, RenderScene& compiled) {
    std::unordered_map<std::string, int> materials;
    for (const MaterialDescription& material : scene.materials) {
        const int material_index = static_cast<int>(compiled.materials.size());
        compiled.materials.push_back(RenderMaterial{material.albedo, material.emission});
        materials.emplace(material.name, material_index);
    }
    return materials;
}

int AddDefaultMaterial(RenderScene& compiled) {
    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
    return material_index;
}

std::optional<int> ResolveMaterialIndex(
    const SceneDescription& scene,
    const InstanceDescription& instance,
    const std::unordered_map<std::string, int>& materials,
    RenderScene& compiled,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (instance.material.empty()) {
        return AddDefaultMaterial(compiled);
    }

    const auto material = materials.find(instance.material);
    if (material == materials.end()) {
        diagnostics.push_back(Error(scene, "instances.material", "references unknown material"));
        return std::nullopt;
    }
    return material->second;
}

void AppendBuiltinTriangle(RenderScene& compiled, const TransformDescription& transform, int material_index) {
    constexpr Point3f p0{-0.5f, 0.0f, 0.0f};
    constexpr Point3f p1{0.5f, 0.0f, 0.0f};
    constexpr Point3f p2{0.0f, 1.0f, 0.0f};

    const Point3f world_p0 = ApplyTransform(p0, transform);
    const Point3f world_p1 = ApplyTransform(p1, transform);
    const Point3f world_p2 = ApplyTransform(p2, transform);

    compiled.triangles.push_back(RenderTriangle{
        world_p0,
        world_p1,
        world_p2,
        Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
        material_index
    });
}

void AppendImportedMesh(RenderScene& compiled, const ImportedMesh& mesh, const TransformDescription& transform, int material_index) {
    for (const ImportedTriangle& triangle : mesh.triangles) {
        const Point3f world_p0 = ApplyTransform(triangle.p0, transform);
        const Point3f world_p1 = ApplyTransform(triangle.p1, transform);
        const Point3f world_p2 = ApplyTransform(triangle.p2, transform);

        compiled.triangles.push_back(RenderTriangle{
            world_p0,
            world_p1,
            world_p2,
            Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
            material_index
        });
    }
}

void AppendInlineQuadAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const AssetDescription& asset,
    const TransformDescription& transform,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    for (const QuadDescription& quad : asset.quads) {
        const Point3f p0 = ApplyTransform(quad.p0, transform);
        const Point3f p1 = ApplyTransform(quad.p1, transform);
        const Point3f p2 = ApplyTransform(quad.p2, transform);
        const Point3f p3 = ApplyTransform(quad.p3, transform);

        AppendTriangle(scene, compiled, p0, p1, p2, material_index, diagnostics);
        AppendTriangle(scene, compiled, p0, p2, p3, material_index, diagnostics);
    }
}

void AppendObjAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& asset_path,
    const TransformDescription& transform,
    int material_index,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string cache_key = asset_path.generic_string();
    auto cached = mesh_cache.find(cache_key);
    if (cached == mesh_cache.end()) {
        AssetLoadResult load_result = LoadObjMesh(asset_path);
        for (const std::string& warning : load_result.warnings) {
            diagnostics.push_back(Warning(scene, "assets.path", warning));
        }
        for (const std::string& error : load_result.errors) {
            diagnostics.push_back(Error(scene, "assets.path", error));
        }
        if (!load_result.errors.empty()) {
            return;
        }
        if (!load_result.mesh.has_value()) {
            diagnostics.push_back(Error(scene, "assets.path", "OBJ loader returned no mesh: " + cache_key));
            return;
        }
        cached = mesh_cache.emplace(cache_key, std::move(load_result.mesh.value())).first;
    }

    AppendImportedMesh(compiled, cached->second, transform, material_index);
}

} // namespace

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderScene compiled;
    compiled.backend = scene.render.backend;
    compiled.integrator = scene.render.integrator;
    compiled.sampler = scene.render.sampler;
    compiled.width = scene.render.width;
    compiled.height = scene.render.height;
    compiled.spp = scene.render.spp;
    compiled.max_depth = scene.render.max_depth;
    compiled.seed = scene.render.seed;
    compiled.threads = scene.render.threads;
    compiled.light_samples = scene.render.light_samples;

    if (!scene.camera.has_value()) {
        result.diagnostics.push_back(Error(scene, "camera", "missing camera"));
    } else {
        compiled.camera = CompileCamera(scene.camera.value());
    }

    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        compiled.environment.type = scene.environment.type;
        compiled.environment.radiance = scene.environment.radiance;
        compiled.environment.strength = scene.environment.strength;
    } else if (scene.environment.type == EnvironmentKind::Hdri) {
        result.diagnostics.push_back(Error(scene, "environment.path", "HDRI environment import not implemented yet"));
    }

    CopyAreaLights(scene, compiled);

    const std::unordered_map<std::string, const AssetDescription*> assets = BuildAssetMap(scene);
    const std::unordered_map<std::string, int> materials = BuildMaterialMap(scene, compiled);
    std::unordered_map<std::string, ImportedMesh> mesh_cache;
    for (const InstanceDescription& instance : scene.instances) {
        const auto asset = assets.find(instance.asset);
        if (asset == assets.end()) {
            result.diagnostics.push_back(Error(scene, "instances.asset", "references unknown asset"));
            continue;
        }

        const AssetDescription& asset_description = *asset->second;
        const std::filesystem::path& asset_path = asset_description.path;
        const std::string asset_path_string = asset_path.generic_string();
        const std::optional<int> material_index =
            ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
        if (!material_index.has_value()) {
            continue;
        }

        if (!asset_description.quads.empty()) {
            AppendInlineQuadAsset(scene, compiled, asset_description, instance.transform, *material_index, result.diagnostics);
        } else if (asset_path_string == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform, *material_index);
        } else if (HasObjExtension(asset_path)) {
            AppendObjAsset(scene, compiled, asset_path, instance.transform, *material_index, mesh_cache, result.diagnostics);
        } else {
            result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path_string));
        }
    }

    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    BvhBuildResult bvh_result = BuildBvh(compiled.triangles);
    for (const std::string& error : bvh_result.errors) {
        result.diagnostics.push_back(Error(scene, "render.bvh", error));
    }
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    compiled.bvh = std::move(bvh_result.bvh);
    result.scene = std::move(compiled);
    return result;
}

} // namespace yr
