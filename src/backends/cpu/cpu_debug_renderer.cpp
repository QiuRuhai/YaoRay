#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace yr {
namespace {

Ray3f MakeCameraRay(const RenderScene& scene, int x, int y) {
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

Color3f EnvironmentColor(const RenderScene& scene) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

Color3f ShadeHit(const RenderScene& scene, const Ray3f& ray, const RenderTriangle& triangle) {
    if (triangle.material_index < 0 ||
        static_cast<std::size_t>(triangle.material_index) >= scene.materials.size()) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const float normal_lighting = std::max(0.15f, std::fabs(Dot(triangle.normal, -ray.direction)));
    return material.albedo * normal_lighting + material.emission;
}

} // namespace

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene) {
    CpuDebugRenderResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;
    const auto start = std::chrono::steady_clock::now();

    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            const Ray3f ray = MakeCameraRay(scene, x, y);
            ++result.stats.rays_traced;

            BvhTraceStats trace_stats;
            const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
            result.stats.bvh_node_tests += trace_stats.node_tests;
            result.stats.triangle_tests += trace_stats.triangle_tests;
            if (hit.hit && hit.triangle != nullptr) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(scene, ray, *hit.triangle));
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
