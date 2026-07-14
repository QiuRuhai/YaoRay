#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/lighting/scene_view.hpp>
#include <yaoray/lighting/light.hpp>
#include <yaoray/shading/texture.hpp>

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

Color3f EvaluateEnvironment(LightSceneView scene, Vec3f direction);
EnvironmentSample SampleEnvironment(LightSceneView scene, Vec2f sample);
float PdfEnvironment(LightSceneView scene, Vec3f direction);
bool HasSampleableEnvironment(LightSceneView scene);

} // namespace yr
