#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct FilmSample {
    Color3f beauty;
    Color3f albedo;
    Vec3f normal;
    float depth = 0.0f;
    bool primary_hit = false;
};

struct FilmPixel {
    Color3f sum;
    std::uint32_t samples = 0;
    float luminance_mean = 0.0f;
    float luminance_m2 = 0.0f;
    Color3f albedo_sum;
    Vec3f normal_sum;
    float depth_sum = 0.0f;
    std::uint32_t aov_samples = 0;
};

class Film {
public:
    Film(int width, int height);

    int Width() const { return width_; }
    int Height() const { return height_; }

    void AddSample(int x, int y, Color3f radiance);
    void AddSample(int x, int y, const FilmSample& sample);
    Color3f LinearPixel(int x, int y) const;
    Color3f AlbedoPixel(int x, int y) const;
    Vec3f NormalPixel(int x, int y) const;
    float DepthPixel(int x, int y) const;
    std::uint32_t SampleCount(int x, int y) const;
    float LuminanceVariance(int x, int y) const;
    float LuminanceStandardError(int x, int y) const;
    bool IsConverged(int x, int y, int min_samples, float relative_error,
        float absolute_error, float confidence) const;
    std::uint64_t TotalSampleCount() const;
    std::uint64_t BadSampleCount() const { return bad_sample_count_; }
    const std::vector<FilmPixel>& Pixels() const { return pixels_; }
    void SetPixelForCheckpoint(int x, int y, FilmPixel pixel);

private:
    std::size_t Index(int x, int y) const;
    bool InBounds(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<FilmPixel> pixels_;
    std::uint64_t bad_sample_count_ = 0;
};

} // namespace yr
