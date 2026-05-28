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

ImageWriteResult WritePng(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

ImageWriteResult WriteExr(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

ImageWriteResult WriteImage(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

} // namespace yr
