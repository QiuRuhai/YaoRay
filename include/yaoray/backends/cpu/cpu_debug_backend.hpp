#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CpuDebugBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    RenderResult Render(const RenderSceneIR& scene, const RenderRequest& request) override;
};

} // namespace yr
