#include <yaoray/render/light_sampling.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float MinPdfDistanceSquared = 1.0e-12f;
constexpr float AreaLightPointTolerance = 1.0e-3f;

float Area(const RenderAreaLight& light) {
    return light.width * light.height;
}

Vec3f AreaLightNormal() {
    return Vec3f{0.0f, -1.0f, 0.0f};
}

bool IsPointOnCurrentAreaLightRectangle(const RenderAreaLight& light, Point3f point) {
    const float half_width = light.width * 0.5f;
    const float half_height = light.height * 0.5f;
    return std::fabs(point.y - light.position.y) <= AreaLightPointTolerance &&
           point.x >= light.position.x - half_width - AreaLightPointTolerance &&
           point.x <= light.position.x + half_width + AreaLightPointTolerance &&
           point.z >= light.position.z - half_height - AreaLightPointTolerance &&
           point.z <= light.position.z + half_height + AreaLightPointTolerance;
}

} // namespace

std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Vec2f uv) {
    const float area = Area(light);
    if (area <= 0.0f) {
        return std::nullopt;
    }

    const float u = std::clamp(uv.x, 0.0f, 1.0f);
    const float v = std::clamp(uv.y, 0.0f, 1.0f);
    const float offset_x = (u - 0.5f) * light.width;
    const float offset_z = (v - 0.5f) * light.height;

    return AreaLightSample{
        light.position + Vec3f{offset_x, 0.0f, offset_z},
        AreaLightNormal(),
        light.radiance,
        area,
        1.0f / area
    };
}

float PdfAreaLightSampleSolidAngle(
    const RenderAreaLight& light,
    Point3f shading_point,
    Point3f light_point
) {
    const float area = Area(light);
    if (area <= 0.0f) {
        return 0.0f;
    }

    const Vec3f to_light = light_point - shading_point;
    const float distance_squared = LengthSquared(to_light);
    if (distance_squared <= MinPdfDistanceSquared) {
        return 0.0f;
    }

    const Vec3f wi = to_light / std::sqrt(distance_squared);
    const float cos_light = std::max(0.0f, Dot(AreaLightNormal(), -wi));
    if (cos_light <= 0.0f) {
        return 0.0f;
    }

    return distance_squared / (cos_light * area);
}

float PdfAreaLightsForPointSolidAngle(
    const RenderSceneIR& scene,
    Point3f shading_point,
    Point3f light_point
) {
    float pdf = 0.0f;
    for (const RenderAreaLight& light : scene.area_lights) {
        if (!IsPointOnCurrentAreaLightRectangle(light, light_point)) {
            continue;
        }
        pdf += PdfAreaLightSampleSolidAngle(light, shading_point, light_point);
    }
    return pdf;
}

} // namespace yr
