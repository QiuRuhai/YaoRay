#include <yaoray/sampling/sampler.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace yr {
namespace {

constexpr std::uint64_t DefaultSeed = 0x9E3779B97F4A7C15ull;

struct StratifiedGrid {
    int columns = 1;
    int rows = 1;
};

int AtLeastOne(int value) {
    return std::max(1, value);
}

StratifiedGrid MakeStratifiedGrid(int sample_count) {
    const int count = AtLeastOne(sample_count);
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
    const int rows = (count + columns - 1) / columns;
    return StratifiedGrid{columns, rows};
}

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, std::nextafter(1.0f, 0.0f));
}

std::uint64_t Mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

std::uint32_t Mix32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

std::uint32_t Hash32(std::uint64_t seed, std::uint32_t dimension, int lane) {
    const std::uint64_t mixed = Mix64(seed ^
        (static_cast<std::uint64_t>(dimension) * 0xD1B54A32D192ED03ull) ^
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lane)) *
            0x94D049BB133111EBull));
    return Mix32(static_cast<std::uint32_t>(mixed) ^ static_cast<std::uint32_t>(mixed >> 32));
}

float UintToUnitFloat(std::uint32_t value) {
    constexpr float scale = 1.0f / 16777216.0f;
    return static_cast<float>(value >> 8) * scale;
}

std::uint32_t ReverseBits(std::uint32_t value) {
    value = ((value & 0x55555555u) << 1) | ((value >> 1) & 0x55555555u);
    value = ((value & 0x33333333u) << 2) | ((value >> 2) & 0x33333333u);
    value = ((value & 0x0F0F0F0Fu) << 4) | ((value >> 4) & 0x0F0F0F0Fu);
    value = ((value & 0x00FF00FFu) << 8) | ((value >> 8) & 0x00FF00FFu);
    return (value << 16) | (value >> 16);
}

std::uint32_t SobolBits(std::uint32_t index, int axis) {
    if (axis == 0) return ReverseBits(index);

    static constexpr std::array<std::uint32_t, 32> directions = [] {
        std::array<std::uint32_t, 32> values{};
        std::uint32_t direction = 0x80000000u;
        for (std::uint32_t& entry : values) {
            entry = direction;
            direction ^= direction >> 1;
        }
        return values;
    }();
    std::uint32_t value = 0;
    while (index != 0) {
        const unsigned bit = std::countr_zero(index);
        value ^= directions[bit];
        index &= index - 1;
    }
    return value;
}

// Fast nested-uniform Owen scrambling via the Laine-Karras permutation. The
// bit reversal makes the permutation operate on prefixes, preserving the
// digital-net stratification without a hash invocation for every output bit.
std::uint32_t OwenScramble(std::uint32_t value, std::uint32_t seed) {
    value = ReverseBits(value);
    value += seed;
    value ^= value * 0x6C50B47Cu;
    value ^= value * 0xB82F1E52u;
    value ^= value * 0xC7AFE638u;
    value ^= value * 0x8D22F6E6u;
    return ReverseBits(value);
}

std::uint32_t SpreadBits16(std::uint32_t value) {
    value &= 0x0000FFFFu;
    value = (value | (value << 8)) & 0x00FF00FFu;
    value = (value | (value << 4)) & 0x0F0F0F0Fu;
    value = (value | (value << 2)) & 0x33333333u;
    value = (value | (value << 1)) & 0x55555555u;
    return value;
}

std::uint32_t Morton2D(int x, int y) {
    return SpreadBits16(static_cast<std::uint32_t>(x)) |
        (SpreadBits16(static_cast<std::uint32_t>(y)) << 1);
}

constexpr std::uint32_t BounceDimensionStride = 16;

} // namespace

Sampler::Sampler(
    RenderSamplerKind kind,
    std::uint64_t seed,
    int sample_index,
    int samples_per_pixel,
    int light_samples
)
    : kind_(kind),
      state_(seed == 0 ? DefaultSeed : seed),
      scene_seed_(seed),
      pixel_seed_(Mix64(seed)),
      sobol_index_(static_cast<std::uint32_t>(sample_index)),
      sample_index_(sample_index),
      samples_per_pixel_(AtLeastOne(samples_per_pixel)),
      light_samples_(AtLeastOne(light_samples)) {}

Sampler::Sampler(
    RenderSamplerKind kind,
    std::uint64_t scene_seed,
    int pixel_x,
    int pixel_y,
    int sample_index,
    int samples_per_pixel,
    int light_samples
)
    : kind_(kind),
      state_(SeedForPixelSample(scene_seed, pixel_x, pixel_y, sample_index)),
      scene_seed_(scene_seed),
      pixel_seed_(Mix64(scene_seed ^
          (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pixel_x)) << 32) ^
          static_cast<std::uint32_t>(pixel_y))),
      pixel_x_(pixel_x),
      pixel_y_(pixel_y),
      sample_index_(sample_index),
      samples_per_pixel_(AtLeastOne(samples_per_pixel)),
      light_samples_(AtLeastOne(light_samples)) {
    if (kind_ == RenderSamplerKind::ZSobol) {
        const unsigned sample_bits = std::bit_width(
            std::bit_ceil(static_cast<unsigned>(samples_per_pixel_))) - 1u;
        sobol_index_ = (Morton2D(pixel_x_, pixel_y_) << sample_bits) |
            static_cast<std::uint32_t>(sample_index_);
    } else {
        sobol_index_ = static_cast<std::uint32_t>(sample_index_);
    }
}

void Sampler::BeginBounce(int depth) {
    bounce_ = std::max(0, depth);
    compatibility_dimension_ = 14;
}

std::uint32_t Sampler::AbsoluteDimension(SampleDimension dimension) const {
    const std::uint32_t base = static_cast<std::uint32_t>(dimension);
    return dimension == SampleDimension::Pixel
        ? base
        : base + static_cast<std::uint32_t>(bounce_) * BounceDimensionStride;
}

float Sampler::Sample1DAt(std::uint32_t dimension, int lane) const {
    if (kind_ == RenderSamplerKind::OwenSobol) {
        return UintToUnitFloat(OwenScramble(
            SobolBits(sobol_index_, 0),
            Hash32(pixel_seed_ ^ DefaultSeed, dimension, lane)));
    }
    if (kind_ == RenderSamplerKind::ZSobol) {
        return UintToUnitFloat(OwenScramble(
            SobolBits(sobol_index_, 0), Hash32(scene_seed_, dimension, lane)));
    }
    return UintToUnitFloat(Hash32(
        pixel_seed_ ^ static_cast<std::uint64_t>(sample_index_), dimension, lane));
}

Vec2f Sampler::Sample2DAt(std::uint32_t dimension, int lane) const {
    if (dimension == static_cast<std::uint32_t>(SampleDimension::Pixel) &&
        samples_per_pixel_ == 1) {
        return Vec2f{0.5f, 0.5f};
    }
    if (kind_ == RenderSamplerKind::Stratified) {
        const int count = dimension == static_cast<std::uint32_t>(SampleDimension::Pixel)
            ? samples_per_pixel_
            : light_samples_;
        const int index = dimension == static_cast<std::uint32_t>(SampleDimension::Pixel)
            ? sample_index_
            : lane;
        const StratifiedGrid grid = MakeStratifiedGrid(count);
        const int clamped_index = std::clamp(index, 0, count - 1);
        const int cell_x = clamped_index % grid.columns;
        const int cell_y = clamped_index / grid.columns;
        return Vec2f{
            ClampUnit((static_cast<float>(cell_x) + Sample1DAt(dimension, lane)) /
                static_cast<float>(grid.columns)),
            ClampUnit((static_cast<float>(cell_y) + Sample1DAt(dimension + 1, lane)) /
                static_cast<float>(grid.rows))};
    }

    if (kind_ == RenderSamplerKind::OwenSobol || kind_ == RenderSamplerKind::ZSobol) {
        const std::uint64_t scramble_seed = kind_ == RenderSamplerKind::OwenSobol
            ? pixel_seed_
            : scene_seed_;
        return Vec2f{
            UintToUnitFloat(OwenScramble(SobolBits(sobol_index_, 0),
                Hash32(scramble_seed, dimension, lane))),
            UintToUnitFloat(OwenScramble(SobolBits(sobol_index_, 1),
                Hash32(scramble_seed, dimension + 1, lane)))};
    }

    return Vec2f{Sample1DAt(dimension, lane), Sample1DAt(dimension + 1, lane)};
}

float Sampler::Sample1D(SampleDimension dimension, int lane) const {
    return Sample1DAt(AbsoluteDimension(dimension), lane);
}

Vec2f Sampler::Sample2D(SampleDimension dimension, int lane) const {
    return Sample2DAt(AbsoluteDimension(dimension), lane);
}

std::uint32_t Sampler::NextU32() {
    std::uint64_t x = state_;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state_ = x;
    return static_cast<std::uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 32);
}

float Sampler::NextRandomFloat() {
    constexpr float scale = 1.0f / 16777216.0f;
    return static_cast<float>(NextU32() >> 8) * scale;
}

Vec2f Sampler::NextStratified2D(int sample_index, int sample_count) {
    const int count = AtLeastOne(sample_count);
    const int clamped_index = std::clamp(sample_index, 0, count - 1);
    const StratifiedGrid grid = MakeStratifiedGrid(count);
    const int cell_x = clamped_index % grid.columns;
    const int cell_y = clamped_index / grid.columns;

    const float x = (static_cast<float>(cell_x) + NextRandomFloat()) / static_cast<float>(grid.columns);
    const float y = (static_cast<float>(cell_y) + NextRandomFloat()) / static_cast<float>(grid.rows);
    return Vec2f{ClampUnit(x), ClampUnit(y)};
}

Vec2f Sampler::NextPixel2D() {
    if (samples_per_pixel_ == 1) {
        return Vec2f{0.5f, 0.5f};
    }
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(sample_index_, samples_per_pixel_);
    }
    if (kind_ == RenderSamplerKind::OwenSobol || kind_ == RenderSamplerKind::ZSobol) {
        return Sample2D(SampleDimension::Pixel);
    }
    return Next2D();
}

Vec2f Sampler::NextLight2D(int light_sample_index) {
    if (kind_ == RenderSamplerKind::OwenSobol || kind_ == RenderSamplerKind::ZSobol) {
        return Sample2D(SampleDimension::DirectLightSurface, light_sample_index);
    }
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(light_sample_index, light_samples_);
    }
    return Next2D();
}

float Sampler::Next1D() {
    if (kind_ == RenderSamplerKind::OwenSobol || kind_ == RenderSamplerKind::ZSobol) {
        return Sample1DAt(compatibility_dimension_++, 0);
    }
    return NextRandomFloat();
}

Vec2f Sampler::Next2D() {
    if (kind_ == RenderSamplerKind::OwenSobol || kind_ == RenderSamplerKind::ZSobol) {
        const Vec2f sample = Sample2DAt(compatibility_dimension_, 0);
        compatibility_dimension_ += 2;
        return sample;
    }
    return Vec2f{NextRandomFloat(), NextRandomFloat()};
}

std::uint64_t SeedForPixelSample(std::uint64_t scene_seed, int x, int y, int sample) {
    std::uint64_t seed = Mix64(scene_seed);
    seed ^= Mix64(static_cast<std::uint64_t>(x) + 0xA24BAED4963EE407ull);
    seed ^= Mix64(static_cast<std::uint64_t>(y) + 0x9FB21C651E98DF25ull);
    seed ^= Mix64(static_cast<std::uint64_t>(sample) + 0xC2B2AE3D27D4EB4Full);
    return Mix64(seed);
}

} // namespace yr
