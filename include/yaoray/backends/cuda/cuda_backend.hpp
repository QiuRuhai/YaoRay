#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    RenderBackendCapabilities Capabilities() const override;
    BackendPrepareResult Prepare(RenderSceneIR scene) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
