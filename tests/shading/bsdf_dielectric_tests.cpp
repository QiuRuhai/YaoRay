#include "yr_test.hpp"

#include "bsdf_test_materials.hpp"

#include <yaoray/core/rng.hpp>
#include <yaoray/shading/bsdf.hpp>

namespace fixture = yrtest::bsdf;

YR_TEST(bsdf_smooth_dielectric_refracts_at_normal_incidence) {
    const yr::RenderMaterial material = fixture::Dielectric();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, normal, normal, yr::Vec2f{0.5f, 0.25f}, rng
    );

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid && sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(fixture::IsBlack(
        yr::EvaluateBsdf(material, normal, sample.wi, normal, rng)
    ));
}

YR_TEST(bsdf_smooth_dielectric_reflects_when_fresnel_selects_reflection) {
    yr::RenderMaterial material = fixture::Dielectric();
    material.reflectance = yr::TexParam3f{{0.8f, 0.9f, 1.0f}};
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.0f, 0.25f}, rng
    );

    YR_EXPECT_TRUE(sample.valid && sample.specular);
    YR_EXPECT_GT(yr::Dot(sample.wi, normal), 0.0f);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.9, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 1.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_total_internal_reflection_reflects) {
    const yr::RenderMaterial material = fixture::Dielectric();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, -0.4358899f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.9f, 0.25f}, rng
    );

    YR_EXPECT_TRUE(sample.valid && sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) < 0.0f);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}

YR_TEST(bsdf_rough_dielectric_has_finite_reflection_response) {
    const yr::RenderMaterial material = fixture::Dielectric(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_GT(value.z, 0.0f);
    YR_EXPECT_GT(yr::PdfBsdf(material, wo, wi, normal, rng), 0.0f);
}

YR_TEST(bsdf_rough_dielectric_has_finite_transmission_response) {
    const yr::RenderMaterial material = fixture::Dielectric(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, -0.05f, -1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_GT(value.z, 0.0f);
    YR_EXPECT_GT(yr::PdfBsdf(material, wo, wi, normal, rng), 0.0f);
}

YR_TEST(bsdf_rough_dielectric_samples_reflection_and_transmission) {
    const yr::RenderMaterial material = fixture::Dielectric(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.2f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample reflection = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.0f, 0.35f}, rng
    );
    const yr::BsdfSample transmission = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.95f, 0.35f}, rng
    );

    YR_EXPECT_TRUE(reflection.valid && transmission.valid);
    YR_EXPECT_TRUE(!reflection.specular && !transmission.specular);
    YR_EXPECT_GT(yr::Dot(reflection.wi, normal), 0.0f);
    YR_EXPECT_TRUE(yr::Dot(transmission.wi, normal) < 0.0f);
    YR_EXPECT_GT(reflection.pdf, 0.0f);
    YR_EXPECT_GT(transmission.pdf, 0.0f);
}

YR_TEST(bsdf_thin_dielectric_transmits_straight_through) {
    const yr::RenderMaterial material = fixture::ThinDielectric();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.8f, 0.5f}, rng
    );

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid && sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, -wo.x, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, -wo.y, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -wo.z, 1e-6);
}

YR_TEST(bsdf_rough_thin_dielectric_is_non_delta_and_finite) {
    const yr::RenderMaterial material = fixture::ThinDielectric(0.3f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.1f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, wo, normal, yr::Vec2f{0.95f, 0.4f}, rng
    );

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid && !sample.specular);
    YR_EXPECT_GT(sample.pdf, 0.0f);
    YR_EXPECT_GT(sample.weight.x, 0.0f);
}

YR_TEST(bsdf_unknown_material_fails_closed) {
    yr::RenderMaterial material;
    material.kind = static_cast<yr::RenderMaterialKind>(999);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(
        material, normal, normal, yr::Vec2f{0.5f, 0.5f}, rng
    );

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(fixture::IsBlack(
        yr::EvaluateBsdf(material, normal, normal, normal, rng)
    ));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, normal, normal, normal, rng), 0.0, 1e-6);
    YR_EXPECT_TRUE(!sample.valid);
}
