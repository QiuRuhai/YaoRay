#pragma once

#include <optional>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/render/bvh.hpp>

namespace yr {

struct CpuPreparedScene final : public PreparedScene {
    CpuPreparedScene(RenderSceneIR scene, RenderBvh prepared_bvh);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSceneIR& Scene() const;

    RenderSceneIR render_scene;
    RenderBvh bvh;
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
    double elapsed_seconds = 0.0;
};

CpuPrepareResult PrepareCpuScene(RenderSceneIR scene);

} // namespace yr
