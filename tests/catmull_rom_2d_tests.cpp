#include "yr_test.hpp"
#include <yaoray/render/catmull_rom.hpp>
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

static const yr::BSSRDFTable& Table() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

YR_TEST(sample_catmullrom2d_in_range) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];
    for (float u : {0.05f, 0.25f, 0.5f, 0.75f, 0.95f}) {
        float pdf = -1.0f;
        float x = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                         t.radius_samples.data(), t.profile.data(),
                                         t.profile_cdf.data(), alpha, u, nullptr, &pdf);
        YR_EXPECT_TRUE(x >= t.radius_samples[0] - 1e-4f);
        YR_EXPECT_TRUE(x <= t.radius_samples[t.n_radius - 1] + 1e-3f);
        YR_EXPECT_TRUE(std::isfinite(pdf) && pdf >= 0.0f);
    }
}

YR_TEST(sample_catmullrom2d_monotonic_in_u) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];
    float prev = -1.0f;
    for (int i = 1; i < 20; ++i) {
        float u = i / 20.0f;
        float x = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                         t.radius_samples.data(), t.profile.data(),
                                         t.profile_cdf.data(), alpha, u);
        YR_EXPECT_TRUE(x >= prev - 1e-4f);
        prev = x;
    }
}

YR_TEST(sample_catmullrom2d_endpoints) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];
    float lo = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                      t.radius_samples.data(), t.profile.data(),
                                      t.profile_cdf.data(), alpha, 1e-4f);
    float hi = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                      t.radius_samples.data(), t.profile.data(),
                                      t.profile_cdf.data(), alpha, 0.9999f);
    YR_EXPECT_TRUE(lo < hi);
    YR_EXPECT_NEAR(lo, t.radius_samples[0], 5e-2f);
}
