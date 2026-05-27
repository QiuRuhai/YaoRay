#include "yr_test.hpp"

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
