#include "image_loader_internal.hpp"

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace yr::image_loader_detail {

TextureLoadResult LoadExrTexture(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{
            {}, false, "EXR environment file not found: " + path.generic_string()
        };
    }

    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* error = nullptr;
    const std::string path_string = path.generic_string();
    const int status = LoadEXR(&rgba, &width, &height, path_string.c_str(), &error);
    if (status != TINYEXR_SUCCESS) {
        const std::string message = error == nullptr ? "unknown error" : error;
        if (error != nullptr) FreeEXRErrorMessage(error);
        return TextureLoadResult{
            {}, false,
            "failed to load EXR environment: " + path.generic_string() + " (" + message + ")"
        };
    }
    if (rgba == nullptr || width <= 0 || height <= 0) {
        std::free(rgba);
        return TextureLoadResult{
            {}, false, "EXR environment has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.filter = TextureFilter::Bilinear;
    texture.wrap_s = TextureWrap::Repeat;
    texture.wrap_t = TextureWrap::ClampToEdge;
    texture.color_space = TextureColorSpace::Linear;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    texture.texels.reserve(pixel_count);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t base = i * 4;
        const Color3f color{rgba[base], rgba[base + 1], rgba[base + 2]};
        if (!IsFiniteColor(color)) {
            std::free(rgba);
            return TextureLoadResult{
                {}, false, "EXR environment contains non-finite texels: " + path.generic_string()
            };
        }
        texture.texels.push_back(Color4f{color.x, color.y, color.z, rgba[base + 3]});
    }
    std::free(rgba);
    return TextureLoadResult{std::move(texture), true, {}};
}

} // namespace yr::image_loader_detail
