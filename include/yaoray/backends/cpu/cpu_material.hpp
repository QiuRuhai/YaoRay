#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>

namespace yr {

struct ResolvedMaterialSample {
    RenderMaterial material;
    Vec3f shading_normal{0.0f, 0.0f, 1.0f};
    Vec2f uv;
    float alpha = 1.0f;
};

ResolvedMaterialSample ResolveCpuMaterialSample(
    const RenderSceneIR& scene,
    TriangleRef tri,
    const RenderMaterial& base_material,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal,
    Vec3f wo);

bool IsAlphaVisible(const ResolvedMaterialSample& sample);

} // namespace yr
