#pragma once

#include <filesystem>
#include <string>

#include <yaoray/film/film.hpp>
#include <yaoray/film/tone_mapping.hpp>

namespace yr {

struct ImageWriteResult {
    bool ok = false;
    std::string error;
};

ImageWriteResult WritePpm(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

} // namespace yr
