#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CpuDebugBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    BackendPrepareResult Prepare(RenderSceneIR scene) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
