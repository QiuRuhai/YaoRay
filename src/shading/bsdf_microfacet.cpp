#include "bsdf_internal.hpp"

#include <yaoray/shading/bssrdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float DeltaRoughness = 1.0e-3f;

} // namespace

float AbsDot(Vec3f a, Vec3f b) {
    return std::fabs(Dot(a, b));
}

DielectricFrame MakeDielectricFrame(Vec3f wo, Vec3f normal, float ior) {
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    return DielectricFrame{
        oriented_normal,
        eta_i,
        eta_t,
        eta_i / eta_t,
        std::max(0.0f, Dot(oriented_normal, wo))
    };
}

Vec3f SampleCosineHemisphere(Vec3f normal, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 1.0f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float radius = std::sqrt(u1);
    const float theta = 2.0f * Pi * u2;
    const float local_x = radius * std::cos(theta);
    const float local_y = radius * std::sin(theta);
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - u1));

    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

Color3f LambertianBrdf(Color3f albedo) {
    return albedo / Pi;
}

// Exit-interface directional term Sw(cos) for a subsurface boundary of relative
// IOR eta: a normalized Fresnel-weighted cosine lobe. Sw(cos) = (1 - Fr(cos)) / (c*Pi),
// c = 1 - 2*FresnelMoment1(1/eta).
float SubsurfaceSw(float cos_theta, float eta) {
    const float c = 1.0f - 2.0f * FresnelMoment1(1.0f / eta);
    return (1.0f - FrDielectric(cos_theta, eta)) / (c * Pi);
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float RoughnessToAlpha(float roughness) {
    const float clamped = std::max(DeltaRoughness, roughness);
    return clamped * clamped;
}

float GgxDistribution(float cos_theta_h, float alpha) {
    const float cos2 = cos_theta_h * cos_theta_h;
    const float alpha2 = alpha * alpha;
    const float denom = cos2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (Pi * denom * denom);
}

float SmithG1(float cos_theta, float alpha) {
    const float cos2 = cos_theta * cos_theta;
    if (cos2 <= 0.0f) {
        return 0.0f;
    }
    const float tan2 = (1.0f - cos2) / cos2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alpha * alpha * tan2));
}

Color3f SchlickFresnel(Color3f f0, float cos_theta) {
    const float t = std::pow(1.0f - Clamp01(cos_theta), 5.0f);
    return Lerp(f0, Color3f{1.0f, 1.0f, 1.0f}, t);
}

Color3f GgxSpecularBrdf(Color3f f0, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return Color3f{};
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return Color3f{};
    }

    const float cos_o = std::max(0.0f, Dot(normal, wo));
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_oh <= 0.0f) {
        return Color3f{};
    }

    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const Color3f f = SchlickFresnel(f0, cos_oh);
    return f * (d * g / (4.0f * cos_o * cos_i));
}

float GgxReflectionPdf(float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return 0.0f;
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return 0.0f;
    }

    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_h <= 0.0f || cos_oh <= 0.0f) {
        return 0.0f;
    }

    const float alpha = RoughnessToAlpha(roughness);
    return GgxDistribution(cos_h, alpha) * cos_h / (4.0f * cos_oh);
}

bool SameHemisphere(Vec3f a, Vec3f b, Vec3f normal) {
    return Dot(a, normal) * Dot(b, normal) > 0.0f;
}

Color3f GgxDielectricReflection(Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!SameHemisphere(wo, wi, normal)) {
        return Color3f{};
    }
    const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return Color3f{};
    }
    const float cos_o = AbsDot(oriented_normal, wo);
    const float cos_i = AbsDot(oriented_normal, wi);
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float cos_oh = AbsDot(wo, half_vector);
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_oh <= 0.0f) {
        return Color3f{};
    }
    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const float f = FresnelDielectric(cos_oh, 1.0f, std::max(1.0f, ior));
    return albedo * (f * d * g / (4.0f * cos_o * cos_i));
}

float GgxDielectricReflectionPdf(float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!SameHemisphere(wo, wi, normal)) {
        return 0.0f;
    }
    const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return 0.0f;
    }
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float cos_oh = AbsDot(wo, half_vector);
    if (cos_h <= 0.0f || cos_oh <= 0.0f) {
        return 0.0f;
    }
    const float f = FresnelDielectric(cos_oh, 1.0f, std::max(1.0f, ior));
    return f * GgxDistribution(cos_h, RoughnessToAlpha(roughness)) * cos_h / (4.0f * cos_oh);
}

Vec3f TransmissionHalfVector(Vec3f wo, Vec3f wi, Vec3f normal, float ior) {
    const bool entering = Dot(normal, wo) >= 0.0f;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    Vec3f half_vector = Normalize(wo + wi * eta);
    if (Dot(half_vector, normal) < 0.0f) {
        half_vector = -half_vector;
    }
    return half_vector;
}

Color3f GgxDielectricTransmission(Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (SameHemisphere(wo, wi, normal)) {
        return Color3f{};
    }
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    const Vec3f half_vector = TransmissionHalfVector(wo, wi, oriented_normal, ior);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return Color3f{};
    }

    const float cos_o = AbsDot(oriented_normal, wo);
    const float cos_i = AbsDot(oriented_normal, wi);
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float wo_h = Dot(wo, half_vector);
    const float wi_h = Dot(wi, half_vector);
    const float sqrt_denom = wo_h + eta * wi_h;
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_h <= 0.0f || wo_h <= 0.0f || wi_h >= 0.0f ||
        sqrt_denom == 0.0f) {
        return Color3f{};
    }

    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const float f = FresnelDielectric(std::fabs(wo_h), eta_i, eta_t);
    const float numerator = std::fabs(wi_h) * std::fabs(wo_h) * eta * eta;
    const float denominator = cos_o * cos_i * sqrt_denom * sqrt_denom;
    return albedo * ((1.0f - f) * d * g * numerator / denominator);
}

float GgxDielectricTransmissionPdf(float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (SameHemisphere(wo, wi, normal)) {
        return 0.0f;
    }
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    const Vec3f half_vector = TransmissionHalfVector(wo, wi, oriented_normal, ior);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return 0.0f;
    }

    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float wo_h = Dot(wo, half_vector);
    const float wi_h = Dot(wi, half_vector);
    const float sqrt_denom = wo_h + eta * wi_h;
    if (cos_h <= 0.0f || wo_h <= 0.0f || wi_h >= 0.0f || sqrt_denom == 0.0f) {
        return 0.0f;
    }

    const float f = FresnelDielectric(std::fabs(wo_h), eta_i, eta_t);
    const float dwh_dwi = std::fabs((eta * eta * wi_h) / (sqrt_denom * sqrt_denom));
    return (1.0f - f) * GgxDistribution(cos_h, RoughnessToAlpha(roughness)) * cos_h * dwh_dwi;
}

Vec3f SampleGgxHalfVector(Vec3f normal, float roughness, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 0.999999f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float alpha = RoughnessToAlpha(roughness);
    const float alpha2 = alpha * alpha;
    const float tan2_theta = alpha2 * u1 / std::max(1.0e-6f, 1.0f - u1);
    const float cos_theta = 1.0f / std::sqrt(1.0f + tan2_theta);
    const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    const float phi = 2.0f * Pi * u2;

    const float local_x = sin_theta * std::cos(phi);
    const float local_y = sin_theta * std::sin(phi);
    const float local_z = cos_theta;
    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

BsdfSample SampleGgxReflection(Vec3f wo, Vec3f normal, Vec2f sample, Color3f f0, float roughness) {
    if (!IsAboveSurface(wo, normal) || IsBlack(f0)) {
        return BsdfSample{};
    }

    const Vec3f half_vector = SampleGgxHalfVector(normal, roughness, sample);
    const Vec3f wi = Reflect(-wo, half_vector);
    if (!IsAboveSurface(wi, normal)) {
        return BsdfSample{};
    }

    const float pdf = GgxReflectionPdf(roughness, wo, wi, normal);
    if (pdf <= 0.0f) {
        return BsdfSample{};
    }

    const Color3f f = GgxSpecularBrdf(f0, roughness, wo, wi, normal);
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    return BsdfSample{
        wi,
        f * (cos_i / pdf),
        pdf,
        true,
        false
    };
}


} // namespace yr
