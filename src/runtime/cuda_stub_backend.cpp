#include <yaoray/runtime/cuda_stub_backend.hpp>

namespace yr {

RenderBackendKind CudaBackend::Kind() const {
    return RenderBackendKind::Cuda;
}

RenderBackendCapabilities CudaBackend::Capabilities() const {
    return RenderBackendCapabilities{
        RenderBackendKind::Cuda,
        false,
        false,
        false,
        false,
        false
    };
}

BackendPrepareResult CudaBackend::Prepare(RenderJob job) {
    (void)job;

    BackendPrepareResult result;
    result.ok = false;
    result.error = "CUDA backend preparation is not implemented yet.";
    return result;
}

RenderResult CudaBackend::Render(const PreparedScene& scene, const RenderRequest& request) {
    (void)scene;
    (void)request;

    RenderResult result;
    result.ok = false;
    result.error = "CUDA backend rendering is not implemented yet.";
    return result;
}

} // namespace yr
