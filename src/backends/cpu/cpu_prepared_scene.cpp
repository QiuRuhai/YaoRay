#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include "cpu_worker_pool.hpp"

#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>
#include <yaoray/shading/texture.hpp>
#include <yaoray/lighting/light_sampling.hpp>

#include <chrono>
#include <utility>

namespace yr {

CpuPreparedScene::CpuPreparedScene(RenderJob job, RenderAcceleration prepared_acceleration)
    : render_job(std::move(job)),
      acceleration(std::move(prepared_acceleration)),
      worker_pool(std::make_shared<CpuWorkerPool>(
          BuildCpuTileSchedule(
              render_job.settings.width,
              render_job.settings.height,
              render_job.settings.threads).worker_count)) {
}

RenderBackendKind CpuPreparedScene::Kind() const {
    return RenderBackendKind::Cpu;
}

const RenderSceneIR& CpuPreparedScene::SourceScene() const {
    return render_job.scene;
}

const RenderSettings& CpuPreparedScene::Settings() const {
    return render_job.settings;
}

const RenderSceneIR& CpuPreparedScene::Scene() const {
    return SourceScene();
}

CpuPrepareResult PrepareCpuScene(RenderJob job) {
    CpuPrepareResult result;

    const auto start = std::chrono::steady_clock::now();
    BuildTextureSamplingCaches(job.scene.textures);
    PrepareLightSampling(job.scene);
    const auto bvh_start = std::chrono::steady_clock::now();
    RenderAccelerationBuildResult build = BuildRenderAcceleration(job.scene.Geometry());
    const auto bvh_end = std::chrono::steady_clock::now();
    result.bvh_build_seconds =
        std::chrono::duration<double>(bvh_end - bvh_start).count();
    const auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(std::move(job), std::move(build.acceleration));
    return result;
}

} // namespace yr
