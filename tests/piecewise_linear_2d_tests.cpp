#include "yr_test.hpp"
#include <yaoray/render/piecewise_linear_2d.hpp>
#include <cmath>

// Convention note: ported faithfully from pbrt-v4's PiecewiseLinear2D.
// Evaluate(pos, ...) takes pos in [0,1]^2 and internally scales by
// m_inv_patch_size = (xSize-1, ySize-1). The result is multiplied by
// HProd(m_inv_patch_size) = (xSize-1)*(ySize-1) (the patch-density Jacobian).
// For a 2x2 grid, m_inv_patch_size = (1,1), so HProd == 1 and Evaluate returns
// the raw bilinearly-interpolated value (no scaling). Tests below rely on that.

YR_TEST(plinear2d_eval_constant_grid) {
    const float data[4] = {1, 1, 1, 1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, /*normalize=*/false, /*build_cdf=*/false);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.25f, 0.75f}), 1.0f, 1e-5f);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.0f, 0.0f}), 1.0f, 1e-5f);
}

YR_TEST(plinear2d_eval_bilinear_ramp) {
    const float data[4] = {0, 1, 0, 1};  // x-ramp (row-major: row0={0,1}, row1={0,1})
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, false, false);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.5f, 0.5f}), 0.5f, 1e-5f);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{1.0f, 0.5f}), 1.0f, 1e-4f);
}

YR_TEST(plinear2d_invert_returns_valid) {
    const float data[4] = {1, 1, 1, 1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, true, true);
    auto s = d.Invert(yr::Vec2f{0.3f, 0.6f});
    YR_EXPECT_TRUE(s.p.x >= -1e-4f && s.p.x <= 1.0001f);
    YR_EXPECT_TRUE(s.p.y >= -1e-4f && s.p.y <= 1.0001f);
    YR_EXPECT_TRUE(s.pdf >= 0.0f && std::isfinite(s.pdf));
}

// Sample() is the inverse of Invert(). On a uniform grid the warp is the
// identity, so Sample(u).p == u and pdf == 1.
YR_TEST(plinear2d_sample_uniform_is_identity) {
    const float data[4] = {1, 1, 1, 1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, true, true);
    auto s = d.Sample(yr::Vec2f{0.3f, 0.7f});
    YR_EXPECT_NEAR(s.p.x, 0.3f, 1e-3f);
    YR_EXPECT_NEAR(s.p.y, 0.7f, 1e-3f);
    YR_EXPECT_NEAR(s.pdf, 1.0f, 1e-3f);
}

// Sample and Invert are exact inverses: Invert(Sample(u).p).p ~= u with a
// matching pdf, on a non-uniform grid.
YR_TEST(plinear2d_sample_invert_roundtrip) {
    const float data[9] = {0.2f, 0.5f, 0.9f, 0.3f, 0.7f, 1.0f, 0.1f, 0.4f, 0.8f};
    yr::PiecewiseLinear2D<0> d(data, 3, 3, {}, {}, true, true);
    const yr::Vec2f u{0.42f, 0.63f};
    auto s = d.Sample(u);
    auto inv = d.Invert(s.p);
    YR_EXPECT_NEAR(inv.p.x, u.x, 2e-3f);
    YR_EXPECT_NEAR(inv.p.y, u.y, 2e-3f);
    YR_EXPECT_NEAR(inv.pdf, s.pdf, 1e-3f * s.pdf + 1e-4f);
    YR_EXPECT_TRUE(s.pdf > 0.0f && std::isfinite(s.pdf));
}
