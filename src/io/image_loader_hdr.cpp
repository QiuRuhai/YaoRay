#include <yaoray/io/image_loader.hpp>

#include "image_loader_internal.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace yr {
namespace {

TextureLoadResult LoadPfmTexture(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return TextureLoadResult{
            {}, false, "PFM environment file not found or unreadable: " + path.generic_string()
        };
    }

    std::string magic;
    int width = 0;
    int height = 0;
    float scale = 0.0f;
    file >> magic;
    if (magic != "PF" && magic != "Pf") {
        return TextureLoadResult{
            {}, false, "PFM file has unknown magic: " + path.generic_string()
        };
    }
    const int channels = magic == "PF" ? 3 : 1;
    file >> width >> height >> scale;
    if (width <= 0 || height <= 0 || !std::isfinite(scale) || scale == 0.0f) {
        return TextureLoadResult{
            {}, false,
            "PFM file has invalid header (width/height/scale): " + path.generic_string()
        };
    }
    file.get();

    const bool little_endian = scale < 0.0f;
    const float absolute_scale = std::fabs(scale);
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    std::vector<float> raw(pixel_count * static_cast<std::size_t>(channels));
    file.read(
        reinterpret_cast<char*>(raw.data()),
        static_cast<std::streamsize>(raw.size() * sizeof(float))
    );
    if (!file) {
        return TextureLoadResult{
            {}, false, "PFM file truncated or read failed: " + path.generic_string()
        };
    }
    if (!little_endian) {
        for (float& value : raw) {
            auto* bytes = reinterpret_cast<unsigned char*>(&value);
            std::swap(bytes[0], bytes[3]);
            std::swap(bytes[1], bytes[2]);
        }
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.filter = TextureFilter::Bilinear;
    texture.wrap_s = TextureWrap::Repeat;
    texture.wrap_t = TextureWrap::ClampToEdge;
    texture.color_space = TextureColorSpace::Linear;
    texture.texels.reserve(pixel_count);
    for (int y = 0; y < height; ++y) {
        const int source_y = height - 1 - y;
        for (int x = 0; x < width; ++x) {
            const std::size_t source =
                (static_cast<std::size_t>(source_y) * width + x) * channels;
            Color3f color;
            if (channels == 3) {
                color = Color3f{
                    raw[source] * absolute_scale,
                    raw[source + 1] * absolute_scale,
                    raw[source + 2] * absolute_scale
                };
            } else {
                const float value = raw[source] * absolute_scale;
                color = Color3f{value, value, value};
            }
            if (!image_loader_detail::IsFiniteColor(color)) {
                return TextureLoadResult{
                    {}, false, "PFM file contains non-finite texels: " + path.generic_string()
                };
            }
            texture.texels.push_back(Color4f{color});
        }
    }
    return TextureLoadResult{std::move(texture), true, {}};
}

TextureLoadResult LoadRadianceHdrTexture(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{
            {}, false, "HDR environment file not found: " + path.generic_string()
        };
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            {}, false,
            "failed to load HDR environment: " + path.generic_string() +
                (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{
            {}, false, "HDR environment has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.filter = TextureFilter::Bilinear;
    texture.wrap_s = TextureWrap::Repeat;
    texture.wrap_t = TextureWrap::ClampToEdge;
    texture.color_space = TextureColorSpace::Linear;
    texture.texels.reserve(static_cast<std::size_t>(width) * height);
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 3;
        const Color3f color{pixels[base], pixels[base + 1], pixels[base + 2]};
        if (!image_loader_detail::IsFiniteColor(color)) {
            stbi_image_free(pixels);
            return TextureLoadResult{
                {}, false, "HDR environment contains non-finite texels: " + path.generic_string()
            };
        }
        texture.texels.push_back(Color4f{color});
    }
    stbi_image_free(pixels);
    return TextureLoadResult{std::move(texture), true, {}};
}

} // namespace

TextureLoadResult LoadHdrTexture(const std::filesystem::path& path) {
    const std::string extension = image_loader_detail::LowerExtension(path);
    if (extension == ".pfm") return LoadPfmTexture(path);
    if (extension == ".exr") return image_loader_detail::LoadExrTexture(path);
    if (extension == ".hdr") return LoadRadianceHdrTexture(path);
    return TextureLoadResult{
        {}, false,
        "HDR environment path must use a .hdr, .pfm, or .exr extension: " +
            path.generic_string()
    };
}

} // namespace yr
