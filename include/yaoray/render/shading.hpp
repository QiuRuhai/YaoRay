#pragma once

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>
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

struct SphereHit {
    bool hit = false;
    float t = 0.0f;
};

SphereHit IntersectSphere(Point3f center, float radius, const Ray3f& ray, float t_min, float t_max);
Bounds3f SphereBounds(Point3f center, float radius);
Vec3f SphereNormal(Point3f center, float radius, Point3f surface_point);
Vec2f SphereUv(Vec3f outward_normal);

} // namespace yr
