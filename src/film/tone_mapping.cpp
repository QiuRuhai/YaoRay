#include <yaoray/film/tone_mapping.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

Color3f Clamp01(Color3f c) {
    return Color3f{Clamp01(c.x), Clamp01(c.y), Clamp01(c.z)};
}

Color3f ApplyExposure(Color3f c, float exposure) {
    const float scale = std::exp2(exposure);
    return c * scale;
}

Color3f Reinhard(Color3f c) {
    return Color3f{
        c.x / (1.0f + c.x),
        c.y / (1.0f + c.y),
        c.z / (1.0f + c.z),
    };
}

float AcesChannel(float x) {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return Clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

Color3f Aces(Color3f c) {
    return Color3f{AcesChannel(c.x), AcesChannel(c.y), AcesChannel(c.z)};
}

float LinearToSrgb(float x) {
    x = Clamp01(x);
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

Color3f LinearToSrgb(Color3f c) {
    return Color3f{LinearToSrgb(c.x), LinearToSrgb(c.y), LinearToSrgb(c.z)};
}

} // namespace

Color3f ToDisplayColor(Color3f linear_hdr, const ToneMapSettings& settings) {
    Color3f c = ApplyExposure(linear_hdr, settings.exposure);
    switch (settings.mapper) {
        case ToneMapper::None:
            c = Clamp01(c);
            break;
        case ToneMapper::Reinhard:
            c = Reinhard(c);
            break;
        case ToneMapper::Aces:
            c = Aces(c);
            break;
    }
    return LinearToSrgb(c);
}

} // namespace yr
