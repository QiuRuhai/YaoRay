#include "yr_test.hpp"

#include <cmath>

#include <yaoray/core/rng.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Expand BSDF tests for PBRT material model (CoatedDiffuse, CoatedConductor, Mix, etc.).

namespace {

constexpr float Pi = 3.14159265358979323846f;

bool IsBlack(yr::Color3f color) {
    return color.x == 0.0f && color.y == 0.0f && color.z == 0.0f;
}

yr::RenderMaterial DiffuseMaterial() {
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Diffuse;
    mat.reflectance = yr::TexParam3f{{0.6f, 0.3f, 0.15f}};
    return mat;
}

yr::RenderMaterial ConductorMaterial(float roughness = 0.0f) {
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Conductor;
    mat.reflectance = yr::TexParam3f{{1.0f, 0.72f, 0.32f}};
    mat.uroughness = yr::TexParam1f{roughness};
    mat.vroughness = yr::TexParam1f{roughness};
    return mat;
}

yr::RenderMaterial DielectricMaterial(float roughness = 0.0f) {
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Dielectric;
    mat.reflectance = yr::TexParam3f{{1.0f, 1.0f, 1.0f}};
    mat.ior = 1.5f;
    mat.uroughness = yr::TexParam1f{roughness};
    mat.vroughness = yr::TexParam1f{roughness};
    return mat;
}

yr::RenderMaterial ThinDielectricMaterial(float roughness = 0.0f) {
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::ThinDielectric;
    mat.reflectance = yr::TexParam3f{{1.0f, 1.0f, 1.0f}};
    mat.ior = 1.5f;
    mat.uroughness = yr::TexParam1f{roughness};
    mat.vroughness = yr::TexParam1f{roughness};
    return mat;
}

yr::RenderMaterial UnknownMaterial() {
    yr::RenderMaterial mat;
    mat.kind = static_cast<yr::RenderMaterialKind>(999);
    mat.reflectance = yr::TexParam3f{{1.0f, 1.0f, 1.0f}};
    return mat;
}

} // namespace

YR_TEST(bsdf_diffuse_evaluate_returns_lambertian_brdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
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
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_below{0.0f, 0.0f, -1.0f};
    const yr::Vec3f wi_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi_below{0.0f, 0.0f, -1.0f};
    yr::Rng rng{1u};

    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_below, wi_above, normal, rng)));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_above, wi_below, normal, rng)));
}

YR_TEST(bsdf_diffuse_pdf_uses_cosine_weighted_hemisphere_density) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});
    yr::Rng rng{1u};

    const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_NEAR(pdf, yr::Dot(normal, wi) / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_sample_returns_albedo_weight_and_positive_pdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f}, rng);

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_NEAR(sample.weight.x, 0.6, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.15, 1e-6);
}

YR_TEST(bsdf_polished_conductor_samples_delta_reflection) {
    const yr::RenderMaterial material = ConductorMaterial(0.0f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.0f}, rng);

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
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal, rng), 0.0, 1e-6);
}

YR_TEST(bsdf_rough_conductor_has_finite_non_delta_response) {
    const yr::RenderMaterial material = ConductorMaterial(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);
    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f}, rng);

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

YR_TEST(bsdf_smooth_conductor_is_delta_and_has_no_finite_brdf_pdf) {
    const yr::RenderMaterial material = ConductorMaterial(0.0f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal, rng), 0.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_refracts_at_normal_incidence) {
    const yr::RenderMaterial material = DielectricMaterial(0.0f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.25f}, rng);

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
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal, rng), 0.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_reflects_when_fresnel_sample_selects_reflection) {
    yr::RenderMaterial material = DielectricMaterial(0.0f);
    material.reflectance = yr::TexParam3f{{0.8f, 0.9f, 1.0f}};
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.25f}, rng);

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
    const yr::RenderMaterial material = DielectricMaterial(0.0f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, -0.4358899f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.9f, 0.25f}, rng);

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) < 0.0f);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}

YR_TEST(bsdf_rough_dielectric_has_finite_reflection_response) {
    const yr::RenderMaterial material = DielectricMaterial(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
}

YR_TEST(bsdf_rough_dielectric_has_finite_transmission_response) {
    const yr::RenderMaterial material = DielectricMaterial(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, -0.05f, -1.0f});
    yr::Rng rng{1u};

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);

    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
}

YR_TEST(bsdf_rough_dielectric_samples_reflection_and_transmission) {
    const yr::RenderMaterial material = DielectricMaterial(0.35f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.2f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample reflection = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.35f}, rng);
    const yr::BsdfSample transmission = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.95f, 0.35f}, rng);

    YR_EXPECT_TRUE(reflection.valid);
    YR_EXPECT_TRUE(transmission.valid);
    YR_EXPECT_TRUE(!reflection.specular);
    YR_EXPECT_TRUE(!transmission.specular);
    YR_EXPECT_TRUE(yr::Dot(reflection.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(yr::Dot(transmission.wi, normal) < 0.0f);
    YR_EXPECT_TRUE(reflection.pdf > 0.0f);
    YR_EXPECT_TRUE(transmission.pdf > 0.0f);
    YR_EXPECT_TRUE(reflection.weight.x > 0.0f);
    YR_EXPECT_TRUE(transmission.weight.x > 0.0f);
}

YR_TEST(bsdf_thin_dielectric_transmits_straight_through) {
    const yr::RenderMaterial material = ThinDielectricMaterial(0.0f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.8f, 0.5f}, rng);

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, -wo.x, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, -wo.y, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -wo.z, 1e-6);
}

YR_TEST(bsdf_rough_thin_dielectric_is_non_delta_and_finite) {
    const yr::RenderMaterial material = ThinDielectricMaterial(0.3f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.1f, 0.0f, 1.0f});
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.95f, 0.4f}, rng);

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_TRUE(sample.weight.x > 0.0f);
}

YR_TEST(bsdf_unknown_material_fails_closed) {
    const yr::RenderMaterial material = UnknownMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};
    yr::Rng rng{1u};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.5f}, rng);

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal, rng)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal, rng), 0.0, 1e-6);
    YR_EXPECT_TRUE(!sample.valid);
    YR_EXPECT_NEAR(sample.pdf, 0.0, 1e-6);
}
