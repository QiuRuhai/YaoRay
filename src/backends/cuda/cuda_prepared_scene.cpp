#include <yaoray/backends/cuda/cuda_prepared_scene.hpp>

namespace yr {

CudaPreparedScene::CudaPreparedScene(const RenderSceneIR& scene)
    : render_scene_(&scene) {
}

RenderBackendKind CudaPreparedScene::Kind() const {
    return RenderBackendKind::Cuda;
}

const RenderSceneIR& CudaPreparedScene::SourceScene() const {
    return *render_scene_;
}

} // namespace yr
