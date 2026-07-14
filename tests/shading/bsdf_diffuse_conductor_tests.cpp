#include "yr_test.hpp"

#include "bsdf_test_materials.hpp"

#include <yaoray/core/rng.hpp>
#include <yaoray/shading/bsdf.hpp>

namespace fixture = yrtest::bsdf;

constexpr float Pi = 3.14159265358979323846f;

YR_TEST(bsdf_diffuse_evaluate_returns_lambertian_brdf) {
    const yr::RenderMaterial material = fixture::Diffuse();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.5f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_NEAR(value.x, 0.6 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.y, 0.3 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.z, 0.15 / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_evaluate_rejects_below_surface_directions) {
    const yr::RenderMaterial material = fixture::Diffuse();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    YR_EXPECT_TRUE(fixture::IsBlack(yr::EvaluateBsdf(
        material, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, normal, rng
    )));
    YR_EXPECT_TRUE(fixture::IsBlack(yr::EvaluateBsdf(
        material, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, normal, rng
    )));
}

YR_TEST(bsdf_diffuse_pdf_uses_cosine_weighted_hemisphere_density) {
    const yr::RenderMaterial material = fixture::Diffuse();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});
    yr::Rng rng{1u};

    const float pdf = yr::PdfBsdf(material, normal, wi, normal, rng);

    YR_EXPECT_NEAR(pdf, yr::Dot(normal, wi) / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_sample_returns_albedo_weight_and_positive_pdf) {
    const yr::RenderMaterial material = fixture::Diffuse();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, normal, normal, yr::Vec2f{0.25f, 0.5f}, rng
    );

    YR_EXPECT_TRUE(sample.valid && !sample.specular);
    YR_EXPECT_GT(yr::Dot(sample.wi, normal), 0.0f);
    YR_EXPECT_GT(sample.pdf, 0.0f);
    YR_EXPECT_NEAR(sample.weight.x, 0.6, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.15, 1e-6);
}

YR_TEST(bsdf_polished_conductor_samples_delta_reflection) {
    const yr::RenderMaterial material = fixture::Conductor();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{}, rng
    );

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid && sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.72, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.32, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(fixture::IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal, rng), 0.0, 1e-6);
}

YR_TEST(bsdf_rough_conductor_has_finite_non_delta_response) {
    const yr::RenderMaterial material = fixture::Conductor(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);
    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.25f, 0.5f}, rng
    );

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_GT(value.z, 0.0f);
    YR_EXPECT_TRUE(sample.valid && !sample.specular);
    YR_EXPECT_GT(sample.pdf, 0.0f);
    YR_EXPECT_TRUE(sample.weight.x >= sample.weight.y && sample.weight.y >= sample.weight.z);
}

YR_TEST(bsdf_smooth_conductor_is_delta_and_has_no_finite_brdf_pdf) {
    const yr::RenderMaterial material = fixture::Conductor();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(fixture::IsBlack(yr::EvaluateBsdf(material, normal, normal, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, normal, normal, normal, rng), 0.0, 1e-6);
}
