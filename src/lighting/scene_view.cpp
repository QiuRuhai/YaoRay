#include <yaoray/lighting/scene_view.hpp>

#include <yaoray/scene/render_scene.hpp>

namespace yr {

LightSceneView MakeLightSceneView(const RenderSceneIR& scene) {
    return LightSceneView{
        scene.Geometry(),
        scene.textures,
        scene.emissive_primitives,
        scene.environment,
        scene.environment_distributions,
        scene.analytic_lights,
        scene.light_sampling
    };
}

} // namespace yr
