#include <yaoray/backends/cuda/cuda_backend.hpp>

namespace yr {

RenderBackendKind CudaBackend::Kind() const {
    return RenderBackendKind::Cuda;
}

BackendPrepareResult CudaBackend::Prepare(RenderSceneIR scene) {
    (void)scene;

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
