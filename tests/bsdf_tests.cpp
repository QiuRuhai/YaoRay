#include "yr_test.hpp"

#include <cmath>

#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

constexpr float Pi = 3.14159265358979323846f;

bool IsBlack(yr::Color3f color) {
    return color.x == 0.0f && color.y == 0.0f && color.z == 0.0f;
}

yr::RenderMaterial DiffuseMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.6f, 0.3f, 0.15f},
        yr::Color3f{}
    };
}

yr::RenderMaterial MirrorMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.9f, 0.8f, 0.7f},
        yr::Color3f{}
    };
}

yr::RenderMaterial PolishedMetalMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Metal,
        yr::Color3f{1.0f, 0.72f, 0.32f},
        yr::Color3f{},
        0.0f,
        0.04f
    };
}

yr::RenderMaterial RoughMetalMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Metal,
        yr::Color3f{1.0f, 0.72f, 0.32f},
        yr::Color3f{},
        0.35f,
        0.04f
    };
}

yr::RenderMaterial PlasticMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Plastic,
        yr::Color3f{0.8f, 0.05f, 0.03f},
        yr::Color3f{},
        0.25f,
        0.04f
    };
}

yr::RenderMaterial SmoothGlassMaterial() {
    yr::RenderMaterial material;
    material.type = yr::MaterialKind::Dielectric;
    material.albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    material.roughness = 0.0f;
    material.ior = 1.5f;
    material.thin = false;
    return material;
}

yr::RenderMaterial TintedSmoothGlassMaterial() {
    yr::RenderMaterial material = SmoothGlassMaterial();
    material.albedo = yr::Color3f{0.8f, 0.9f, 1.0f};
    return material;
}

yr::RenderMaterial UnknownMaterial() {
    return yr::RenderMaterial{
        static_cast<yr::MaterialKind>(999),
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    };
}

} // namespace

YR_TEST(bsdf_diffuse_evaluate_returns_lambertian_brdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.5f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);

    YR_EXPECT_NEAR(value.x, 0.6 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.y, 0.3 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.z, 0.15 / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_evaluate_rejects_below_surface_directions) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_below{0.0f, 0.0f, -1.0f};
    const yr::Vec3f wi_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi_below{0.0f, 0.0f, -1.0f};

    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_below, wi_above, normal)));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_above, wi_below, normal)));
}

YR_TEST(bsdf_diffuse_pdf_uses_cosine_weighted_hemisphere_density) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});

    const float pdf = yr::PdfBsdf(material, wo, wi, normal);

    YR_EXPECT_NEAR(pdf, yr::Dot(normal, wi) / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_sample_returns_albedo_weight_and_positive_pdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_NEAR(sample.weight.x, 0.6, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.15, 1e-6);
}

YR_TEST(bsdf_mirror_sample_reflects_incident_direction) {
    const yr::RenderMaterial material = MirrorMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.0f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 0.9, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.8, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.7, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}

YR_TEST(bsdf_polished_metal_samples_tinted_delta_reflection) {
    const yr::RenderMaterial material = PolishedMetalMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.0f});

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.72, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.32, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal), 0.0, 1e-6);
}

YR_TEST(bsdf_rough_metal_has_finite_non_delta_response) {
    const yr::RenderMaterial material = RoughMetalMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);
    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_TRUE(sample.weight.x >= sample.weight.y);
    YR_EXPECT_TRUE(sample.weight.y >= sample.weight.z);
}

YR_TEST(bsdf_plastic_has_finite_diffuse_and_glossy_response) {
    const yr::RenderMaterial material = PlasticMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);
    const yr::BsdfSample diffuse_sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});
    const yr::BsdfSample specular_sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.75f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > value.y);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
    YR_EXPECT_TRUE(diffuse_sample.valid);
    YR_EXPECT_TRUE(specular_sample.valid);
    YR_EXPECT_TRUE(!diffuse_sample.specular);
    YR_EXPECT_TRUE(!specular_sample.specular);
    YR_EXPECT_TRUE(diffuse_sample.pdf > 0.0f);
    YR_EXPECT_TRUE(specular_sample.pdf > 0.0f);
}

YR_TEST(bsdf_mirror_is_delta_and_has_no_finite_brdf_pdf) {
    const yr::RenderMaterial material = MirrorMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal), 0.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_refracts_at_normal_incidence) {
    const yr::RenderMaterial material = SmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.25f});

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal), 0.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_reflects_when_fresnel_sample_selects_reflection) {
    const yr::RenderMaterial material = TintedSmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.25f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.9, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 1.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_total_internal_reflection_reflects) {
    const yr::RenderMaterial material = SmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, -0.4358899f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.9f, 0.25f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) < 0.0f);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}

YR_TEST(bsdf_unknown_material_fails_closed) {
    const yr::RenderMaterial material = UnknownMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal), 0.0, 1e-6);
    YR_EXPECT_TRUE(!sample.valid);
    YR_EXPECT_NEAR(sample.pdf, 0.0, 1e-6);
}
