#include <yaoray/render/scene_compiler.hpp>

#include <yaoray/assets/gltf_loader.hpp>
#include <yaoray/assets/obj_loader.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/render/texture.hpp>

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

bool HasGltfExtension(const std::filesystem::path& path) {
    return path.extension() == ".gltf" || path.extension() == ".glb";
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

void CompileEnvironment(
    const SceneDescription& scene,
    RenderScene& compiled,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        compiled.environment.type = scene.environment.type;
        compiled.environment.radiance = scene.environment.radiance;
        compiled.environment.strength = scene.environment.strength;
        return;
    }

    if (scene.environment.type != EnvironmentKind::Hdri) {
        return;
    }
    if (scene.environment.path.empty()) {
        diagnostics.push_back(Error(scene, "environment.path", "must not be empty for hdri environment"));
        return;
    }

    TextureLoadResult load = LoadHdrTexture(scene.environment.path);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "environment.path", load.error));
        return;
    }

    const int texture_index = static_cast<int>(compiled.textures.size());
    compiled.textures.push_back(std::move(load.texture));
    const int distribution_index = static_cast<int>(compiled.environment_distributions.size());
    compiled.environment_distributions.push_back(
        BuildEnvironmentDistribution(compiled.textures[static_cast<std::size_t>(texture_index)])
    );

    compiled.environment.type = EnvironmentKind::Hdri;
    compiled.environment.strength = scene.environment.strength;
    compiled.environment.rotation_radians = DegreesToRadians(scene.environment.rotation_degrees);
    compiled.environment.texture_index = texture_index;
    compiled.environment.distribution_index = distribution_index;
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

Vec3f ApplyNormalTransform(Vec3f normal, const TransformDescription& transform) {
    Vec3f value{
        transform.scale.x != 0.0f ? normal.x / transform.scale.x : normal.x,
        transform.scale.y != 0.0f ? normal.y / transform.scale.y : normal.y,
        transform.scale.z != 0.0f ? normal.z / transform.scale.z : normal.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Normalize(value);
}

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

struct TextureCache {
    std::unordered_map<std::string, int> indices;
};

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
        RenderMaterial render_material;
        render_material.type = material.type;
        render_material.albedo = material.albedo;
        render_material.emission = material.emission;
        render_material.roughness = material.roughness;
        render_material.specular = material.specular;
        render_material.ior = material.ior;
        render_material.thin = material.thin;
        render_material.absorption_color = material.absorption_color;
        render_material.absorption_distance = material.absorption_distance;
        compiled.materials.push_back(render_material);
        materials.emplace(material.name, material_index);
    }
    return materials;
}

int AddDefaultMaterial(RenderScene& compiled) {
    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
    return material_index;
}

std::string TextureWrapName(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::Repeat:
            return "repeat";
        case TextureWrap::ClampToEdge:
            return "clamp";
        case TextureWrap::MirroredRepeat:
            return "mirror";
    }
    return "repeat";
}

std::string CanonicalTextureKey(const std::filesystem::path& path, TextureWrap wrap_s, TextureWrap wrap_t) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string normalized = ec ? path.lexically_normal().generic_string() : canonical.generic_string();
    return normalized + "|s=" + TextureWrapName(wrap_s) + "|t=" + TextureWrapName(wrap_t);
}

std::optional<int> LoadTextureIndex(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& path,
    TextureWrap wrap_s,
    TextureWrap wrap_t,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string key = CanonicalTextureKey(path, wrap_s, wrap_t);
    const auto found = texture_cache.indices.find(key);
    if (found != texture_cache.indices.end()) {
        return found->second;
    }

    TextureLoadResult load = LoadPngTexture(path);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "assets.path", load.error));
        return std::nullopt;
    }
    load.texture.wrap_s = wrap_s;
    load.texture.wrap_t = wrap_t;
    load.texture.filter = TextureFilter::Bilinear;

    const int texture_index = static_cast<int>(compiled.textures.size());
    compiled.textures.push_back(std::move(load.texture));
    texture_cache.indices.emplace(key, texture_index);
    return texture_index;
}

std::vector<int> CompileImportedMaterials(
    const SceneDescription& scene,
    RenderScene& compiled,
    const ImportedMesh& mesh,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::vector<int> material_indices;
    material_indices.reserve(mesh.materials.size());

    for (const ImportedMaterial& material : mesh.materials) {
        RenderMaterial render_material;
        render_material.type = material.type;
        render_material.albedo = material.diffuse;
        render_material.emission = material.emission;
        render_material.roughness = material.roughness;
        render_material.specular = material.specular;
        if (material.has_diffuse_texture) {
            const std::optional<int> texture_index = LoadTextureIndex(
                scene,
                compiled,
                material.diffuse_texture_path,
                material.diffuse_texture_wrap_s,
                material.diffuse_texture_wrap_t,
                texture_cache,
                diagnostics
            );
            if (!texture_index.has_value()) {
                material_indices.push_back(-1);
                continue;
            }
            render_material.albedo_texture = *texture_index;
        }

        const int render_material_index = static_cast<int>(compiled.materials.size());
        compiled.materials.push_back(render_material);
        material_indices.push_back(render_material_index);
    }

    return material_indices;
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

void AppendImportedMesh(
    RenderScene& compiled,
    const ImportedMesh& mesh,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    const std::vector<int>& imported_material_indices
) {
    int fallback_material_index = -1;
    for (const ImportedTriangle& triangle : mesh.triangles) {
        const Point3f world_p0 = ApplyTransform(triangle.p0, transform);
        const Point3f world_p1 = ApplyTransform(triangle.p1, transform);
        const Point3f world_p2 = ApplyTransform(triangle.p2, transform);
        const Vec3f n0 = ApplyNormalTransform(triangle.n0, transform);
        const Vec3f n1 = ApplyNormalTransform(triangle.n1, transform);
        const Vec3f n2 = ApplyNormalTransform(triangle.n2, transform);
        const bool has_vertex_normals =
            triangle.has_vertex_normals &&
            LengthSquared(n0) > 0.0f &&
            LengthSquared(n1) > 0.0f &&
            LengthSquared(n2) > 0.0f;
        int material_index = override_material_index.value_or(-1);
        if (!override_material_index.has_value() &&
            triangle.material_index >= 0 &&
            static_cast<std::size_t>(triangle.material_index) < imported_material_indices.size()) {
            material_index = imported_material_indices[static_cast<std::size_t>(triangle.material_index)];
        }
        if (material_index < 0) {
            if (fallback_material_index < 0) {
                fallback_material_index = AddDefaultMaterial(compiled);
            }
            material_index = fallback_material_index;
        }

        RenderTriangle render_triangle;
        render_triangle.p0 = world_p0;
        render_triangle.p1 = world_p1;
        render_triangle.p2 = world_p2;
        render_triangle.normal = Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0));
        render_triangle.material_index = material_index;
        render_triangle.uv0 = triangle.uv0;
        render_triangle.uv1 = triangle.uv1;
        render_triangle.uv2 = triangle.uv2;
        render_triangle.has_uv = triangle.has_uv;
        render_triangle.n0 = n0;
        render_triangle.n1 = n1;
        render_triangle.n2 = n2;
        render_triangle.has_vertex_normals = has_vertex_normals;
        compiled.triangles.push_back(render_triangle);
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
    std::optional<int> override_material_index,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
    TextureCache& texture_cache,
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

    std::vector<int> imported_material_indices;
    if (!override_material_index.has_value()) {
        imported_material_indices = CompileImportedMaterials(scene, compiled, cached->second, texture_cache, diagnostics);
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }

    AppendImportedMesh(compiled, cached->second, transform, override_material_index, imported_material_indices);
}

void AppendGltfAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& asset_path,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string cache_key = asset_path.generic_string();
    auto cached = mesh_cache.find(cache_key);
    if (cached == mesh_cache.end()) {
        AssetLoadResult load_result = LoadGltfMesh(asset_path);
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
            diagnostics.push_back(Error(scene, "assets.path", "glTF loader returned no mesh: " + cache_key));
            return;
        }
        cached = mesh_cache.emplace(cache_key, std::move(load_result.mesh.value())).first;
    }

    std::vector<int> imported_material_indices;
    if (!override_material_index.has_value()) {
        imported_material_indices = CompileImportedMaterials(scene, compiled, cached->second, texture_cache, diagnostics);
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }

    AppendImportedMesh(compiled, cached->second, transform, override_material_index, imported_material_indices);
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
    compiled.radiance_clamp = scene.render.radiance_clamp;

    if (!scene.camera.has_value()) {
        result.diagnostics.push_back(Error(scene, "camera", "missing camera"));
    } else {
        compiled.camera = CompileCamera(scene.camera.value());
    }

    CompileEnvironment(scene, compiled, result.diagnostics);

    CopyAreaLights(scene, compiled);

    const std::unordered_map<std::string, const AssetDescription*> assets = BuildAssetMap(scene);
    const std::unordered_map<std::string, int> materials = BuildMaterialMap(scene, compiled);
    std::unordered_map<std::string, ImportedMesh> mesh_cache;
    TextureCache texture_cache;
    for (const InstanceDescription& instance : scene.instances) {
        const auto asset = assets.find(instance.asset);
        if (asset == assets.end()) {
            result.diagnostics.push_back(Error(scene, "instances.asset", "references unknown asset"));
            continue;
        }

        const AssetDescription& asset_description = *asset->second;
        const std::filesystem::path& asset_path = asset_description.path;
        const std::string asset_path_string = asset_path.generic_string();
        std::optional<int> material_index;
        if (!instance.material.empty()) {
            material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
            if (!material_index.has_value()) {
                continue;
            }
        }

        if (!asset_description.quads.empty()) {
            if (!material_index.has_value()) {
                material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
                if (!material_index.has_value()) {
                    continue;
                }
            }
            AppendInlineQuadAsset(scene, compiled, asset_description, instance.transform, *material_index, result.diagnostics);
        } else if (asset_path_string == "builtin:triangle") {
            if (!material_index.has_value()) {
                material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
                if (!material_index.has_value()) {
                    continue;
                }
            }
            AppendBuiltinTriangle(compiled, instance.transform, *material_index);
        } else if (HasObjExtension(asset_path)) {
            AppendObjAsset(
                scene,
                compiled,
                asset_path,
                instance.transform,
                material_index,
                mesh_cache,
                texture_cache,
                result.diagnostics
            );
        } else if (HasGltfExtension(asset_path)) {
            AppendGltfAsset(
                scene,
                compiled,
                asset_path,
                instance.transform,
                material_index,
                mesh_cache,
                texture_cache,
                result.diagnostics
            );
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
