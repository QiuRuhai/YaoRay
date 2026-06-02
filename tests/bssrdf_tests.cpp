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
