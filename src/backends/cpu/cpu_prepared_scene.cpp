#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include <chrono>
#include <utility>

namespace yr {

CpuPreparedScene::CpuPreparedScene(RenderSceneIR scene, RenderBvh prepared_bvh)
    : render_scene(std::move(scene)),
      bvh(std::move(prepared_bvh)) {
}

RenderBackendKind CpuPreparedScene::Kind() const {
    return RenderBackendKind::Cpu;
}

const RenderSceneIR& CpuPreparedScene::SourceScene() const {
    return render_scene;
}

const RenderSceneIR& CpuPreparedScene::Scene() const {
    return SourceScene();
}

CpuPrepareResult PrepareCpuScene(RenderSceneIR scene) {
    CpuPrepareResult result;

    const auto start = std::chrono::steady_clock::now();
    BvhBuildResult build = BuildBvh(scene);
    const auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(std::move(scene), std::move(build.bvh));
    return result;
}

} // namespace yr
