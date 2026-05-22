#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaPreparedScene final : public PreparedScene {
public:
    explicit CudaPreparedScene(const RenderSceneIR& scene);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;

private:
    const RenderSceneIR* render_scene_ = nullptr;
};

} // namespace yr
