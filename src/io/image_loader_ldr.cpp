#include <yaoray/io/image_loader.hpp>

#include "image_loader_internal.hpp"

#include <yaoray/core/color.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace yr {
namespace {

bool IsSupportedLdrExtension(std::string_view extension) {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".tga" || extension == ".bmp";
}

float DecodeLdrChannel(unsigned char value, TextureColorSpace color_space) {
    const float normalized = static_cast<float>(value) / 255.0f;
    return color_space == TextureColorSpace::Srgb ? SrgbToLinear(normalized) : normalized;
}

} // namespace

TextureLoadResult LoadLdrTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space
) {
    const std::string extension = image_loader_detail::LowerExtension(path);
    if (!IsSupportedLdrExtension(extension)) {
        return TextureLoadResult{
            {}, false,
            "texture path must use a .png, .jpg, .jpeg, .tga, or .bmp extension: " +
                path.generic_string()
        };
    }
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{{}, false, "texture file not found: " + path.generic_string()};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            {}, false,
            "failed to load LDR texture: " + path.generic_string() +
                (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{
            {}, false, "LDR texture has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.color_space = color_space;
    texture.texels.reserve(static_cast<std::size_t>(width) * height);
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 4;
        texture.texels.push_back(Color4f{
            DecodeLdrChannel(pixels[base], color_space),
            DecodeLdrChannel(pixels[base + 1], color_space),
            DecodeLdrChannel(pixels[base + 2], color_space),
            static_cast<float>(pixels[base + 3]) / 255.0f
        });
    }
    stbi_image_free(pixels);
    return TextureLoadResult{std::move(texture), true, {}};
}

TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space
) {
    if (image_loader_detail::LowerExtension(path) != ".png") {
        return TextureLoadResult{
            {}, false, "texture path must use a .png extension: " + path.generic_string()
        };
    }
    return LoadLdrTexture(path, color_space);
}

} // namespace yr
