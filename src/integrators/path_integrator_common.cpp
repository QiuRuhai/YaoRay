#include "path_integrator_internal.hpp"

#include <yaoray/integrators/mis.hpp>
#include <yaoray/lighting/environment.hpp>
#include <yaoray/lighting/light_sampling.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr {
namespace {

constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;
constexpr int RussianRouletteStartDepth = 3;
constexpr float RussianRouletteMinSurvival = 0.05f;
constexpr float RussianRouletteMaxSurvival = 0.95f;
constexpr float AbsorptionEpsilon = 1.0e-6f;

} // namespace

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool IsNearBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float MaxComponent(Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
}

Color3f ClampMaxComponent(Color3f value, float limit) {
    if (limit <= 0.0f) {
        return value;
    }
    const float max_component = MaxComponent(value);
    if (max_component <= limit || max_component <= 0.0f) {
        return value;
    }
    return value * (limit / max_component);
}

float SafeAbsorptionChannel(float value) {
    return std::clamp(value, AbsorptionEpsilon, 1.0f);
}

Color3f BeerLambertTransmittance(Color3f absorption_color, float absorption_distance, float distance) {
    if (distance <= 0.0f || absorption_distance <= 0.0f) {
        return Color3f{1.0f, 1.0f, 1.0f};
    }
    const float inverse_distance = 1.0f / absorption_distance;
    return Color3f{
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.x)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.y)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.z)) * distance * inverse_distance)
    };
}

void ApplyMediumAttenuation(Color3f& throughput, const PathMediumState& medium, float distance) {
    if (!medium.active) {
        return;
    }
    throughput = Multiply(
        throughput,
        BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, distance)
    );
}

bool IsShadowTransmittanceBlack(Color3f color) {
    return MaxComponent(color) <= AbsorptionEpsilon;
}

float ClampTransmittanceChannel(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Color3f ClampTransmittance(Color3f value) {
    return Color3f{
        ClampTransmittanceChannel(value.x),
        ClampTransmittanceChannel(value.y),
        ClampTransmittanceChannel(value.z)
    };
}

bool IsShadowTransparentMaterial(const RenderMaterial& material) {
    return material.kind == RenderMaterialKind::Dielectric || material.kind == RenderMaterialKind::ThinDielectric;
}

Color3f ThinGlassShadowTransmittance(const RenderMaterial& material) {
    return ClampTransmittance(material.reflectance.value);
}

void ToggleShadowMedium(PathMediumState& medium, const RenderMaterial& material) {
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
}

bool IsThickDielectricTransmission(const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    return material.kind == RenderMaterialKind::Dielectric && Dot(wi, normal) < 0.0f;
}

void UpdateMediumStateAfterBsdf(PathMediumState& medium, const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    if (!IsThickDielectricTransmission(material, normal, wi)) {
        return;
    }
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
}

float MaxAbsComponent(Point3f point) {
    return std::max(std::fabs(point.x), std::max(std::fabs(point.y), std::fabs(point.z)));
}

float SurfaceBias(Point3f point) {
    return std::max(MinShadowBias, MaxAbsComponent(point) * ShadowBiasScale);
}

float ShadowBias(Point3f origin, Point3f target, float distance) {
    const float coordinate_scale = std::max(MaxAbsComponent(origin), MaxAbsComponent(target));
    const float scaled_bias = coordinate_scale * ShadowBiasScale;
    const float capped_bias = distance * MaxShadowBiasDistanceFraction;
    return std::min(std::max(MinShadowBias, scaled_bias), capped_bias);
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

Color3f EnvironmentColor(const RenderSceneIR& scene, Vec3f direction) {
    return EvaluateEnvironment(MakeLightSceneView(scene), direction);
}

float EmissiveHitMisWeight(const RenderSceneIR& scene, const PreviousBounce& previous,
    Point3f hit_point, Vec3f light_normal, int primitive_index) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    int emissive_index = -1;
    for (int index = 0; index < static_cast<int>(scene.emissive_primitives.size()); ++index) {
        if (scene.emissive_primitives[static_cast<std::size_t>(index)].primitive_index == primitive_index) {
            emissive_index = index;
            break;
        }
    }
    const float pdf_light = PdfEmissiveLightSolidAngle(
        MakeLightSceneView(scene), previous.origin, hit_point, light_normal, emissive_index);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}

float EnvironmentHitMisWeight(const RenderSceneIR& scene, const PreviousBounce& previous, Vec3f direction) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfEnvironment(MakeLightSceneView(scene), direction);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}

bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(PathTraceStats& stats, const BvhTraceStats& trace_stats);

int PathLightSampleCount(const RenderSceneIR& /*scene*/) {
    return 1;
}

bool SurviveRussianRoulette(int depth, Color3f& throughput, Sampler& sampler) {
    if (depth < RussianRouletteStartDepth) {
        return true;
    }

    const float survival = std::clamp(
        MaxComponent(throughput),
        RussianRouletteMinSurvival,
        RussianRouletteMaxSurvival
    );
    if (sampler.Sample1D(SampleDimension::RussianRoulette) >= survival) {
        return false;
    }

    throughput = throughput / survival;
    return true;
}

void AccumulateTraceStats(PathTraceStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
    stats.sphere_tests += trace_stats.sphere_tests;
}


} // namespace yr
