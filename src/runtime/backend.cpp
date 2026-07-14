#include <yaoray/runtime/backend.hpp>

#include <yaoray/backends/cpu/cpu_backend.hpp>
#include <yaoray/runtime/cuda_stub_backend.hpp>

#include <memory>

namespace yr {

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::Cpu:
            return std::make_unique<CpuBackend>();
        case RenderBackendKind::Cuda:
            return std::make_unique<CudaBackend>();
    }
    return nullptr;
}

} // namespace yr
