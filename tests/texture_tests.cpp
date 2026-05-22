#include "yr_test.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>

#include <yaoray/render/texture.hpp>

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

namespace {

std::filesystem::path TextureFixturePath(const std::string& relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / relative;
}

} // namespace

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

YR_TEST(texture_loader_reads_png_texels) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[0].y, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].z, 1.0, 1e-6);
}

YR_TEST(texture_loader_reads_hdr_texels_as_linear_float) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/tiny_env.hdr"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::ClampToEdge);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[0].y, 0.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].x, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].y, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].z, 1.0, 1e-5);
}

YR_TEST(texture_loader_rejects_non_hdr_extension_for_hdr_load) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".hdr") != std::string::npos);
}

YR_TEST(texture_loader_reports_missing_hdr_file) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/missing_environment.hdr"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("not found") != std::string::npos);
}

YR_TEST(texture_loader_rejects_non_png_extension) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}

YR_TEST(texture_loader_preserves_png_alpha_channel) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/gltf/SimpleTexture/glTF/testTexture.png"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(!result.texture.texels.empty());
    for (const yr::Color4f& texel : result.texture.texels) {
        YR_EXPECT_TRUE(texel.w >= 0.0f);
        YR_EXPECT_TRUE(texel.w <= 1.0f);
    }
}

YR_TEST(texture_loader_uses_requested_color_space) {
    const std::filesystem::path path = TextureFixturePath("assets/gltf/SimpleTexture/glTF/testTexture.png");
    const yr::TextureLoadResult srgb = yr::LoadPngTexture(path, yr::TextureColorSpace::Srgb);
    const yr::TextureLoadResult linear = yr::LoadPngTexture(path, yr::TextureColorSpace::Linear);

    YR_EXPECT_TRUE(srgb.ok);
    YR_EXPECT_TRUE(linear.ok);
    YR_EXPECT_EQ(srgb.texture.width, linear.texture.width);
    YR_EXPECT_EQ(srgb.texture.height, linear.texture.height);
    YR_EXPECT_EQ(srgb.texture.texels.size(), linear.texture.texels.size());
    YR_EXPECT_EQ(srgb.texture.color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(linear.texture.color_space, yr::TextureColorSpace::Linear);

    bool found_rgb_difference = false;
    for (std::size_t index = 0; index < srgb.texture.texels.size(); ++index) {
        const yr::Color4f srgb_texel = srgb.texture.texels[index];
        const yr::Color4f linear_texel = linear.texture.texels[index];
        YR_EXPECT_NEAR(srgb_texel.w, linear_texel.w, 1e-6);
        found_rgb_difference = found_rgb_difference ||
                               std::abs(srgb_texel.x - linear_texel.x) > 1.0e-6f ||
                               std::abs(srgb_texel.y - linear_texel.y) > 1.0e-6f ||
                               std::abs(srgb_texel.z - linear_texel.z) > 1.0e-6f;
    }
    YR_EXPECT_TRUE(found_rgb_difference);
}

YR_TEST(texture_loader_reports_missing_file) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/missing_texture.png"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("not found") != std::string::npos);
}
