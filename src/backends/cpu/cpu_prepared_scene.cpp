#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include <utility>

namespace yr {

CpuPreparedScene::CpuPreparedScene(const RenderSceneIR& scene, RenderBvh prepared_bvh)
    : render_scene(&scene),
      bvh(std::move(prepared_bvh)) {
}

RenderBackendKind CpuPreparedScene::Kind() const {
    return RenderBackendKind::Cpu;
}

const RenderSceneIR& CpuPreparedScene::SourceScene() const {
    return *render_scene;
}

const RenderSceneIR& CpuPreparedScene::Scene() const {
    return SourceScene();
}

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene) {
    CpuPrepareResult result;

    BvhBuildResult build = BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(scene, std::move(build.bvh));
    return result;
}

} // namespace yr
