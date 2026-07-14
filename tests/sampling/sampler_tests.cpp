#include "yr_test.hpp"

#include <cstdint>
#include <array>

#include <yaoray/sampling/sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace {

bool InRange(float value, float low, float high) {
    return value >= low && value < high;
}

} // namespace

YR_TEST(sampler_independent_uses_center_pixel_for_one_spp) {
    yr::Sampler sampler{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{123},
        0,
        1,
        1
    };

    const yr::Vec2f pixel = sampler.NextPixel2D();

    YR_EXPECT_NEAR(pixel.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 0.5, 1e-6);
}

YR_TEST(sampler_independent_is_deterministic) {
    yr::Sampler first{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{456},
        3,
        8,
        4
    };
    yr::Sampler second{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{456},
        3,
        8,
        4
    };

    const yr::Vec2f first_pixel = first.NextPixel2D();
    const yr::Vec2f second_pixel = second.NextPixel2D();
    const yr::Vec2f first_light = first.NextLight2D(2);
    const yr::Vec2f second_light = second.NextLight2D(2);

    YR_EXPECT_EQ(first_pixel.x, second_pixel.x);
    YR_EXPECT_EQ(first_pixel.y, second_pixel.y);
    YR_EXPECT_EQ(first_light.x, second_light.x);
    YR_EXPECT_EQ(first_light.y, second_light.y);
}

YR_TEST(sampler_stratifies_pixel_samples_in_square_grid) {
    for (int sample = 0; sample < 4; ++sample) {
        yr::Sampler sampler{
            yr::RenderSamplerKind::Stratified,
            std::uint64_t{100 + static_cast<std::uint64_t>(sample)},
            sample,
            4,
            1
        };

        const yr::Vec2f pixel = sampler.NextPixel2D();
        const int cell_x = sample % 2;
        const int cell_y = sample / 2;

        YR_EXPECT_TRUE(InRange(pixel.x, static_cast<float>(cell_x) * 0.5f, static_cast<float>(cell_x + 1) * 0.5f));
        YR_EXPECT_TRUE(InRange(pixel.y, static_cast<float>(cell_y) * 0.5f, static_cast<float>(cell_y + 1) * 0.5f));
    }
}

YR_TEST(sampler_stratifies_light_samples_in_square_grid) {
    yr::Sampler sampler{
        yr::RenderSamplerKind::Stratified,
        std::uint64_t{789},
        0,
        1,
        4
    };

    for (int sample = 0; sample < 4; ++sample) {
        const yr::Vec2f light = sampler.NextLight2D(sample);
        const int cell_x = sample % 2;
        const int cell_y = sample / 2;

        YR_EXPECT_TRUE(InRange(light.x, static_cast<float>(cell_x) * 0.5f, static_cast<float>(cell_x + 1) * 0.5f));
        YR_EXPECT_TRUE(InRange(light.y, static_cast<float>(cell_y) * 0.5f, static_cast<float>(cell_y + 1) * 0.5f));
    }
}

YR_TEST(sampler_stratifies_non_square_counts) {
    yr::Sampler sampler{
        yr::RenderSamplerKind::Stratified,
        std::uint64_t{999},
        5,
        6,
        1
    };

    const yr::Vec2f pixel = sampler.NextPixel2D();

    YR_EXPECT_TRUE(InRange(pixel.x, 2.0f / 3.0f, 1.0f));
    YR_EXPECT_TRUE(InRange(pixel.y, 0.5f, 1.0f));
}

YR_TEST(sampler_pixel_seed_is_deterministic_and_distinguishes_samples) {
    const std::uint64_t first = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t second = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t different_sample = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 6);

    YR_EXPECT_EQ(first, second);
    YR_EXPECT_TRUE(first != different_sample);
}

YR_TEST(sampler_semantic_dimensions_do_not_depend_on_call_order) {
    yr::Sampler first{yr::RenderSamplerKind::OwenSobol, 41, 7, 9, 3, 8, 1};
    yr::Sampler second{yr::RenderSamplerKind::OwenSobol, 41, 7, 9, 3, 8, 1};
    first.BeginBounce(2);
    second.BeginBounce(2);

    const yr::Vec2f first_bsdf = first.Sample2D(yr::SampleDimension::Bsdf);
    const float first_light = first.Sample1D(yr::SampleDimension::DirectLightSelect);
    const float second_light = second.Sample1D(yr::SampleDimension::DirectLightSelect);
    const yr::Vec2f second_bsdf = second.Sample2D(yr::SampleDimension::Bsdf);

    YR_EXPECT_EQ(first_bsdf.x, second_bsdf.x);
    YR_EXPECT_EQ(first_bsdf.y, second_bsdf.y);
    YR_EXPECT_EQ(first_light, second_light);
}

YR_TEST(sampler_owen_sobol_stratifies_power_of_two_prefixes) {
    std::array<bool, 4> x_strata{};
    std::array<bool, 4> y_strata{};
    for (int sample = 0; sample < 4; ++sample) {
        yr::Sampler sampler{yr::RenderSamplerKind::OwenSobol, 73, 2, 5, sample, 4, 1};
        const yr::Vec2f value = sampler.Sample2D(yr::SampleDimension::Pixel);
        YR_EXPECT_TRUE(InRange(value.x, 0.0f, 1.0f));
        YR_EXPECT_TRUE(InRange(value.y, 0.0f, 1.0f));
        x_strata[static_cast<std::size_t>(value.x * 4.0f)] = true;
        y_strata[static_cast<std::size_t>(value.y * 4.0f)] = true;
    }
    for (int stratum = 0; stratum < 4; ++stratum) {
        YR_EXPECT_TRUE(x_strata[static_cast<std::size_t>(stratum)]);
        YR_EXPECT_TRUE(y_strata[static_cast<std::size_t>(stratum)]);
    }
}

YR_TEST(sampler_zsobol_is_deterministic_and_decorrelates_pixels) {
    yr::Sampler first{yr::RenderSamplerKind::ZSobol, 91, 2, 3, 1, 8, 1};
    yr::Sampler second{yr::RenderSamplerKind::ZSobol, 91, 2, 3, 1, 8, 1};
    yr::Sampler neighbor{yr::RenderSamplerKind::ZSobol, 91, 3, 3, 1, 8, 1};

    const yr::Vec2f a = first.Sample2D(yr::SampleDimension::Pixel);
    const yr::Vec2f b = second.Sample2D(yr::SampleDimension::Pixel);
    const yr::Vec2f c = neighbor.Sample2D(yr::SampleDimension::Pixel);
    YR_EXPECT_EQ(a.x, b.x);
    YR_EXPECT_EQ(a.y, b.y);
    YR_EXPECT_TRUE(a.x != c.x || a.y != c.y);
}

YR_TEST(sampler_names_round_trip_low_discrepancy_variants) {
    YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::OwenSobol), "sobol");
    YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::ZSobol), "zsobol");
    YR_EXPECT_EQ(*yr::ParseRenderSamplerName("paddedsobol"), yr::RenderSamplerKind::OwenSobol);
    YR_EXPECT_EQ(*yr::ParseRenderSamplerName("zsobol"), yr::RenderSamplerKind::ZSobol);
}
