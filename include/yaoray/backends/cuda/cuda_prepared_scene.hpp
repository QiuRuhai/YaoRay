#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaPreparedScene final : public PreparedScene {
public:
    explicit CudaPreparedScene(RenderSceneIR scene);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;

private:
    RenderSceneIR render_scene_;
};

} // namespace yr
