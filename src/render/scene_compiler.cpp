#include <yaoray/render/scene_compiler.hpp>

#include <yaoray/assets/gltf_loader.hpp>
#include <yaoray/assets/obj_loader.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/render/texture.hpp>

#include <array>
#include <cmath>
#include <cstdint>
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

Mat4 FromAssetTransform(const AssetTransform& transform) {
    Mat4 result;
    result.m = transform.local_to_parent;
    return result;
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

Mat4 RotationXMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    return result;
}

Mat4 RotationYMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 RotationZMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[0] = c;
    result.m[1] = s;
    result.m[4] = -s;
    result.m[5] = c;
    return result;
}

Mat4 InstanceTransformMatrix(const TransformDescription& transform) {
    return Multiply(
        Multiply(
            Multiply(
                Multiply(
                    TranslationMatrix(transform.translate),
                    RotationZMatrix(DegreesToRadians(transform.rotate_degrees.z))
                ),
                RotationYMatrix(DegreesToRadians(transform.rotate_degrees.y))
            ),
            RotationXMatrix(DegreesToRadians(transform.rotate_degrees.x))
        ),
        ScaleMatrix(transform.scale)
    );
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

void CopyAreaLights(const SceneDescription& scene, RenderSceneIR& compiled) {
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
    RenderSceneIR& compiled,
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
    RenderSceneIR& compiled,
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

std::unordered_map<std::string, int> BuildMaterialMap(const SceneDescription& scene, RenderSceneIR& compiled) {
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

int AddDefaultMaterial(RenderSceneIR& compiled) {
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
    RenderSceneIR& compiled,
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

std::vector<int> CompileAssetMaterials(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetResource& resource,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::vector<int> material_indices;
    material_indices.reserve(resource.materials.size());

    for (const AssetMaterial& material : resource.materials) {
        RenderMaterial render_material;
        render_material.type = material.approximate_type;
        render_material.albedo = material.base_color;
        render_material.emission = material.emission;
        render_material.roughness = material.roughness;
        render_material.specular = material.specular;
        if (material.base_color_texture < -1) {
            diagnostics.push_back(Error(scene, "assets.path", "asset material references an invalid base color texture"));
            material_indices.push_back(-1);
            continue;
        }
        if (material.base_color_texture >= 0) {
            if (static_cast<std::size_t>(material.base_color_texture) >= resource.textures.size()) {
                diagnostics.push_back(Error(scene, "assets.path", "asset material references an invalid base color texture"));
                material_indices.push_back(-1);
                continue;
            }

            const AssetTexture& texture = resource.textures[static_cast<std::size_t>(material.base_color_texture)];
            if (texture.image < 0 || static_cast<std::size_t>(texture.image) >= resource.images.size()) {
                diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid image"));
                material_indices.push_back(-1);
                continue;
            }
            if (texture.sampler < -1 ||
                (texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) >= resource.samplers.size())) {
                diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid sampler"));
                material_indices.push_back(-1);
                continue;
            }

            TextureWrap wrap_s = TextureWrap::Repeat;
            TextureWrap wrap_t = TextureWrap::Repeat;
            if (texture.sampler >= 0) {
                const AssetSampler& sampler = resource.samplers[static_cast<std::size_t>(texture.sampler)];
                wrap_s = sampler.wrap_s;
                wrap_t = sampler.wrap_t;
            }

            const std::optional<int> texture_index = LoadTextureIndex(
                scene,
                compiled,
                resource.images[static_cast<std::size_t>(texture.image)].path,
                wrap_s,
                wrap_t,
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
    RenderSceneIR& compiled,
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

void AppendBuiltinTriangle(RenderSceneIR& compiled, const TransformDescription& transform, int material_index) {
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

bool AppendAssetPrimitive(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetPrimitive& primitive,
    Mat4 transform,
    std::optional<int> override_material_index,
    const std::vector<int>& asset_material_indices,
    int& fallback_material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (primitive.topology != AssetPrimitiveTopology::Triangles) {
        diagnostics.push_back(Error(scene, "assets.path", "asset primitive topology is not supported"));
        return false;
    }
    if (primitive.indices.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "assets.path", "asset triangle primitive index count is not divisible by three"));
        return false;
    }
    if (!primitive.texcoords0.empty() && primitive.texcoords0.size() != primitive.positions.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset primitive texcoord count does not match positions"));
        return false;
    }
    if (!primitive.normals.empty() && primitive.normals.size() != primitive.positions.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset primitive normal count does not match positions"));
        return false;
    }

    const bool has_uv = primitive.texcoords0.size() == primitive.positions.size();
    const bool has_normals = primitive.normals.size() == primitive.positions.size();
    for (std::size_t index_offset = 0; index_offset < primitive.indices.size(); index_offset += 3) {
        const std::uint32_t i0 = primitive.indices[index_offset + 0];
        const std::uint32_t i1 = primitive.indices[index_offset + 1];
        const std::uint32_t i2 = primitive.indices[index_offset + 2];
        if (i0 >= primitive.positions.size() || i1 >= primitive.positions.size() || i2 >= primitive.positions.size()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset triangle index references an invalid position"));
            return false;
        }

        const Point3f world_p0 = TransformPoint(transform, primitive.positions[i0]);
        const Point3f world_p1 = TransformPoint(transform, primitive.positions[i1]);
        const Point3f world_p2 = TransformPoint(transform, primitive.positions[i2]);
        const Vec3f face_normal = Cross(world_p1 - world_p0, world_p2 - world_p0);
        if (LengthSquared(face_normal) <= DegenerateTriangleEpsilon) {
            diagnostics.push_back(Error(scene, "assets.path", "asset primitive produces degenerate triangle"));
            return false;
        }

        int material_index = override_material_index.value_or(-1);
        if (!override_material_index.has_value()) {
            if (primitive.material < -1) {
                diagnostics.push_back(Error(scene, "assets.path", "asset primitive references an invalid material"));
                return false;
            }
            if (primitive.material >= 0) {
                if (static_cast<std::size_t>(primitive.material) >= asset_material_indices.size()) {
                    diagnostics.push_back(Error(scene, "assets.path", "asset primitive references an invalid material"));
                    return false;
                }
                material_index = asset_material_indices[static_cast<std::size_t>(primitive.material)];
            }
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
        render_triangle.normal = Normalize(face_normal);
        render_triangle.material_index = material_index;
        if (has_uv) {
            render_triangle.uv0 = primitive.texcoords0[i0];
            render_triangle.uv1 = primitive.texcoords0[i1];
            render_triangle.uv2 = primitive.texcoords0[i2];
            render_triangle.has_uv = true;
        }
        if (has_normals) {
            const Vec3f n0 = TransformNormal(transform, primitive.normals[i0]);
            const Vec3f n1 = TransformNormal(transform, primitive.normals[i1]);
            const Vec3f n2 = TransformNormal(transform, primitive.normals[i2]);
            render_triangle.n0 = n0;
            render_triangle.n1 = n1;
            render_triangle.n2 = n2;
            render_triangle.has_vertex_normals =
                LengthSquared(n0) > 0.0f &&
                LengthSquared(n1) > 0.0f &&
                LengthSquared(n2) > 0.0f;
        }
        compiled.triangles.push_back(render_triangle);
    }
    return true;
}

bool AppendAssetNode(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetResource& resource,
    int node_index,
    Mat4 parent_transform,
    std::optional<int> override_material_index,
    const std::vector<int>& asset_material_indices,
    int& fallback_material_index,
    std::vector<int>& node_stack,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (node_index < 0 || static_cast<std::size_t>(node_index) >= resource.nodes.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset scene references an invalid node"));
        return false;
    }
    for (int active_node : node_stack) {
        if (active_node == node_index) {
            diagnostics.push_back(Error(scene, "assets.path", "asset node hierarchy contains a cycle"));
            return false;
        }
    }
    node_stack.push_back(node_index);
    struct StackPop {
        std::vector<int>& values;
        ~StackPop() {
            values.pop_back();
        }
    } stack_pop{node_stack};

    const AssetNode& node = resource.nodes[static_cast<std::size_t>(node_index)];
    const Mat4 transform = Multiply(parent_transform, FromAssetTransform(node.transform));
    if (node.mesh < -1) {
        diagnostics.push_back(Error(scene, "assets.path", "asset node references an invalid mesh"));
        return false;
    }
    if (node.mesh >= 0) {
        if (static_cast<std::size_t>(node.mesh) >= resource.meshes.size()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset node references an invalid mesh"));
            return false;
        }
        const AssetMesh& mesh = resource.meshes[static_cast<std::size_t>(node.mesh)];
        for (const AssetPrimitive& primitive : mesh.primitives) {
            if (!AppendAssetPrimitive(
                    scene,
                    compiled,
                    primitive,
                    transform,
                    override_material_index,
                    asset_material_indices,
                    fallback_material_index,
                    diagnostics
                )) {
                return false;
            }
        }
    }

    for (int child_index : node.children) {
        if (child_index < 0 || static_cast<std::size_t>(child_index) >= resource.nodes.size()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset node references an invalid child"));
            return false;
        }
        if (!AppendAssetNode(
                scene,
                compiled,
                resource,
                child_index,
                transform,
                override_material_index,
                asset_material_indices,
                fallback_material_index,
                node_stack,
                diagnostics
            )) {
            return false;
        }
    }

    return true;
}

void AppendInlineQuadAsset(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
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

enum class AssetFileKind {
    Obj,
    Gltf,
};

void AppendImportedAssetResource(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const std::filesystem::path& asset_path,
    AssetFileKind kind,
    const InstanceDescription& instance,
    std::optional<int> override_material_index,
    std::unordered_map<std::string, AssetResource>& asset_cache,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string cache_key = asset_path.generic_string();
    auto cached = asset_cache.find(cache_key);
    if (cached == asset_cache.end()) {
        AssetLoadResult load_result = kind == AssetFileKind::Obj
            ? LoadObjResource(asset_path)
            : LoadGltfResource(asset_path);
        for (const std::string& warning : load_result.warnings) {
            diagnostics.push_back(Warning(scene, "assets.path", warning));
        }
        for (const std::string& error : load_result.errors) {
            diagnostics.push_back(Error(scene, "assets.path", error));
        }
        if (!load_result.errors.empty()) {
            return;
        }
        if (!load_result.resource.has_value()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset loader returned no resource: " + cache_key));
            return;
        }
        cached = asset_cache.emplace(cache_key, std::move(load_result.resource.value())).first;
    }

    const AssetResource& resource = cached->second;
    if (resource.default_scene < 0 || static_cast<std::size_t>(resource.default_scene) >= resource.scenes.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset resource default scene index is invalid"));
        return;
    }

    std::vector<int> asset_material_indices;
    if (!override_material_index.has_value()) {
        asset_material_indices = CompileAssetMaterials(scene, compiled, resource, texture_cache, diagnostics);
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }

    const AssetScene& asset_scene = resource.scenes[static_cast<std::size_t>(resource.default_scene)];
    const Mat4 instance_transform = InstanceTransformMatrix(instance.transform);
    int fallback_material_index = -1;
    std::vector<int> node_stack;
    for (int root_node : asset_scene.root_nodes) {
        if (!AppendAssetNode(
                scene,
                compiled,
                resource,
                root_node,
                instance_transform,
                override_material_index,
                asset_material_indices,
                fallback_material_index,
                node_stack,
                diagnostics
            )) {
            return;
        }
    }
}

} // namespace

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderSceneIR compiled;
    compiled.requested_backend = scene.render.backend;
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
    std::unordered_map<std::string, AssetResource> asset_cache;
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
            AppendImportedAssetResource(
                scene,
                compiled,
                asset_path,
                AssetFileKind::Obj,
                instance,
                material_index,
                asset_cache,
                texture_cache,
                result.diagnostics
            );
        } else if (HasGltfExtension(asset_path)) {
            AppendImportedAssetResource(
                scene,
                compiled,
                asset_path,
                AssetFileKind::Gltf,
                instance,
                material_index,
                asset_cache,
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

    result.scene = std::move(compiled);
    return result;
}

} // namespace yr
