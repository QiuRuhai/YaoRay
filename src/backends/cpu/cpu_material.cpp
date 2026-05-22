#include <yaoray/backends/cpu/cpu_material.hpp>

#include <yaoray/render/shading.hpp>
#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr {
namespace {

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool TextureIndexValid(const RenderSceneIR& scene, int texture_index) {
    return texture_index >= 0 && static_cast<std::size_t>(texture_index) < scene.textures.size();
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

Vec3f InterpolateTangent(const RenderTriangle& triangle, Vec3f barycentric) {
    return Normalize(
        triangle.t0 * barycentric.x +
        triangle.t1 * barycentric.y +
        triangle.t2 * barycentric.z
    );
}

float InterpolateHandedness(const RenderTriangle& triangle, Vec3f barycentric) {
    const float handedness =
        triangle.tangent_handedness0 * barycentric.x +
        triangle.tangent_handedness1 * barycentric.y +
        triangle.tangent_handedness2 * barycentric.z;
    return handedness < 0.0f ? -1.0f : 1.0f;
}

Vec3f OrthogonalizeTangent(Vec3f tangent, Vec3f normal) {
    const Vec3f projected = tangent - normal * Dot(tangent, normal);
    if (LengthSquared(projected) <= 1.0e-12f) {
        return Vec3f{};
    }
    return Normalize(projected);
}

Vec3f ResolveNormalMap(
    const RenderSceneIR& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& material,
    Vec3f barycentric,
    Vec2f uv,
    Vec3f shading_normal
) {
    if (!triangle.has_tangents || !TextureIndexValid(scene, material.normal_texture)) {
        return shading_normal;
    }

    const Color3f encoded = SampleTexture(scene.textures[static_cast<std::size_t>(material.normal_texture)], uv);
    Vec3f tangent_space{
        encoded.x * 2.0f - 1.0f,
        encoded.y * 2.0f - 1.0f,
        encoded.z * 2.0f - 1.0f
    };
    tangent_space.x *= material.normal_scale;
    tangent_space.y *= material.normal_scale;
    tangent_space = Normalize(tangent_space);
    if (LengthSquared(tangent_space) == 0.0f) {
        return shading_normal;
    }

    Vec3f tangent = OrthogonalizeTangent(InterpolateTangent(triangle, barycentric), shading_normal);
    if (LengthSquared(tangent) == 0.0f) {
        return shading_normal;
    }
    const float handedness = InterpolateHandedness(triangle, barycentric);
    const Vec3f bitangent = Cross(shading_normal, tangent) * handedness;
    const Vec3f mapped = Normalize(
        tangent * tangent_space.x +
        bitangent * tangent_space.y +
        shading_normal * tangent_space.z
    );
    return LengthSquared(mapped) > 0.0f ? mapped : shading_normal;
}

} // namespace

ResolvedMaterialSample ResolveCpuMaterialSample(
    const RenderSceneIR& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& base_material,
    Vec3f barycentric,
    Vec3f geometric_normal,
    Vec3f wo
) {
    ResolvedMaterialSample sample;
    sample.material = base_material;
    sample.uv = triangle.has_uv ? InterpolateUv(triangle, barycentric) : Vec2f{};
    sample.alpha = base_material.albedo_alpha;

    if (triangle.has_uv && TextureIndexValid(scene, sample.material.albedo_texture)) {
        const Color4f albedo = SampleTexture4(scene.textures[static_cast<std::size_t>(sample.material.albedo_texture)], sample.uv);
        sample.material.albedo = Multiply(sample.material.albedo, albedo.rgb());
        sample.alpha *= albedo.w;
    }

    if (triangle.has_uv && TextureIndexValid(scene, sample.material.metallic_roughness_texture)) {
        const Color4f metallic_roughness =
            SampleTexture4(scene.textures[static_cast<std::size_t>(sample.material.metallic_roughness_texture)], sample.uv);
        sample.material.roughness = std::clamp(metallic_roughness.y, 0.0f, 1.0f);
        sample.material.metallic = std::clamp(metallic_roughness.z, 0.0f, 1.0f);
        if (sample.material.metallic >= 0.5f) {
            sample.material.type = MaterialKind::Metal;
        }
    }

    if (triangle.has_uv && TextureIndexValid(scene, sample.material.emissive_texture)) {
        const Color3f emissive = SampleTexture(scene.textures[static_cast<std::size_t>(sample.material.emissive_texture)], sample.uv);
        sample.material.emission = Multiply(sample.material.emission, emissive);
    }

    sample.shading_normal = ResolveShadingNormal(triangle, barycentric, geometric_normal);
    if (triangle.has_uv) {
        sample.shading_normal = ResolveNormalMap(
            scene,
            triangle,
            sample.material,
            barycentric,
            sample.uv,
            sample.shading_normal
        );
    }
    sample.shading_normal = FaceForward(sample.shading_normal, wo);
    return sample;
}

bool IsAlphaVisible(const ResolvedMaterialSample& sample) {
    if (sample.material.alpha_mode != RenderAlphaMode::Mask) {
        return true;
    }
    return sample.alpha >= sample.material.alpha_cutoff;
}

} // namespace yr
