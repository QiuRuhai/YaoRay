#pragma once

#include <cstdint>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/render_options.hpp>

namespace yr {

// Stable semantic dimensions keep a path's random choices independent of call
// order. Two-dimensional entries consume the named dimension and the next one.
enum class SampleDimension : std::uint32_t {
    Pixel = 0,
    DirectLightSelect = 2,
    DirectLightSurface = 3,
    Bsdf = 5,
    RussianRoulette = 7,
    BssrdfReflect = 8,
    BssrdfAxis = 9,
    BssrdfDisk = 10,
    BssrdfExitBsdf = 12,
    AnalyticLightSelect = 14,
};

class Sampler {
public:
    Sampler(
        RenderSamplerKind kind,
        std::uint64_t seed,
        int sample_index,
        int samples_per_pixel,
        int light_samples
    );
    Sampler(
        RenderSamplerKind kind,
        std::uint64_t scene_seed,
        int pixel_x,
        int pixel_y,
        int sample_index,
        int samples_per_pixel,
        int light_samples
    );

    void BeginBounce(int depth);
    float Sample1D(SampleDimension dimension, int lane = 0) const;
    Vec2f Sample2D(SampleDimension dimension, int lane = 0) const;

    // Compatibility cursor for callers that have not yet assigned semantics.
    Vec2f NextPixel2D();
    Vec2f NextLight2D(int light_sample_index);
    float Next1D();
    Vec2f Next2D();

private:
    std::uint32_t NextU32();
    float NextRandomFloat();
    Vec2f NextStratified2D(int sample_index, int sample_count);
    float Sample1DAt(std::uint32_t dimension, int lane) const;
    Vec2f Sample2DAt(std::uint32_t dimension, int lane) const;
    std::uint32_t AbsoluteDimension(SampleDimension dimension) const;

    RenderSamplerKind kind_ = RenderSamplerKind::Independent;
    std::uint64_t state_ = 0;
    std::uint64_t scene_seed_ = 0;
    std::uint64_t pixel_seed_ = 0;
    std::uint32_t sobol_index_ = 0;
    int pixel_x_ = 0;
    int pixel_y_ = 0;
    int sample_index_ = 0;
    int samples_per_pixel_ = 1;
    int light_samples_ = 1;
    int bounce_ = 0;
    std::uint32_t compatibility_dimension_ = 14;
};

std::uint64_t SeedForPixelSample(std::uint64_t scene_seed, int x, int y, int sample);

} // namespace yr
