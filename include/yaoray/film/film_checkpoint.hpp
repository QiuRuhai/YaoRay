#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <yaoray/film/film.hpp>

namespace yr {

struct FilmCheckpointMetadata {
    int width = 0;
    int height = 0;
    int target_spp = 0;
    int completed_spp = 0;
    std::uint64_t settings_hash = 0;
};

struct FilmCheckpointWriteResult {
    bool ok = false;
    std::string error;
};

struct FilmCheckpointLoadResult {
    bool ok = false;
    std::string error;
    std::optional<Film> film;
    FilmCheckpointMetadata metadata;
};

FilmCheckpointWriteResult WriteFilmCheckpoint(
    const std::filesystem::path& path,
    const Film& film,
    FilmCheckpointMetadata metadata
);

FilmCheckpointLoadResult LoadFilmCheckpoint(
    const std::filesystem::path& path,
    int expected_width,
    int expected_height,
    int expected_target_spp,
    std::uint64_t expected_settings_hash
);

} // namespace yr
