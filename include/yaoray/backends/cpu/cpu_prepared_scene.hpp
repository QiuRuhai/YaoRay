#pragma once

#include <optional>
#include <string>

#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuPreparedScene {
    const RenderSceneIR* render_scene = nullptr;
    RenderBvh bvh;

    const RenderSceneIR& Scene() const {
        return *render_scene;
    }
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
};

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene);

} // namespace yr
