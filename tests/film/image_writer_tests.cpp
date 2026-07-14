#include "film_test_paths.hpp"
#include "yr_test.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <yaoray/film/image_writer.hpp>

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream input{path};
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

std::array<unsigned char, 8> ReadSignature(const std::filesystem::path& path) {
    std::array<unsigned char, 8> bytes{};
    std::ifstream input{path, std::ios::binary};
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

yr::Film MakeTwoPixelFilm() {
    yr::Film film{2, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 0.0f, 0.0f});
    film.AddSample(1, 0, yr::Color3f{0.0f, 1.0f, 0.0f});
    return film;
}

constexpr std::array<unsigned char, 8> PngSignature{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

} // namespace

YR_TEST(ppm_writer_rejects_non_ppm_extension) {
    const yr::ImageWriteResult result = yr::WritePpm(
        MakeTwoPixelFilm(),
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        yr::test_support::FilmTestPath("bad.txt"));
    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".ppm") != std::string::npos);
}

YR_TEST(ppm_writer_writes_p3_header_and_rgb_values) {
    const std::filesystem::path path = yr::test_support::FilmTestPath("two_pixels.ppm");
    std::filesystem::remove(path);
    const yr::ImageWriteResult result = yr::WritePpm(
        MakeTwoPixelFilm(), yr::ToneMapSettings{yr::ToneMapper::None, 0.0f}, path);

    YR_EXPECT_TRUE(result.ok);
    const std::string text = ReadTextFile(path);
    YR_EXPECT_TRUE(text.find("P3\n2 1\n255\n") == 0);
    YR_EXPECT_TRUE(text.find("255 0 0") != std::string::npos);
    YR_EXPECT_TRUE(text.find("0 255 0") != std::string::npos);
}

YR_TEST(png_writer_rejects_non_png_extension) {
    const yr::ImageWriteResult result = yr::WritePng(
        MakeTwoPixelFilm(),
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        yr::test_support::FilmTestPath("bad.txt"));
    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}

YR_TEST(png_writer_writes_png_signature) {
    const std::filesystem::path path = yr::test_support::FilmTestPath("two_pixels.png");
    std::filesystem::remove(path);
    const yr::ImageWriteResult result = yr::WritePng(
        MakeTwoPixelFilm(), yr::ToneMapSettings{yr::ToneMapper::None, 0.0f}, path);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(ReadSignature(path) == PngSignature);
}

YR_TEST(image_writer_dispatches_ppm_and_png) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{0.25f, 0.5f, 1.0f});
    const std::filesystem::path ppm_path = yr::test_support::FilmTestPath("dispatch.ppm");
    const std::filesystem::path png_path = yr::test_support::FilmTestPath("dispatch.png");
    const yr::ToneMapSettings tone_map{yr::ToneMapper::None, 0.0f};

    YR_EXPECT_TRUE(yr::WriteImage(film, tone_map, ppm_path).ok);
    YR_EXPECT_TRUE(yr::WriteImage(film, tone_map, png_path).ok);
    YR_EXPECT_TRUE(ReadTextFile(ppm_path).find("P3\n1 1\n255\n") == 0);
    YR_EXPECT_TRUE(ReadSignature(png_path) == PngSignature);
}

YR_TEST(image_writer_rejects_unsupported_extension) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::ImageWriteResult result = yr::WriteImage(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        yr::test_support::FilmTestPath("bad.bmp"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".ppm") != std::string::npos);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}
