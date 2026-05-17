#pragma once

#include <optional>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct AreaLightSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float area = 0.0f;
    float pdf_area = 0.0f;
};

std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Vec2f uv);

float PdfAreaLightSampleSolidAngle(
    const RenderAreaLight& light,
    Point3f shading_point,
    Point3f light_point
);

float PdfAreaLightsForPointSolidAngle(
    const RenderScene& scene,
    Point3f shading_point,
    Point3f light_point
);

} // namespace yr
