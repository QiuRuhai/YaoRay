#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

enum class TextureColorSpace {
    Srgb,
    Linear,
};

enum class RenderTextureKind {
    Image,
    Constant,
    Scale,
    Checkerboard,
};

struct RenderTexture {
    RenderTextureKind kind = RenderTextureKind::Image;
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    Color4f constant_value;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureColorSpace color_space = TextureColorSpace::Linear;
};

struct TextureLoadResult {
    RenderTexture texture;
    bool ok = false;
    std::string error;
};

float SrgbToLinear(float value);

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv);
float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv);

TextureLoadResult LoadLdrTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path);

} // namespace yr
