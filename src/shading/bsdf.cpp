#include <yaoray/shading/bsdf.hpp>
#include "bsdf_internal.hpp"
#include <yaoray/shading/bssrdf.hpp>
#include <yaoray/shading/measured_brdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float DeltaRoughness = 1.0e-3f;

} // namespace

Color3f EvaluateBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng) {
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
        case RenderMaterialKind::Measured:
            // Real measured-BRDF f (M3 Measured Slice 2). When the data loaded,
            // evaluate the Dupuy-Jakob response; otherwise fall through to the
            // conductor degrade below (the compiler leaves measured_brdf null on
            // load failure and degrades kind to Conductor, so this guards the
            // bare-RenderMaterial case the unit tests construct).
            if (material.measured_brdf != nullptr) {
                return EvaluateMeasured(*material.measured_brdf, wo, wi, normal);
            }
            [[fallthrough]];
        case RenderMaterialKind::Conductor:
            // Sample/Pdf for Measured stay conductor-aliased until Slice 3.
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
        case RenderMaterialKind::SubsurfaceExit: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            const float sw = SubsurfaceSw(std::max(0.0f, Dot(normal, wi)), material.ior);
            return Color3f{sw, sw, sw};
        }
        case RenderMaterialKind::Subsurface:
            return Color3f{};  // entry interface handled by the integrator
    }
    return Color3f{};
}

Color3f EvaluateBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f wi,
    Vec3f normal,
    Rng& rng
) {
    return EvaluateBsdf(ShadingMaterial{material}, wo, wi, normal, rng);
}

float PdfBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng) {
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
        case RenderMaterialKind::Measured:
            // Real data-driven measured pdf (M3 Measured Slice 3) when the table
            // loaded; otherwise fall through to the conductor alias (the compiler
            // degrades kind to Conductor on load failure, leaving the pointer
            // null, so this guards the bare-RenderMaterial case the tests build).
            if (material.measured_brdf != nullptr) {
                return PdfMeasured(*material.measured_brdf, wo, wi, normal);
            }
            [[fallthrough]];
        case RenderMaterialKind::Conductor:
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
        case RenderMaterialKind::SubsurfaceExit:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case RenderMaterialKind::Subsurface:
            return 0.0f;
    }
    return 0.0f;
}

float PdfBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f wi,
    Vec3f normal,
    Rng& rng
) {
    return PdfBsdf(ShadingMaterial{material}, wo, wi, normal, rng);
}

BsdfSample SampleBsdf(const ShadingMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, Rng& rng) {
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
        case RenderMaterialKind::Measured:
            // Real data-driven measured importance sampling (M3 Measured Slice 3)
            // when the table loaded; otherwise fall through to the conductor alias
            // (the compiler degrades kind to Conductor on load failure, leaving
            // the pointer null, guarding the bare-RenderMaterial case the tests
            // build).
            if (material.measured_brdf != nullptr) {
                return SampleMeasured(*material.measured_brdf, wo, normal, sample);
            }
            [[fallthrough]];
        case RenderMaterialKind::Conductor:
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
        case RenderMaterialKind::SubsurfaceExit: {
            if (!IsAboveSurface(wo, normal)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            const float pdf = std::max(0.0f, Dot(normal, wi)) / Pi;
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            // weight = f * cos / pdf = Sw(cos) * pi.
            const float w = SubsurfaceSw(std::max(0.0f, Dot(normal, wi)), material.ior) * Pi;
            return BsdfSample{wi, Color3f{w, w, w}, pdf, true, false};
        }
        case RenderMaterialKind::Subsurface:
            return BsdfSample{};  // entry interface handled by the integrator
    }
    return BsdfSample{};
}

BsdfSample SampleBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f normal,
    Vec2f sample,
    Rng& rng
) {
    return SampleBsdf(ShadingMaterial{material}, wo, normal, sample, rng);
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
        case RenderMaterialKind::Subsurface:
            // Entry interface: treated as a special delta event by the integrator.
            return true;
        default:
            return false;
    }
}

} // namespace yr
