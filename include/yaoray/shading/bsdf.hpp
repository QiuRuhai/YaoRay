#pragma once

#include <yaoray/core/rng.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/shading/material.hpp>
#include <yaoray/shading/shading_material.hpp>

namespace yr {

struct BsdfSample {
    Vec3f wi;
    Color3f weight;
    float pdf = 0.0f;
    bool valid = false;
    bool specular = false;
};

Color3f EvaluateBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);
Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);

float PdfBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);
float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);

BsdfSample SampleBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, Rng& rng);
BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, Rng& rng);

bool IsDeltaBsdf(const RenderMaterial& material);

} // namespace yr
