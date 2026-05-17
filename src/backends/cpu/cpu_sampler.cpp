#include <yaoray/backends/cpu/cpu_sampler.hpp>

#include <algorithm>
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

} // namespace

CpuSampler::CpuSampler(
    RenderSamplerKind kind,
    std::uint64_t seed,
    int sample_index,
    int samples_per_pixel,
    int light_samples
)
    : kind_(kind),
      state_(seed == 0 ? DefaultSeed : seed),
      sample_index_(sample_index),
      samples_per_pixel_(AtLeastOne(samples_per_pixel)),
      light_samples_(AtLeastOne(light_samples)) {}

std::uint32_t CpuSampler::NextU32() {
    std::uint64_t x = state_;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state_ = x;
    return static_cast<std::uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 32);
}

float CpuSampler::NextRandomFloat() {
    constexpr float scale = 1.0f / 16777216.0f;
    return static_cast<float>(NextU32() >> 8) * scale;
}

Vec2f CpuSampler::NextStratified2D(int sample_index, int sample_count) {
    const int count = AtLeastOne(sample_count);
    const int clamped_index = std::clamp(sample_index, 0, count - 1);
    const StratifiedGrid grid = MakeStratifiedGrid(count);
    const int cell_x = clamped_index % grid.columns;
    const int cell_y = clamped_index / grid.columns;

    const float x = (static_cast<float>(cell_x) + NextRandomFloat()) / static_cast<float>(grid.columns);
    const float y = (static_cast<float>(cell_y) + NextRandomFloat()) / static_cast<float>(grid.rows);
    return Vec2f{ClampUnit(x), ClampUnit(y)};
}

Vec2f CpuSampler::NextPixel2D() {
    if (samples_per_pixel_ == 1) {
        return Vec2f{0.5f, 0.5f};
    }
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(sample_index_, samples_per_pixel_);
    }
    return Next2D();
}

Vec2f CpuSampler::NextLight2D(int light_sample_index) {
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(light_sample_index, light_samples_);
    }
    return Next2D();
}

float CpuSampler::Next1D() {
    return NextRandomFloat();
}

Vec2f CpuSampler::Next2D() {
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
