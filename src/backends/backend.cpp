#include <yaoray/backends/backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_backend.hpp>
#include <yaoray/backends/cuda/cuda_backend.hpp>

#include <memory>

namespace yr {

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::Cpu:
            return std::make_unique<CpuDebugBackend>();
        case RenderBackendKind::Cuda:
            return std::make_unique<CudaBackend>();
    }
    return nullptr;
}

} // namespace yr
