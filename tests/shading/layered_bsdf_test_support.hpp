#pragma once

#include <algorithm>
#include <cmath>

#include <yaoray/core/rng.hpp>
#include <yaoray/shading/bsdf.hpp>

namespace yrtest::layered {

inline yr::Vec3f Up() { return yr::Vec3f{0.0f, 0.0f, 1.0f}; }

inline yr::RenderMaterial MakeCoatedDiffuse(yr::Color3f reflectance) {
    yr::RenderMaterial material;
    material.kind = yr::RenderMaterialKind::CoatedDiffuse;
    material.reflectance.value = reflectance;
    material.coating_ior = 1.5f;
    material.coating_roughness.value = 0.0f;
    material.coat_thickness = 0.01f;
    material.coat_maxdepth = 10;
    return material;
}

inline yr::RenderMaterial MakeCoatedConductor(yr::Color3f f0) {
    yr::RenderMaterial material = MakeCoatedDiffuse(f0);
    material.kind = yr::RenderMaterialKind::CoatedConductor;
    material.uroughness.value = 0.1f;
    material.vroughness.value = 0.1f;
    return material;
}

inline bool IsFiniteColor(yr::Color3f color) {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}

inline float MaxComponent(yr::Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
}

inline float MinComponent(yr::Color3f color) {
    return std::min(color.x, std::min(color.y, color.z));
}

inline bool Refract(yr::Vec3f wo, yr::Vec3f normal, float eta, yr::Vec3f& wi) {
    const float cos_theta_i = yr::Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) return false;
    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = yr::Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
}

inline yr::Vec3f UniformHemisphereDirection(yr::Vec3f normal, yr::Vec2f sample) {
    constexpr float TwoPi = 6.28318530717958647692f;
    const float z = std::clamp(sample.x, 0.0f, 1.0f);
    const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
    const float phi = TwoPi * sample.y;
    const yr::Vec3f helper = std::fabs(normal.z) < 0.999f
        ? yr::Vec3f{0.0f, 0.0f, 1.0f}
        : yr::Vec3f{1.0f, 0.0f, 0.0f};
    const yr::Vec3f tangent = yr::Normalize(yr::Cross(helper, normal));
    const yr::Vec3f bitangent = yr::Cross(normal, tangent);
    return yr::Normalize(
        tangent * (radius * std::cos(phi)) +
        bitangent * (radius * std::sin(phi)) +
        normal * z
    );
}

inline yr::Vec3f CosineHemisphereDirection(yr::Vec3f normal, yr::Vec2f sample) {
    constexpr float TwoPi = 6.28318530717958647692f;
    const float radius = std::sqrt(std::clamp(sample.x, 0.0f, 1.0f));
    const float phi = TwoPi * sample.y;
    const float z = std::sqrt(std::max(0.0f, 1.0f - sample.x));
    const yr::Vec3f helper = std::fabs(normal.z) < 0.999f
        ? yr::Vec3f{0.0f, 0.0f, 1.0f}
        : yr::Vec3f{1.0f, 0.0f, 0.0f};
    const yr::Vec3f tangent = yr::Normalize(yr::Cross(helper, normal));
    const yr::Vec3f bitangent = yr::Cross(normal, tangent);
    return yr::Normalize(
        tangent * (radius * std::cos(phi)) +
        bitangent * (radius * std::sin(phi)) +
        normal * z
    );
}

inline float DirectionalAlbedoViaEval(
    const yr::RenderMaterial& material,
    yr::Vec3f wo,
    yr::Vec3f normal,
    unsigned seed,
    int sample_count
) {
    yr::Rng rng{seed};
    double sum = 0.0;
    for (int i = 0; i < sample_count; ++i) {
        const yr::Vec3f wi = CosineHemisphereDirection(normal, rng.NextFloat2());
        sum += MaxComponent(yr::EvaluateBsdf(material, wo, wi, normal, rng)) *
            3.14159265358979323846f;
    }
    return static_cast<float>(sum / sample_count);
}

inline float PdfHemisphereIntegral(
    const yr::RenderMaterial& material,
    yr::Vec3f wo,
    yr::Vec3f normal,
    unsigned seed,
    int sample_count
) {
    constexpr float TwoPi = 6.28318530717958647692f;
    yr::Rng rng{seed};
    double sum = 0.0;
    for (int i = 0; i < sample_count; ++i) {
        const yr::Vec3f wi = UniformHemisphereDirection(normal, rng.NextFloat2());
        sum += yr::PdfBsdf(material, wo, wi, normal, rng);
    }
    return static_cast<float>(sum / sample_count) * TwoPi;
}

} // namespace yrtest::layered
