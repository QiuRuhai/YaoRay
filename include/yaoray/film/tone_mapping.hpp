#pragma once

#include <yaoray/core/vec.hpp>

namespace yr {

enum class ToneMapper {
    None,
    Reinhard,
    Aces,
};

struct ToneMapSettings {
    ToneMapper mapper = ToneMapper::Aces;
    float exposure = 0.0f;
};

Color3f ToDisplayColor(Color3f linear_hdr, const ToneMapSettings& settings);

} // namespace yr
