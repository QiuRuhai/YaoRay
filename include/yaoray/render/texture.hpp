#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};

struct TextureLoadResult {
    RenderTexture texture;
    bool ok = false;
    std::string error;
};

float SrgbToLinear(float value);

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv);

TextureLoadResult LoadPngTexture(const std::filesystem::path& path);
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path);

} // namespace yr
