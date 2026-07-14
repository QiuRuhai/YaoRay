#pragma once

#include <yaoray/runtime/backend.hpp>

namespace yr {

class CpuBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    RenderBackendCapabilities Capabilities() const override;
    BackendPrepareResult Prepare(RenderJob job) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
