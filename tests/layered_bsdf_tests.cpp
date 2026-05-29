#include "yr_test.hpp"

#include <yaoray/core/rng.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

#include <algorithm>
#include <cmath>

namespace {

yr::Vec3f Up() { return yr::Vec3f{0.0f, 0.0f, 1.0f}; }

yr::RenderMaterial MakeCoatedDiffuse(yr::Color3f base_reflectance) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::CoatedDiffuse;
    m.reflectance.value = base_reflectance;
    m.coating_ior = 1.5f;
    m.coating_roughness.value = 0.0f;
    m.coat_thickness = 0.01f;
    m.coat_absorption = yr::Color3f{0.0f, 0.0f, 0.0f};
    m.coat_maxdepth = 10;
    return m;
}

yr::RenderMaterial MakeCoatedConductor(yr::Color3f base_f0) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::CoatedConductor;
    m.reflectance.value = base_f0;     // conductor base f0 (compiler-derived in real scenes)
    m.uroughness.value = 0.1f;
    m.vroughness.value = 0.1f;
    m.coating_ior = 1.5f;
    m.coating_roughness.value = 0.0f;
    m.coat_thickness = 0.01f;
    m.coat_absorption = yr::Color3f{0.0f, 0.0f, 0.0f};
    m.coat_maxdepth = 10;
    return m;
}

bool IsFiniteColor(yr::Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

float MaxComp(yr::Color3f c) { return std::max(c.x, std::max(c.y, c.z)); }
float MinComp(yr::Color3f c) { return std::min(c.x, std::min(c.y, c.z)); }

// Local replica of bsdf.cpp's anonymous-namespace Refract(wo, n, eta, wi),
// eta = etaI/etaT. Returns false on total internal reflection. Used only by
// the convention-roundtrip test (the real Refract is unreachable from this TU).
bool RefractLocal(yr::Vec3f wo, yr::Vec3f normal, float eta, yr::Vec3f& wi) {
    const float cos_theta_i = yr::Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) {
        return false;
    }
    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = yr::Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
}

// Monte-Carlo directional albedo of EvaluateBsdf, cosine-sampling wi.
float DirectionalAlbedoViaEval(const yr::RenderMaterial& m, yr::Vec3f wo,
                               yr::Vec3f n, unsigned seed, int N) {
    yr::Rng rng{seed};
    double acc = 0.0;
    for (int i = 0; i < N; ++i) {
        const yr::Vec2f u = rng.NextFloat2();
        const float r = std::sqrt(std::clamp(u.x, 0.0f, 1.0f));
        const float phi = 2.0f * 3.14159265358979f * u.y;
        const float z = std::sqrt(std::max(0.0f, 1.0f - u.x));
        const yr::Vec3f helper = std::fabs(n.z) < 0.999f ? yr::Vec3f{0, 0, 1} : yr::Vec3f{1, 0, 0};
        const yr::Vec3f t = yr::Normalize(yr::Cross(helper, n));
        const yr::Vec3f b = yr::Cross(n, t);
        const yr::Vec3f wi = yr::Normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * z);
        const yr::Color3f f = yr::EvaluateBsdf(m, wo, wi, n, rng);
        acc += MaxComp(f) * 3.14159265358979f;   // mean of f*pi estimates integral f cos dw
    }
    return static_cast<float>(acc / N);
}

} // namespace

// Validates the 2a refraction-reversibility convention that EvaluateLayered
// relies on: the internal up-going direction that exits to an above wi is
// wi_internal = -Refract(wi, n, 1/ce). Asserts it is up-going and that
// re-refracting it out of the coat (ce, then negate per 2a) returns ~wi.
YR_TEST(layered_eval_exit_convention_roundtrip) {
    const yr::Vec3f n = Up();
    const float ce = 1.5f;
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.4f, -0.3f, 0.9f});

    yr::Vec3f wi_down;
    YR_EXPECT_TRUE(RefractLocal(wi, n, 1.0f / ce, wi_down));
    const yr::Vec3f wi_internal = -wi_down;
    // The internal connection direction must be up-going (above the base).
    YR_EXPECT_TRUE(yr::Dot(wi_internal, n) > 0.0f);

    // Re-refract the internal up-going ray back out to air (eta = ce), then
    // negate (2a's "crossing-to-opposite-side" convention) to get the air-side
    // exit. It must reproduce the original wi.
    yr::Vec3f wexit;
    YR_EXPECT_TRUE(RefractLocal(wi_internal, n, ce, wexit));
    wexit = -wexit;
    YR_EXPECT_TRUE(yr::Dot(wexit, n) > 0.0f);
    YR_EXPECT_TRUE(std::fabs(wexit.x - wi.x) < 1e-4f);
    YR_EXPECT_TRUE(std::fabs(wexit.y - wi.y) < 1e-4f);
    YR_EXPECT_TRUE(std::fabs(wexit.z - wi.z) < 1e-4f);
}

YR_TEST(layered_eval_furnace_matches_sample_energy) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    const float rho = DirectionalAlbedoViaEval(m, wo, n, 13u, 40000);
    YR_EXPECT_TRUE(rho >= 0.90f);
    YR_EXPECT_TRUE(rho <= 1.02f);
}

YR_TEST(layered_eval_is_reciprocal) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.6f, 0.5f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.2f, 0.5f, 0.8f});
    auto meanF = [&](yr::Vec3f a, yr::Vec3f b) {
        yr::Rng rng{77u};
        double s = 0.0;
        const int N = 60000;
        for (int i = 0; i < N; ++i) s += MaxComp(yr::EvaluateBsdf(m, a, b, n, rng));
        return static_cast<float>(s / N);
    };
    const float f_ab = meanF(wo, wi), f_ba = meanF(wi, wo);
    YR_EXPECT_TRUE(std::fabs(f_ab - f_ba) <= 0.05f * std::max(f_ab, f_ba) + 1e-4f);
}

YR_TEST(layered_eval_differs_from_bare_base) {
    yr::RenderMaterial coated = MakeCoatedDiffuse(yr::Color3f{0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = yr::Color3f{0.5f, 0.5f, 0.5f};
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, 0.2f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.9f, 0.0f, 0.2f});
    yr::Rng ra{5u}, rb{5u};
    double cs = 0;
    const int N = 40000;
    for (int i = 0; i < N; ++i) cs += MaxComp(yr::EvaluateBsdf(coated, wo, wi, n, ra));
    const float bare_f = MaxComp(yr::EvaluateBsdf(bare, wo, wi, n, rb));
    YR_EXPECT_TRUE(std::fabs(static_cast<float>(cs / N) - bare_f) > 0.005f);
}

YR_TEST(layered_eval_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng ra{321u}, rb{321u};
    const yr::Color3f a = yr::EvaluateBsdf(m, wo, wi, n, ra);
    const yr::Color3f b = yr::EvaluateBsdf(m, wo, wi, n, rb);
    YR_EXPECT_EQ(a.x, b.x);
    YR_EXPECT_EQ(a.y, b.y);
    YR_EXPECT_EQ(a.z, b.z);
}

YR_TEST(layered_sample_is_energy_conserving_white_furnace) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});

    yr::Rng rng{2024u};
    const int N = 20000;
    yr::Color3f sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (s.valid) {
            YR_EXPECT_TRUE(IsFiniteColor(s.weight));
            sum = sum + s.weight;
        }
    }
    const yr::Color3f mean = sum / static_cast<float>(N);
    YR_EXPECT_TRUE(MaxComp(mean) <= 1.05f);
    // Lower bound: a white (reflectance 1) coated-diffuse base with zero
    // absorption must reflect almost all energy back out — a near-lossless
    // coat. A regression that drops exit paths (e.g. a wrong exit-refraction
    // sign) collapses this toward ~0.04, so guard it.
    YR_EXPECT_TRUE(MinComp(mean) >= 0.90f);
}

YR_TEST(layered_sample_differs_from_bare_base) {
    yr::RenderMaterial coated = MakeCoatedDiffuse(yr::Color3f{0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = yr::Color3f{0.5f, 0.5f, 0.5f};

    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.0f, 1.0f});

    yr::Rng rng_a{7u};
    yr::Rng rng_b{7u};
    const int N = 20000;
    yr::Color3f coated_sum{0, 0, 0};
    yr::Color3f bare_sum{0, 0, 0};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample sc = yr::SampleBsdf(coated, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample sb = yr::SampleBsdf(bare, wo, n, rng_b.NextFloat2(), rng_b);
        if (sc.valid) coated_sum = coated_sum + sc.weight;
        if (sb.valid) bare_sum = bare_sum + sb.weight;
    }
    const float diff = std::fabs(MaxComp(coated_sum) - MaxComp(bare_sum)) / static_cast<float>(N);
    YR_EXPECT_TRUE(diff > 0.01f);
}

YR_TEST(layered_sample_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});

    yr::Rng rng_a{999u};
    yr::Rng rng_b{999u};
    for (int i = 0; i < 50; ++i) {
        const yr::BsdfSample a = yr::SampleBsdf(m, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample b = yr::SampleBsdf(m, wo, n, rng_b.NextFloat2(), rng_b);
        YR_EXPECT_EQ(a.valid, b.valid);
        if (a.valid && b.valid) {
            YR_EXPECT_EQ(a.wi.x, b.wi.x);
            YR_EXPECT_EQ(a.wi.y, b.wi.y);
            YR_EXPECT_EQ(a.wi.z, b.wi.z);
            YR_EXPECT_EQ(a.weight.x, b.weight.x);
        }
    }
}

YR_TEST(layered_sample_directions_are_valid) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.7f, 0.7f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    yr::Rng rng{55u};
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        YR_EXPECT_TRUE(std::isfinite(s.wi.x) && std::isfinite(s.wi.y) && std::isfinite(s.wi.z));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);
        YR_EXPECT_TRUE(s.pdf > 0.0f);
    }
}

static float PdfHemisphereIntegral(const yr::RenderMaterial& m, yr::Vec3f wo,
                                   yr::Vec3f n, unsigned seed, int N) {
    yr::Rng rng{seed};
    double acc = 0.0; const float twoPi = 2.0f * 3.14159265358979f;
    for (int i = 0; i < N; ++i) {
        const yr::Vec2f u = rng.NextFloat2();
        const float z = std::clamp(u.x, 0.0f, 1.0f);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z*z));
        const float phi = twoPi * u.y;
        const yr::Vec3f helper = std::fabs(n.z) < 0.999f ? yr::Vec3f{0,0,1} : yr::Vec3f{1,0,0};
        const yr::Vec3f t = yr::Normalize(yr::Cross(helper, n));
        const yr::Vec3f b = yr::Cross(n, t);
        const yr::Vec3f wi = yr::Normalize(t*(r*std::cos(phi)) + b*(r*std::sin(phi)) + n*z);
        acc += yr::PdfBsdf(m, wo, wi, n, rng);
    }
    return static_cast<float>(acc / N) * twoPi;
}

YR_TEST(layered_pdf_integrates_to_exit_probability) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    const float integral = PdfHemisphereIntegral(m, wo, n, 91u, 40000);
    YR_EXPECT_TRUE(integral >= 0.85f);
    YR_EXPECT_TRUE(integral <= 1.05f);
}

YR_TEST(layered_pdf_is_nonnegative_and_finite) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.6f, 0.6f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    yr::Rng rng{17u};
    for (int i = 0; i < 3000; ++i) {
        const yr::Vec3f wi = yr::Normalize(yr::Vec3f{rng.NextFloat()*2-1, rng.NextFloat()*2-1, rng.NextFloat()*0.5f+0.5f});
        const float p = yr::PdfBsdf(m, wo, wi, n, rng);
        YR_EXPECT_TRUE(std::isfinite(p) && p >= 0.0f);
    }
}

YR_TEST(layered_pdf_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng ra{43u}, rb{43u};
    YR_EXPECT_EQ(yr::PdfBsdf(m, wo, wi, n, ra), yr::PdfBsdf(m, wo, wi, n, rb));
}

YR_TEST(layered_sample_sets_real_pdf_not_proxy) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.7f, 0.7f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{61u};
    int nonspec = 0; double pdf_acc = 0.0;
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (s.valid && !s.specular) { ++nonspec; pdf_acc += s.pdf; YR_EXPECT_TRUE(s.pdf > 0.0f); }
    }
    YR_EXPECT_TRUE(nonspec > 0);
    YR_EXPECT_TRUE(std::fabs(pdf_acc / std::max(1, nonspec) - 1.0) > 1e-3);   // not the 1.0 proxy
}

YR_TEST(layered_mis_partition_is_consistent) {
    // ρ_bsdf (Sample, all lobes) ≈ ρ_light (Eval integral, diffuse lobe only) + F(wo) (specular).
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});

    yr::Rng rs{2024u}; double sb = 0.0; const int N = 40000;
    for (int i = 0; i < N; ++i) { const auto s = yr::SampleBsdf(m, wo, n, rs.NextFloat2(), rs); if (s.valid) sb += MaxComp(s.weight); }
    const float rho_bsdf = static_cast<float>(sb / N);

    const float rho_light = DirectionalAlbedoViaEval(m, wo, n, 2025u, 40000);

    // F(wo): dielectric Fresnel at entry, IOR 1.5 (replicate the formula; do NOT leave 0).
    const float cos_o = yr::Dot(wo, n);
    float F = 0.0f;
    { const float ei = 1.0f, et = 1.5f; const float c = std::clamp(cos_o, -1.0f, 1.0f);
      const float s2t = (ei/et)*(ei/et)*std::max(0.0f, 1.0f - c*c);
      if (s2t < 1.0f) { const float ct = std::sqrt(1.0f - s2t);
        const float rp = ((et*c)-(ei*ct))/((et*c)+(ei*ct));
        const float rs2 = ((ei*c)-(et*ct))/((ei*c)+(et*ct));
        F = 0.5f*(rp*rp + rs2*rs2); } else F = 1.0f; }
    YR_EXPECT_TRUE(F > 0.0f);

    YR_EXPECT_TRUE(std::fabs(rho_bsdf - (rho_light + F)) <= 0.06f);
}

YR_TEST(layered_conductor_sample_is_energy_conserving_and_valid) {
    // Coated conductor (gold-ish f0), zero absorption. The base reflects only
    // its f0 fraction, so mean throughput is < 1 (not white-furnace), but it
    // must never gain energy and must always exit in the upper hemisphere.
    yr::RenderMaterial m = MakeCoatedConductor(yr::Color3f{1.0f, 0.78f, 0.34f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.1f, 1.0f});

    yr::Rng rng{4242u};
    const int N = 20000;
    yr::Color3f sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        YR_EXPECT_TRUE(IsFiniteColor(s.weight));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);
        YR_EXPECT_TRUE(s.pdf > 0.0f);
        sum = sum + s.weight;
    }
    const yr::Color3f mean = sum / static_cast<float>(N);
    YR_EXPECT_TRUE(MaxComp(mean) <= 1.05f);   // no energy gain
}
