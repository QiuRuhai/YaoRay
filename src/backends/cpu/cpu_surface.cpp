#include <yaoray/backends/cpu/cpu_surface.hpp>

#include <yaoray/render/shading.hpp>

#include <cmath>
#include <cstddef>

namespace yr {
namespace {

constexpr int MaxAlphaSkippedHits = 64;
constexpr float AlphaSkipTOffset = 1.0e-4f;

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

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
        if (!geometry_hit.hit || geometry_hit.triangle == nullptr) {
            return CpuSurfaceHit{};
        }

        CpuSurfaceHit surface_hit;
        surface_hit.hit = true;
        surface_hit.geometry_hit = geometry_hit;
        surface_hit.barycentric = BarycentricCoordinates(ray.At(geometry_hit.t), *geometry_hit.triangle);
        surface_hit.uv = geometry_hit.triangle->has_uv ? InterpolateUv(*geometry_hit.triangle, surface_hit.barycentric) : Vec2f{};

        if (!IsValidMaterialIndex(scene, geometry_hit.triangle->material_index)) {
            return surface_hit;
        }

        const RenderMaterial& base_material = scene.materials[static_cast<std::size_t>(geometry_hit.triangle->material_index)];
        const Vec3f geometric_normal = FaceForward(Normalize(geometry_hit.triangle->normal), -ray.direction);
        surface_hit.sample = ResolveCpuMaterialSample(
            scene,
            *geometry_hit.triangle,
            base_material,
            surface_hit.barycentric,
            geometric_normal,
            -ray.direction
        );
        surface_hit.uv = surface_hit.sample.uv;

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
