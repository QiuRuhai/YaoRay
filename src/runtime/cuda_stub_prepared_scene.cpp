#include <yaoray/runtime/cuda_stub_prepared_scene.hpp>

#include <utility>

namespace yr {

CudaPreparedScene::CudaPreparedScene(RenderJob job)
    : render_job_(std::move(job)) {
}

RenderBackendKind CudaPreparedScene::Kind() const {
    return RenderBackendKind::Cuda;
}

const RenderSceneIR& CudaPreparedScene::SourceScene() const {
    return render_job_.scene;
}

const RenderSettings& CudaPreparedScene::Settings() const {
    return render_job_.settings;
}

} // namespace yr
