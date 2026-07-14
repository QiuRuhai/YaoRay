#pragma once

#include <optional>
#include <span>

#include <yaoray/core/vec.hpp>
#include <yaoray/lighting/light.hpp>
#include <yaoray/lighting/scene_view.hpp>

namespace yr {

struct RenderSceneIR;

struct AliasSelection {
    int index = -1;
    float pmf = 0.0f;
    float remapped_sample = 0.0f;
};

AliasTable BuildAliasTable(std::span<const float> weights);
AliasSelection SampleAliasTable(const AliasTable& table, float sample);
void PrepareLightSampling(RenderSceneIR& scene);

struct EmissiveSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float pdf = 0.0f;
    int emissive_index = -1;
};

std::optional<EmissiveSample> SampleEmissivePrimitive(
    LightSceneView scene,
    int emissive_index,
    Vec2f sample_triangle,
    Vec2f sample_select);

std::optional<EmissiveSample> SampleEmissiveLights(
    LightSceneView scene,
    float select_sample,
    Vec2f triangle_sample);

float PdfEmissiveLightSolidAngle(
    LightSceneView scene,
    Point3f shading_point,
    Point3f light_point,
    Vec3f light_normal,
    int emissive_index = -1);

// --- Analytic light sampling ---

struct AnalyticLightSample {
    Vec3f wi{0.0f, 0.0f, 0.0f};         // Direction from shading point to light.
    float distance = 0.0f;              // Shadow ray segment length.
    Color3f radiance{0.0f, 0.0f, 0.0f}; // Radiance reaching the shading point.
    bool is_delta = false;   // Only true on valid samples; meaningless when valid == false.
    bool valid = false;
    int light_index = -1;
    float selection_pdf = 0.0f;
};

AnalyticLightSample SampleAnalyticLight(
    LightSceneView scene, Point3f shading_point, float select_sample);

AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point);
AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point);
AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point);

} // namespace yr
