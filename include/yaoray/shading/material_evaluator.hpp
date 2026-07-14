#pragma once

#include <yaoray/core/transform.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/geometry/intersection.hpp>
#include <yaoray/shading/material.hpp>
#include <yaoray/shading/scene_view.hpp>
#include <yaoray/shading/shading_material.hpp>
#include <yaoray/shading/texture.hpp>

namespace yr {

struct ResolvedMaterial {
    ShadingMaterial material;
    Vec3f shading_normal{0.0f, 0.0f, 1.0f};
    Vec2f uv;
    float alpha = 1.0f;
};

ShadingMaterial ResolveShadingMaterial(
    ShadingSceneView scene,
    const RenderMaterial& material
);

ResolvedMaterial EvaluateMaterialAtSurface(
    ShadingSceneView scene,
    TriangleRef tri,
    const RenderMaterial& base_material,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal,
    Vec3f wo,
    Mat4f object_to_world = {},
    bool transform_shading_frame = false,
    TextureFootprint texture_footprint = {});

bool IsAlphaVisible(const ResolvedMaterial& sample);

} // namespace yr
