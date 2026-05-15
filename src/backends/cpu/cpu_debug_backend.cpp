#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include <utility>

namespace yr {
namespace {

RenderStats ToRenderStats(const CpuDebugRenderStats& stats) {
    RenderStats result;
    result.rays_traced = stats.rays_traced;
    result.shadow_rays = stats.shadow_rays;
    result.occluded_shadow_rays = stats.occluded_shadow_rays;
    result.triangle_tests = stats.triangle_tests;
    result.bvh_node_tests = stats.bvh_node_tests;
    result.bvh_nodes = stats.bvh_nodes;
    result.bvh_max_depth = stats.bvh_max_depth;
    result.hits = stats.hits;
    result.misses = stats.misses;
    result.elapsed_seconds = stats.elapsed_seconds;
    return result;
}

RenderStats ToRenderStats(const CpuPathTraceStats& stats) {
    RenderStats result;
    result.rays_traced = stats.rays_traced;
    result.shadow_rays = stats.shadow_rays;
    result.occluded_shadow_rays = stats.occluded_shadow_rays;
    result.triangle_tests = stats.triangle_tests;
    result.bvh_node_tests = stats.bvh_node_tests;
    result.bvh_nodes = stats.bvh_nodes;
    result.bvh_max_depth = stats.bvh_max_depth;
    result.hits = stats.hits;
    result.misses = stats.misses;
    result.elapsed_seconds = stats.elapsed_seconds;
    return result;
}

} // namespace

RenderBackendKind CpuDebugBackend::Kind() const {
    return RenderBackendKind::Cpu;
}

RenderResult CpuDebugBackend::Render(const RenderScene& scene, const RenderRequest& request) {
    (void)request;

    RenderResult result;
    result.ok = true;

    if (scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(scene);
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}

} // namespace yr
