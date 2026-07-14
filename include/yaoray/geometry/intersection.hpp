#pragma once

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/geometry.hpp>

namespace yr {

struct TriangleRef {
    MeshPrimitiveHandle primitive;
    int local_triangle = -1;
};

TriangleRef LocateTriangle(GeometryView geometry, int flat_triangle_index);

Vec2f InterpolateUv(GeometryView geometry, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateNormal(GeometryView geometry, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateTangent(GeometryView geometry, TriangleRef tri, float bary_u, float bary_v);
float InterpolateHandedness(GeometryView geometry, TriangleRef tri, float bary_u, float bary_v);
Vec3f GeometricNormal(GeometryView geometry, TriangleRef tri);

Vec3f ResolveShadingNormal(
    GeometryView geometry,
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
