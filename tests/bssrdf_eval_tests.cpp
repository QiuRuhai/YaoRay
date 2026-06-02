#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <yaoray/render/catmull_rom.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

static const yr::BSSRDFTable& SkinTable() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

YR_TEST(bssrdf_sr_nonnegative_and_decays) {
    yr::TabulatedBSSRDF s({0.0011f, 0.0024f, 0.014f}, {2.55f, 3.21f, 3.77f}, 1.33f, SkinTable());
    yr::Color3f near_r = s.Sr(0.005f);
    yr::Color3f far_r = s.Sr(0.5f);
    YR_EXPECT_TRUE(std::isfinite(near_r.x) && near_r.x >= 0.0f);
    YR_EXPECT_TRUE(std::isfinite(far_r.x) && far_r.x >= 0.0f);
    YR_EXPECT_TRUE(near_r.x > far_r.x);
}

YR_TEST(bssrdf_sw_normalized) {
    yr::TabulatedBSSRDF s({0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    const int N = 100000;
    double acc = 0.0;
    for (int i = 0; i < N; ++i) {
        double theta = (i + 0.5) / N * (kPi / 2.0);
        double cos_t = std::cos(theta), sin_t = std::sin(theta);
        acc += s.Sw((float)cos_t) * cos_t * sin_t;
    }
    double integral = 2.0 * kPi * acc * (kPi / 2.0) / N;
    YR_EXPECT_NEAR((float)integral, 1.0f, 2e-2f);
}

YR_TEST(bssrdf_sr_integrates_to_rho_eff) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    const yr::BSSRDFTable& t = SkinTable();

    int off; float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(yr::CatmullRomWeights(t.n_rho, t.rho_samples.data(), s.rho.x, off, w));
    float rho_eff = 0;
    for (int k = 0; k < 4; ++k) {
        int idx = off + k;
        if (idx >= 0 && idx < t.n_rho) rho_eff += w[k] * t.rho_eff[idx];
    }

    double r_max = (double)t.radius_samples[t.n_radius - 1] / s.sigma_t.x;
    const int M = 20000;
    double acc = 0.0;
    for (int i = 0; i < M; ++i) {
        double r = (i + 0.5) / M * r_max;
        acc += s.Sr((float)r).x * 2.0 * kPi * r;
    }
    double integral = acc * r_max / M;
    YR_EXPECT_NEAR((float)integral, rho_eff, 3e-2f);
}

YR_TEST(bssrdf_s_combines_terms) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    float cos_o = 0.8f, cos_i = 0.6f, r = 0.05f;
    yr::Color3f full = s.S(cos_o, r, cos_i);
    float ft = 1.0f - yr::FrDielectric(cos_o, 1.33f);
    float expected_x = ft * s.Sp(r).x * s.Sw(cos_i);
    YR_EXPECT_NEAR(full.x, expected_x, 1e-6f * expected_x + 1e-9f);
    YR_EXPECT_TRUE(full.x >= 0.0f && std::isfinite(full.x));
}
