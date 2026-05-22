#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include <memory>
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
    result.threads = 1;
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
    result.threads = stats.threads;
    result.elapsed_seconds = stats.elapsed_seconds;
    return result;
}

BackendPrepareResult ToBackendPrepareResult(CpuPrepareResult prepared) {
    BackendPrepareResult result;
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    CpuPreparedScene& cpu_scene = prepared.scene.value();
    result.ok = true;
    result.scene = std::make_unique<CpuPreparedScene>(cpu_scene.Scene(), std::move(cpu_scene.bvh));
    return result;
}

} // namespace

RenderBackendKind CpuDebugBackend::Kind() const {
    return RenderBackendKind::Cpu;
}

BackendPrepareResult CpuDebugBackend::Prepare(const RenderSceneIR& scene) {
    return ToBackendPrepareResult(PrepareCpuScene(scene));
}

RenderResult CpuDebugBackend::Render(const PreparedScene& scene, const RenderRequest& request) {
    (void)request;

    RenderResult result;
    const auto* cpu_scene = dynamic_cast<const CpuPreparedScene*>(&scene);
    if (scene.Kind() != RenderBackendKind::Cpu || cpu_scene == nullptr) {
        result.ok = false;
        result.error = "CPU backend received a non-CPU prepared scene.";
        return result;
    }

    const RenderSceneIR& render_scene = cpu_scene->Scene();
    result.ok = true;
    if (render_scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(*cpu_scene);
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(*cpu_scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}

} // namespace yr
