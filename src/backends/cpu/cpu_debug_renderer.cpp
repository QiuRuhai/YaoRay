#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <yaoray/backends/cpu/cpu_surface.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/render/shading.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace yr {
namespace {

Ray3f MakeCameraRay(const RenderSceneIR& scene, int x, int y) {
    const float width = static_cast<float>(scene.width);
    const float height = static_cast<float>(scene.height);
    const float aspect = width / height;
    const float half_height = std::tan(scene.camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * (static_cast<float>(x) + 0.5f) / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / height) * half_height;
    const Vec3f direction = Normalize(
        scene.camera.forward +
        scene.camera.right * screen_x +
        scene.camera.up * screen_y
    );
    return Ray3f{scene.camera.origin, direction};
}

Color3f EnvironmentColor(const RenderSceneIR& scene) {
    if (scene.environment.active) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

float MaxAbsComponent(Point3f point) {
    return std::max(std::fabs(point.x), std::max(std::fabs(point.y), std::fabs(point.z)));
}

float ShadowBias(Point3f origin, Point3f target, float distance) {
    const float coordinate_scale = std::max(MaxAbsComponent(origin), MaxAbsComponent(target));
    const float scaled_bias = coordinate_scale * ShadowBiasScale;
    const float capped_bias = distance * MaxShadowBiasDistanceFraction;
    return std::min(std::max(MinShadowBias, scaled_bias), capped_bias);
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(CpuDebugRenderStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
}

Color3f ShadeHit(
    const CpuPreparedScene& prepared_scene,
    const Ray3f& ray,
    const CpuSurfaceHit& hit,
    CpuDebugRenderStats& stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    if (hit.geometry_hit.triangle_index < 0 ||
        !IsValidMaterialIndex(scene, scene.primitives[hit.geometry_hit.primitive_index].material_index)) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderMaterial& material = hit.sample.material;
    const Point3f hit_point = ray.origin + ray.direction * hit.geometry_hit.t;
    const TriangleRef tri_ref = LocateTriangle(scene, hit.geometry_hit.triangle_index);
    const Vec3f raw_normal = GeometricNormal(scene, tri_ref);
    const Vec3f normal = FaceForward(raw_normal, -ray.direction);

    Color3f radiance = material.emission;
    for (const EmissivePrimitive& emissive : scene.emissive_primitives) {
        const RenderPrimitive& light_prim = scene.primitives[emissive.primitive_index];
        if (light_prim.index_count < 3) continue;
        const std::uint32_t i0 = scene.indices[light_prim.first_index];
        const std::uint32_t i1 = scene.indices[light_prim.first_index + 1];
        const std::uint32_t i2 = scene.indices[light_prim.first_index + 2];
        const Point3f light_pos = (scene.vertices[i0].position + scene.vertices[i1].position + scene.vertices[i2].position) * (1.0f / 3.0f);

        const Vec3f to_light = light_pos - hit_point;
        const float distance_squared = LengthSquared(to_light);
        if (distance_squared <= MinShadowBias * MinShadowBias) continue;

        const float distance = std::sqrt(distance_squared);
        const float shadow_bias = ShadowBias(hit_point, light_pos, distance);
        if (distance <= shadow_bias) continue;

        const Point3f shadow_origin = hit_point + normal * shadow_bias;
        const Vec3f shadow_to_light = light_pos - shadow_origin;
        const float shadow_distance_squared = LengthSquared(shadow_to_light);
        if (shadow_distance_squared <= MinShadowBias * MinShadowBias) continue;

        const float shadow_distance = std::sqrt(shadow_distance_squared);
        const Vec3f wi = shadow_to_light / shadow_distance;
        const float n_dot_l = std::max(0.0f, Dot(normal, wi));
        if (n_dot_l <= 0.0f) continue;

        ++stats.shadow_rays;
        BvhTraceStats shadow_trace;
        const Ray3f shadow_ray{shadow_origin, wi};
        const CpuSurfaceHit shadow_hit = TraceVisibleSurface(
            prepared_scene,
            shadow_ray,
            1.0e-5f,
            shadow_distance - shadow_bias,
            &shadow_trace
        );
        AccumulateTraceStats(stats, shadow_trace);
        if (shadow_hit.hit || shadow_hit.exhausted) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float scale = emissive.area * n_dot_l / distance_squared;
        radiance = radiance + Multiply(material.reflectance.value, emissive.radiance) * scale;
    }

    return radiance;
}

} // namespace

CpuDebugRenderResult RenderCpuDebug(const CpuPreparedScene& prepared_scene) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    CpuDebugRenderResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(prepared_scene.bvh.nodes.size());
    result.stats.bvh_max_depth = prepared_scene.bvh.max_depth;
    const auto start = std::chrono::steady_clock::now();

    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            const Ray3f ray = MakeCameraRay(scene, x, y);
            ++result.stats.rays_traced;

            BvhTraceStats trace_stats;
            const CpuSurfaceHit hit = TraceVisibleSurface(
                prepared_scene,
                ray,
                1.0e-5f,
                std::numeric_limits<float>::infinity(),
                &trace_stats
            );
            AccumulateTraceStats(result.stats, trace_stats);
            if (hit.hit && hit.geometry_hit.triangle_index >= 0) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(prepared_scene, ray, hit, result.stats));
            } else {
                ++result.stats.misses;
                result.film.AddSample(x, y, EnvironmentColor(scene));
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
