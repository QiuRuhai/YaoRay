#include "yr_test.hpp"

#include <cstddef>

#include <yaoray/render/texture.hpp>

YR_TEST(texture_nearest_samples_expected_texels) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f lower_left = yr::SampleTextureNearest(texture, yr::Vec2f{0.25f, 0.25f});
    const yr::Color3f lower_right = yr::SampleTextureNearest(texture, yr::Vec2f{0.75f, 0.25f});
    const yr::Color3f upper_left = yr::SampleTextureNearest(texture, yr::Vec2f{0.25f, 0.75f});
    const yr::Color3f upper_right = yr::SampleTextureNearest(texture, yr::Vec2f{0.75f, 0.75f});

    YR_EXPECT_NEAR(lower_left.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(lower_right.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_left.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.z, 1.0, 1e-6);
}

YR_TEST(texture_nearest_repeats_wrapped_uvs) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f wrapped = yr::SampleTextureNearest(texture, yr::Vec2f{1.25f, -0.75f});

    YR_EXPECT_NEAR(wrapped.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(wrapped.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(wrapped.z, 0.0, 1e-6);
}

YR_TEST(texture_nearest_returns_black_for_empty_texture) {
    const yr::Color3f color = yr::SampleTextureNearest(yr::RenderTexture{}, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_NEAR(color.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.0, 1e-6);
}
