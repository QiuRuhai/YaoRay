#include <memory>
#include <string>
#include <utility>
#include <yaoray/backends/cpu/cpu_backend.hpp>
#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

namespace yr {
namespace {

RenderStats ToRenderStats(const CpuRenderStats& stats) {
    RenderStats result;
    result.samples_rendered      = stats.samples_rendered;
    result.converged_pixels      = stats.converged_pixels;
    result.rays_traced          = stats.rays_traced;
    result.shadow_rays          = stats.shadow_rays;
    result.occluded_shadow_rays = stats.occluded_shadow_rays;
    result.triangle_tests       = stats.triangle_tests;
    result.sphere_tests         = stats.sphere_tests;
    result.bvh_node_tests       = stats.bvh_node_tests;
    result.bvh_nodes            = stats.bvh_nodes;
    result.bvh_max_depth        = stats.bvh_max_depth;
    result.hits                 = stats.hits;
    result.misses               = stats.misses;
    result.threads              = stats.threads;
    result.elapsed_seconds      = stats.elapsed_seconds;
    return result;
}

BackendPrepareResult ToBackendPrepareResult(CpuPrepareResult prepared) {
    BackendPrepareResult result;
    result.elapsed_seconds            = prepared.elapsed_seconds;
    result.acceleration_build_seconds = prepared.bvh_build_seconds;
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok    = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    CpuPreparedScene& cpu_scene = prepared.scene.value();
    result.ok                   = true;
    result.scene =
        std::make_unique<CpuPreparedScene>(std::move(cpu_scene.render_job), std::move(cpu_scene.acceleration));
    return result;
}

}  // namespace

RenderBackendKind CpuBackend::Kind() const {
    return RenderBackendKind::Cpu;
}

RenderBackendCapabilities CpuBackend::Capabilities() const {
    return RenderBackendCapabilities{RenderBackendKind::Cpu, true, true, true, true, true};
}

BackendPrepareResult CpuBackend::Prepare(RenderJob job) {
    if (job.settings.requested_backend != RenderBackendKind::Cpu) {
        BackendPrepareResult result;
        result.ok    = false;
        result.error = "CPU backend cannot prepare a scene requested for " +
                       std::string{RenderBackendName(job.settings.requested_backend)};
        return result;
    }

    return ToBackendPrepareResult(PrepareCpuScene(std::move(job)));
}

RenderResult CpuBackend::Render(const PreparedScene& scene, const RenderRequest& request) {
    RenderResult result;
    const auto*  cpu_scene = dynamic_cast<const CpuPreparedScene*>(&scene);
    if (scene.Kind() != RenderBackendKind::Cpu || cpu_scene == nullptr) {
        result.ok    = false;
        result.error = "CPU backend received a non-CPU prepared scene.";
        return result;
    }

    const RenderSceneIR&  render_scene = cpu_scene->Scene();
    const RenderSettings& settings     = cpu_scene->Settings();
    result.ok                          = true;
    if (settings.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(*cpu_scene, request);
        if (!path_result.ok) {
            result.ok    = false;
            result.error = path_result.error.empty() ? "CPU path tracing failed" : path_result.error;
            result.stats = ToRenderStats(path_result.stats);
            if (path_result.film.Width() > 0 && path_result.film.Height() > 0) {
                result.film.emplace(std::move(path_result.film));
            }
            return result;
        }
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(*cpu_scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}

}  // namespace yr
