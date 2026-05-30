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
