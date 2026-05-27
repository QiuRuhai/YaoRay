#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/backends/cpu/cpu_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Expand CPU sampler tests.

namespace {

bool InRange(float value, float low, float high) {
    return value >= low && value < high;
}

} // namespace

YR_TEST(cpu_sampler_independent_uses_center_pixel_for_one_spp) {
    yr::CpuSampler sampler{
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

YR_TEST(cpu_sampler_independent_is_deterministic) {
    yr::CpuSampler first{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{456},
        3,
        8,
        4
    };
    yr::CpuSampler second{
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

YR_TEST(cpu_sampler_stratifies_pixel_samples_in_square_grid) {
    for (int sample = 0; sample < 4; ++sample) {
        yr::CpuSampler sampler{
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

YR_TEST(cpu_sampler_stratifies_light_samples_in_square_grid) {
    yr::CpuSampler sampler{
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

YR_TEST(cpu_sampler_stratifies_non_square_counts) {
    yr::CpuSampler sampler{
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

YR_TEST(cpu_sampler_pixel_seed_is_deterministic_and_distinguishes_samples) {
    const std::uint64_t first = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t second = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t different_sample = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 6);

    YR_EXPECT_EQ(first, second);
    YR_EXPECT_TRUE(first != different_sample);
}
