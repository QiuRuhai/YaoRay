#include <yaoray/render/texture.hpp>

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

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

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
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
    if (LowerExtension(path) != ".png") {
        return TextureLoadResult{RenderTexture{}, false, "texture path must use a .png extension: " + path.generic_string()};
    }
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{RenderTexture{}, false, "texture file not found: " + path.generic_string()};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            RenderTexture{},
            false,
            "failed to load PNG texture: " + path.generic_string() + (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 4;
        texture.texels.push_back(Color3f{
            static_cast<float>(pixels[base + 0]) / 255.0f,
            static_cast<float>(pixels[base + 1]) / 255.0f,
            static_cast<float>(pixels[base + 2]) / 255.0f
        });
    }
    stbi_image_free(pixels);

    return TextureLoadResult{std::move(texture), true, {}};
}

} // namespace yr
