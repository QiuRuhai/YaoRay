#include <yaoray/film/image_writer.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
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

bool HasExrExtension(const std::filesystem::path& path) {
    return NormalizedExtension(path) == ".exr";
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

ImageWriteResult WriteExr(
    const Film& film,
    const ToneMapSettings& /*tone_map*/,
    const std::filesystem::path& path
) {
    if (!HasExrExtension(path)) {
        return ImageWriteResult{false, "EXR output path must use a .exr extension"};
    }

    const ImageWriteResult directory_result = EnsureParentDirectory(path);
    if (!directory_result.ok) {
        return directory_result;
    }

    const int width = film.Width();
    const int height = film.Height();
    if (width <= 0 || height <= 0) {
        return ImageWriteResult{false, "EXR output requires a non-empty film: " + path.generic_string()};
    }

    // EXR is HDR: write raw linear radiance with no tone-mapping. This keeps
    // the full dynamic range available for downstream post-processing.
    std::vector<float> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Color3f c = film.LinearPixel(x, y);
            const std::size_t base = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                                      + static_cast<std::size_t>(x)) * 3;
            rgb[base + 0] = c.x;
            rgb[base + 1] = c.y;
            rgb[base + 2] = c.z;
        }
    }

    const char* err = nullptr;
    const std::string path_str = path.generic_string();
    const int status = SaveEXR(
        rgb.data(),
        width,
        height,
        3,           // RGB components
        0,           // save_as_fp16 = 0 -> fp32
        path_str.c_str(),
        &err
    );

    if (status != TINYEXR_SUCCESS) {
        const std::string message = err != nullptr ? std::string{err} : std::string{"unknown error"};
        if (err != nullptr) {
            FreeEXRErrorMessage(err);
        }
        return ImageWriteResult{
            false,
            "failed to write EXR image: " + path.generic_string() + " (" + message + ")"
        };
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
    if (extension == ".exr") {
        return WriteExr(film, tone_map, path);
    }
    return ImageWriteResult{
        false,
        "unsupported image output extension: " + extension + " (expected .ppm, .png, or .exr)"
    };
}

} // namespace yr
