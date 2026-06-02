#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

constexpr float kInv4Pi = 0.07957747154594767f;

YR_TEST(frdielectric_normal_incidence) {
    YR_EXPECT_NEAR(yr::FrDielectric(1.0f, 1.5f), 0.04f, 1e-3f);
}

YR_TEST(frdielectric_matched_ior) {
    YR_EXPECT_NEAR(yr::FrDielectric(0.7f, 1.0f), 0.0f, 1e-6f);
}

YR_TEST(frdielectric_total_internal_reflection) {
    YR_EXPECT_NEAR(yr::FrDielectric(0.1f, 1.0f / 1.5f), 1.0f, 1e-5f);
}

YR_TEST(henyey_greenstein_isotropic) {
    YR_EXPECT_NEAR(yr::HenyeyGreenstein(0.3f, 0.0f), kInv4Pi, 1e-6f);
    YR_EXPECT_NEAR(yr::HenyeyGreenstein(-0.8f, 0.0f), kInv4Pi, 1e-6f);
}

// pbrt convention: cos_theta = Dot(wo, wi) with both vectors pointing away from
// the scatter point. For forward scattering (g>0) the photon continues forward,
// i.e. wi ~= -wo, so the phase function peaks at cos_theta = -1. Thus
// HenyeyGreenstein(-1, g) must exceed HenyeyGreenstein(+1, g) for g>0. (denom uses
// the faithful pbrt sign: 1 + g^2 + 2*g*cos_theta.)
YR_TEST(henyey_greenstein_forward_bias) {
    float forward_peak = yr::HenyeyGreenstein(-1.0f, 0.5f);
    float back = yr::HenyeyGreenstein(1.0f, 0.5f);
    YR_EXPECT_TRUE(forward_peak > back);
    YR_EXPECT_TRUE(std::isfinite(forward_peak) && std::isfinite(back));
}

YR_TEST(fresnel_moment1_no_interface) {
    YR_EXPECT_NEAR(yr::FresnelMoment1(1.0f), 0.0f, 1e-2f);
}

YR_TEST(fresnel_moments_finite) {
    YR_EXPECT_TRUE(std::isfinite(yr::FresnelMoment1(1.33f)) && yr::FresnelMoment1(1.33f) > 0.0f);
    YR_EXPECT_TRUE(std::isfinite(yr::FresnelMoment2(1.33f)));
}

// Beam diffusion terms are non-negative and finite for a typical skin-like medium.
YR_TEST(beam_diffusion_nonnegative_finite) {
    const float sigma_s = 2.0f, sigma_a = 0.01f, g = 0.0f, eta = 1.33f;
    for (float r : {0.001f, 0.01f, 0.1f, 0.5f, 1.0f}) {
        float ms = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, r);
        float ss = yr::BeamDiffusionSS(sigma_s, sigma_a, g, eta, r);
        YR_EXPECT_TRUE(std::isfinite(ms) && ms >= 0.0f);
        YR_EXPECT_TRUE(std::isfinite(ss) && ss >= 0.0f);
    }
}

// The multiple-scattering fluence decays with radius (more spreading = less return
// far away). Compare a near and a far radius.
YR_TEST(beam_diffusion_ms_decays_with_radius) {
    const float sigma_s = 2.0f, sigma_a = 0.01f, g = 0.0f, eta = 1.33f;
    float near_r = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, 0.02f);
    float far_r = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, 0.8f);
    YR_EXPECT_TRUE(near_r > far_r);
}

// More absorption reduces the multiple-scattering response at a fixed radius.
YR_TEST(beam_diffusion_ms_absorption_reduces) {
    const float sigma_s = 2.0f, g = 0.0f, eta = 1.33f, r = 0.1f;
    float low_abs = yr::BeamDiffusionMS(sigma_s, 0.01f, g, eta, r);
    float high_abs = yr::BeamDiffusionMS(sigma_s, 0.5f, g, eta, r);
    YR_EXPECT_TRUE(low_abs > high_abs);
}

// The table builds without NaNs: profile finite & non-negative, CDFs monotone and
// starting at 0, rho_eff finite in [0,1.05].
YR_TEST(bssrdf_table_well_formed) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(/*g=*/0.0f, /*eta=*/1.33f, table);

    YR_EXPECT_EQ((int)table.profile.size(), 100 * 64);
    YR_EXPECT_EQ((int)table.rho_eff.size(), 100);

    for (float v : table.profile) YR_EXPECT_TRUE(std::isfinite(v) && v >= 0.0f);

    for (int i = 0; i < table.n_rho; ++i) {
        YR_EXPECT_TRUE(std::isfinite(table.rho_eff[i]));
        YR_EXPECT_TRUE(table.rho_eff[i] >= 0.0f && table.rho_eff[i] <= 1.05f);
        const float* cdf = &table.profile_cdf[i * table.n_radius];
        YR_EXPECT_NEAR(cdf[0], 0.0f, 1e-6f);
        for (int j = 1; j < table.n_radius; ++j) YR_EXPECT_TRUE(cdf[j] >= cdf[j - 1]);
    }
}

// Effective albedo increases monotonically with the single-scattering albedo.
YR_TEST(bssrdf_table_rho_eff_monotonic) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);

    YR_EXPECT_NEAR(table.rho_eff.front(), 0.0f, 1e-2f);
    YR_EXPECT_TRUE(table.rho_eff.back() > table.rho_eff.front());
    for (int i = 1; i < table.n_rho; ++i)
        YR_EXPECT_TRUE(table.rho_eff[i] >= table.rho_eff[i - 1] - 1e-4f);
}

// First radius node is 0 and radii increase geometrically (faithful discretization).
YR_TEST(bssrdf_table_radius_discretization) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
    YR_EXPECT_NEAR(table.radius_samples[0], 0.0f, 1e-9f);
    YR_EXPECT_NEAR(table.radius_samples[1], 2.5e-3f, 1e-9f);
    for (int j = 2; j < table.n_radius; ++j)
        YR_EXPECT_TRUE(table.radius_samples[j] > table.radius_samples[j - 1]);
}

// Determinism: two independent builds with identical params are bit-for-bit equal.
YR_TEST(bssrdf_table_deterministic) {
    yr::BSSRDFTable a(50, 32), b(50, 32);
    yr::ComputeBeamDiffusionBSSRDF(0.2f, 1.4f, a);
    yr::ComputeBeamDiffusionBSSRDF(0.2f, 1.4f, b);
    for (size_t i = 0; i < a.profile.size(); ++i) YR_EXPECT_EQ(a.profile[i], b.profile[i]);
    for (size_t i = 0; i < a.rho_eff.size(); ++i) YR_EXPECT_EQ(a.rho_eff[i], b.rho_eff[i]);
}
