#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct ResolvedMaterialSample {
    RenderMaterial material;
    Vec3f shading_normal{0.0f, 0.0f, 1.0f};
    Vec2f uv;
    float alpha = 1.0f;
};

ResolvedMaterialSample ResolveCpuMaterialSample(
    const RenderSceneIR& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& base_material,
    Vec3f barycentric,
    Vec3f geometric_normal,
    Vec3f wo);

bool IsAlphaVisible(const ResolvedMaterialSample& sample);

} // namespace yr
