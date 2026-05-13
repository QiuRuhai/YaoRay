#include <yaoray/film/image_writer.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace yr {
namespace {

int ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::lround(clamped * 255.0f));
}

std::string NormalizedExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool HasPpmExtension(const std::filesystem::path& path) {
    return NormalizedExtension(path) == ".ppm";
}

bool HasPngExtension(const std::filesystem::path& path) {
    return NormalizedExtension(path) == ".png";
}

ImageWriteResult EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return ImageWriteResult{false, "failed to create output directory: " + ec.message()};
        }
    }
    return ImageWriteResult{true, {}};
}

std::vector<unsigned char> BuildRgb8Pixels(const Film& film, const ToneMapSettings& tone_map) {
    std::vector<unsigned char> pixels;
    pixels.reserve(static_cast<std::size_t>(film.Width()) * static_cast<std::size_t>(film.Height()) * 3);

    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            const Color3f display = ToDisplayColor(film.LinearPixel(x, y), tone_map);
            pixels.push_back(static_cast<unsigned char>(ToByte(display.x)));
            pixels.push_back(static_cast<unsigned char>(ToByte(display.y)));
            pixels.push_back(static_cast<unsigned char>(ToByte(display.z)));
        }
    }

    return pixels;
}

} // namespace

ImageWriteResult WritePpm(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    if (!HasPpmExtension(path)) {
        return ImageWriteResult{false, "PPM output path must use a .ppm extension"};
    }

    const ImageWriteResult directory_result = EnsureParentDirectory(path);
    if (!directory_result.ok) {
        return directory_result;
    }

    std::ofstream out{path, std::ios::out | std::ios::trunc};
    if (!out) {
        return ImageWriteResult{false, "failed to open output image: " + path.generic_string()};
    }

    out << "P3\n";
    out << film.Width() << ' ' << film.Height() << "\n";
    out << "255\n";

    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            const Color3f display = ToDisplayColor(film.LinearPixel(x, y), tone_map);
            out << ToByte(display.x) << ' '
                << ToByte(display.y) << ' '
                << ToByte(display.z) << '\n';
        }
    }

    if (!out) {
        return ImageWriteResult{false, "failed while writing output image: " + path.generic_string()};
    }

    return ImageWriteResult{true, {}};
}

ImageWriteResult WritePng(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    if (!HasPngExtension(path)) {
        return ImageWriteResult{false, "PNG output path must use a .png extension"};
    }

    const ImageWriteResult directory_result = EnsureParentDirectory(path);
    if (!directory_result.ok) {
        return directory_result;
    }

    const std::vector<unsigned char> pixels = BuildRgb8Pixels(film, tone_map);
    const int stride_bytes = film.Width() * 3;
    const int ok = stbi_write_png(
        path.string().c_str(),
        film.Width(),
        film.Height(),
        3,
        pixels.data(),
        stride_bytes
    );
    if (ok == 0) {
        return ImageWriteResult{false, "failed to write PNG image: " + path.generic_string()};
    }

    return ImageWriteResult{true, {}};
}

ImageWriteResult WriteImage(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    const std::string extension = NormalizedExtension(path);
    if (extension == ".ppm") {
        return WritePpm(film, tone_map, path);
    }
    if (extension == ".png") {
        return WritePng(film, tone_map, path);
    }
    return ImageWriteResult{
        false,
        "unsupported image output extension: " + extension + " (expected .ppm or .png)"
    };
}

} // namespace yr
