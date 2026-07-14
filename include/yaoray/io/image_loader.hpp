#pragma once

#include <filesystem>
#include <string>

#include <yaoray/scene/texture.hpp>

namespace yr {

struct TextureLoadResult {
    RenderTexture texture;
    bool ok = false;
    std::string error;
};

TextureLoadResult LoadLdrTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path);

} // namespace yr
