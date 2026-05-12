#include <yaoray/film/image_writer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>

namespace yr {
namespace {

int ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::lround(clamped * 255.0f));
}

bool HasPpmExtension(const std::filesystem::path& path) {
    return path.extension() == ".ppm";
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

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return ImageWriteResult{false, "failed to create output directory: " + ec.message()};
        }
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

} // namespace yr
