#include <yaoray/film/film_checkpoint.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace yr {
namespace {

constexpr std::array<char, 8> Magic{'Y', 'R', 'C', 'H', 'E', 'C', 'K', '1'};
constexpr std::uint32_t Version = 3;

struct FileHeader {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t target_spp = 0;
    std::uint32_t completed_spp = 0;
    std::uint64_t settings_hash = 0;
    std::uint64_t pixel_count = 0;
};

struct StoredPixel {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t samples = 0;
    float luminance_mean = 0.0f;
    float luminance_m2 = 0.0f;
    float albedo_x = 0.0f;
    float albedo_y = 0.0f;
    float albedo_z = 0.0f;
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 0.0f;
    float depth_sum = 0.0f;
    std::uint32_t aov_samples = 0;
};

template <typename T>
bool WritePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(out);
}

template <typename T>
bool ReadPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(in);
}

FilmCheckpointWriteResult EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return FilmCheckpointWriteResult{false, "failed to create checkpoint directory: " + ec.message()};
        }
    }
    return FilmCheckpointWriteResult{true, {}};
}

} // namespace

FilmCheckpointWriteResult WriteFilmCheckpoint(
    const std::filesystem::path& path,
    const Film& film,
    FilmCheckpointMetadata metadata
) {
    if (metadata.width != film.Width() || metadata.height != film.Height()) {
        return FilmCheckpointWriteResult{false, "checkpoint metadata dimensions do not match film"};
    }
    if (metadata.target_spp <= 0 || metadata.completed_spp < 0 || metadata.completed_spp > metadata.target_spp) {
        return FilmCheckpointWriteResult{false, "checkpoint metadata has invalid sample counts"};
    }

    const FilmCheckpointWriteResult directory = EnsureParentDirectory(path);
    if (!directory.ok) {
        return directory;
    }

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        return FilmCheckpointWriteResult{false, "failed to open checkpoint for writing: " + path.generic_string()};
    }

    FileHeader header;
    header.magic = Magic;
    header.version = Version;
    header.width = static_cast<std::uint32_t>(metadata.width);
    header.height = static_cast<std::uint32_t>(metadata.height);
    header.target_spp = static_cast<std::uint32_t>(metadata.target_spp);
    header.completed_spp = static_cast<std::uint32_t>(metadata.completed_spp);
    header.settings_hash = metadata.settings_hash;
    header.pixel_count = static_cast<std::uint64_t>(film.Pixels().size());
    if (!WritePod(out, header)) {
        return FilmCheckpointWriteResult{false, "failed to write checkpoint header: " + path.generic_string()};
    }

    for (const FilmPixel& pixel : film.Pixels()) {
        const StoredPixel stored{pixel.sum.x, pixel.sum.y, pixel.sum.z, pixel.samples,
            pixel.luminance_mean, pixel.luminance_m2,
            pixel.albedo_sum.x, pixel.albedo_sum.y, pixel.albedo_sum.z,
            pixel.normal_sum.x, pixel.normal_sum.y, pixel.normal_sum.z,
            pixel.depth_sum, pixel.aov_samples};
        if (!WritePod(out, stored)) {
            return FilmCheckpointWriteResult{false, "failed to write checkpoint pixels: " + path.generic_string()};
        }
    }

    return FilmCheckpointWriteResult{true, {}};
}

FilmCheckpointLoadResult LoadFilmCheckpoint(
    const std::filesystem::path& path,
    int expected_width,
    int expected_height,
    int expected_target_spp,
    std::uint64_t expected_settings_hash
) {
    FilmCheckpointLoadResult result;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        result.error = "failed to open checkpoint for reading: " + path.generic_string();
        return result;
    }

    FileHeader header;
    if (!ReadPod(in, header)) {
        result.error = "failed to read checkpoint header: " + path.generic_string();
        return result;
    }
    if (header.magic != Magic) {
        result.error = "checkpoint has invalid magic: " + path.generic_string();
        return result;
    }
    if (header.version != Version) {
        result.error = "checkpoint has unsupported version: " + path.generic_string();
        return result;
    }
    if (static_cast<int>(header.width) != expected_width || static_cast<int>(header.height) != expected_height) {
        result.error = "checkpoint dimensions do not match current render";
        return result;
    }
    if (static_cast<int>(header.target_spp) != expected_target_spp) {
        result.error = "checkpoint target spp does not match current render";
        return result;
    }
    if (header.settings_hash != expected_settings_hash) {
        result.error = "checkpoint settings hash does not match current render";
        return result;
    }
    const std::uint64_t expected_pixels =
        static_cast<std::uint64_t>(expected_width) * static_cast<std::uint64_t>(expected_height);
    if (header.pixel_count != expected_pixels) {
        result.error = "checkpoint pixel count does not match dimensions";
        return result;
    }

    Film film{expected_width, expected_height};
    for (std::uint64_t index = 0; index < header.pixel_count; ++index) {
        StoredPixel stored;
        if (!ReadPod(in, stored)) {
            result.error = "failed to read checkpoint pixels: " + path.generic_string();
            return result;
        }
        if (stored.samples > header.completed_spp) {
            result.error = "checkpoint pixel sample count exceeds completed passes";
            return result;
        }
        const int x = static_cast<int>(index % static_cast<std::uint64_t>(expected_width));
        const int y = static_cast<int>(index / static_cast<std::uint64_t>(expected_width));
        film.SetPixelForCheckpoint(x, y, FilmPixel{Color3f{stored.x, stored.y, stored.z},
            stored.samples, stored.luminance_mean, stored.luminance_m2,
            Color3f{stored.albedo_x, stored.albedo_y, stored.albedo_z},
            Vec3f{stored.normal_x, stored.normal_y, stored.normal_z},
            stored.depth_sum, stored.aov_samples});
    }

    result.ok = true;
    result.metadata = FilmCheckpointMetadata{
        static_cast<int>(header.width),
        static_cast<int>(header.height),
        static_cast<int>(header.target_spp),
        static_cast<int>(header.completed_spp),
        header.settings_hash
    };
    result.film.emplace(std::move(film));
    return result;
}

} // namespace yr
