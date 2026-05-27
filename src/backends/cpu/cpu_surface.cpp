#include <yaoray/backends/cpu/cpu_surface.hpp>

#include <yaoray/render/shading.hpp>

#include <cstddef>

namespace yr {
namespace {

constexpr int MaxAlphaSkippedHits = 64;
constexpr float AlphaSkipTOffset = 1.0e-4f;

bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

} // namespace

CpuSurfaceHit TraceVisibleSurface(
    const CpuPreparedScene& prepared_scene,
    const Ray3f& ray,
    float t_min,
    float t_max,
    BvhTraceStats* stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    BvhTraceStats local_stats;
    BvhTraceStats& trace_stats = stats == nullptr ? local_stats : *stats;
    float current_t_min = t_min;

    for (int skipped_hits = 0; skipped_hits < MaxAlphaSkippedHits; ++skipped_hits) {
        const BvhHit geometry_hit = IntersectBvh(scene, prepared_scene.bvh, ray, trace_stats, current_t_min, t_max);
        if (!geometry_hit.hit) {
            return CpuSurfaceHit{};
        }

        CpuSurfaceHit surface_hit;
        surface_hit.hit = true;
        surface_hit.geometry_hit = geometry_hit;

        // --- Sphere hit path ---
        if (geometry_hit.sphere_index >= 0 &&
            static_cast<std::size_t>(geometry_hit.sphere_index) < scene.spheres.size()) {
            const RenderSphere& sphere = scene.spheres[static_cast<std::size_t>(geometry_hit.sphere_index)];
            const Point3f hit_point = ray.At(geometry_hit.t);
            const Vec3f geometric_normal = SphereNormal(sphere.center, sphere.radius, hit_point);
            const Vec3f shading_normal = sphere.flip_normals
                ? Vec3f{-geometric_normal.x, -geometric_normal.y, -geometric_normal.z}
                : geometric_normal;

            if (!IsValidMaterialIndex(scene, sphere.material_index)) {
                return surface_hit;
            }
            surface_hit.sample.material = scene.materials[static_cast<std::size_t>(sphere.material_index)];
            surface_hit.sample.shading_normal = shading_normal;
            surface_hit.sample.alpha = 1.0f;  // Spheres are opaque in M1.
            return surface_hit;
        }

        // --- Triangle hit path ---
        if (geometry_hit.triangle_index < 0) {
            return CpuSurfaceHit{};
        }

        if (!IsValidMaterialIndex(scene, scene.primitives[geometry_hit.primitive_index].material_index)) {
            return surface_hit;
        }

        const RenderMaterial& base_material = scene.materials[static_cast<std::size_t>(scene.primitives[geometry_hit.primitive_index].material_index)];
        const TriangleRef tri_ref = LocateTriangle(scene, geometry_hit.triangle_index);
        const Vec3f raw_geometric = GeometricNormal(scene, tri_ref);
        const Vec3f geometric_normal = Dot(raw_geometric, -ray.direction) < 0.0f ? -raw_geometric : raw_geometric;
        surface_hit.sample = ResolveCpuMaterialSample(
            scene,
            tri_ref,
            base_material,
            geometry_hit.bary_u,
            geometry_hit.bary_v,
            geometric_normal,
            -ray.direction
        );

        if (IsAlphaVisible(surface_hit.sample)) {
            return surface_hit;
        }

        current_t_min = geometry_hit.t + AlphaSkipTOffset;
        if (current_t_min >= t_max) {
            return CpuSurfaceHit{};
        }
    }

    CpuSurfaceHit exhausted;
    exhausted.exhausted = true;
    return exhausted;
}

} // namespace yr
