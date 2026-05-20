#include <yaoray/render/shading.hpp>

#include <cmath>

namespace yr {

Vec3f BarycentricCoordinates(Point3f point, const RenderTriangle& triangle) {
    const Vec3f v0 = triangle.p1 - triangle.p0;
    const Vec3f v1 = triangle.p2 - triangle.p0;
    const Vec3f v2 = point - triangle.p0;
    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) <= 1.0e-12f) {
        return Vec3f{1.0f, 0.0f, 0.0f};
    }
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return Vec3f{1.0f - v - w, v, w};
}

Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric) {
    return Vec2f{
        triangle.uv0.x * barycentric.x + triangle.uv1.x * barycentric.y + triangle.uv2.x * barycentric.z,
        triangle.uv0.y * barycentric.x + triangle.uv1.y * barycentric.y + triangle.uv2.y * barycentric.z
    };
}

Vec3f ResolveShadingNormal(const RenderTriangle& triangle, Vec3f barycentric, Vec3f geometric_normal) {
    Vec3f normal = Normalize(geometric_normal);
    if (triangle.has_vertex_normals) {
        const Vec3f interpolated = Normalize(
            triangle.n0 * barycentric.x +
            triangle.n1 * barycentric.y +
            triangle.n2 * barycentric.z
        );
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

} // namespace yr
