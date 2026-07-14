#include "yr_test.hpp"

#include "layered_bsdf_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace fixture = yrtest::layered;

YR_TEST(layered_pdf_integrates_to_exit_probability) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({1.0f, 1.0f, 1.0f});
    const float integral = fixture::PdfHemisphereIntegral(
        material,
        yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f}),
        fixture::Up(),
        91u,
        40000
    );
    YR_EXPECT_TRUE(integral >= 0.85f && integral <= 1.05f);
}

YR_TEST(layered_pdf_is_nonnegative_and_finite) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.6f, 0.6f, 0.6f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    yr::Rng rng{17u};
    for (int i = 0; i < 3000; ++i) {
        const yr::Vec3f wi = fixture::UniformHemisphereDirection(normal, rng.NextFloat2());
        const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);
        YR_EXPECT_TRUE(std::isfinite(pdf));
        YR_EXPECT_TRUE(pdf >= 0.0f);
    }
}

YR_TEST(layered_pdf_is_deterministic_under_fixed_seed) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.6f, 0.4f, 0.2f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng a_rng{43u};
    yr::Rng b_rng{43u};
    YR_EXPECT_EQ(
        yr::PdfBsdf(material, wo, wi, normal, a_rng),
        yr::PdfBsdf(material, wo, wi, normal, b_rng)
    );
}

YR_TEST(layered_mis_partition_is_consistent) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({1.0f, 1.0f, 1.0f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{2024u};
    double sample_sum = 0.0;
    constexpr int SampleCount = 40000;
    for (int i = 0; i < SampleCount; ++i) {
        const yr::BsdfSample sample = yr::SampleBsdf(
            material, wo, normal, rng.NextFloat2(), rng
        );
        if (sample.valid) sample_sum += fixture::MaxComponent(sample.weight);
    }
    const float rho_bsdf = static_cast<float>(sample_sum / SampleCount);
    const float rho_light = fixture::DirectionalAlbedoViaEval(
        material, wo, normal, 2025u, SampleCount
    );

    const float cos_o = yr::Dot(wo, normal);
    const float eta = 1.0f / 1.5f;
    const float sin2_t = eta * eta * std::max(0.0f, 1.0f - cos_o * cos_o);
    const float cos_t = std::sqrt(1.0f - sin2_t);
    const float r_parallel = (1.5f * cos_o - cos_t) / (1.5f * cos_o + cos_t);
    const float r_perpendicular = (cos_o - 1.5f * cos_t) / (cos_o + 1.5f * cos_t);
    const float fresnel = 0.5f * (
        r_parallel * r_parallel + r_perpendicular * r_perpendicular
    );

    YR_EXPECT_GT(fresnel, 0.0f);
    YR_EXPECT_TRUE(std::fabs(rho_bsdf - (rho_light + fresnel)) <= 0.06f);
}
