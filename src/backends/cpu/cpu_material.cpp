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

Vec3f OrthogonalizeTangent(Vec3f tangent, Vec3f normal) {
    const Vec3f projected = tangent - normal * Dot(tangent, normal);
    if (LengthSquared(projected) <= 1.0e-12f) {
        return Vec3f{};
    }
    return Normalize(projected);
}

Vec3f ResolveNormalMap(
    const RenderSceneIR& scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    const RenderMaterial& material,
    Vec2f uv,
    Vec3f shading_normal
) {
    if (!scene.primitives[tri.primitive_index].has_tangents || !TextureIndexValid(scene, material.normal_map)) {
        return shading_normal;
    }

    const Color3f encoded = SampleTexture(scene.textures[static_cast<std::size_t>(material.normal_map)], uv);
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

    Vec3f tangent = OrthogonalizeTangent(InterpolateTangent(scene, tri, bary_u, bary_v), shading_normal);
    if (LengthSquared(tangent) == 0.0f) {
        return shading_normal;
    }
    const float handedness = InterpolateHandedness(scene, tri, bary_u, bary_v);
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
    TriangleRef tri,
    const RenderMaterial& base_material,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal,
    Vec3f wo
) {
    const RenderPrimitive& prim = scene.primitives[tri.primitive_index];

    ResolvedMaterialSample sample;
    sample.material = base_material;
    sample.uv = prim.has_uvs ? InterpolateUv(scene, tri, bary_u, bary_v) : Vec2f{};
    sample.alpha = base_material.alpha.value;

    if (prim.has_uvs && TextureIndexValid(scene, sample.material.reflectance.texture)) {
        const Color3f tex_color = SampleTexture(scene.textures[static_cast<std::size_t>(sample.material.reflectance.texture)], sample.uv);
        sample.material.reflectance.value = Multiply(sample.material.reflectance.value, tex_color);
    }

    if (prim.has_uvs && TextureIndexValid(scene, sample.material.alpha.texture)) {
        const Color4f alpha_tex = SampleTexture4(scene.textures[static_cast<std::size_t>(sample.material.alpha.texture)], sample.uv);
        sample.alpha *= alpha_tex.x;
    }

    sample.shading_normal = ResolveShadingNormal(scene, tri, bary_u, bary_v, geometric_normal);
    if (prim.has_uvs) {
        sample.shading_normal = ResolveNormalMap(
            scene,
            tri,
            bary_u,
            bary_v,
            sample.material,
            sample.uv,
            sample.shading_normal
        );
    }
    sample.shading_normal = FaceForward(sample.shading_normal, wo);
    return sample;
}

bool IsAlphaVisible(const ResolvedMaterialSample& sample) {
    return sample.alpha >= 0.5f;
}

} // namespace yr
