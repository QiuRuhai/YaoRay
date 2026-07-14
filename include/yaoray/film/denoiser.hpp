#pragma once

#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

class Film;

struct DenoiseResult {
    bool ok = false;
    std::string error;
    std::vector<Color3f> beauty;
};

bool IsOidnDenoiserAvailable();
DenoiseResult DenoiseFilmWithOidn(const Film& film);

} // namespace yr
