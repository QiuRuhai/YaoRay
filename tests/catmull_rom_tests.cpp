#include "yr_test.hpp"
#include <yaoray/render/catmull_rom.hpp>
#include <cmath>

// Helper: evaluate the Catmull-Rom spline at x by combining the basis weights
// with the sample values, mirroring how Slice 2 will look up the profile.
static float EvalSpline(int n, const float* nodes, const float* values, float x) {
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    if (!yr::CatmullRomWeights(n, nodes, x, offset, w)) return 0.0f;
    float acc = 0.0f;
    for (int k = 0; k < 4; ++k) {
        int idx = offset + k;
        if (idx >= 0 && idx < n) acc += w[k] * values[idx];
    }
    return acc;
}

YR_TEST(catmullrom_weights_reproduce_linear) {
    const float nodes[5] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    float values[5];
    for (int i = 0; i < 5; ++i) values[i] = 2.0f + 3.0f * nodes[i];
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(yr::CatmullRomWeights(5, nodes, 2.4f, offset, w));
    YR_EXPECT_NEAR(w[0] + w[1] + w[2] + w[3], 1.0f, 1e-5f);
    YR_EXPECT_NEAR(EvalSpline(5, nodes, values, 2.4f), 2.0f + 3.0f * 2.4f, 1e-4f);
}

YR_TEST(catmullrom_weights_out_of_range) {
    const float nodes[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(!yr::CatmullRomWeights(4, nodes, -0.5f, offset, w));
    YR_EXPECT_TRUE(!yr::CatmullRomWeights(4, nodes, 3.5f, offset, w));
}

YR_TEST(catmullrom_integrate_linear_exact) {
    const float x[5] = {0.0f, 0.5f, 1.5f, 2.0f, 3.0f};  // non-uniform nodes
    float values[5];
    for (int i = 0; i < 5; ++i) values[i] = 1.0f + 2.0f * x[i];  // f(x)=1+2x
    float cdf[5] = {0, 0, 0, 0, 0};
    float total = yr::IntegrateCatmullRom(5, x, values, cdf);
    YR_EXPECT_NEAR(total, 12.0f, 1e-3f);
    YR_EXPECT_NEAR(cdf[0], 0.0f, 1e-6f);
    YR_EXPECT_NEAR(cdf[4], total, 1e-5f);
    for (int i = 1; i < 5; ++i) YR_EXPECT_TRUE(cdf[i] >= cdf[i - 1]);
}

YR_TEST(catmullrom_invert_roundtrip) {
    const float x[6] = {0.0f, 0.4f, 1.0f, 1.7f, 2.5f, 3.0f};
    float values[6];
    for (int i = 0; i < 6; ++i) values[i] = x[i] * x[i];  // monotone increasing on [0,3]
    const float xstar = 1.3f;
    const float vstar = EvalSpline(6, x, values, xstar);
    float recovered = yr::InvertCatmullRom(6, x, values, vstar);
    YR_EXPECT_NEAR(recovered, xstar, 2e-3f);
}

YR_TEST(catmullrom_invert_clamps) {
    const float x[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    const float values[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    YR_EXPECT_NEAR(yr::InvertCatmullRom(4, x, values, -5.0f), 0.0f, 1e-6f);
    YR_EXPECT_NEAR(yr::InvertCatmullRom(4, x, values, 99.0f), 3.0f, 1e-6f);
}
