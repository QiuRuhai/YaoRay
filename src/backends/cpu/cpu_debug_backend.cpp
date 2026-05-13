#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <utility>

namespace yr {
namespace {

RenderStats ToRenderStats(const CpuDebugRenderStats& stats) {
    RenderStats result;
    result.rays_traced = stats.rays_traced;
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

    CpuDebugRenderResult debug_result = RenderCpuDebug(scene);

    RenderResult result;
    result.ok = true;
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}

} // namespace yr
