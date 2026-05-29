#include "yr_test.hpp"

#include <yaoray/core/rng.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

#include <cmath>

namespace {

yr::Vec3f Up() { return yr::Vec3f{0.0f, 0.0f, 1.0f}; }

yr::RenderMaterial MakeCoatedDiffuse(yr::Color3f base_reflectance) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::CoatedDiffuse;
    m.reflectance.value = base_reflectance;
    m.coating_ior = 1.5f;
    m.coating_roughness.value = 0.0f;
    m.coat_thickness = 0.01f;
    m.coat_absorption = yr::Color3f{0.0f, 0.0f, 0.0f};
    m.coat_maxdepth = 10;
    return m;
}

yr::RenderMaterial MakeCoatedConductor(yr::Color3f base_f0) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::CoatedConductor;
    m.reflectance.value = base_f0;     // conductor base f0 (compiler-derived in real scenes)
    m.uroughness.value = 0.1f;
    m.vroughness.value = 0.1f;
    m.coating_ior = 1.5f;
    m.coating_roughness.value = 0.0f;
    m.coat_thickness = 0.01f;
    m.coat_absorption = yr::Color3f{0.0f, 0.0f, 0.0f};
    m.coat_maxdepth = 10;
    return m;
}

bool IsFiniteColor(yr::Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

float MaxComp(yr::Color3f c) { return std::max(c.x, std::max(c.y, c.z)); }
float MinComp(yr::Color3f c) { return std::min(c.x, std::min(c.y, c.z)); }

} // namespace

YR_TEST(layered_sample_is_energy_conserving_white_furnace) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});

    yr::Rng rng{2024u};
    const int N = 20000;
    yr::Color3f sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (s.valid) {
            YR_EXPECT_TRUE(IsFiniteColor(s.weight));
            sum = sum + s.weight;
        }
    }
    const yr::Color3f mean = sum / static_cast<float>(N);
    YR_EXPECT_TRUE(MaxComp(mean) <= 1.05f);
    // Lower bound: a white (reflectance 1) coated-diffuse base with zero
    // absorption must reflect almost all energy back out — a near-lossless
    // coat. A regression that drops exit paths (e.g. a wrong exit-refraction
    // sign) collapses this toward ~0.04, so guard it.
    YR_EXPECT_TRUE(MinComp(mean) >= 0.90f);
}

YR_TEST(layered_sample_differs_from_bare_base) {
    yr::RenderMaterial coated = MakeCoatedDiffuse(yr::Color3f{0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = yr::Color3f{0.5f, 0.5f, 0.5f};

    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.0f, 1.0f});

    yr::Rng rng_a{7u};
    yr::Rng rng_b{7u};
    const int N = 20000;
    yr::Color3f coated_sum{0, 0, 0};
    yr::Color3f bare_sum{0, 0, 0};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample sc = yr::SampleBsdf(coated, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample sb = yr::SampleBsdf(bare, wo, n, rng_b.NextFloat2(), rng_b);
        if (sc.valid) coated_sum = coated_sum + sc.weight;
        if (sb.valid) bare_sum = bare_sum + sb.weight;
    }
    const float diff = std::fabs(MaxComp(coated_sum) - MaxComp(bare_sum)) / static_cast<float>(N);
    YR_EXPECT_TRUE(diff > 0.01f);
}

YR_TEST(layered_sample_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});

    yr::Rng rng_a{999u};
    yr::Rng rng_b{999u};
    for (int i = 0; i < 50; ++i) {
        const yr::BsdfSample a = yr::SampleBsdf(m, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample b = yr::SampleBsdf(m, wo, n, rng_b.NextFloat2(), rng_b);
        YR_EXPECT_EQ(a.valid, b.valid);
        if (a.valid && b.valid) {
            YR_EXPECT_EQ(a.wi.x, b.wi.x);
            YR_EXPECT_EQ(a.wi.y, b.wi.y);
            YR_EXPECT_EQ(a.wi.z, b.wi.z);
            YR_EXPECT_EQ(a.weight.x, b.weight.x);
        }
    }
}

YR_TEST(layered_sample_directions_are_valid) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.7f, 0.7f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    yr::Rng rng{55u};
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        YR_EXPECT_TRUE(std::isfinite(s.wi.x) && std::isfinite(s.wi.y) && std::isfinite(s.wi.z));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);
        YR_EXPECT_TRUE(s.pdf > 0.0f);
    }
}

YR_TEST(layered_conductor_sample_is_energy_conserving_and_valid) {
    // Coated conductor (gold-ish f0), zero absorption. The base reflects only
    // its f0 fraction, so mean throughput is < 1 (not white-furnace), but it
    // must never gain energy and must always exit in the upper hemisphere.
    yr::RenderMaterial m = MakeCoatedConductor(yr::Color3f{1.0f, 0.78f, 0.34f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.1f, 1.0f});

    yr::Rng rng{4242u};
    const int N = 20000;
    yr::Color3f sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        YR_EXPECT_TRUE(IsFiniteColor(s.weight));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);
        YR_EXPECT_TRUE(s.pdf > 0.0f);
        sum = sum + s.weight;
    }
    const yr::Color3f mean = sum / static_cast<float>(N);
    YR_EXPECT_TRUE(MaxComp(mean) <= 1.05f);   // no energy gain
}
