#pragma once

#include <yaoray/shading/bsdf.hpp>
#include <yaoray/shading/measured_brdf.hpp>

namespace yr {

bool IsAboveSurface(Vec3f direction, Vec3f normal);
Vec3f Reflect(Vec3f direction, Vec3f normal);
float FresnelDielectric(float cos_theta_i, float eta_i, float eta_t);
bool Refract(Vec3f wo, Vec3f normal, float eta, Vec3f& wi);
bool IsBlack(Color3f color);
Color3f Lerp(Color3f a, Color3f b, float t);

struct DielectricFrame {
    Vec3f normal;
    float eta_i = 1.0f;
    float eta_t = 1.0f;
    float eta = 1.0f;
    float cos_o = 0.0f;
};

float AbsDot(Vec3f a, Vec3f b);
DielectricFrame MakeDielectricFrame(Vec3f wo, Vec3f normal, float ior);
Vec3f SampleCosineHemisphere(Vec3f normal, Vec2f sample);
Color3f LambertianBrdf(Color3f albedo);
float SubsurfaceSw(float cos_theta, float eta);
Color3f GgxSpecularBrdf(
    Color3f f0, float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
float GgxReflectionPdf(float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
bool SameHemisphere(Vec3f a, Vec3f b, Vec3f normal);
Color3f GgxDielectricReflection(
    Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
float GgxDielectricReflectionPdf(
    float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
Color3f GgxDielectricTransmission(
    Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
float GgxDielectricTransmissionPdf(
    float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal);
Vec3f SampleGgxHalfVector(Vec3f normal, float roughness, Vec2f sample);
BsdfSample SampleGgxReflection(
    Vec3f wo, Vec3f normal, Vec2f sample, Color3f f0, float roughness);

Color3f EvaluateMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f wi, Vec3f normal);
BsdfSample SampleMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f normal, Vec2f sample);
float PdfMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f wi, Vec3f normal);

Color3f EvaluateLayered(
    const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
    Rng& rng, bool conductor_base);
float PdfLayered(
    const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
    Rng& rng, bool conductor_base);
BsdfSample SampleLayered(
    const RenderMaterial& material, Vec3f wo, Vec3f normal,
    Rng& rng, bool conductor_base);

} // namespace yr
