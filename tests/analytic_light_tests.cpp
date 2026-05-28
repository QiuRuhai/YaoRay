#include "yr_test.hpp"

#include <cmath>

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

YR_TEST(sample_analytic_point_returns_direction_and_inverse_square_radiance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{0.0f, 2.0f, 0.0f};
    light.intensity = yr::Color3f{10.0f, 10.0f, 10.0f};

    const yr::AnalyticLightSample s = yr::SampleAnalyticPoint(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.distance, 2.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.y, 1.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.z, 0.0f, 1.0e-5);
    // Radiance = intensity / distance² = 10 / 4 = 2.5.
    YR_EXPECT_NEAR(s.radiance.x, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.y, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.z, 2.5f, 1.0e-5);
}

YR_TEST(sample_analytic_point_returns_invalid_when_zero_distance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.intensity = yr::Color3f{1.0f, 1.0f, 1.0f};
    const yr::AnalyticLightSample s = yr::SampleAnalyticPoint(light, yr::Point3f{1.0f, 2.0f, 3.0f});
    YR_EXPECT_TRUE(!s.valid);
}

YR_TEST(sample_analytic_distant_returns_normalized_wi_toward_light_and_infinite_distance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    // Light shines DOWN the +Y axis (from above to below); wi at any point
    // should point UP toward the light, i.e. +Y.
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{2.0f, 3.0f, 4.0f};

    const yr::AnalyticLightSample s = yr::SampleAnalyticDistant(light, yr::Point3f{1.0f, 2.0f, 3.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.wi.x, 0.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.wi.y, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.wi.z, 0.0f, 1.0e-6);
    YR_EXPECT_TRUE(s.distance > 1.0e5f);
    YR_EXPECT_NEAR(s.radiance.x, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.radiance.y, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.radiance.z, 4.0f, 1.0e-6);
}

YR_TEST(sample_analytic_distant_is_independent_of_shading_point) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::AnalyticLightSample s_a = yr::SampleAnalyticDistant(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    const yr::AnalyticLightSample s_b = yr::SampleAnalyticDistant(light, yr::Point3f{10.0f, 20.0f, 30.0f});
    YR_EXPECT_NEAR(s_a.wi.x, s_b.wi.x, 1.0e-6);
    YR_EXPECT_NEAR(s_a.wi.y, s_b.wi.y, 1.0e-6);
    YR_EXPECT_NEAR(s_a.wi.z, s_b.wi.z, 1.0e-6);
}

YR_TEST(sample_analytic_spot_returns_full_intensity_inside_inner_cone) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};   // shining straight down
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);              // cos(30°)
    light.cone_cos_inner = std::cos(0.4363f);          // cos(25°)

    // Shading point directly below the light.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.distance, 1.0f, 1.0e-5);
    // Inside the inner cone — full intensity / distance² = 4.0.
    YR_EXPECT_NEAR(s.radiance.x, 4.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.y, 4.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.z, 4.0f, 1.0e-5);
}

YR_TEST(sample_analytic_spot_returns_zero_outside_outer_cone) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);   // 30°
    light.cone_cos_inner = std::cos(0.4363f); // 25°

    // Shading point off to the side, well outside the 30° cone.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{2.0f, 0.0f, 0.0f});
    // Either the sample is invalid (because radiance is zero) or radiance is exactly zero.
    if (s.valid) {
        YR_EXPECT_NEAR(s.radiance.x, 0.0f, 1.0e-5);
    }
}

YR_TEST(sample_analytic_spot_falloff_band_returns_intermediate_value) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);     // 30°
    light.cone_cos_inner = std::cos(0.4363f); // 25°

    // Shading point in the falloff band — ~27.5° off the cone axis.
    // tan(27.5°) ≈ 0.521; with y=1 the horizontal offset is 0.521.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{0.521f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    // Should be strictly between 0 and the full-intensity peak.
    YR_EXPECT_TRUE(s.radiance.x > 0.0f);
    YR_EXPECT_TRUE(s.radiance.x < 4.0f);
}
