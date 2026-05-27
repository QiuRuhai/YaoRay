#pragma once

#include <cstdint>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

class CpuSampler {
public:
    CpuSampler(
        RenderSamplerKind kind,
        std::uint64_t seed,
        int sample_index,
        int samples_per_pixel,
        int light_samples
    );

    Vec2f NextPixel2D();
    Vec2f NextLight2D(int light_sample_index);
    float Next1D();
    Vec2f Next2D();

private:
    std::uint32_t NextU32();
    float NextRandomFloat();
    Vec2f NextStratified2D(int sample_index, int sample_count);

    RenderSamplerKind kind_ = RenderSamplerKind::Independent;
    std::uint64_t state_ = 0;
    int sample_index_ = 0;
    int samples_per_pixel_ = 1;
    int light_samples_ = 1;
};

std::uint64_t SeedForPixelSample(std::uint64_t scene_seed, int x, int y, int sample);

} // namespace yr
