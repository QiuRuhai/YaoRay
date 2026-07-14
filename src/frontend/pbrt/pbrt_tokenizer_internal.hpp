#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>

namespace yr::pbrt_parse {

std::vector<std::string> Tokenize(std::string_view text);
std::optional<float> ParseFloatToken(std::string_view token);
std::vector<PbrtParam> ReadParams(
    const std::vector<std::string>& tokens,
    std::size_t& index
);
const PbrtParam* FindParam(
    const std::vector<PbrtParam>& params,
    std::string_view name
);
std::string StringParam(
    const std::vector<PbrtParam>& params,
    std::string_view name,
    const std::string& fallback = ""
);

} // namespace yr::pbrt_parse
