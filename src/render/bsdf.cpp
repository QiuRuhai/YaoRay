#include <yaoray/render/bsdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float DeltaRoughness = 1.0e-3f;

bool IsAboveSurface(Vec3f direction, Vec3f normal) {
    return Dot(direction, normal) > 0.0f;
}

Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}

float AbsDot(Vec3f a, Vec3f b) {
    return std::fabs(Dot(a, b));
}

float Clamp(float value, float low, float high) {
    return std::clamp(value, low, high);
}

float FresnelDielectric(float cos_theta_i, float eta_i, float eta_t) {
    cos_theta_i = Clamp(cos_theta_i, -1.0f, 1.0f);
    bool entering = cos_theta_i > 0.0f;
    if (!entering) {
        std::swap(eta_i, eta_t);
        cos_theta_i = std::fabs(cos_theta_i);
    }

    const float sin_theta_i = std::sqrt(std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    const float sin_theta_t = eta_i / eta_t * sin_theta_i;
    if (sin_theta_t >= 1.0f) {
        return 1.0f;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    const float r_parallel =
        ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) /
        ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
    const float r_perpendicular =
        ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) /
        ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));
    return 0.5f * (r_parallel * r_parallel + r_perpendicular * r_perpendicular);
}

struct DielectricFrame {
    Vec3f normal;
    float eta_i = 1.0f;
    float eta_t = 1.0f;
    float eta = 1.0f;
    float cos_o = 0.0f;
};

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

bool Refract(Vec3f wo, Vec3f normal, float eta, Vec3f& wi) {
    const float cos_theta_i = Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) {
        return false;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
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

bool IsBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Color3f Lerp(Color3f a, Color3f b, float t) {
    return a * (1.0f - t) + b * t;
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

Color3f ApplyBeerLambert(Color3f throughput, Color3f absorption, float thickness, Vec3f w, Vec3f normal) {
    const float cos = std::max(1.0e-4f, std::fabs(Dot(w, normal)));
    const float dist = thickness / cos;
    return Color3f{
        throughput.x * std::exp(-absorption.x * dist),
        throughput.y * std::exp(-absorption.y * dist),
        throughput.z * std::exp(-absorption.z * dist),
    };
}

// Builds the implicit base material (under the clearcoat) for a coated* kind.
// Shared by the three layered estimators (SampleLayered, EvaluateLayered,
// PdfLayered) so the base construction + enter-refract logic stay in lockstep.
RenderMaterial MakeLayeredBase(const RenderMaterial& material, bool conductor_base) {
    RenderMaterial base;
    if (conductor_base) {
        base.kind = RenderMaterialKind::Conductor;
        base.reflectance = material.reflectance;        // f0 (compiler-derived)
        base.uroughness = material.uroughness;
        base.vroughness = material.vroughness;
    } else {
        base.kind = RenderMaterialKind::Diffuse;
        base.reflectance = material.reflectance;
    }
    return base;
}

// Forward declaration: SampleLayered's real exit pdf is the stochastic
// PdfLayered estimate (defined below SampleLayered).
float PdfLayered(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
                 Rng& rng, bool conductor_base);

// Stochastic two-layer walk for coated* materials (M3 Slice 2a): a SMOOTH
// dielectric clearcoat over a (possibly rough) diffuse/conductor base, with a
// Beer-Lambert absorbing medium between them. Russian-roulette reflect/transmit
// at the coat keeps throughput unbiased. The non-specular exit pdf is the real
// stochastic PdfLayered estimate (M3 Slice 2b); the coat's specular reflection
// lobe stays a delta (pdf=1, specular=true).
BsdfSample SampleLayered(const RenderMaterial& material, Vec3f wo, Vec3f normal, Rng& rng,
                         bool conductor_base) {
    if (!IsAboveSurface(wo, normal)) {
        return BsdfSample{};
    }

    const float ce = std::max(1.0f, material.coating_ior);

    const RenderMaterial base = MakeLayeredBase(material, conductor_base);

    // --- Top interface, entering from air (smooth Fresnel) ---
    const float cos_o = std::max(0.0f, Dot(wo, normal));
    const float f_enter = FresnelDielectric(cos_o, 1.0f, ce);
    if (rng.NextFloat() < f_enter) {
        const Vec3f wr = Reflect(-wo, normal);
        if (!IsAboveSurface(wr, normal)) {
            return BsdfSample{};
        }
        return BsdfSample{wr, Color3f{1.0f, 1.0f, 1.0f}, 1.0f, true, true};
    }

    Vec3f w;
    if (!Refract(wo, normal, 1.0f / ce, w)) {
        const Vec3f wr = Reflect(-wo, normal);
        return IsAboveSurface(wr, normal)
            ? BsdfSample{wr, Color3f{1.0f, 1.0f, 1.0f}, 1.0f, true, true}
            : BsdfSample{};
    }

    Color3f throughput{1.0f, 1.0f, 1.0f};

    for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
        throughput = ApplyBeerLambert(throughput, material.coat_absorption, material.coat_thickness, w, normal);

        const BsdfSample base_sample = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
        if (!base_sample.valid || IsBlack(base_sample.weight)) {
            return BsdfSample{};
        }
        throughput = Color3f{
            throughput.x * base_sample.weight.x,
            throughput.y * base_sample.weight.y,
            throughput.z * base_sample.weight.z,
        };
        w = base_sample.wi;
        if (!IsAboveSurface(w, normal)) {
            return BsdfSample{};
        }

        throughput = ApplyBeerLambert(throughput, material.coat_absorption, material.coat_thickness, w, normal);

        const float cos_t = std::max(0.0f, Dot(w, normal));
        const float f_back = FresnelDielectric(cos_t, ce, 1.0f);
        if (rng.NextFloat() < f_back) {
            w = Reflect(w, normal);
            if (IsAboveSurface(w, normal)) {
                return BsdfSample{};
            }
            continue;
        }

        Vec3f wexit;
        if (!Refract(w, normal, ce, wexit)) {
            w = Reflect(w, normal);
            continue;
        }
        // Refract() expresses both incident and transmitted rays in the
        // "crossing-to-the-opposite-side" convention, so for an up-going w it
        // returns the geometrically-correct exit direction but pointing back
        // down into the coat. Negate it to get the up-going air-side exit.
        wexit = -wexit;
        if (!IsAboveSurface(wexit, normal)) {
            return BsdfSample{};
        }
        // Real stochastic layered pdf (M3 Slice 2b): the solid-angle density of
        // this non-specular exit, consistent with EvaluateLayered + PdfBsdf so
        // light-sampling MIS weights are correct. Floored at 1e-4 to keep the
        // PowerHeuristic finite if the estimator returns ~0 for a rare exit.
        const float exit_pdf = PdfLayered(material, wo, wexit, normal, rng, conductor_base);
        return BsdfSample{wexit, throughput, std::max(exit_pdf, 1.0e-4f), true, false};
    }

    return BsdfSample{};
}

// Stochastic estimator of the solid-angle pdf of SampleLayered producing the
// NON-specular exit wi from wo, for a coated* material (M3 Slice 2b). The coat's
// specular reflection is a delta — excluded here (handled by the specular flag).
// This is the pdf-analog of EvaluateLayered: it walks the same internal path
// (refract in, scatter off the base with internal TIR between bounces) and at
// each base interaction adds the deterministic connection's density toward wi.
//
// Exit-coupling derivation. Reconstructed from PBRT v4 LayeredBxDF::PDF, whose
// smooth-top connection accumulates rInterface.PDF(-wos->wi, -wis->wi) -- the
// BASE pdf between the refracted directions -- weighted by the path's sampling
// throughput (our explicit `reach`), then blends a uniform floor. To express
// that base pdf (normalized in the INTERNAL solid angle) as a density in the
// EXTERNAL solid angle around wi, and to account for the exit transmission, the
// connection is multiplied by the exit coupling
//   exit_pdf_coupling = T_exit * cos(theta_wi) / (ce^2 * cos(theta_internal)),
// where T_exit = 1 - F(theta_wi) is the exit transmittance and the remaining
// factor is the full refraction (Snell) solid-angle Jacobian dw_int/dw_wi.
//
// The 1/ce^2 is REQUIRED (not cancelled): the internal walk decays `reach` by
// the internal Fresnel reflectance f_back, which counts the whole TIR region
// (theta > critical) as full reflection, so E[f_back] is large (~0.6) and the
// expected base-visit count Sum P(reach k) = T_enter/(1-E[f_back]) ~= 2.4. The
// pdf must still integrate to P(non-spec exit) ~= T_enter, so each visit's
// integrated exit probability must equal (1 - E[f_back]). Measuring the
// connection in the external solid angle with only T_exit*cos_wi/cos_int gives
// per-visit <T_exit> ~= 0.91 and the series blows up to ~2.0. The external
// hemisphere maps to only the internal critical cone, an area ratio of ce^2;
// dividing by ce^2 restores per-visit (1/ce^2)<T_exit> ~= 0.40 ~= (1-E[f_back]),
// and the geometric series telescopes to ~T_enter. (EvaluateLayered's f coupling
// carries the SAME 1/ce^2, there as the medium->air radiance Jacobian.)
// Calibration pin (white diffuse base, zero absorption): the measured
//   integral pdf dw_wi ~= 0.906  is in [0.85, 1.05] and ~= 1 - F(wo) ~= 0.955.
// The returned value is finally blended with a tiny uniform-hemisphere floor
// (PBRT's Lerp(0.9, 1/(4pi), .)) for MIS robustness against near-zero estimates.
float PdfLayered(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
                 Rng& rng, bool conductor_base) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return 0.0f;
    }
    const float ce = std::max(1.0f, material.coating_ior);
    const RenderMaterial base = MakeLayeredBase(material, conductor_base);

    Vec3f wo_t, wi_down;
    if (!Refract(wo, normal, 1.0f / ce, wo_t)) {
        return 0.0f;
    }
    if (!Refract(wi, normal, 1.0f / ce, wi_down)) {
        return 0.0f;
    }
    const Vec3f wi_internal = -wi_down;
    if (!IsAboveSurface(wi_internal, normal)) {
        return 0.0f;
    }

    const float T_enter = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wo, normal)), 1.0f, ce);
    const float T_exit = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wi, normal)), 1.0f, ce);
    if (T_enter <= 0.0f || T_exit <= 0.0f) {
        return 0.0f;
    }

    // Exit coupling = T_exit * Snell solid-angle Jacobian (with the 1/ce^2 that
    // makes the multi-scatter series telescope to ~T_enter). See header.
    const float cos_wi = std::max(0.0f, Dot(wi, normal));
    const float cos_internal = std::max(1.0e-4f, Dot(wi_internal, normal));
    const float exit_pdf_coupling = T_exit * cos_wi / (ce * ce * cos_internal);

    const int ns = std::max(1, material.coat_nsamples);
    double pdf_sum = 0.0;
    for (int s = 0; s < ns; ++s) {
        float reach = T_enter;   // probability the path reaches this base bounce
        Vec3f w = wo_t;          // down-going inside the medium
        for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
            // A smooth (delta) conductor base returns base_pdf = 0 here (measure-zero
            // connection); the estimate then collapses to the uniform MIS floor below,
            // which is correct — such coated materials are delta and use BSDF sampling.
            const float base_pdf = PdfBsdf(base, -w, wi_internal, normal, rng);
            pdf_sum += static_cast<double>(reach) * base_pdf * exit_pdf_coupling;

            // Continue the internal walk: sample the base, then Russian-roulette
            // reflect at the top interface (the only way to reach a deeper base
            // bounce). A transmit leaves the coat (different exit) and ends this
            // path's contribution.
            const BsdfSample bs = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
            if (!bs.valid || !IsAboveSurface(bs.wi, normal)) {
                break;
            }
            const float f_back = FresnelDielectric(std::max(0.0f, Dot(bs.wi, normal)), ce, 1.0f);
            // Deterministic reach decay (not stochastic RR like Eval/Sample): same
            // expectation, lower variance for the pdf estimate.
            reach *= f_back;
            if (reach <= 0.0f) {
                break;
            }
            w = Reflect(bs.wi, normal);
            if (IsAboveSurface(w, normal)) {
                break;
            }
        }
    }
    const float estimate = static_cast<float>(pdf_sum / ns);
    // PBRT's MIS-robustness floor: blend a tiny uniform-hemisphere density so a
    // near-zero estimate never produces a degenerate (infinite) MIS weight.
    constexpr float kUniformPdf = 1.0f / (4.0f * Pi);
    return 0.9f * estimate + 0.1f * kUniformPdf;
}

// Stochastic estimator of the non-delta transmitted lobe f(wo, wi) for a
// coated* material (M3 Slice 2b). The coat's specular reflection is a delta —
// it contributes 0 here (it lives only in SampleLayered). This estimates light
// that transmits into the coat, scatters off the base (one or MORE bounces with
// internal TIR between them — multiple scattering recovers the energy a single-
// scatter estimator drops), and transmits back out toward wi. Reconstructed
// from PBRT v4 LayeredBxDF::f specialized to: smooth dielectric top (perfect
// Fresnel), Beer-Lambert-only medium (no phase function), opaque base.
//
// Returns f WITHOUT the cos(wi) factor (the path tracer applies the world
// cosine separately). Both wo, wi must be above the surface.
//
// Connection / exit-coupling derivation. The internal up-going direction that
// refracts out to a given above wi is wi_internal = -Refract(wi, n, 1/ce) (2a's
// refraction-reversibility convention, pinned by the roundtrip test). At each
// base interaction the deterministic exit connection contributes
//   f += beta * base.f(-w, wi_internal) * Tr_exit * exit_coupling
// where exit_coupling = T_exit / ce^2. This is PBRT v4's smooth-top connection:
// it would call wis = top.Sample_f(wi, Transmission) and accumulate
//   base.f(-w, wis.wi) * AbsCosTheta(wis.wi) * wis.f / wis.pdf.
// For a smooth dielectric, DielectricBxDF::Sample_f (transmission, Radiance
// mode) returns wis.f = T_exit / (cosTheta_t * ce^2) and wis.pdf = 1 when only
// Transmission is requested, so AbsCosTheta(wis.wi)=cosTheta_t CANCELS the
// 1/cosTheta_t and the connection reduces to base.f * T_exit / ce^2 -- with NO
// surviving base cosine. The 1/ce^2 (which the scaffold's exit_coupling omitted)
// is the medium->air radiance Jacobian; omitting it doubles the directional
// albedo (~1.9 instead of ~1-F(wo)). Pinned by furnace-via-eval and reciprocity.
Color3f EvaluateLayered(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
                        Rng& rng, bool conductor_base) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return Color3f{};
    }
    const float ce = std::max(1.0f, material.coating_ior);

    const RenderMaterial base = MakeLayeredBase(material, conductor_base);

    // Refract wo and wi into the medium. wo_t is the down-going entry ray; the
    // up-going connection that exits to wi is wi_internal = -Refract(wi,n,1/ce).
    Vec3f wo_t, wi_down;
    if (!Refract(wo, normal, 1.0f / ce, wo_t)) {
        return Color3f{};
    }
    if (!Refract(wi, normal, 1.0f / ce, wi_down)) {
        return Color3f{};
    }
    const Vec3f wi_internal = -wi_down;
    if (!IsAboveSurface(wi_internal, normal)) {
        return Color3f{};
    }

    const float T_enter = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wo, normal)), 1.0f, ce);
    const float T_exit = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wi, normal)), 1.0f, ce);
    if (T_enter <= 0.0f || T_exit <= 0.0f) {
        return Color3f{};
    }

    const int ns = std::max(1, material.coat_nsamples);
    Color3f f_sum{0.0f, 0.0f, 0.0f};
    for (int s = 0; s < ns; ++s) {
        Color3f beta{T_enter, T_enter, T_enter};
        Vec3f w = wo_t;   // down-going inside the medium
        for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
            beta = ApplyBeerLambert(beta, material.coat_absorption, material.coat_thickness, w, normal);

            // Deterministic connection to the exit through the smooth top.
            const Color3f base_f = EvaluateBsdf(base, -w, wi_internal, normal, rng);
            if (!IsBlack(base_f)) {
                const Color3f tr_exit = ApplyBeerLambert(Color3f{1.0f, 1.0f, 1.0f},
                    material.coat_absorption, material.coat_thickness, wi_internal, normal);
                // Exit-coupling = exit Fresnel transmittance T_exit times the
                // medium->air radiance Jacobian 1/ce^2. There is deliberately NO
                // AbsCosTheta(wi_internal) factor: in PBRT's smooth-top connection
                // it cancels against the dielectric BTDF's 1/cosTheta_t (the BTDF
                // value is T/(cosTheta_t*ce^2) and the accumulation multiplies by
                // AbsCosTheta(wis.wi)). Pinned by furnace-via-eval (rho ~= 1-F(wo))
                // and reciprocity.
                const float exit_coupling = T_exit / (ce * ce);
                f_sum = f_sum + Color3f{
                    beta.x * base_f.x * tr_exit.x * exit_coupling,
                    beta.y * base_f.y * tr_exit.y * exit_coupling,
                    beta.z * base_f.z * tr_exit.z * exit_coupling,
                };
            }

            // Sample the base to continue the internal walk.
            const BsdfSample bs = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
            if (!bs.valid || IsBlack(bs.weight) || !IsAboveSurface(bs.wi, normal)) {
                break;
            }
            beta = Color3f{beta.x * bs.weight.x, beta.y * bs.weight.y, beta.z * bs.weight.z};
            beta = ApplyBeerLambert(beta, material.coat_absorption, material.coat_thickness, bs.wi, normal);

            // Russian-roulette reflect/transmit at the top interface. If it
            // reflects (TIR or Fresnel), bounce back down and scatter again
            // (multiple scattering). If it transmits, the energy left through a
            // generic direction != wi and contributes nothing to this f estimate.
            const float f_back = FresnelDielectric(std::max(0.0f, Dot(bs.wi, normal)), ce, 1.0f);
            if (rng.NextFloat() < f_back) {
                w = Reflect(bs.wi, normal);
                if (IsAboveSurface(w, normal)) {
                    break;
                }
                continue;
            }
            break;
        }
    }
    return f_sum / static_cast<float>(ns);
}

} // namespace

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng) {
    switch (material.kind) {
        case RenderMaterialKind::CoatedDiffuse:
            return EvaluateLayered(material, wo, wi, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return EvaluateLayered(material, wo, wi, normal, rng, /*conductor_base=*/true);
        case RenderMaterialKind::Diffuse:
        case RenderMaterialKind::DiffuseTransmission:
        case RenderMaterialKind::Mix:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            return LambertianBrdf(material.reflectance.value);
        case RenderMaterialKind::Conductor:
        case RenderMaterialKind::Measured:
            // TODO(measured Slice 2/3): real MeasuredBxDF f/Sample/Pdf via measured_index
            if (material.uroughness.value <= DeltaRoughness && material.vroughness.value <= DeltaRoughness) {
                return Color3f{};
            }
            return GgxSpecularBrdf(material.reflectance.value, material.uroughness.value, wo, wi, normal);
        case RenderMaterialKind::ThinDielectric: {
            if (material.uroughness.value <= DeltaRoughness) {
                return Color3f{};
            }
            const Vec3f forward = Normalize(-wo);
            const float cos_forward = std::max(0.0f, Dot(forward, wi));
            if (cos_forward <= 0.0f) {
                return Color3f{};
            }
            const float fresnel = FresnelDielectric(std::fabs(Dot(normal, wo)), 1.0f, std::max(1.0f, material.ior));
            return LambertianBrdf(material.reflectance.value) * (1.0f - fresnel);
        }
        case RenderMaterialKind::Dielectric:
            if (material.uroughness.value <= DeltaRoughness) {
                return Color3f{};
            }
            return SameHemisphere(wo, wi, normal)
                ? GgxDielectricReflection(material.reflectance.value, material.ior, material.uroughness.value, wo, wi, normal)
                : GgxDielectricTransmission(material.reflectance.value, material.ior, material.uroughness.value, wo, wi, normal);
    }
    return Color3f{};
}

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng) {
    switch (material.kind) {
        case RenderMaterialKind::CoatedDiffuse:
            return PdfLayered(material, wo, wi, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return PdfLayered(material, wo, wi, normal, rng, /*conductor_base=*/true);
        case RenderMaterialKind::Diffuse:
        case RenderMaterialKind::DiffuseTransmission:
        case RenderMaterialKind::Mix:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case RenderMaterialKind::Conductor:
        case RenderMaterialKind::Measured:
            // TODO(measured Slice 2/3): real MeasuredBxDF f/Sample/Pdf via measured_index
            if (material.uroughness.value <= DeltaRoughness && material.vroughness.value <= DeltaRoughness) {
                return 0.0f;
            }
            return GgxReflectionPdf(material.uroughness.value, wo, wi, normal);
        case RenderMaterialKind::ThinDielectric: {
            if (material.uroughness.value <= DeltaRoughness) {
                return 0.0f;
            }
            const Vec3f forward = Normalize(-wo);
            return std::max(0.0f, Dot(forward, wi)) / Pi;
        }
        case RenderMaterialKind::Dielectric:
            if (material.uroughness.value <= DeltaRoughness) {
                return 0.0f;
            }
            return SameHemisphere(wo, wi, normal)
                ? GgxDielectricReflectionPdf(material.ior, material.uroughness.value, wo, wi, normal)
                : GgxDielectricTransmissionPdf(material.ior, material.uroughness.value, wo, wi, normal);
    }
    return 0.0f;
}

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, Rng& rng) {
    switch (material.kind) {
        case RenderMaterialKind::CoatedDiffuse:
            return SampleLayered(material, wo, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return SampleLayered(material, wo, normal, rng, /*conductor_base=*/true);
        case RenderMaterialKind::Diffuse:
        case RenderMaterialKind::DiffuseTransmission:
        case RenderMaterialKind::Mix: {
            if (!IsAboveSurface(wo, normal) || IsBlack(material.reflectance.value)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            return BsdfSample{
                wi,
                material.reflectance.value,
                PdfBsdf(material, wo, wi, normal, rng),
                true,
                false
            };
        }
        case RenderMaterialKind::Conductor:
        case RenderMaterialKind::Measured:
            // TODO(measured Slice 2/3): real MeasuredBxDF f/Sample/Pdf via measured_index
            if (material.uroughness.value <= DeltaRoughness && material.vroughness.value <= DeltaRoughness) {
                if (!IsAboveSurface(wo, normal) || IsBlack(material.reflectance.value)) {
                    return BsdfSample{};
                }
                return BsdfSample{
                    Reflect(-wo, normal),
                    material.reflectance.value,
                    1.0f,
                    true,
                    true
                };
            }
            return SampleGgxReflection(wo, normal, sample, material.reflectance.value, material.uroughness.value);
        case RenderMaterialKind::ThinDielectric: {
            if (IsBlack(material.reflectance.value)) {
                return BsdfSample{};
            }
            const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
            const float fresnel =
                FresnelDielectric(std::fabs(Dot(oriented_normal, wo)), 1.0f, std::max(1.0f, material.ior));
            if (material.uroughness.value <= DeltaRoughness) {
                if (sample.x < fresnel) {
                    return BsdfSample{Reflect(-wo, oriented_normal), material.reflectance.value, 1.0f, true, true};
                }
                return BsdfSample{Normalize(-wo), material.reflectance.value, 1.0f, true, true};
            }

            Vec2f remapped = sample;
            if (sample.x < fresnel) {
                remapped.x = fresnel > 0.0f ? sample.x / fresnel : sample.x;
                BsdfSample reflection = SampleGgxReflection(
                    wo, oriented_normal, remapped, material.reflectance.value * fresnel, material.uroughness.value);
                if (reflection.valid) {
                    reflection.specular = false;
                }
                return reflection;
            }

            remapped.x = (1.0f - fresnel) > 0.0f ? (sample.x - fresnel) / (1.0f - fresnel) : sample.x;
            const Vec3f forward = Normalize(-wo);
            const Vec3f wi = SampleCosineHemisphere(forward, remapped);
            const float pdf = std::max(0.0f, Dot(forward, wi)) / Pi;
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            return BsdfSample{wi, material.reflectance.value * (1.0f - fresnel), pdf, true, false};
        }
        case RenderMaterialKind::Dielectric: {
            if (IsBlack(material.reflectance.value)) {
                return BsdfSample{};
            }
            if (material.uroughness.value <= DeltaRoughness) {
                const DielectricFrame frame = MakeDielectricFrame(wo, normal, material.ior);
                Vec3f refracted;
                const bool can_refract = Refract(wo, frame.normal, frame.eta, refracted);
                const float fresnel = can_refract ? FresnelDielectric(frame.cos_o, frame.eta_i, frame.eta_t) : 1.0f;
                if (!can_refract || sample.x < fresnel) {
                    return BsdfSample{Reflect(-wo, frame.normal), material.reflectance.value, 1.0f, true, true};
                }
                return BsdfSample{refracted, material.reflectance.value, 1.0f, true, true};
            }
            const DielectricFrame frame = MakeDielectricFrame(wo, normal, material.ior);
            Vec2f remapped = sample;
            Vec3f half_vector = SampleGgxHalfVector(frame.normal, material.uroughness.value, remapped);
            if (Dot(half_vector, wo) < 0.0f) {
                half_vector = -half_vector;
            }
            const float fresnel = FresnelDielectric(std::fabs(Dot(wo, half_vector)), frame.eta_i, frame.eta_t);
            if (sample.x < fresnel) {
                remapped.x = fresnel > 0.0f ? sample.x / fresnel : sample.x;
                half_vector = SampleGgxHalfVector(frame.normal, material.uroughness.value, remapped);
                if (Dot(half_vector, wo) < 0.0f) {
                    half_vector = -half_vector;
                }
                const Vec3f wi = Reflect(-wo, half_vector);
                const float pdf = PdfBsdf(material, wo, wi, normal, rng);
                if (pdf <= 0.0f) {
                    return BsdfSample{};
                }
                const Color3f f = EvaluateBsdf(material, wo, wi, normal, rng);
                const float cos_i = AbsDot(normal, wi);
                return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
            }

            remapped.x = (1.0f - fresnel) > 0.0f ? (sample.x - fresnel) / (1.0f - fresnel) : sample.x;
            half_vector = SampleGgxHalfVector(frame.normal, material.uroughness.value, remapped);
            if (Dot(half_vector, wo) < 0.0f) {
                half_vector = -half_vector;
            }
            Vec3f wi;
            if (!Refract(wo, half_vector, frame.eta, wi)) {
                return BsdfSample{Reflect(-wo, half_vector), material.reflectance.value, 1.0f, true, true};
            }
            const float pdf = PdfBsdf(material, wo, wi, normal, rng);
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            const Color3f f = EvaluateBsdf(material, wo, wi, normal, rng);
            const float cos_i = AbsDot(normal, wi);
            return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
        }
    }
    return BsdfSample{};
}

bool IsDeltaBsdf(const RenderMaterial& material) {
    switch (material.kind) {
        case RenderMaterialKind::Conductor:
        case RenderMaterialKind::CoatedConductor:
            return material.uroughness.value == 0.0f && material.vroughness.value == 0.0f;
        case RenderMaterialKind::Dielectric:
        case RenderMaterialKind::ThinDielectric:
            return material.uroughness.value == 0.0f && material.vroughness.value == 0.0f;
        case RenderMaterialKind::Measured:
            // Glossy measured BRDF — light sampling applies (not a delta).
            return false;
        default:
            return false;
    }
}

} // namespace yr
