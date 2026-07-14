#include "yr_test.hpp"

#include "layered_bsdf_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace fixture = yrtest::layered;

YR_TEST(layered_eval_exit_convention_roundtrip) {
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.4f, -0.3f, 0.9f});
    yr::Vec3f internal_down;
    YR_EXPECT_TRUE(fixture::Refract(wi, normal, 1.0f / 1.5f, internal_down));
    const yr::Vec3f internal_up = -internal_down;
    YR_EXPECT_TRUE(yr::Dot(internal_up, normal) > 0.0f);

    yr::Vec3f exit;
    YR_EXPECT_TRUE(fixture::Refract(internal_up, normal, 1.5f, exit));
    exit = -exit;
    YR_EXPECT_NEAR(exit.x, wi.x, 1e-4f);
    YR_EXPECT_NEAR(exit.y, wi.y, 1e-4f);
    YR_EXPECT_NEAR(exit.z, wi.z, 1e-4f);
}

YR_TEST(layered_eval_furnace_matches_sample_energy) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({1.0f, 1.0f, 1.0f});
    const float rho = fixture::DirectionalAlbedoViaEval(
        material,
        yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f}),
        fixture::Up(),
        13u,
        40000
    );
    YR_EXPECT_TRUE(rho >= 0.90f && rho <= 1.02f);
}

YR_TEST(layered_eval_is_reciprocal) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.7f, 0.6f, 0.5f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.2f, 0.5f, 0.8f});
    const auto mean = [&](yr::Vec3f a, yr::Vec3f b) {
        yr::Rng rng{77u};
        double sum = 0.0;
        for (int i = 0; i < 60000; ++i) {
            sum += fixture::MaxComponent(yr::EvaluateBsdf(material, a, b, normal, rng));
        }
        return static_cast<float>(sum / 60000.0);
    };
    const float ab = mean(wo, wi);
    const float ba = mean(wi, wo);
    YR_EXPECT_TRUE(std::fabs(ab - ba) <= 0.05f * std::max(ab, ba) + 1e-4f);
}

YR_TEST(layered_eval_differs_from_bare_base) {
    const yr::RenderMaterial coated = fixture::MakeCoatedDiffuse({0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = {0.5f, 0.5f, 0.5f};
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, 0.2f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.9f, 0.0f, 0.2f});
    yr::Rng coated_rng{5u};
    yr::Rng bare_rng{5u};
    double coated_sum = 0.0;
    for (int i = 0; i < 40000; ++i) {
        coated_sum += fixture::MaxComponent(
            yr::EvaluateBsdf(coated, wo, wi, normal, coated_rng)
        );
    }
    const float bare_value = fixture::MaxComponent(
        yr::EvaluateBsdf(bare, wo, wi, normal, bare_rng)
    );
    YR_EXPECT_TRUE(std::fabs(static_cast<float>(coated_sum / 40000.0) - bare_value) > 0.005f);
}

YR_TEST(layered_eval_is_deterministic_under_fixed_seed) {
    const yr::RenderMaterial material = fixture::MakeCoatedDiffuse({0.6f, 0.4f, 0.2f});
    const yr::Vec3f normal = fixture::Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng a_rng{321u};
    yr::Rng b_rng{321u};
    const yr::Color3f a = yr::EvaluateBsdf(material, wo, wi, normal, a_rng);
    const yr::Color3f b = yr::EvaluateBsdf(material, wo, wi, normal, b_rng);
    YR_EXPECT_EQ(a.x, b.x);
    YR_EXPECT_EQ(a.y, b.y);
    YR_EXPECT_EQ(a.z, b.z);
}
