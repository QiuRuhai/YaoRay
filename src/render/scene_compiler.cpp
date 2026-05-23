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

bool IsFinite(Vec3f value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vec3f FallbackTangent(Vec3f normal) {
    const Vec3f axis = std::fabs(normal.x) < 0.9f ? Vec3f{1.0f, 0.0f, 0.0f} : Vec3f{0.0f, 1.0f, 0.0f};
    const Vec3f tangent = axis - normal * Dot(axis, normal);
    return Normalize(tangent);
}

Vec3f OrthogonalizeTangent(Vec3f tangent, Vec3f normal) {
    if (LengthSquared(normal) == 0.0f) {
        return Normalize(tangent);
    }
    const Vec3f unit_normal = Normalize(normal);
    const Vec3f projected = tangent - unit_normal * Dot(tangent, unit_normal);
    if (LengthSquared(projected) <= DegenerateTriangleEpsilon || !IsFinite(projected)) {
        return FallbackTangent(unit_normal);
    }
    return Normalize(projected);
}

struct PrimitiveTangent {
    Vec3f direction;
    float handedness = 1.0f;
    bool valid = false;
};

std::vector<PrimitiveTangent> BuildPrimitiveTangents(
    const AssetPrimitive& primitive,
    bool has_uv,
    bool has_normals
) {
    std::vector<PrimitiveTangent> tangents(primitive.positions.size());
    if (primitive.tangents.size() == primitive.positions.size()) {
        for (std::size_t vertex = 0; vertex < primitive.tangents.size(); ++vertex) {
            const Vec3f normal = has_normals ? primitive.normals[vertex] : Vec3f{};
            const Vec3f tangent = OrthogonalizeTangent(primitive.tangents[vertex].direction, normal);
            tangents[vertex] = PrimitiveTangent{tangent, primitive.tangents[vertex].handedness, LengthSquared(tangent) > 0.0f};
        }
        return tangents;
    }

    if (!has_uv || !has_normals) {
        return tangents;
    }

    std::vector<Vec3f> accumulated(primitive.positions.size());
    for (std::size_t index_offset = 0; index_offset < primitive.indices.size(); index_offset += 3) {
        const std::uint32_t i0 = primitive.indices[index_offset + 0];
        const std::uint32_t i1 = primitive.indices[index_offset + 1];
        const std::uint32_t i2 = primitive.indices[index_offset + 2];
        if (i0 >= primitive.positions.size() || i1 >= primitive.positions.size() || i2 >= primitive.positions.size()) {
            continue;
        }

        const Vec3f edge1 = primitive.positions[i1] - primitive.positions[i0];
        const Vec3f edge2 = primitive.positions[i2] - primitive.positions[i0];
        const Vec2f uv0 = primitive.texcoords0[i0];
        const Vec2f uv1 = primitive.texcoords0[i1];
        const Vec2f uv2 = primitive.texcoords0[i2];
        const Vec2f duv1{uv1.x - uv0.x, uv1.y - uv0.y};
        const Vec2f duv2{uv2.x - uv0.x, uv2.y - uv0.y};
        const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::fabs(determinant) <= 1.0e-12f) {
            continue;
        }
        const Vec3f tangent = (edge1 * duv2.y - edge2 * duv1.y) / determinant;
        if (!IsFinite(tangent) || LengthSquared(tangent) <= DegenerateTriangleEpsilon) {
            continue;
        }
        accumulated[i0] = accumulated[i0] + tangent;
        accumulated[i1] = accumulated[i1] + tangent;
        accumulated[i2] = accumulated[i2] + tangent;
    }

    for (std::size_t vertex = 0; vertex < tangents.size(); ++vertex) {
        const Vec3f normal = Normalize(primitive.normals[vertex]);
        if (LengthSquared(normal) == 0.0f) {
            continue;
        }
        const Vec3f tangent = OrthogonalizeTangent(accumulated[vertex], normal);
        tangents[vertex] = PrimitiveTangent{tangent, 1.0f, LengthSquared(tangent) > 0.0f};
    }
    return tangents;
}

struct TextureCache {
    std::unordered_map<std::string, int> indices;
};

enum class TextureUsage {
    Color,
    Data,
};

TextureColorSpace TextureColorSpaceForUsage(TextureUsage usage) {
    return usage == TextureUsage::Color ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
}

std::string TextureUsageName(TextureUsage usage) {
    return usage == TextureUsage::Color ? "color" : "data";
}

RenderVertex VertexFromTriangleCorner(
    const RenderTriangle& triangle,
    Point3f position,
    Vec2f uv,
    Vec3f normal,
    Vec3f tangent,
    float tangent_handedness
) {
    RenderVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.uv = uv;
    vertex.tangent = tangent;
    vertex.tangent_handedness = tangent_handedness;
    vertex.has_uv = triangle.has_uv;
    vertex.has_normal = triangle.has_vertex_normals;
    vertex.has_tangent = triangle.has_tangents;
    return vertex;
}

void AppendRenderTriangle(RenderSceneIR& compiled, RenderTriangle triangle) {
    const std::uint32_t first_vertex = static_cast<std::uint32_t>(compiled.vertices.size());
    const std::uint32_t first_index = static_cast<std::uint32_t>(compiled.indices.size());

    const Vec3f n0 = triangle.has_vertex_normals ? triangle.n0 : triangle.normal;
    const Vec3f n1 = triangle.has_vertex_normals ? triangle.n1 : triangle.normal;
    const Vec3f n2 = triangle.has_vertex_normals ? triangle.n2 : triangle.normal;

    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p0,
        triangle.uv0,
        n0,
        triangle.t0,
        triangle.tangent_handedness0
    ));
    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p1,
        triangle.uv1,
        n1,
        triangle.t1,
        triangle.tangent_handedness1
    ));
    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p2,
        triangle.uv2,
        n2,
        triangle.t2,
        triangle.tangent_handedness2
    ));

    compiled.indices.push_back(first_vertex + 0);
    compiled.indices.push_back(first_vertex + 1);
    compiled.indices.push_back(first_vertex + 2);
    compiled.primitives.push_back(RenderPrimitive{first_index, 3, triangle.material_index});
    compiled.triangles.push_back(triangle);
}

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

    AppendRenderTriangle(compiled, RenderTriangle{
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

std::string CanonicalTextureKey(
    const std::filesystem::path& path,
    TextureWrap wrap_s,
    TextureWrap wrap_t,
    TextureUsage usage
) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string normalized = ec ? path.lexically_normal().generic_string() : canonical.generic_string();
    return normalized + "|s=" + TextureWrapName(wrap_s) + "|t=" + TextureWrapName(wrap_t) + "|usage=" + TextureUsageName(usage);
}

std::optional<int> LoadTextureIndex(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const std::filesystem::path& path,
    TextureWrap wrap_s,
    TextureWrap wrap_t,
    TextureUsage usage,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string key = CanonicalTextureKey(path, wrap_s, wrap_t, usage);
    const auto found = texture_cache.indices.find(key);
    if (found != texture_cache.indices.end()) {
        return found->second;
    }

    TextureLoadResult load = LoadLdrTexture(path, TextureColorSpaceForUsage(usage));
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

RenderAlphaMode ConvertAssetAlphaMode(AssetAlphaMode mode) {
    switch (mode) {
        case AssetAlphaMode::Opaque:
            return RenderAlphaMode::Opaque;
        case AssetAlphaMode::Mask:
            return RenderAlphaMode::Mask;
        case AssetAlphaMode::Blend:
            return RenderAlphaMode::Blend;
    }
    return RenderAlphaMode::Opaque;
}

std::optional<int> CompileMaterialTexture(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetResource& resource,
    int asset_texture_index,
    std::string_view slot_name,
    TextureUsage usage,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (asset_texture_index == -1) {
        return -1;
    }
    if (asset_texture_index < -1 || static_cast<std::size_t>(asset_texture_index) >= resource.textures.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset material references an invalid " + std::string{slot_name} + " texture"));
        return std::nullopt;
    }

    const AssetTexture& texture = resource.textures[static_cast<std::size_t>(asset_texture_index)];
    if (texture.image < 0 || static_cast<std::size_t>(texture.image) >= resource.images.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid image"));
        return std::nullopt;
    }
    if (texture.sampler < -1 ||
        (texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) >= resource.samplers.size())) {
        diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid sampler"));
        return std::nullopt;
    }

    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    if (texture.sampler >= 0) {
        const AssetSampler& sampler = resource.samplers[static_cast<std::size_t>(texture.sampler)];
        wrap_s = sampler.wrap_s;
        wrap_t = sampler.wrap_t;
    }

    return LoadTextureIndex(
        scene,
        compiled,
        resource.images[static_cast<std::size_t>(texture.image)].path,
        wrap_s,
        wrap_t,
        usage,
        texture_cache,
        diagnostics
    );
}

MaterialKind LowerAssetMaterialKind(const AssetMaterial& material) {
    if (material.metallic >= 0.5f) {
        return MaterialKind::Metal;
    }
    if (material.roughness < 0.35f) {
        return MaterialKind::Plastic;
    }
    return MaterialKind::Diffuse;
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
        render_material.type = LowerAssetMaterialKind(material);
        render_material.albedo = material.base_color;
        render_material.albedo_alpha = material.base_color_alpha;
        render_material.emission = material.emission;
        render_material.metallic = material.metallic;
        render_material.roughness = material.roughness;
        render_material.specular = material.specular;
        render_material.normal_scale = material.normal_scale;
        render_material.occlusion_strength = material.occlusion_strength;
        render_material.alpha_mode = ConvertAssetAlphaMode(material.alpha_mode);
        render_material.alpha_cutoff = material.alpha_cutoff;
        render_material.double_sided = material.double_sided;
        if (render_material.alpha_mode == RenderAlphaMode::Blend) {
            diagnostics.push_back(Warning(scene, "assets.path", "glTF alphaMode BLEND is preserved but rendered as opaque in this compatibility slice"));
        }

        const std::optional<int> albedo_texture = CompileMaterialTexture(
            scene,
            compiled,
            resource,
            material.base_color_texture,
            "base color",
            TextureUsage::Color,
            texture_cache,
            diagnostics
        );
        const std::optional<int> metallic_roughness_texture = CompileMaterialTexture(
            scene,
            compiled,
            resource,
            material.metallic_roughness_texture,
            "metallic-roughness",
            TextureUsage::Data,
            texture_cache,
            diagnostics
        );
        const std::optional<int> normal_texture = CompileMaterialTexture(
            scene,
            compiled,
            resource,
            material.normal_texture,
            "normal",
            TextureUsage::Data,
            texture_cache,
            diagnostics
        );
        const std::optional<int> occlusion_texture = CompileMaterialTexture(
            scene,
            compiled,
            resource,
            material.occlusion_texture,
            "occlusion",
            TextureUsage::Data,
            texture_cache,
            diagnostics
        );
        const std::optional<int> emissive_texture = CompileMaterialTexture(
            scene,
            compiled,
            resource,
            material.emissive_texture,
            "emissive",
            TextureUsage::Color,
            texture_cache,
            diagnostics
        );
        if (!albedo_texture.has_value() ||
            !metallic_roughness_texture.has_value() ||
            !normal_texture.has_value() ||
            !occlusion_texture.has_value() ||
            !emissive_texture.has_value()) {
            material_indices.push_back(-1);
            continue;
        }

        render_material.albedo_texture = *albedo_texture;
        render_material.metallic_roughness_texture = *metallic_roughness_texture;
        render_material.normal_texture = *normal_texture;
        render_material.occlusion_texture = *occlusion_texture;
        render_material.emissive_texture = *emissive_texture;

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

    AppendRenderTriangle(compiled, RenderTriangle{
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
    if (!primitive.tangents.empty() && primitive.tangents.size() != primitive.positions.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset primitive tangent count does not match positions"));
        return false;
    }

    const bool has_uv = primitive.texcoords0.size() == primitive.positions.size();
    const bool has_normals = primitive.normals.size() == primitive.positions.size();
    const std::vector<PrimitiveTangent> primitive_tangents = BuildPrimitiveTangents(primitive, has_uv, has_normals);
    bool warned_degenerate_triangle = false;
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
            if (!warned_degenerate_triangle) {
                diagnostics.push_back(Warning(scene, "assets.path", "skipping degenerate asset triangle"));
                warned_degenerate_triangle = true;
            }
            continue;
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
        if (i0 < primitive_tangents.size() &&
            i1 < primitive_tangents.size() &&
            i2 < primitive_tangents.size() &&
            primitive_tangents[i0].valid &&
            primitive_tangents[i1].valid &&
            primitive_tangents[i2].valid) {
            const Vec3f n0 = render_triangle.has_vertex_normals ? render_triangle.n0 : render_triangle.normal;
            const Vec3f n1 = render_triangle.has_vertex_normals ? render_triangle.n1 : render_triangle.normal;
            const Vec3f n2 = render_triangle.has_vertex_normals ? render_triangle.n2 : render_triangle.normal;
            render_triangle.t0 = OrthogonalizeTangent(TransformVector(transform, primitive_tangents[i0].direction), n0);
            render_triangle.t1 = OrthogonalizeTangent(TransformVector(transform, primitive_tangents[i1].direction), n1);
            render_triangle.t2 = OrthogonalizeTangent(TransformVector(transform, primitive_tangents[i2].direction), n2);
            render_triangle.tangent_handedness0 = primitive_tangents[i0].handedness;
            render_triangle.tangent_handedness1 = primitive_tangents[i1].handedness;
            render_triangle.tangent_handedness2 = primitive_tangents[i2].handedness;
            render_triangle.has_tangents =
                LengthSquared(render_triangle.t0) > 0.0f &&
                LengthSquared(render_triangle.t1) > 0.0f &&
                LengthSquared(render_triangle.t2) > 0.0f;
        }
        AppendRenderTriangle(compiled, render_triangle);
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
