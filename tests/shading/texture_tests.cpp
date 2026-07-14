#include "yr_test.hpp"

#include <cstddef>
#include <cmath>

#include <yaoray/shading/texture.hpp>

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

YR_TEST(texture_bilinear_blends_center_of_2x2_texture) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.filter = yr::TextureFilter::Bilinear;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f color = yr::SampleTexture(texture, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_NEAR(color.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.5, 1e-6);
}

YR_TEST(texture_clamp_to_edge_clamps_out_of_range_uvs) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Nearest;
    texture.wrap_s = yr::TextureWrap::ClampToEdge;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f}
    };

    const yr::Color3f left = yr::SampleTexture(texture, yr::Vec2f{-2.0f, 0.5f});
    const yr::Color3f right = yr::SampleTexture(texture, yr::Vec2f{3.0f, 0.5f});

    YR_EXPECT_NEAR(left.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(left.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(right.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(right.y, 1.0, 1e-6);
}

YR_TEST(texture_mirrored_repeat_mirrors_adjacent_intervals) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Nearest;
    texture.wrap_s = yr::TextureWrap::MirroredRepeat;
    texture.wrap_t = yr::TextureWrap::Repeat;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f}
    };

    const yr::Color3f mirrored = yr::SampleTexture(texture, yr::Vec2f{1.25f, 0.5f});
    const yr::Color3f repeated_again = yr::SampleTexture(texture, yr::Vec2f{2.25f, 0.5f});

    YR_EXPECT_NEAR(mirrored.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(mirrored.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(repeated_again.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(repeated_again.y, 0.0, 1e-6);
}

YR_TEST(texture_bilinear_single_pixel_returns_only_texel) {
    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Bilinear;
    texture.wrap_s = yr::TextureWrap::MirroredRepeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color3f{0.25f, 0.5f, 0.75f}
    };

    const yr::Color3f color = yr::SampleTexture(texture, yr::Vec2f{12.5f, -4.0f});

    YR_EXPECT_NEAR(color.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.75, 1e-6);
}

YR_TEST(texture_sample_texture4_preserves_alpha) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Nearest;
    texture.texels = {
        yr::Color4f{1.0f, 0.0f, 0.0f, 0.25f},
        yr::Color4f{0.0f, 1.0f, 0.0f, 0.75f}
    };

    const yr::Color4f left = yr::SampleTexture4(texture, yr::Vec2f{0.25f, 0.5f});
    const yr::Color4f right = yr::SampleTexture4(texture, yr::Vec2f{0.75f, 0.5f});

    YR_EXPECT_NEAR(left.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(left.w, 0.25, 1e-6);
    YR_EXPECT_NEAR(right.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(right.w, 0.75, 1e-6);
    YR_EXPECT_NEAR(yr::SampleTextureAlpha(texture, yr::Vec2f{0.75f, 0.5f}), 0.75, 1e-6);
}

YR_TEST(texture_srgb_to_linear_uses_standard_transfer_curve) {
    YR_EXPECT_NEAR(yr::SrgbToLinear(0.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::SrgbToLinear(1.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::SrgbToLinear(0.5f), 0.21404114, 1e-6);
}

YR_TEST(texture_nearest_returns_black_for_empty_texture) {
    const yr::Color3f color = yr::SampleTextureNearest(yr::RenderTexture{}, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_NEAR(color.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.0, 1e-6);
}

YR_TEST(texture_mipmap_chain_reaches_one_texel_average) {
    yr::RenderTexture texture;
    texture.width = 4;
    texture.height = 4;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float value = ((x + y) & 1) == 0 ? 0.0f : 1.0f;
            texture.texels.push_back(yr::Color4f{value, value, value, 1.0f});
        }
    }
    yr::BuildTextureMipChain(texture);

    YR_EXPECT_EQ(texture.mip_levels.size(), std::size_t{2});
    YR_EXPECT_EQ(texture.mip_levels.back().width, 1);
    YR_EXPECT_NEAR(texture.mip_levels.back().texels[0].x, 0.5, 1e-6);
}

YR_TEST(texture_trilinear_uses_footprint_to_reduce_aliasing) {
    yr::RenderTexture texture;
    texture.width = 4;
    texture.height = 4;
    texture.filter = yr::TextureFilter::Trilinear;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float value = ((x + y) & 1) == 0 ? 0.0f : 1.0f;
            texture.texels.push_back(yr::Color4f{value, value, value, 1.0f});
        }
    }
    yr::BuildTextureMipChain(texture);
    const yr::Color3f filtered = yr::SampleTexture(texture, yr::Vec2f{0.13f, 0.27f},
        yr::TextureFootprint{1.0f, 0.0f, 0.0f, 1.0f});

    YR_EXPECT_NEAR(filtered.x, 0.5, 1e-5);
}

YR_TEST(texture_ewa_handles_anisotropic_footprints) {
    yr::RenderTexture texture;
    texture.width = 8;
    texture.height = 8;
    texture.filter = yr::TextureFilter::Ewa;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const float value = (x & 1) == 0 ? 0.0f : 1.0f;
            texture.texels.push_back(yr::Color4f{value, value, value, 1.0f});
        }
    }
    yr::BuildTextureMipChain(texture);
    const yr::Color3f filtered = yr::SampleTexture(texture, yr::Vec2f{0.31f, 0.42f},
        yr::TextureFootprint{0.75f, 0.0f, 0.0f, 0.02f});

    YR_EXPECT_TRUE(std::isfinite(filtered.x));
    YR_EXPECT_TRUE(filtered.x > 0.1f && filtered.x < 0.9f);
}

YR_TEST(texture_sampling_cache_deduplicates_identical_pyramids) {
    std::vector<yr::RenderTexture> textures(2);
    for (yr::RenderTexture& texture : textures) {
        texture.width = 2;
        texture.height = 2;
        texture.texels = {
            yr::Color4f{1.0f, 0.0f, 0.0f, 1.0f},
            yr::Color4f{0.0f, 1.0f, 0.0f, 1.0f},
            yr::Color4f{0.0f, 0.0f, 1.0f, 1.0f},
            yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f}};
    }
    yr::BuildTextureSamplingCaches(textures);

    YR_EXPECT_TRUE(textures[0].sampling_cache != nullptr);
    YR_EXPECT_TRUE(textures[0].sampling_cache == textures[1].sampling_cache);
    YR_EXPECT_TRUE(textures[0].texels.empty());
    YR_EXPECT_NEAR(yr::SampleTexture(textures[1], yr::Vec2f{0.25f, 0.25f}).x, 1.0, 1e-6);
}
