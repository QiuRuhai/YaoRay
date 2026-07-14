#include <yaoray/shading/material_evaluator.hpp>

#include <yaoray/geometry/intersection.hpp>
#include <yaoray/shading/texture.hpp>
#include <yaoray/shading/bssrdf.hpp>
#include <yaoray/shading/measured_brdf.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr {
namespace {

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool TextureIndexValid(ShadingSceneView scene, int texture_index) {
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
    ShadingSceneView scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    const RenderMaterial& material,
    Vec2f uv,
    Vec3f shading_normal,
    Mat4f object_to_world,
    bool transform_shading_frame,
    TextureFootprint texture_footprint
) {
    const RenderPrimitive* primitive = scene.geometry.Find(tri.primitive);
    if (primitive == nullptr || !primitive->has_tangents ||
        !TextureIndexValid(scene, material.normal_map)) {
        return shading_normal;
    }

    const Color3f encoded = SampleTexture(
        scene.textures[static_cast<std::size_t>(material.normal_map)], uv, texture_footprint);
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

    Vec3f tangent = InterpolateTangent(scene.geometry, tri, bary_u, bary_v);
    if (transform_shading_frame) {
        tangent = TransformVector(object_to_world, tangent);
    }
    tangent = OrthogonalizeTangent(tangent, shading_normal);
    if (LengthSquared(tangent) == 0.0f) {
        return shading_normal;
    }
    float orientation = 1.0f;
    if (transform_shading_frame) {
        const float determinant =
            object_to_world.m[0] * (object_to_world.m[5] * object_to_world.m[10] - object_to_world.m[9] * object_to_world.m[6]) -
            object_to_world.m[4] * (object_to_world.m[1] * object_to_world.m[10] - object_to_world.m[9] * object_to_world.m[2]) +
            object_to_world.m[8] * (object_to_world.m[1] * object_to_world.m[6] - object_to_world.m[5] * object_to_world.m[2]);
        orientation = determinant < 0.0f ? -1.0f : 1.0f;
    }
    const float handedness =
        InterpolateHandedness(scene.geometry, tri, bary_u, bary_v) * orientation;
    const Vec3f bitangent = Cross(shading_normal, tangent) * handedness;
    const Vec3f mapped = Normalize(
        tangent * tangent_space.x +
        bitangent * tangent_space.y +
        shading_normal * tangent_space.z
    );
    return LengthSquared(mapped) > 0.0f ? mapped : shading_normal;
}

} // namespace

ShadingMaterial ResolveShadingMaterial(
    ShadingSceneView scene,
    const RenderMaterial& material
) {
    ShadingMaterial resolved{material};
    if (material.measured_index >= 0 &&
        static_cast<std::size_t>(material.measured_index) < scene.measured_brdfs.size()) {
        resolved.measured_brdf = scene.measured_brdfs[material.measured_index].get();
    }
    if (material.bssrdf_index >= 0 &&
        static_cast<std::size_t>(material.bssrdf_index) < scene.bssrdf_tables.size()) {
        resolved.bssrdf_table = scene.bssrdf_tables[material.bssrdf_index].get();
    }
    return resolved;
}

ResolvedMaterial EvaluateMaterialAtSurface(
    ShadingSceneView scene,
    TriangleRef tri,
    const RenderMaterial& base_material,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal,
    Vec3f wo,
    Mat4f object_to_world,
    bool transform_shading_frame,
    TextureFootprint texture_footprint
) {
    const RenderPrimitive* primitive = scene.geometry.Find(tri.primitive);
    if (primitive == nullptr) {
        return ResolvedMaterial{};
    }
    const RenderPrimitive& prim = *primitive;

    ResolvedMaterial sample;
    sample.material = ResolveShadingMaterial(scene, base_material);
    sample.uv = prim.has_uvs
        ? InterpolateUv(scene.geometry, tri, bary_u, bary_v)
        : Vec2f{};
    sample.alpha = base_material.alpha.value;

    if (prim.has_uvs && TextureIndexValid(scene, sample.material.reflectance.texture)) {
        const Color3f tex_color = SampleTexture(
            scene.textures[static_cast<std::size_t>(sample.material.reflectance.texture)],
            sample.uv, texture_footprint);
        sample.material.reflectance.value = Multiply(sample.material.reflectance.value, tex_color);
    }

    if (prim.has_uvs && TextureIndexValid(scene, sample.material.alpha.texture)) {
        const Color4f alpha_tex = SampleTexture4(
            scene.textures[static_cast<std::size_t>(sample.material.alpha.texture)],
            sample.uv, texture_footprint);
        sample.alpha *= alpha_tex.x;
    }

    if (transform_shading_frame) {
        const Vec3f object_geometric_normal = GeometricNormal(scene.geometry, tri);
        sample.shading_normal = TransformNormal(
            object_to_world,
            ResolveShadingNormal(
                scene.geometry, tri, bary_u, bary_v, object_geometric_normal));
        if (Dot(sample.shading_normal, geometric_normal) < 0.0f) {
            sample.shading_normal = -sample.shading_normal;
        }
    } else {
        sample.shading_normal = ResolveShadingNormal(
            scene.geometry, tri, bary_u, bary_v, geometric_normal);
    }
    if (prim.has_uvs) {
        sample.shading_normal = ResolveNormalMap(
            scene,
            tri,
            bary_u,
            bary_v,
            sample.material,
            sample.uv,
            sample.shading_normal,
            object_to_world,
            transform_shading_frame,
            texture_footprint
        );
    }
    sample.shading_normal = FaceForward(sample.shading_normal, wo);
    return sample;
}

bool IsAlphaVisible(const ResolvedMaterial& sample) {
    return sample.alpha >= 0.5f;
}

} // namespace yr
