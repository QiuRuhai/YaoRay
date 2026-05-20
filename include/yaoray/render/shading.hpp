#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

Vec3f BarycentricCoordinates(Point3f point, const RenderTriangle& triangle);
Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric);
Vec3f ResolveShadingNormal(const RenderTriangle& triangle, Vec3f barycentric, Vec3f geometric_normal);

} // namespace yr
