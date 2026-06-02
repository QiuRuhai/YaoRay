#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

YR_TEST(bssrdf_sample_sr_valid) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    for (float u : {0.1f, 0.4f, 0.7f, 0.95f}) {
        float r = s.Sample_Sr(0, u);
        YR_EXPECT_TRUE(r >= 0.0f && std::isfinite(r));
        YR_EXPECT_TRUE(s.Pdf_Sr(0, r) > 0.0f && std::isfinite(s.Pdf_Sr(0, r)));
    }
}

YR_TEST(bssrdf_sample_sr_nonscattering_channel) {
    yr::TabulatedBSSRDF s({0.0f, 0.02f, 0.02f}, {0.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    YR_EXPECT_NEAR(s.Sample_Sr(0, 0.5f), -1.0f, 1e-6f);
    YR_EXPECT_TRUE(s.Sample_Sr(1, 0.5f) >= 0.0f);
}

YR_TEST(bssrdf_pdf_sr_normalized) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    const yr::BSSRDFTable& t = Tbl();
    double r_max = (double)t.radius_samples[t.n_radius - 1] / s.sigma_t.x;
    const int M = 20000;
    double acc = 0.0;
    for (int i = 0; i < M; ++i) {
        double r = (i + 0.5) / M * r_max;
        acc += s.Pdf_Sr(0, (float)r) * 2.0 * kPi * r;
    }
    double integral = acc * r_max / M;
    YR_EXPECT_NEAR((float)integral, 1.0f, 3e-2f);
}

YR_TEST(bssrdf_sample_sr_distribution_sane) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    const int N = 4000;
    double sum_r = 0.0;
    int valid = 0;
    for (int i = 0; i < N; ++i) {
        float u = (i + 0.5f) / N;
        float r = s.Sample_Sr(0, u);
        if (r >= 0.0f) { sum_r += r; ++valid; }
    }
    YR_EXPECT_TRUE(valid == N);
    double mean_r = sum_r / valid;
    YR_EXPECT_TRUE(mean_r > 0.0 && std::isfinite(mean_r));
}
