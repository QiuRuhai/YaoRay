#include <yaoray/backends/cuda/cuda_prepared_scene.hpp>

#include <utility>

namespace yr {

CudaPreparedScene::CudaPreparedScene(RenderSceneIR scene)
    : render_scene_(std::move(scene)) {
}

RenderBackendKind CudaPreparedScene::Kind() const {
    return RenderBackendKind::Cuda;
}

const RenderSceneIR& CudaPreparedScene::SourceScene() const {
    return render_scene_;
}

} // namespace yr
