#include <yaoray/render/shading.hpp>

#include <algorithm>
#include <cmath>

namespace yr {

TriangleRef LocateTriangle(const RenderSceneIR& scene, int flat_triangle_index) {
    int flat = 0;
    for (int pi = 0; pi < static_cast<int>(scene.primitives.size()); ++pi) {
        int tri_count = static_cast<int>(scene.primitives[pi].index_count / 3);
        if (flat_triangle_index < flat + tri_count) {
            return TriangleRef{pi, flat_triangle_index - flat};
        }
        flat += tri_count;
    }
    return TriangleRef{};
}

Vec2f InterpolateUv(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v) {
    const auto& prim = scene.primitives[tri.primitive_index];
    if (!prim.has_uvs) return Vec2f{};
    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri.local_triangle) * 3;
    const auto& v0 = scene.vertices[scene.indices[base + 0]];
    const auto& v1 = scene.vertices[scene.indices[base + 1]];
    const auto& v2 = scene.vertices[scene.indices[base + 2]];
    float w = 1.0f - bary_u - bary_v;
    return Vec2f{
        v0.uv.x * w + v1.uv.x * bary_u + v2.uv.x * bary_v,
        v0.uv.y * w + v1.uv.y * bary_u + v2.uv.y * bary_v
    };
}

Vec3f InterpolateNormal(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v) {
    const auto& prim = scene.primitives[tri.primitive_index];
    if (!prim.has_normals) return Vec3f{};
    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri.local_triangle) * 3;
    const auto& v0 = scene.vertices[scene.indices[base + 0]];
    const auto& v1 = scene.vertices[scene.indices[base + 1]];
    const auto& v2 = scene.vertices[scene.indices[base + 2]];
    float w = 1.0f - bary_u - bary_v;
    return Normalize(Vec3f{
        v0.normal.x * w + v1.normal.x * bary_u + v2.normal.x * bary_v,
        v0.normal.y * w + v1.normal.y * bary_u + v2.normal.y * bary_v,
        v0.normal.z * w + v1.normal.z * bary_u + v2.normal.z * bary_v
    });
}

Vec3f InterpolateTangent(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v) {
    const auto& prim = scene.primitives[tri.primitive_index];
    if (!prim.has_tangents) return Vec3f{};
    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri.local_triangle) * 3;
    const auto& v0 = scene.vertices[scene.indices[base + 0]];
    const auto& v1 = scene.vertices[scene.indices[base + 1]];
    const auto& v2 = scene.vertices[scene.indices[base + 2]];
    float w = 1.0f - bary_u - bary_v;
    return Normalize(Vec3f{
        v0.tangent.x * w + v1.tangent.x * bary_u + v2.tangent.x * bary_v,
        v0.tangent.y * w + v1.tangent.y * bary_u + v2.tangent.y * bary_v,
        v0.tangent.z * w + v1.tangent.z * bary_u + v2.tangent.z * bary_v
    });
}

float InterpolateHandedness(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v) {
    const auto& prim = scene.primitives[tri.primitive_index];
    if (!prim.has_tangents) return 1.0f;
    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri.local_triangle) * 3;
    const auto& v0 = scene.vertices[scene.indices[base + 0]];
    const auto& v1 = scene.vertices[scene.indices[base + 1]];
    const auto& v2 = scene.vertices[scene.indices[base + 2]];
    float w = 1.0f - bary_u - bary_v;
    return v0.tangent_handedness * w + v1.tangent_handedness * bary_u + v2.tangent_handedness * bary_v;
}

Vec3f GeometricNormal(const RenderSceneIR& scene, TriangleRef tri) {
    const auto& prim = scene.primitives[tri.primitive_index];
    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(tri.local_triangle) * 3;
    Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
    Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
    Point3f p2 = scene.vertices[scene.indices[base + 2]].position;
    return Normalize(Cross(p1 - p0, p2 - p0));
}

Vec3f ResolveShadingNormal(
    const RenderSceneIR& scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal
) {
    Vec3f normal = Normalize(geometric_normal);
    const auto& prim = scene.primitives[tri.primitive_index];
    if (prim.has_normals) {
        const Vec3f interpolated = InterpolateNormal(scene, tri, bary_u, bary_v);
        if (LengthSquared(interpolated) > 0.0f) {
            normal = interpolated;
        }
    }
    if (Dot(normal, geometric_normal) < 0.0f) {
        normal = -normal;
    }
    if (LengthSquared(normal) == 0.0f) {
        return Normalize(geometric_normal);
    }
    return normal;
}

SphereHit IntersectSphere(Point3f center, float radius, const Ray3f& ray, float t_min, float t_max) {
    const Vec3f oc{ray.origin.x - center.x, ray.origin.y - center.y, ray.origin.z - center.z};
    const float a = Dot(ray.direction, ray.direction);
    const float b = 2.0f * Dot(oc, ray.direction);
    const float c = Dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return SphereHit{false, 0.0f};
    }
    const float sqrt_disc = std::sqrt(disc);
    const float inv_2a = 0.5f / a;
    const float t0 = (-b - sqrt_disc) * inv_2a;
    if (t0 > t_min && t0 < t_max) {
        return SphereHit{true, t0};
    }
    const float t1 = (-b + sqrt_disc) * inv_2a;
    if (t1 > t_min && t1 < t_max) {
        return SphereHit{true, t1};
    }
    return SphereHit{false, 0.0f};
}

Bounds3f SphereBounds(Point3f center, float radius) {
    const Point3f lo{center.x - radius, center.y - radius, center.z - radius};
    const Point3f hi{center.x + radius, center.y + radius, center.z + radius};
    return Bounds3f{lo, hi};
}

Vec3f SphereNormal(Point3f center, float radius, Point3f surface_point) {
    const Vec3f d{surface_point.x - center.x, surface_point.y - center.y, surface_point.z - center.z};
    const float inv_r = radius > 0.0f ? 1.0f / radius : 0.0f;
    return Vec3f{d.x * inv_r, d.y * inv_r, d.z * inv_r};
}

Vec2f SphereUv(Vec3f outward_normal) {
    constexpr float Pi = 3.14159265358979323846f;
    const float clamped_y = std::clamp(outward_normal.y, -1.0f, 1.0f);
    const float u = (std::atan2(outward_normal.x, outward_normal.z) + Pi) / (2.0f * Pi);
    const float v = (Pi - std::acos(clamped_y)) / Pi;
    return Vec2f{u, v};
}

} // namespace yr
