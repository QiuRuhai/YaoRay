#include <yaoray/backends/backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <memory>

namespace yr {
namespace {

class CudaNotImplementedBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override {
        return RenderBackendKind::Cuda;
    }

    RenderResult Render(const RenderScene& scene, const RenderRequest& request) override {
        (void)scene;
        (void)request;

        RenderResult result;
        result.ok = false;
        result.error = "CUDA backend not implemented yet.";
        return result;
    }
};

} // namespace

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::Cpu:
            return std::make_unique<CpuDebugBackend>();
        case RenderBackendKind::Cuda:
            return std::make_unique<CudaNotImplementedBackend>();
    }
    return nullptr;
}

} // namespace yr
