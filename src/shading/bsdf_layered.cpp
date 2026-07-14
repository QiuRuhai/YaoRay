#include "bsdf_internal.hpp"

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

} // namespace

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


} // namespace yr
