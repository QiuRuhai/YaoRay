#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct EnvironmentSample {
    Vec3f direction;
    Color3f radiance;
    float pdf_solid_angle = 0.0f;
    bool valid = false;
};

Vec2f DirectionToEnvironmentUv(Vec3f direction, float rotation_radians);
Vec3f EnvironmentUvToDirection(Vec2f uv, float rotation_radians);

RenderEnvironmentDistribution BuildEnvironmentDistribution(const RenderTexture& texture);

Color3f EvaluateEnvironment(const RenderScene& scene, Vec3f direction);
EnvironmentSample SampleEnvironment(const RenderScene& scene, Vec2f sample);
float PdfEnvironment(const RenderScene& scene, Vec3f direction);
bool HasSampleableEnvironment(const RenderScene& scene);

} // namespace yr
