#include <yaoray/render/texture.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace yr {
namespace {

float ApplyWrap(float value, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) {
        return std::clamp(value, 0.0f, 1.0f);
    }
    const float base = std::floor(value);
    const float fraction = value - base;
    if (wrap == TextureWrap::MirroredRepeat) {
        const int interval = static_cast<int>(base);
        return (interval & 1) == 0 ? fraction : 1.0f - fraction;
    }
    return fraction < 0.0f ? fraction + 1.0f : fraction;
}

int NearestIndex(float value, int count, TextureWrap wrap) {
    const float wrapped = ApplyWrap(value, wrap);
    return std::clamp(static_cast<int>(std::floor(wrapped * static_cast<float>(count))), 0, count - 1);
}

int WrappedTexelIndex(int value, int count, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) {
        return std::clamp(value, 0, count - 1);
    }
    if (wrap == TextureWrap::MirroredRepeat) {
        const int period = count * 2;
        int wrapped = value % period;
        if (wrapped < 0) {
            wrapped += period;
        }
        return wrapped < count ? wrapped : period - 1 - wrapped;
    }
    int wrapped = value % count;
    if (wrapped < 0) {
        wrapped += count;
    }
    return wrapped;
}

Color4f TexelAt(const RenderTexture& texture, int x, int y) {
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                              static_cast<std::size_t>(x);
    if (index >= texture.texels.size()) {
        return Color4f{};
    }
    return texture.texels[index];
}

Color4f Lerp(Color4f a, Color4f b, float t) {
    const float one_minus_t = 1.0f - t;
    return Color4f{
        a.x * one_minus_t + b.x * t,
        a.y * one_minus_t + b.y * t,
        a.z * one_minus_t + b.z * t,
        a.w * one_minus_t + b.w * t
    };
}

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool IsFiniteColor(Color3f color) {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}

} // namespace

float SrgbToLinear(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped <= 0.04045f) {
        return clamped / 12.92f;
    }
    return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
}

namespace {

Color4f SampleTextureNearest4(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color4f{};
    }

    const int x = NearestIndex(uv.x, texture.width, texture.wrap_s);
    const int y = NearestIndex(uv.y, texture.height, texture.wrap_t);
    return TexelAt(texture, x, y);
}

Color4f SampleTextureBilinear4(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color4f{};
    }
    if (texture.width == 1 && texture.height == 1) {
        return texture.texels[0];
    }

    const float x = ApplyWrap(uv.x, texture.wrap_s) * static_cast<float>(texture.width) - 0.5f;
    const float y = ApplyWrap(uv.y, texture.wrap_t) * static_cast<float>(texture.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const int ix0 = WrappedTexelIndex(x0, texture.width, texture.wrap_s);
    const int ix1 = WrappedTexelIndex(x0 + 1, texture.width, texture.wrap_s);
    const int iy0 = WrappedTexelIndex(y0, texture.height, texture.wrap_t);
    const int iy1 = WrappedTexelIndex(y0 + 1, texture.height, texture.wrap_t);

    const Color4f c00 = TexelAt(texture, ix0, iy0);
    const Color4f c10 = TexelAt(texture, ix1, iy0);
    const Color4f c01 = TexelAt(texture, ix0, iy1);
    const Color4f c11 = TexelAt(texture, ix1, iy1);
    return Lerp(Lerp(c00, c10, tx), Lerp(c01, c11, tx), ty);
}

bool IsSupportedLdrExtension(std::string_view extension) {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

float DecodeLdrChannel(unsigned char value, TextureColorSpace color_space) {
    const float normalized = static_cast<float>(value) / 255.0f;
    return color_space == TextureColorSpace::Srgb ? SrgbToLinear(normalized) : normalized;
}

} // namespace

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv) {
    return SampleTexture4(texture, uv).rgb();
}

Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv) {
    if (texture.filter == TextureFilter::Nearest) {
        return SampleTextureNearest4(texture, uv);
    }
    return SampleTextureBilinear4(texture, uv);
}

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv) {
    return SampleTextureNearest4(texture, uv).rgb();
}

Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv) {
    return SampleTextureBilinear4(texture, uv).rgb();
}

float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv) {
    return SampleTexture4(texture, uv).w;
}

TextureLoadResult LoadLdrTexture(const std::filesystem::path& path, TextureColorSpace color_space) {
    const std::string extension = LowerExtension(path);
    if (!IsSupportedLdrExtension(extension)) {
        return TextureLoadResult{
            RenderTexture{},
            false,
            "texture path must use a .png, .jpg, or .jpeg extension: " + path.generic_string()
        };
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
            "failed to load LDR texture: " + path.generic_string() +
                (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{
            RenderTexture{},
            false,
            "LDR texture has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.color_space = color_space;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 4;
        texture.texels.push_back(Color4f{
            DecodeLdrChannel(pixels[base + 0], color_space),
            DecodeLdrChannel(pixels[base + 1], color_space),
            DecodeLdrChannel(pixels[base + 2], color_space),
            static_cast<float>(pixels[base + 3]) / 255.0f
        });
    }
    stbi_image_free(pixels);

    return TextureLoadResult{std::move(texture), true, {}};
}

TextureLoadResult LoadPngTexture(const std::filesystem::path& path, TextureColorSpace color_space) {
    if (LowerExtension(path) != ".png") {
        return TextureLoadResult{RenderTexture{}, false, "texture path must use a .png extension: " + path.generic_string()};
    }
    return LoadLdrTexture(path, color_space);
}

TextureLoadResult LoadHdrTexture(const std::filesystem::path& path) {
    if (LowerExtension(path) != ".hdr") {
        return TextureLoadResult{
            RenderTexture{},
            false,
            "HDR environment path must use a .hdr extension: " + path.generic_string()
        };
    }
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{
            RenderTexture{},
            false,
            "HDR environment file not found: " + path.generic_string()
        };
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            RenderTexture{},
            false,
            "failed to load HDR environment: " + path.generic_string() +
                (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{
            RenderTexture{},
            false,
            "HDR environment has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.filter = TextureFilter::Bilinear;
    texture.wrap_s = TextureWrap::Repeat;
    texture.wrap_t = TextureWrap::ClampToEdge;
    texture.color_space = TextureColorSpace::Linear;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 3;
        const Color3f color{pixels[base + 0], pixels[base + 1], pixels[base + 2]};
        if (!IsFiniteColor(color)) {
            stbi_image_free(pixels);
            return TextureLoadResult{
                RenderTexture{},
                false,
                "HDR environment contains non-finite texels: " + path.generic_string()
            };
        }
        texture.texels.push_back(Color4f{color});
    }
    stbi_image_free(pixels);

    if (texture.texels.empty()) {
        return TextureLoadResult{
            RenderTexture{},
            false,
            "HDR environment contains no texels: " + path.generic_string()
        };
    }
    return TextureLoadResult{std::move(texture), true, {}};
}

} // namespace yr
