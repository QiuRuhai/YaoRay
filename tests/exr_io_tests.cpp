#include "yr_test.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>

#include <yaoray/film/film.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/render/texture.hpp>

namespace {

std::filesystem::path ExrTestPath(std::string_view name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "yaoray_exr_io_tests";
    std::filesystem::create_directories(dir);
    return dir / std::string{name};
}

// Build a 4x4 film with a known per-pixel HDR pattern: ramp red horizontally
// (0..3), ramp green vertically (0..3), and a constant high blue (5.0) to
// exercise values above 1.0 that LDR formats would clamp.
yr::Film BuildHdrPatternFilm(int width, int height) {
    yr::Film film{width, height};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const yr::Color3f sample{
                static_cast<float>(x),
                static_cast<float>(y),
                5.0f
            };
            film.AddSample(x, y, sample);
        }
    }
    return film;
}

} // namespace

YR_TEST(exr_write_then_read_round_trip_preserves_hdr_values) {
    constexpr int kWidth = 4;
    constexpr int kHeight = 4;
    const yr::Film film = BuildHdrPatternFilm(kWidth, kHeight);

    const std::filesystem::path path = ExrTestPath("round_trip.exr");
    std::filesystem::remove(path);

    const yr::ToneMapSettings tone_map{yr::ToneMapper::None, 0.0f};
    const yr::ImageWriteResult write_result = yr::WriteExr(film, tone_map, path);
    YR_EXPECT_TRUE(write_result.ok);
    YR_EXPECT_TRUE(write_result.error.empty());
    YR_EXPECT_TRUE(std::filesystem::exists(path));

    const yr::TextureLoadResult load_result = yr::LoadHdrTexture(path);
    YR_EXPECT_TRUE(load_result.ok);
    YR_EXPECT_TRUE(load_result.error.empty());
    YR_EXPECT_EQ(load_result.texture.width, kWidth);
    YR_EXPECT_EQ(load_result.texture.height, kHeight);
    YR_EXPECT_EQ(load_result.texture.color_space, yr::TextureColorSpace::Linear);
    YR_EXPECT_EQ(load_result.texture.texels.size(),
                 static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight));

    // Verify every pixel survives the round trip with float precision. We use
    // raw float32 storage (save_as_fp16 = 0), so a tight epsilon is appropriate.
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x);
            const yr::Color4f texel = load_result.texture.texels[idx];
            YR_EXPECT_NEAR(texel.x, static_cast<double>(x), 1e-5);
            YR_EXPECT_NEAR(texel.y, static_cast<double>(y), 1e-5);
            YR_EXPECT_NEAR(texel.z, 5.0, 1e-5);
        }
    }
}

YR_TEST(exr_write_image_dispatch_succeeds) {
    yr::Film film{2, 2};
    film.AddSample(0, 0, yr::Color3f{0.5f, 1.5f, 3.0f});
    film.AddSample(1, 0, yr::Color3f{0.25f, 0.75f, 2.0f});
    film.AddSample(0, 1, yr::Color3f{0.0f, 0.0f, 0.0f});
    film.AddSample(1, 1, yr::Color3f{4.0f, 8.0f, 16.0f});

    const std::filesystem::path path = ExrTestPath("dispatch.exr");
    std::filesystem::remove(path);

    const yr::ToneMapSettings tone_map{yr::ToneMapper::None, 0.0f};
    const yr::ImageWriteResult result = yr::WriteImage(film, tone_map, path);
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(std::filesystem::exists(path));

    // EXR magic number is 0x76 0x2f 0x31 0x01 (per OpenEXR spec).
    std::array<unsigned char, 4> magic{};
    std::ifstream in{path, std::ios::binary};
    in.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    const std::array<unsigned char, 4> expected{0x76, 0x2f, 0x31, 0x01};
    YR_EXPECT_TRUE(magic == expected);
}

YR_TEST(exr_write_image_ignores_tone_map) {
    // EXR output should write raw radiance, not tone-mapped LDR. Build a film
    // with a high value that the Reinhard tone-map would compress, then verify
    // the value comes back unchanged via the HDR loader.
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{10.0f, 10.0f, 10.0f});

    const std::filesystem::path path = ExrTestPath("tone_map_ignored.exr");
    std::filesystem::remove(path);

    const yr::ToneMapSettings tone_map{yr::ToneMapper::Reinhard, 1.0f};
    const yr::ImageWriteResult write_result = yr::WriteImage(film, tone_map, path);
    YR_EXPECT_TRUE(write_result.ok);

    const yr::TextureLoadResult load_result = yr::LoadHdrTexture(path);
    YR_EXPECT_TRUE(load_result.ok);
    YR_EXPECT_EQ(load_result.texture.texels.size(), static_cast<std::size_t>(1));
    YR_EXPECT_NEAR(load_result.texture.texels[0].x, 10.0, 1e-4);
    YR_EXPECT_NEAR(load_result.texture.texels[0].y, 10.0, 1e-4);
    YR_EXPECT_NEAR(load_result.texture.texels[0].z, 10.0, 1e-4);
}

YR_TEST(exr_write_image_rejects_non_exr_extension) {
    // The WriteImage dispatch must continue to reject unknown extensions and
    // mention .exr as a supported alternative.
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});

    const yr::ImageWriteResult result = yr::WriteImage(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        ExrTestPath("bad.tiff")
    );

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".exr") != std::string::npos);
}

YR_TEST(load_hdr_texture_rejects_missing_exr_file) {
    const std::filesystem::path path = ExrTestPath("does_not_exist_on_disk.exr");
    std::error_code ec;
    std::filesystem::remove(path, ec);

    const yr::TextureLoadResult result = yr::LoadHdrTexture(path);
    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".exr") != std::string::npos ||
                   result.error.find("not found") != std::string::npos);
}

YR_TEST(load_hdr_texture_error_message_lists_exr_extension) {
    // Unsupported extension should now mention .exr alongside .hdr / .pfm.
    const std::filesystem::path path = "ignored.tiff";
    const yr::TextureLoadResult result = yr::LoadHdrTexture(path);
    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".exr") != std::string::npos);
}
