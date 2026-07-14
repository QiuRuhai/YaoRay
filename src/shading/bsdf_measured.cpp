#include "bsdf_internal.hpp"

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

} // namespace

// Real measured-BRDF reflectance evaluation (Dupuy & Jakob 2018), ported from
// pbrt-v4 MeasuredBxDF::f. Builds a LOCAL frame from the shading normal (the
// material is isotropic this milestone phase, so the azimuth origin of the
// tangent basis is irrelevant). Transforms wo/wi into that frame, forms the
// half-vector, maps (theta, phi) onto the unit square, runs the inverse VNDF
// warp to recover the spectral grid coordinate, looks the spectra table up at
// the three sRGB primary wavelengths, and assembles
//   f = spectra * ndf / (4 * sigma * cos_wi).
// Reflection only (same-hemisphere); returns black for transmission, degenerate
// geometry, or any non-finite intermediate. Sample/Pdf stay conductor-aliased
// until Slice 3 -- this function only provides the f term.
Color3f EvaluateMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f wi, Vec3f normal) {
    // theta -> [0,1] (sqrt warp, pbrt theta2u) and phi -> [0,1] (pbrt phi2u).
    const auto theta2u = [](float t) { return std::sqrt(t * (2.0f / Pi)); };
    const auto phi2u = [](float p) { return p * (1.0f / (2.0f * Pi)) + 0.5f; };

    // Build a local orthonormal frame; reuse the same helper-vector construction
    // as SampleCosineHemisphere so the basis matches the rest of the BSDF code.
    const Vec3f helper =
        std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    const auto to_local = [&](Vec3f w) {
        return Vec3f{Dot(w, tangent), Dot(w, bitangent), Dot(w, normal)};
    };

    Vec3f lwo = to_local(wo);
    Vec3f lwi = to_local(wi);

    // Reflection only: both directions on the same side of the surface.
    if (lwo.z * lwi.z <= 0.0f) {
        return Color3f{};
    }
    // Canonicalize to the upper hemisphere (the measurement is symmetric).
    if (lwo.z < 0.0f) {
        lwo = -lwo;
        lwi = -lwi;
    }

    Vec3f wm = lwo + lwi;
    if (LengthSquared(wm) <= 0.0f) {
        return Color3f{};
    }
    wm = Normalize(wm);

    const float theta_o = std::acos(std::clamp(lwo.z, -1.0f, 1.0f));
    const float phi_o = std::atan2(lwo.y, lwo.x);
    const float theta_m = std::acos(std::clamp(wm.z, -1.0f, 1.0f));
    const float phi_m = std::atan2(wm.y, wm.x);

    const Vec2f u_wo{theta2u(theta_o), phi2u(phi_o)};
    Vec2f u_wm{theta2u(theta_m), phi2u(brdf.isotropic ? (phi_m - phi_o) : phi_m)};
    u_wm.y -= std::floor(u_wm.y);   // wrap azimuth into [0,1)

    // Inverse VNDF warp recovers the spectral-grid coordinate (conditioned on
    // the outgoing direction, passed as the variadic floats phi_o, theta_o).
    const PiecewiseLinear2D<2>::PLSample ui = brdf.vndf_warp.Invert(u_wm, phi_o, theta_o);

    const float cos_i = lwi.z;   // > 0 after the flip + hemisphere guard
    const float sig = brdf.sigma_warp.Evaluate(u_wo);
    const float denom = 4.0f * sig * cos_i;
    if (denom <= 0.0f) {
        return Color3f{};
    }

    const float ndf = brdf.ndf_warp.Evaluate(u_wm);

    // Spectra lookup at the three sRGB primary wavelengths (nm), conditioned on
    // (phi_o, theta_o). Clamp negatives to zero (synthetic/degenerate tables).
    const float r = std::max(0.0f, brdf.spectra_warp.Evaluate(ui.p, phi_o, theta_o, 630.0f));
    const float g = std::max(0.0f, brdf.spectra_warp.Evaluate(ui.p, phi_o, theta_o, 532.0f));
    const float b = std::max(0.0f, brdf.spectra_warp.Evaluate(ui.p, phi_o, theta_o, 467.0f));

    const float scale = ndf / denom;
    Color3f f{r * scale, g * scale, b * scale};
    if (!std::isfinite(f.x) || !std::isfinite(f.y) || !std::isfinite(f.z)) {
        return Color3f{};
    }
    return f;
}

// --- M3 Measured Slice 3: data-driven importance sampling (Dupuy & Jakob
// 2018), ported from pbrt-v4 MeasuredBxDF::Sample_f / PDF. Uses the SAME local
// frame and theta2u/phi2u conventions as EvaluateMeasured so f, Sample, and Pdf
// are mutually consistent: PdfMeasured(wo, sampled_wi) == SampleMeasured().pdf.

// Inverse of EvaluateMeasured's theta2u/phi2u: map a unit-square coordinate back
// to spherical angles. SphericalDir assembles a direction from (sin/cos, phi).
float u2theta(float u) { return u * u * (Pi / 2.0f); }
float u2phi(float u) { return (2.0f * u - 1.0f) * Pi; }
Vec3f SphericalDir(float sin_theta, float cos_theta, float phi) {
    return Vec3f{sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
}

// Forward decl: SampleMeasured reports its pdf by re-evaluating PdfMeasured on
// the final world-space wi (defined just below), so SampleMeasured().pdf and
// PdfMeasured(wo, sampled_wi) are the SAME quantity by construction.
float PdfMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f wi, Vec3f normal);

// Importance-samples the measured BRDF for an incident direction. Two-stage warp
// (luminance -> VNDF) yields a microfacet normal wm; wi is wo reflected about wm.
// The returned pdf is the solid-angle density (warp pdfs * reflection+domain
// Jacobian); weight = f * cos(wi) / pdf. Reflection only.
BsdfSample SampleMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f normal, Vec2f u) {
    // Same deterministic local frame as EvaluateMeasured / SampleCosineHemisphere.
    const Vec3f helper =
        std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    const auto to_local = [&](Vec3f w) {
        return Vec3f{Dot(w, tangent), Dot(w, bitangent), Dot(w, normal)};
    };
    const auto to_world = [&](Vec3f w) {
        return tangent * w.x + bitangent * w.y + normal * w.z;
    };

    Vec3f lwo = to_local(wo);
    const bool flip = lwo.z <= 0.0f;   // canonicalize to upper hemisphere
    if (flip) {
        lwo = -lwo;
    }

    const float theta_o = std::acos(std::clamp(lwo.z, -1.0f, 1.0f));
    const float phi_o = std::atan2(lwo.y, lwo.x);

    // Stage 1: luminance warp (conditioned on the outgoing direction) picks a
    // unit-square point; stage 2: VNDF warp maps it to a microfacet-normal coord.
    const PiecewiseLinear2D<2>::PLSample s1 = brdf.luminance_warp.Sample(u, phi_o, theta_o);
    const PiecewiseLinear2D<2>::PLSample s2 = brdf.vndf_warp.Sample(s1.p, phi_o, theta_o);

    float phi_m = u2phi(s2.p.y);
    const float theta_m = u2theta(s2.p.x);
    if (brdf.isotropic) {
        phi_m += phi_o;
    }
    const float sin_theta_m = std::sin(theta_m);
    const float cos_theta_m = std::cos(theta_m);
    const Vec3f wm = SphericalDir(sin_theta_m, cos_theta_m, phi_m);   // local frame

    const float dot_wm = Dot(lwo, wm);
    if (dot_wm <= 0.0f) {
        return BsdfSample{};
    }
    Vec3f wi_local = wm * (2.0f * dot_wm) - lwo;   // reflect lwo about wm
    if (wi_local.z <= 0.0f) {
        return BsdfSample{};
    }

    // Validity gate on the sampling-side solid-angle density (warp densities over
    // the reflection + spherical-domain Jacobian); reject degenerate draws.
    const float jac = 4.0f * dot_wm *
                      std::max(2.0f * Pi * Pi * s2.p.x * sin_theta_m, 1e-6f);
    const float sample_pdf = s2.pdf * s1.pdf / jac;
    if (!(sample_pdf > 0.0f) || !std::isfinite(sample_pdf)) {
        return BsdfSample{};
    }

    if (flip) {
        wi_local = -wi_local;
    }
    const Vec3f wi_world = Normalize(to_world(wi_local));

    // Report the pdf via PdfMeasured on the final world-space wi rather than the
    // sampling-side `sample_pdf`. Both densities are the same in exact arithmetic,
    // but near perfect-mirror reflection (theta_m -> 0) the round-trip through
    // wm -> wi -> normalize -> recovered wm shifts u_wm.x across the warp's first
    // cell, so the sampling value and the Pdf value diverge in float32. Pinning
    // the reported pdf to PdfMeasured(wo, wi) makes SampleMeasured().pdf and
    // PdfBsdf(wo, sampled_wi) byte-identical by construction -> clean MIS.
    const float pdf = PdfMeasured(brdf, wo, wi_world, normal);
    if (!(pdf > 0.0f) || !std::isfinite(pdf)) {
        return BsdfSample{};
    }

    const Color3f f = EvaluateMeasured(brdf, wo, wi_world, normal);
    const float cos_wi = std::fabs(Dot(wi_world, normal));
    return BsdfSample{wi_world, f * (cos_wi / pdf), pdf, true, false};
}

// Solid-angle pdf of SampleMeasured producing wi from wo. Mirrors the sampler:
// recover the microfacet-normal unit-square coord from the half-vector, invert
// the VNDF warp, evaluate the luminance warp, divide by the same Jacobian. Equal
// to SampleMeasured's pdf for the same (wo, wi) by construction. Reflection only.
float PdfMeasured(const MeasuredBrdf& brdf, Vec3f wo, Vec3f wi, Vec3f normal) {
    // Same theta2u/phi2u as EvaluateMeasured.
    const auto theta2u = [](float t) { return std::sqrt(t * (2.0f / Pi)); };
    const auto phi2u = [](float p) { return p * (1.0f / (2.0f * Pi)) + 0.5f; };

    const Vec3f helper =
        std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    const auto to_local = [&](Vec3f w) {
        return Vec3f{Dot(w, tangent), Dot(w, bitangent), Dot(w, normal)};
    };

    Vec3f lwo = to_local(wo);
    Vec3f lwi = to_local(wi);
    if (lwo.z * lwi.z <= 0.0f) {   // reflection only
        return 0.0f;
    }
    if (lwo.z < 0.0f) {
        lwo = -lwo;
        lwi = -lwi;
    }

    Vec3f wm = lwi + lwo;
    if (LengthSquared(wm) <= 0.0f) {
        return 0.0f;
    }
    wm = Normalize(wm);

    const float theta_o = std::acos(std::clamp(lwo.z, -1.0f, 1.0f));
    const float phi_o = std::atan2(lwo.y, lwo.x);
    const float theta_m = std::acos(std::clamp(wm.z, -1.0f, 1.0f));
    const float phi_m = std::atan2(wm.y, wm.x);

    Vec2f u_wm{theta2u(theta_m), phi2u(brdf.isotropic ? (phi_m - phi_o) : phi_m)};
    u_wm.y -= std::floor(u_wm.y);   // wrap azimuth into [0,1)

    const PiecewiseLinear2D<2>::PLSample ui = brdf.vndf_warp.Invert(u_wm, phi_o, theta_o);
    const float lum = brdf.luminance_warp.Evaluate(ui.p, phi_o, theta_o);

    const float sin_theta_m = std::sqrt(wm.x * wm.x + wm.y * wm.y);
    const float jac = 4.0f * Dot(lwo, wm) *
                      std::max(2.0f * Pi * Pi * u_wm.x * sin_theta_m, 1e-6f);
    return (jac > 0.0f) ? (ui.pdf * lum / jac) : 0.0f;
}

} // namespace yr
