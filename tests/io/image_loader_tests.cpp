#include "yr_test.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>

#include <yaoray/io/image_loader.hpp>

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

namespace {

std::filesystem::path ImageFixturePath(const std::string& relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / relative;
}

} // namespace

YR_TEST(image_loader_reads_png_texels) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(ImageFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-6);
}

YR_TEST(image_loader_reads_jpeg_texels) {
    const yr::TextureLoadResult result = yr::LoadLdrTexture(ImageFixturePath("assets/checker_2x2.jpg"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    for (const yr::Color4f& texel : result.texture.texels) {
        YR_EXPECT_TRUE(texel.x >= 0.0f && texel.x <= 1.0f);
        YR_EXPECT_TRUE(texel.y >= 0.0f && texel.y <= 1.0f);
        YR_EXPECT_TRUE(texel.z >= 0.0f && texel.z <= 1.0f);
        YR_EXPECT_NEAR(texel.w, 1.0, 1e-6);
    }
}

YR_TEST(image_loader_honors_requested_color_space) {
    const std::filesystem::path path = ImageFixturePath("assets/checker_2x2.jpg");
    const yr::TextureLoadResult srgb = yr::LoadLdrTexture(path, yr::TextureColorSpace::Srgb);
    const yr::TextureLoadResult linear = yr::LoadLdrTexture(path, yr::TextureColorSpace::Linear);

    YR_EXPECT_TRUE(srgb.ok);
    YR_EXPECT_TRUE(linear.ok);
    YR_EXPECT_EQ(srgb.texture.texels.size(), linear.texture.texels.size());
    YR_EXPECT_EQ(srgb.texture.color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(linear.texture.color_space, yr::TextureColorSpace::Linear);

    bool found_rgb_difference = false;
    for (std::size_t index = 0; index < srgb.texture.texels.size(); ++index) {
        const yr::Color4f a = srgb.texture.texels[index];
        const yr::Color4f b = linear.texture.texels[index];
        found_rgb_difference = found_rgb_difference ||
            std::abs(a.x - b.x) > 1.0e-6f ||
            std::abs(a.y - b.y) > 1.0e-6f ||
            std::abs(a.z - b.z) > 1.0e-6f;
    }
    YR_EXPECT_TRUE(found_rgb_difference);
}

YR_TEST(image_loader_honors_requested_png_color_space_and_preserves_alpha) {
    const std::filesystem::path path = ImageFixturePath("assets/test_texture_with_alpha.png");
    const yr::TextureLoadResult srgb = yr::LoadPngTexture(path, yr::TextureColorSpace::Srgb);
    const yr::TextureLoadResult linear = yr::LoadPngTexture(path, yr::TextureColorSpace::Linear);

    YR_EXPECT_TRUE(srgb.ok);
    YR_EXPECT_TRUE(linear.ok);
    YR_EXPECT_EQ(srgb.texture.texels.size(), linear.texture.texels.size());
    bool found_rgb_difference = false;
    for (std::size_t index = 0; index < srgb.texture.texels.size(); ++index) {
        const yr::Color4f a = srgb.texture.texels[index];
        const yr::Color4f b = linear.texture.texels[index];
        YR_EXPECT_NEAR(a.w, b.w, 1e-6);
        found_rgb_difference = found_rgb_difference ||
            std::abs(a.x - b.x) > 1.0e-6f ||
            std::abs(a.y - b.y) > 1.0e-6f ||
            std::abs(a.z - b.z) > 1.0e-6f;
    }
    YR_EXPECT_TRUE(found_rgb_difference);
}

YR_TEST(image_loader_reads_hdr_texels_as_linear_float) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(ImageFixturePath("assets/tiny_env.hdr"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::ClampToEdge);
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-5);
}

YR_TEST(image_loader_preserves_png_alpha_channel) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(
        ImageFixturePath("assets/test_texture_with_alpha.png")
    );

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(!result.texture.texels.empty());
    for (const yr::Color4f& texel : result.texture.texels) {
        YR_EXPECT_TRUE(texel.w >= 0.0f && texel.w <= 1.0f);
    }
}

YR_TEST(image_loader_rejects_unsupported_extensions) {
    const std::filesystem::path unsupported = ImageFixturePath("assets/triangle.obj");
    const yr::TextureLoadResult ldr = yr::LoadLdrTexture(unsupported);
    const yr::TextureLoadResult png = yr::LoadPngTexture(unsupported);
    const yr::TextureLoadResult hdr = yr::LoadHdrTexture(ImageFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(!ldr.ok);
    YR_EXPECT_TRUE(!png.ok);
    YR_EXPECT_TRUE(!hdr.ok);
    YR_EXPECT_TRUE(ldr.error.find(".jpg") != std::string::npos);
    YR_EXPECT_TRUE(png.error.find(".png") != std::string::npos);
    YR_EXPECT_TRUE(hdr.error.find(".hdr") != std::string::npos);
}

YR_TEST(image_loader_reports_missing_files) {
    const yr::TextureLoadResult ldr = yr::LoadLdrTexture(
        ImageFixturePath("assets/missing_texture.jpg")
    );
    const yr::TextureLoadResult hdr = yr::LoadHdrTexture(
        ImageFixturePath("assets/missing_environment.hdr")
    );

    YR_EXPECT_TRUE(!ldr.ok);
    YR_EXPECT_TRUE(!hdr.ok);
    YR_EXPECT_TRUE(ldr.error.find("not found") != std::string::npos);
    YR_EXPECT_TRUE(hdr.error.find("not found") != std::string::npos);
}
