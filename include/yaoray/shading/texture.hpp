#pragma once

#include <yaoray/core/color.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/texture.hpp>

#include <span>

namespace yr {

struct TextureFootprint {
    float dudx = 0.0f;
    float dvdx = 0.0f;
    float dudy = 0.0f;
    float dvdy = 0.0f;

    bool IsZero() const {
        return dudx == 0.0f && dvdx == 0.0f && dudy == 0.0f && dvdy == 0.0f;
    }
};

void BuildTextureMipChain(RenderTexture& texture);
void BuildTextureSamplingCaches(std::span<RenderTexture> textures);
Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
Color3f SampleTexture(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint);
Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv);
Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint);
Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv);
float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv);

} // namespace yr
