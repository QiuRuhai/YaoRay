#include <yaoray/shading/scene_view.hpp>

#include <yaoray/scene/render_scene.hpp>

namespace yr {

ShadingSceneView MakeShadingSceneView(const RenderSceneIR& scene) {
    return ShadingSceneView{
        scene.Geometry(),
        scene.materials,
        scene.textures,
        scene.measured_brdfs,
        scene.bssrdf_tables
    };
}

} // namespace yr
