#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    BackendPrepareResult Prepare(const RenderSceneIR& scene) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
