#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

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
    if (scene.environment.type == EnvironmentKind::Constant) {
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
    const BvhHit& hit,
    CpuDebugRenderStats& stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    if (hit.triangle == nullptr || !IsValidMaterialIndex(scene, hit.triangle->material_index)) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderTriangle& triangle = *hit.triangle;
    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const Point3f hit_point = ray.origin + ray.direction * hit.t;
    const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);

    Color3f radiance = material.emission;
    for (const RenderAreaLight& light : scene.area_lights) {
        const float area = light.width * light.height;
        if (area <= 0.0f) {
            continue;
        }

        const Vec3f to_light = light.position - hit_point;
        const float distance_squared = LengthSquared(to_light);
        if (distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float distance = std::sqrt(distance_squared);
        const float shadow_bias = ShadowBias(hit_point, light.position, distance);
        if (distance <= shadow_bias) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * shadow_bias;
        const Vec3f shadow_to_light = light.position - shadow_origin;
        const float shadow_distance_squared = LengthSquared(shadow_to_light);
        if (shadow_distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float shadow_distance = std::sqrt(shadow_distance_squared);
        const Vec3f wi = shadow_to_light / shadow_distance;
        const float n_dot_l = std::max(0.0f, Dot(normal, wi));
        if (n_dot_l <= 0.0f) {
            continue;
        }

        ++stats.shadow_rays;
        BvhTraceStats shadow_trace;
        const Ray3f shadow_ray{shadow_origin, wi};
        const BvhHit shadow_hit = IntersectBvh(scene, prepared_scene.bvh, shadow_ray, shadow_trace);
        AccumulateTraceStats(stats, shadow_trace);
        if (shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float scale = area * n_dot_l / distance_squared;
        radiance = radiance + Multiply(material.albedo, light.radiance) * scale;
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
            const BvhHit hit = IntersectBvh(scene, prepared_scene.bvh, ray, trace_stats);
            AccumulateTraceStats(result.stats, trace_stats);
            if (hit.hit && hit.triangle != nullptr) {
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
