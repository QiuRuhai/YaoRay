#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct BsdfSample {
    Vec3f wi;
    Color3f weight;
    float pdf = 0.0f;
    bool valid = false;
    bool specular = false;
};

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample);

bool IsDeltaBsdf(const RenderMaterial& material);

} // namespace yr
