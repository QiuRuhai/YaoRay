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

} // namespace yr
