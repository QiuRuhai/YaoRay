#pragma once

#include <optional>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/render/bvh.hpp>

namespace yr {

struct CpuPreparedScene final : public PreparedScene {
    CpuPreparedScene() = default;
    CpuPreparedScene(const RenderSceneIR& scene, RenderBvh prepared_bvh);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSceneIR& Scene() const;

    const RenderSceneIR* render_scene = nullptr;
    RenderBvh bvh;
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
};

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene);

} // namespace yr
