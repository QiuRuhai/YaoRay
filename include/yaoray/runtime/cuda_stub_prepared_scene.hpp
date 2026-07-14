#pragma once

#include <yaoray/runtime/backend.hpp>

namespace yr {

class CudaPreparedScene final : public PreparedScene {
public:
    explicit CudaPreparedScene(RenderJob job);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSettings& Settings() const override;

private:
    RenderJob render_job_;
};

} // namespace yr
