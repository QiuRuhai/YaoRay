#include <yaoray/render/scene_compiler.hpp>

namespace yr {

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderScene compiled;
    compiled.backend = scene.render.backend;
    compiled.width = scene.render.width;
    compiled.height = scene.render.height;
    compiled.spp = scene.render.spp;
    compiled.max_depth = scene.render.max_depth;
    compiled.seed = scene.render.seed;
    result.scene = compiled;
    return result;
}

} // namespace yr
