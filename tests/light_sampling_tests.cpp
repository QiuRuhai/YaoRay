#include "yr_test.hpp"

#include <optional>

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderAreaLight MakeAreaLight() {
    return yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        4.0f,
        2.0f,
        yr::Color3f{3.0f, 2.0f, 1.0f}
    };
}

} // namespace

YR_TEST(light_sampling_samples_current_xz_rectangle_geometry) {
    const yr::RenderAreaLight light = MakeAreaLight();

    const std::optional<yr::AreaLightSample> center = yr::SampleAreaLight(light, yr::Vec2f{0.5f, 0.5f});
    const std::optional<yr::AreaLightSample> corner = yr::SampleAreaLight(light, yr::Vec2f{1.0f, 0.0f});

    YR_EXPECT_TRUE(center.has_value());
    YR_EXPECT_NEAR(center->point.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->point.y, 2.0, 1e-6);
    YR_EXPECT_NEAR(center->point.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.y, -1.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->radiance.x, 3.0, 1e-6);
    YR_EXPECT_NEAR(center->area, 8.0, 1e-6);
    YR_EXPECT_NEAR(center->pdf_area, 0.125, 1e-6);

    YR_EXPECT_TRUE(corner.has_value());
    YR_EXPECT_NEAR(corner->point.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(corner->point.y, 2.0, 1e-6);
    YR_EXPECT_NEAR(corner->point.z, -1.0, 1e-6);
}

YR_TEST(light_sampling_rejects_invalid_area) {
    yr::RenderAreaLight light = MakeAreaLight();
    light.width = 0.0f;

    const std::optional<yr::AreaLightSample> sample = yr::SampleAreaLight(light, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_TRUE(!sample.has_value());
}

YR_TEST(light_sampling_converts_area_pdf_to_solid_angle_pdf) {
    const yr::RenderAreaLight light = MakeAreaLight();
    const float pdf = yr::PdfAreaLightSampleSolidAngle(
        light,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f}
    );

    YR_EXPECT_NEAR(pdf, 0.5, 1e-6);
}

YR_TEST(light_sampling_returns_zero_pdf_for_invalid_solid_angle_cases) {
    const yr::RenderAreaLight light = MakeAreaLight();

    YR_EXPECT_NEAR(
        yr::PdfAreaLightSampleSolidAngle(light, yr::Point3f{0.0f, 3.0f, 0.0f}, yr::Point3f{0.0f, 2.0f, 0.0f}),
        0.0,
        1e-6
    );
    YR_EXPECT_NEAR(
        yr::PdfAreaLightSampleSolidAngle(light, yr::Point3f{0.0f, 2.0f, 0.0f}, yr::Point3f{0.0f, 2.0f, 0.0f}),
        0.0,
        1e-6
    );
}

YR_TEST(light_sampling_sums_scene_light_pdf_for_points_on_area_lights) {
    yr::RenderScene scene;
    scene.area_lights.push_back(MakeAreaLight());

    const float on_light_pdf = yr::PdfAreaLightsForPointSolidAngle(
        scene,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f}
    );
    const float outside_light_pdf = yr::PdfAreaLightsForPointSolidAngle(
        scene,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{3.0f, 2.0f, 0.0f}
    );

    YR_EXPECT_NEAR(on_light_pdf, 0.5, 1e-6);
    YR_EXPECT_NEAR(outside_light_pdf, 0.0, 1e-6);
}
