#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include <utility>

namespace yr {

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene) {
    CpuPrepareResult result;

    BvhBuildResult build = BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene = CpuPreparedScene{&scene, std::move(build.bvh)};
    return result;
}

} // namespace yr
