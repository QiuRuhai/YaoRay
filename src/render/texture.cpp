#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

float WrapRepeat(float value) {
    const float wrapped = value - std::floor(value);
    return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

int NearestIndex(float value, int count) {
    const float wrapped = WrapRepeat(value);
    return std::clamp(static_cast<int>(std::floor(wrapped * static_cast<float>(count))), 0, count - 1);
}

} // namespace

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color3f{};
    }

    const int x = NearestIndex(uv.x, texture.width);
    const int y = NearestIndex(uv.y, texture.height);
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                              static_cast<std::size_t>(x);
    if (index >= texture.texels.size()) {
        return Color3f{};
    }
    return texture.texels[index];
}

TextureLoadResult LoadPngTexture(const std::filesystem::path& path) {
    return TextureLoadResult{RenderTexture{}, false, "PNG texture loading not implemented yet: " + path.generic_string()};
}

} // namespace yr
