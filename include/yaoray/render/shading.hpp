#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct TriangleRef {
    int primitive_index = -1;
    int local_triangle = -1;
};

TriangleRef LocateTriangle(const RenderSceneIR& scene, int flat_triangle_index);

Vec2f InterpolateUv(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateNormal(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateTangent(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
float InterpolateHandedness(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f GeometricNormal(const RenderSceneIR& scene, TriangleRef tri);

Vec3f ResolveShadingNormal(
    const RenderSceneIR& scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal);

} // namespace yr
