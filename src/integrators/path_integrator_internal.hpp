#pragma once

#include <yaoray/integrators/path_integrator.hpp>
#include <yaoray/shading/material.hpp>
#include <yaoray/shading/shading_material.hpp>

namespace yr {

struct PreviousBounce {
    bool valid = false;
    bool delta = false;
    Point3f origin;
    float bsdf_pdf = 0.0f;
    int light_sample_count = 1;
};

struct PathMediumState {
    bool active = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

struct ShadowVisibility {
    bool visible = true;
    Color3f transmittance{1.0f, 1.0f, 1.0f};
};

Color3f Multiply(Color3f a, Color3f b);
bool IsNearBlack(Color3f color);
Color3f ClampMaxComponent(Color3f value, float limit);
Color3f BeerLambertTransmittance(
    Color3f absorption_color, float absorption_distance, float distance);
void ApplyMediumAttenuation(
    Color3f& throughput, const PathMediumState& medium, float distance);
bool IsShadowTransmittanceBlack(Color3f color);
Color3f ClampTransmittance(Color3f value);
bool IsShadowTransparentMaterial(const RenderMaterial& material);
Color3f ThinGlassShadowTransmittance(const RenderMaterial& material);
void ToggleShadowMedium(PathMediumState& medium, const RenderMaterial& material);
void UpdateMediumStateAfterBsdf(
    PathMediumState& medium, const RenderMaterial& material, Vec3f normal, Vec3f wi);
float SurfaceBias(Point3f point);
float ShadowBias(Point3f origin, Point3f target, float distance);
Vec3f FaceForward(Vec3f normal, Vec3f reference);
Color3f EnvironmentColor(const RenderSceneIR& scene, Vec3f direction);
float EmissiveHitMisWeight(
    const RenderSceneIR& scene, const PreviousBounce& previous,
    Point3f hit_point, Vec3f light_normal, int primitive_index);
float EnvironmentHitMisWeight(
    const RenderSceneIR& scene, const PreviousBounce& previous, Vec3f direction);
bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index);
bool SurviveRussianRoulette(int depth, Color3f& throughput, Sampler& sampler);
void AccumulateTraceStats(PathTraceStats& stats, const BvhTraceStats& trace_stats);
ShadowVisibility TraceShadowVisibility(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    Ray3f ray,
    float max_distance,
    PathTraceStats& stats);

Color3f EstimateDirectEnvironmentLight(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const ShadingMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats);

Color3f EstimateDirectLight(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const ShadingMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats);

} // namespace yr
