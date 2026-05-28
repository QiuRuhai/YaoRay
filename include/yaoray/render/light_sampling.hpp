#pragma once

#include <optional>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct EmissiveSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float pdf = 0.0f;
    int emissive_index = -1;
};

std::optional<EmissiveSample> SampleEmissivePrimitive(
    const RenderSceneIR& scene,
    int emissive_index,
    Vec2f sample_triangle,
    Vec2f sample_select);

std::optional<EmissiveSample> SampleEmissiveLights(
    const RenderSceneIR& scene,
    float select_sample,
    Vec2f triangle_sample);

float PdfEmissiveLightSolidAngle(
    const RenderSceneIR& scene,
    Point3f shading_point,
    Point3f light_point,
    Vec3f light_normal);

// --- Analytic light sampling ---

struct AnalyticLightSample {
    Vec3f wi{0.0f, 0.0f, 0.0f};         // Direction from shading point to light.
    float distance = 0.0f;              // Shadow ray segment length.
    Color3f radiance{0.0f, 0.0f, 0.0f}; // Radiance reaching the shading point.
    bool is_delta = false;   // Only true on valid samples; meaningless when valid == false.
    bool valid = false;
};

AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point);
AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point);
AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point);

} // namespace yr
