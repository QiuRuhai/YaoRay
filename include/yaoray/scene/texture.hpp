#pragma once

#include <memory>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/texture_options.hpp>

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

struct TextureMipLevel {
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
};

struct TextureSamplingCache {
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    std::vector<TextureMipLevel> mip_levels;
};

struct RenderTexture {
    RenderTextureKind kind = RenderTextureKind::Image;
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    // Cached levels 1..N; level 0 remains in texels to preserve the scene IR.
    std::vector<TextureMipLevel> mip_levels;
    std::shared_ptr<const TextureSamplingCache> sampling_cache;
    Color4f constant_value;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureColorSpace color_space = TextureColorSpace::Linear;
};

} // namespace yr
