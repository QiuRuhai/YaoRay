#include <yaoray/integrators/surface_query.hpp>

#include <yaoray/geometry/intersection.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <cstddef>
#include <cmath>

namespace yr {
namespace {

constexpr int MaxAlphaSkippedHits = 64;
constexpr float AlphaSkipTOffset = 1.0e-4f;

bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

Mat4f HitObjectToWorld(
    const RenderAcceleration& acceleration,
    InstanceHandle instance_handle
) {
    if (acceleration.kind != RenderAccelerationKind::TwoLevel ||
        !instance_handle.IsValid()) {
        return Mat4f{};
    }
    const int index = instance_handle.Value();
    if (index < 0 ||
        static_cast<std::size_t>(index) >= acceleration.two_level.instances.size()) {
        return Mat4f{};
    }
    return acceleration.two_level.instances[static_cast<std::size_t>(index)].object_to_world;
}

TextureFootprint ComputeTextureFootprint(
    GeometryView geometry,
    TriangleRef tri,
    const Ray3f& ray,
    Point3f hit_point,
    Vec3f geometric_normal,
    Mat4f object_to_world,
    bool transform_geometry) {
    if (!ray.has_differentials || !tri.primitive.IsValid()) return {};
    const RenderPrimitive* primitive = geometry.Find(tri.primitive);
    if (primitive == nullptr || !primitive->has_uvs) return {};
    const std::uint32_t base = primitive->first_index +
        static_cast<std::uint32_t>(tri.local_triangle) * 3;
    Point3f p0 = geometry.vertices[geometry.indices[base]].position;
    Point3f p1 = geometry.vertices[geometry.indices[base + 1]].position;
    Point3f p2 = geometry.vertices[geometry.indices[base + 2]].position;
    if (transform_geometry) {
        p0 = TransformPoint(object_to_world, p0);
        p1 = TransformPoint(object_to_world, p1);
        p2 = TransformPoint(object_to_world, p2);
    }
    const Vec2f uv0 = geometry.vertices[geometry.indices[base]].uv;
    const Vec2f uv1 = geometry.vertices[geometry.indices[base + 1]].uv;
    const Vec2f uv2 = geometry.vertices[geometry.indices[base + 2]].uv;
    const float du1 = uv1.x - uv0.x;
    const float dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x;
    const float dv2 = uv2.y - uv0.y;
    const float uv_det = du1 * dv2 - dv1 * du2;
    if (std::fabs(uv_det) < 1.0e-12f) return {};
    const float inverse_uv_det = 1.0f / uv_det;
    const Vec3f dp1 = p1 - p0;
    const Vec3f dp2 = p2 - p0;
    const Vec3f dpdu = (dp1 * dv2 - dp2 * dv1) * inverse_uv_det;
    const Vec3f dpdv = (dp2 * du1 - dp1 * du2) * inverse_uv_det;

    const auto differential_point = [&](Point3f origin, Vec3f direction) {
        const float denominator = Dot(geometric_normal, direction);
        if (std::fabs(denominator) < 1.0e-12f) return hit_point;
        const float t = Dot(geometric_normal, hit_point - origin) / denominator;
        return origin + direction * t;
    };
    const Vec3f dpdx = differential_point(ray.rx_origin, ray.rx_direction) - hit_point;
    const Vec3f dpdy = differential_point(ray.ry_origin, ray.ry_direction) - hit_point;
    const float a = Dot(dpdu, dpdu);
    const float b = Dot(dpdu, dpdv);
    const float c = Dot(dpdv, dpdv);
    const float gram_det = a * c - b * b;
    if (std::fabs(gram_det) < 1.0e-20f) return {};
    const float inverse_gram_det = 1.0f / gram_det;
    const auto solve = [&](Vec3f dp) {
        const float u_rhs = Dot(dp, dpdu);
        const float v_rhs = Dot(dp, dpdv);
        return Vec2f{
            (u_rhs * c - v_rhs * b) * inverse_gram_det,
            (v_rhs * a - u_rhs * b) * inverse_gram_det};
    };
    const Vec2f dx = solve(dpdx);
    const Vec2f dy = solve(dpdy);
    if (!std::isfinite(dx.x) || !std::isfinite(dx.y) ||
        !std::isfinite(dy.x) || !std::isfinite(dy.y)) return {};
    return TextureFootprint{dx.x, dx.y, dy.x, dy.y};
}

} // namespace

SurfaceHit TraceVisibleSurface(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    float t_min,
    float t_max,
    BvhTraceStats* stats
) {
    BvhTraceStats local_stats;
    BvhTraceStats& trace_stats = stats == nullptr ? local_stats : *stats;
    float current_t_min = t_min;

    for (int skipped_hits = 0; skipped_hits < MaxAlphaSkippedHits; ++skipped_hits) {
        const GeometryView geometry = scene.Geometry();
        const BvhHit geometry_hit =
            acceleration.kind == RenderAccelerationKind::FlatReference
            ? IntersectBvh(
                geometry, acceleration.flat, ray, trace_stats, current_t_min, t_max)
            : IntersectTwoLevelBvh(
                geometry, acceleration.two_level, ray, trace_stats, current_t_min, t_max);
        if (!geometry_hit.hit) {
            return SurfaceHit{};
        }

        SurfaceHit surface_hit;
        surface_hit.hit = true;
        surface_hit.geometry_hit = geometry_hit;

        // --- Sphere hit path ---
        if (const RenderSphere* sphere_ptr = geometry.Find(geometry_hit.sphere)) {
            const RenderSphere& sphere = *sphere_ptr;
            const Point3f hit_point = ray.At(geometry_hit.t);
            const Vec3f geometric_normal = SphereNormal(sphere.center, sphere.radius, hit_point);
            const Vec3f shading_normal = sphere.flip_normals
                ? Vec3f{-geometric_normal.x, -geometric_normal.y, -geometric_normal.z}
                : geometric_normal;

            if (!IsValidMaterialIndex(scene, sphere.material_index)) {
                return surface_hit;
            }
            surface_hit.sample.material = ResolveShadingMaterial(
                MakeShadingSceneView(scene),
                scene.materials[static_cast<std::size_t>(sphere.material_index)]
            );
            surface_hit.sample.shading_normal = shading_normal;
            surface_hit.sample.alpha = 1.0f;  // Spheres are opaque in M1.
            return surface_hit;
        }

        // --- Triangle hit path ---
        if (geometry_hit.triangle_index < 0) {
            return SurfaceHit{};
        }

        const RenderPrimitive* primitive = geometry.Find(geometry_hit.mesh_primitive);
        if (primitive == nullptr || !IsValidMaterialIndex(scene, primitive->material_index)) {
            return surface_hit;
        }

        const RenderMaterial& base_material =
            scene.materials[static_cast<std::size_t>(primitive->material_index)];
        const TriangleRef tri_ref = LocateTriangle(geometry, geometry_hit.triangle_index);
        const Mat4f object_to_world = HitObjectToWorld(
            acceleration, geometry_hit.instance);
        const bool is_instance = geometry_hit.instance.IsValid();
        Vec3f raw_geometric = GeometricNormal(geometry, tri_ref);
        if (is_instance) {
            raw_geometric = TransformNormal(object_to_world, raw_geometric);
        }
        const Vec3f geometric_normal = Dot(raw_geometric, -ray.direction) < 0.0f ? -raw_geometric : raw_geometric;
        const TextureFootprint texture_footprint = ComputeTextureFootprint(
            geometry, tri_ref, ray, ray.At(geometry_hit.t), geometric_normal,
            object_to_world, is_instance);
        surface_hit.sample = EvaluateMaterialAtSurface(
            MakeShadingSceneView(scene),
            tri_ref,
            base_material,
            geometry_hit.bary_u,
            geometry_hit.bary_v,
            geometric_normal,
            -ray.direction,
            object_to_world,
            is_instance,
            texture_footprint
        );

        if (IsAlphaVisible(surface_hit.sample)) {
            return surface_hit;
        }

        current_t_min = geometry_hit.t + AlphaSkipTOffset;
        if (current_t_min >= t_max) {
            return SurfaceHit{};
        }
    }

    SurfaceHit exhausted;
    exhausted.exhausted = true;
    return exhausted;
}

} // namespace yr
