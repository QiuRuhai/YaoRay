#include "yr_test.hpp"

#include "layered_bsdf_test_support.hpp"

#include <cmath>

namespace fixture = yrtest::layered;

YR_TEST(layered_sample_is_energy_conserving_white_furnace) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({1.0f, 1.0f, 1.0f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{2024u};
    yr::Color3f sum;
    constexpr int SampleCount = 20000;
    for (int i = 0; i < SampleCount; ++i) {
        const yr::BsdfSample sample = yr::SampleBsdf(
            material, wo, normal, rng.NextFloat2(), rng
        );
        if (!sample.valid) continue;
        YR_EXPECT_TRUE(fixture::IsFiniteColor(sample.weight));
        sum = sum + sample.weight;
    }
    const yr::Color3f mean = sum / static_cast<float>(SampleCount);
    YR_EXPECT_LE(fixture::MaxComponent(mean), 1.05f);
    YR_EXPECT_GT(fixture::MinComponent(mean), 0.90f);
}

YR_TEST(layered_sample_differs_from_bare_base) {
    const yr::RenderMaterial coated = fixture::MakeCoatedDiffuse({0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = {0.5f, 0.5f, 0.5f};
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    yr::Rng coated_rng{7u};
    yr::Rng bare_rng{7u};
    yr::Color3f coated_sum;
    yr::Color3f bare_sum;
    constexpr int SampleCount = 20000;
    for (int i = 0; i < SampleCount; ++i) {
        const yr::BsdfSample a = yr::SampleBsdf(
            coated, wo, normal, coated_rng.NextFloat2(), coated_rng
        );
        const yr::BsdfSample b = yr::SampleBsdf(
            bare, wo, normal, bare_rng.NextFloat2(), bare_rng
        );
        if (a.valid) coated_sum = coated_sum + a.weight;
        if (b.valid) bare_sum = bare_sum + b.weight;
    }
    const float difference = std::fabs(
        fixture::MaxComponent(coated_sum) - fixture::MaxComponent(bare_sum)
    ) / SampleCount;
    YR_EXPECT_GT(difference, 0.01f);
}

YR_TEST(layered_sample_is_deterministic_under_fixed_seed) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.6f, 0.4f, 0.2f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    yr::Rng a_rng{999u};
    yr::Rng b_rng{999u};
    for (int i = 0; i < 50; ++i) {
        const yr::BsdfSample a = yr::SampleBsdf(
            material, wo, normal, a_rng.NextFloat2(), a_rng
        );
        const yr::BsdfSample b = yr::SampleBsdf(
            material, wo, normal, b_rng.NextFloat2(), b_rng
        );
        YR_EXPECT_EQ(a.valid, b.valid);
        if (!a.valid) continue;
        YR_EXPECT_EQ(a.wi.x, b.wi.x);
        YR_EXPECT_EQ(a.wi.y, b.wi.y);
        YR_EXPECT_EQ(a.wi.z, b.wi.z);
        YR_EXPECT_EQ(a.weight.x, b.weight.x);
    }
}

YR_TEST(layered_sample_directions_are_valid) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.7f, 0.7f, 0.7f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    yr::Rng rng{55u};
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample sample = yr::SampleBsdf(
            material, wo, normal, rng.NextFloat2(), rng
        );
        if (!sample.valid) continue;
        YR_EXPECT_TRUE(fixture::IsFiniteColor(sample.wi));
        YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > -1e-4f);
        YR_EXPECT_GT(sample.pdf, 0.0f);
    }
}

YR_TEST(layered_sample_sets_real_pdf_not_proxy) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.7f, 0.7f, 0.7f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{61u};
    int non_specular_count = 0;
    double pdf_sum = 0.0;
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample sample = yr::SampleBsdf(
            material, wo, normal, rng.NextFloat2(), rng
        );
        if (!sample.valid || sample.specular) continue;
        ++non_specular_count;
        pdf_sum += sample.pdf;
        YR_EXPECT_GT(sample.pdf, 0.0f);
    }
    YR_EXPECT_GT(non_specular_count, 0);
    YR_EXPECT_TRUE(std::fabs(pdf_sum / non_specular_count - 1.0) > 1e-3);
}

YR_TEST(layered_conductor_sample_is_energy_conserving_and_valid) {
    const yr::RenderMaterial material = fixture::MakeCoatedConductor({1.0f, 0.78f, 0.34f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.1f, 1.0f});
    yr::Rng rng{4242u};
    yr::Color3f sum;
    constexpr int SampleCount = 20000;
    for (int i = 0; i < SampleCount; ++i) {
        const yr::BsdfSample sample = yr::SampleBsdf(
            material, wo, normal, rng.NextFloat2(), rng
        );
        if (!sample.valid) continue;
        YR_EXPECT_TRUE(fixture::IsFiniteColor(sample.weight));
        YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > -1e-4f);
        YR_EXPECT_GT(sample.pdf, 0.0f);
        sum = sum + sample.weight;
    }
    const yr::Color3f mean = sum / static_cast<float>(SampleCount);
    YR_EXPECT_LE(fixture::MaxComponent(mean), 1.05f);
}
