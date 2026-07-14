#pragma once

#include <optional>
#include <memory>
#include <string>

#include <yaoray/runtime/backend.hpp>
#include <yaoray/accel/acceleration.hpp>

namespace yr {

class CpuWorkerPool;

struct CpuPreparedScene final : public PreparedScene {
    CpuPreparedScene(RenderJob job, RenderAcceleration prepared_acceleration);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSettings& Settings() const override;
    const RenderSceneIR& Scene() const;

    RenderJob render_job;
    RenderAcceleration acceleration;
    std::shared_ptr<CpuWorkerPool> worker_pool;
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
    double elapsed_seconds = 0.0;
    double bvh_build_seconds = 0.0;
};

CpuPrepareResult PrepareCpuScene(RenderJob job);

} // namespace yr
