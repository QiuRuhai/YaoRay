#include <yaoray/shading/texture.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <span>
#include <unordered_map>

namespace yr {
namespace {

struct MipView {
    int width = 0;
    int height = 0;
    std::span<const Color4f> texels;
};

float ApplyWrap(float value, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) return std::clamp(value, 0.0f, 1.0f);
    const float base = std::floor(value);
    const float fraction = value - base;
    if (wrap == TextureWrap::MirroredRepeat) {
        return (static_cast<int>(base) & 1) == 0 ? fraction : 1.0f - fraction;
    }
    return fraction < 0.0f ? fraction + 1.0f : fraction;
}

int WrappedTexelIndex(int value, int count, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) return std::clamp(value, 0, count - 1);
    if (wrap == TextureWrap::MirroredRepeat) {
        const int period = count * 2;
        int wrapped = value % period;
        if (wrapped < 0) wrapped += period;
        return wrapped < count ? wrapped : period - 1 - wrapped;
    }
    int wrapped = value % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

MipView Level(const RenderTexture& texture, int level) {
    if (texture.sampling_cache) {
        const TextureSamplingCache& cache = *texture.sampling_cache;
        if (level <= 0 || cache.mip_levels.empty()) {
            return MipView{cache.width, cache.height, cache.texels};
        }
        const TextureMipLevel& mip = cache.mip_levels[static_cast<std::size_t>(
            std::min(level - 1, static_cast<int>(cache.mip_levels.size()) - 1))];
        return MipView{mip.width, mip.height, mip.texels};
    }
    if (level <= 0 || texture.mip_levels.empty()) {
        return MipView{texture.width, texture.height, texture.texels};
    }
    const TextureMipLevel& mip = texture.mip_levels[static_cast<std::size_t>(
        std::min(level - 1, static_cast<int>(texture.mip_levels.size()) - 1))];
    return MipView{mip.width, mip.height, mip.texels};
}

int LevelCount(const RenderTexture& texture) {
    return 1 + static_cast<int>(texture.sampling_cache
        ? texture.sampling_cache->mip_levels.size()
        : texture.mip_levels.size());
}

bool HasTextureData(const RenderTexture& texture) {
    return texture.sampling_cache
        ? !texture.sampling_cache->texels.empty()
        : !texture.texels.empty();
}

std::uint64_t TextureStorageHash(const RenderTexture& texture) {
    std::uint64_t hash = 14695981039346656037ull;
    auto mix = [&](std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
            hash *= 1099511628211ull;
        }
    };
    mix(static_cast<std::uint32_t>(texture.width));
    mix(static_cast<std::uint32_t>(texture.height));
    mix(static_cast<std::uint32_t>(texture.wrap_s));
    mix(static_cast<std::uint32_t>(texture.wrap_t));
    for (const Color4f texel : texture.texels) {
        mix(std::bit_cast<std::uint32_t>(texel.x));
        mix(std::bit_cast<std::uint32_t>(texel.y));
        mix(std::bit_cast<std::uint32_t>(texel.z));
        mix(std::bit_cast<std::uint32_t>(texel.w));
    }
    return hash;
}

Color4f TexelAt(MipView level, int x, int y, TextureWrap wrap_s, TextureWrap wrap_t) {
    if (level.width <= 0 || level.height <= 0 || level.texels.empty()) return Color4f{};
    const int ix = WrappedTexelIndex(x, level.width, wrap_s);
    const int iy = WrappedTexelIndex(y, level.height, wrap_t);
    const std::size_t index = static_cast<std::size_t>(iy) *
        static_cast<std::size_t>(level.width) + static_cast<std::size_t>(ix);
    return index < level.texels.size() ? level.texels[index] : Color4f{};
}

Color4f Lerp(Color4f a, Color4f b, float t) {
    return a * (1.0f - t) + b * t;
}

Color4f SampleNearest(MipView level, const RenderTexture& texture, Vec2f uv) {
    if (level.width <= 0 || level.height <= 0 || level.texels.empty()) return Color4f{};
    const int x = std::clamp(static_cast<int>(std::floor(
        ApplyWrap(uv.x, texture.wrap_s) * static_cast<float>(level.width))), 0, level.width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(
        ApplyWrap(uv.y, texture.wrap_t) * static_cast<float>(level.height))), 0, level.height - 1);
    return TexelAt(level, x, y, texture.wrap_s, texture.wrap_t);
}

Color4f SampleBilinear(MipView level, const RenderTexture& texture, Vec2f uv) {
    if (level.width <= 0 || level.height <= 0 || level.texels.empty()) return Color4f{};
    if (level.width == 1 && level.height == 1) return level.texels[0];
    const float s = ApplyWrap(uv.x, texture.wrap_s) * static_cast<float>(level.width) - 0.5f;
    const float t = ApplyWrap(uv.y, texture.wrap_t) * static_cast<float>(level.height) - 0.5f;
    const int s0 = static_cast<int>(std::floor(s));
    const int t0 = static_cast<int>(std::floor(t));
    const float ds = s - static_cast<float>(s0);
    const float dt = t - static_cast<float>(t0);
    const Color4f c00 = TexelAt(level, s0, t0, texture.wrap_s, texture.wrap_t);
    const Color4f c10 = TexelAt(level, s0 + 1, t0, texture.wrap_s, texture.wrap_t);
    const Color4f c01 = TexelAt(level, s0, t0 + 1, texture.wrap_s, texture.wrap_t);
    const Color4f c11 = TexelAt(level, s0 + 1, t0 + 1, texture.wrap_s, texture.wrap_t);
    return Lerp(Lerp(c00, c10, ds), Lerp(c01, c11, ds), dt);
}

float LodForFootprint(const RenderTexture& texture, TextureFootprint footprint) {
    const float width = static_cast<float>(std::max(1, texture.width));
    const float height = static_cast<float>(std::max(1, texture.height));
    const float dx = std::hypot(footprint.dudx * width, footprint.dvdx * height);
    const float dy = std::hypot(footprint.dudy * width, footprint.dvdy * height);
    return std::max(0.0f, std::log2(std::max(1.0f, std::max(dx, dy))));
}

const std::array<float, 128>& EwaWeights() {
    static const std::array<float, 128> weights = [] {
        std::array<float, 128> values{};
        constexpr float alpha = 2.0f;
        const float edge = std::exp(-alpha);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float r2 = static_cast<float>(i) / static_cast<float>(values.size() - 1);
            values[i] = std::exp(-alpha * r2) - edge;
        }
        return values;
    }();
    return weights;
}

Color4f SampleEwaLevel(const RenderTexture& texture, Vec2f uv,
    TextureFootprint footprint, int level_index) {
    const MipView level = Level(texture, level_index);
    if (level.width <= 0 || level.height <= 0 || level.texels.empty()) return Color4f{};

    float dsdx = footprint.dudx * static_cast<float>(level.width);
    float dtdx = footprint.dvdx * static_cast<float>(level.height);
    float dsdy = footprint.dudy * static_cast<float>(level.width);
    float dtdy = footprint.dvdy * static_cast<float>(level.height);
    if (dsdx * dsdx + dtdx * dtdx < dsdy * dsdy + dtdy * dtdy) {
        std::swap(dsdx, dsdy);
        std::swap(dtdx, dtdy);
    }
    const float major = std::hypot(dsdx, dtdx);
    float minor = std::hypot(dsdy, dtdy);
    constexpr float max_anisotropy = 8.0f;
    if (minor > 0.0f && major > minor * max_anisotropy) {
        const float scale = major / (minor * max_anisotropy);
        dsdy *= scale;
        dtdy *= scale;
        minor *= scale;
    }
    if (minor == 0.0f) return SampleBilinear(level, texture, uv);

    const float s = ApplyWrap(uv.x, texture.wrap_s) * static_cast<float>(level.width) - 0.5f;
    const float t = ApplyWrap(uv.y, texture.wrap_t) * static_cast<float>(level.height) - 0.5f;
    float a = dtdx * dtdx + dtdy * dtdy + 1.0f;
    float b = -2.0f * (dsdx * dtdx + dsdy * dtdy);
    float c = dsdx * dsdx + dsdy * dsdy + 1.0f;
    const float inverse = 1.0f / (a * c - 0.25f * b * b);
    a *= inverse;
    b *= inverse;
    c *= inverse;
    const float determinant = -b * b + 4.0f * a * c;
    if (determinant <= 0.0f) return SampleBilinear(level, texture, uv);
    const float inverse_det = 1.0f / determinant;
    const float radius_s = 2.0f * std::sqrt(c * inverse_det);
    const float radius_t = 2.0f * std::sqrt(a * inverse_det);
    const int s0 = static_cast<int>(std::ceil(s - radius_s));
    const int s1 = static_cast<int>(std::floor(s + radius_s));
    const int t0 = static_cast<int>(std::ceil(t - radius_t));
    const int t1 = static_cast<int>(std::floor(t + radius_t));

    Color4f sum;
    float weight_sum = 0.0f;
    const auto& weights = EwaWeights();
    for (int it = t0; it <= t1; ++it) {
        for (int is = s0; is <= s1; ++is) {
            const float ss = static_cast<float>(is) - s;
            const float tt = static_cast<float>(it) - t;
            const float r2 = a * ss * ss + b * ss * tt + c * tt * tt;
            if (r2 >= 1.0f) continue;
            const std::size_t weight_index = std::min(weights.size() - 1,
                static_cast<std::size_t>(r2 * static_cast<float>(weights.size())));
            const float weight = weights[weight_index];
            sum = sum + TexelAt(level, is, it, texture.wrap_s, texture.wrap_t) * weight;
            weight_sum += weight;
        }
    }
    return weight_sum > 0.0f ? sum / weight_sum : SampleBilinear(level, texture, uv);
}

Color4f SampleTrilinear(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint) {
    const float lod = std::min(LodForFootprint(texture, footprint),
        static_cast<float>(LevelCount(texture) - 1));
    const int level0 = static_cast<int>(std::floor(lod));
    const int level1 = std::min(level0 + 1, LevelCount(texture) - 1);
    return Lerp(SampleBilinear(Level(texture, level0), texture, uv),
        SampleBilinear(Level(texture, level1), texture, uv), lod - static_cast<float>(level0));
}

Color4f SampleEwa(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint) {
    const float width = static_cast<float>(std::max(1, texture.width));
    const float height = static_cast<float>(std::max(1, texture.height));
    const float dx = std::hypot(footprint.dudx * width, footprint.dvdx * height);
    const float dy = std::hypot(footprint.dudy * width, footprint.dvdy * height);
    const float minor = std::max(1.0f, std::min(dx, dy));
    const float lod = std::min(std::max(0.0f, std::log2(minor)),
        static_cast<float>(LevelCount(texture) - 1));
    const int level0 = static_cast<int>(std::floor(lod));
    const int level1 = std::min(level0 + 1, LevelCount(texture) - 1);
    return Lerp(SampleEwaLevel(texture, uv, footprint, level0),
        SampleEwaLevel(texture, uv, footprint, level1), lod - static_cast<float>(level0));
}

} // namespace

void BuildTextureMipChain(RenderTexture& texture) {
    texture.sampling_cache.reset();
    texture.mip_levels.clear();
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) return;
    int source_width = texture.width;
    int source_height = texture.height;
    std::span<const Color4f> source = texture.texels;
    while (source_width > 1 || source_height > 1) {
        TextureMipLevel level;
        level.width = std::max(1, (source_width + 1) / 2);
        level.height = std::max(1, (source_height + 1) / 2);
        level.texels.resize(static_cast<std::size_t>(level.width) *
            static_cast<std::size_t>(level.height));
        const MipView source_view{source_width, source_height, source};
        for (int y = 0; y < level.height; ++y) {
            for (int x = 0; x < level.width; ++x) {
                const Color4f filtered =
                    TexelAt(source_view, x * 2, y * 2, texture.wrap_s, texture.wrap_t) +
                    TexelAt(source_view, x * 2 + 1, y * 2, texture.wrap_s, texture.wrap_t) +
                    TexelAt(source_view, x * 2, y * 2 + 1, texture.wrap_s, texture.wrap_t) +
                    TexelAt(source_view, x * 2 + 1, y * 2 + 1, texture.wrap_s, texture.wrap_t);
                level.texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(level.width) +
                    static_cast<std::size_t>(x)] = filtered * 0.25f;
            }
        }
        texture.mip_levels.push_back(std::move(level));
        const TextureMipLevel& stored = texture.mip_levels.back();
        source_width = stored.width;
        source_height = stored.height;
        source = stored.texels;
    }
}

void BuildTextureSamplingCaches(std::span<RenderTexture> textures) {
    std::unordered_map<std::uint64_t, std::shared_ptr<const TextureSamplingCache>> cache_by_hash;
    cache_by_hash.reserve(textures.size());
    for (RenderTexture& texture : textures) {
        BuildTextureMipChain(texture);
        if (texture.texels.empty()) continue;
        const std::uint64_t hash = TextureStorageHash(texture);
        const auto found = cache_by_hash.find(hash);
        if (found != cache_by_hash.end()) {
            texture.sampling_cache = found->second;
            texture.texels.clear();
            texture.texels.shrink_to_fit();
            texture.mip_levels.clear();
            texture.mip_levels.shrink_to_fit();
            continue;
        }
        auto cache = std::make_shared<TextureSamplingCache>();
        cache->width = texture.width;
        cache->height = texture.height;
        cache->texels = std::move(texture.texels);
        cache->mip_levels = std::move(texture.mip_levels);
        texture.sampling_cache = cache;
        cache_by_hash.emplace(hash, std::move(cache));
    }
}

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv) {
    return SampleTexture4(texture, uv).rgb();
}

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint) {
    return SampleTexture4(texture, uv, footprint).rgb();
}

Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv) {
    return SampleTexture4(texture, uv, TextureFootprint{});
}

Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv, TextureFootprint footprint) {
    if (texture.width <= 0 || texture.height <= 0 || !HasTextureData(texture)) return Color4f{};
    if (texture.filter == TextureFilter::Nearest) return SampleNearest(Level(texture, 0), texture, uv);
    if (texture.filter == TextureFilter::Trilinear && !footprint.IsZero() && LevelCount(texture) > 1) {
        return SampleTrilinear(texture, uv, footprint);
    }
    if (texture.filter == TextureFilter::Ewa && !footprint.IsZero() && LevelCount(texture) > 1) {
        return SampleEwa(texture, uv, footprint);
    }
    return SampleBilinear(Level(texture, 0), texture, uv);
}

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv) {
    return SampleNearest(Level(texture, 0), texture, uv).rgb();
}

Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv) {
    return SampleBilinear(Level(texture, 0), texture, uv).rgb();
}

float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv) {
    return SampleTexture4(texture, uv).w;
}

} // namespace yr
