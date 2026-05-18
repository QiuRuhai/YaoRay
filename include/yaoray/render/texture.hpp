#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
};

struct TextureLoadResult {
    RenderTexture texture;
    bool ok = false;
    std::string error;
};

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);

TextureLoadResult LoadPngTexture(const std::filesystem::path& path);

} // namespace yr
