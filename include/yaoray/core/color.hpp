#pragma once

#include <algorithm>
#include <cmath>

namespace yr {

inline float SrgbToLinear(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped <= 0.04045f) {
        return clamped / 12.92f;
    }
    return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
}

} // namespace yr
