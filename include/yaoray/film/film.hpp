#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct FilmPixel {
    Color3f sum;
    std::uint32_t samples = 0;
};

class Film {
public:
    Film(int width, int height);

    int Width() const { return width_; }
    int Height() const { return height_; }

    void AddSample(int x, int y, Color3f radiance);
    Color3f LinearPixel(int x, int y) const;
    std::uint32_t SampleCount(int x, int y) const;
    std::uint64_t BadSampleCount() const { return bad_sample_count_; }

private:
    std::size_t Index(int x, int y) const;
    bool InBounds(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<FilmPixel> pixels_;
    std::uint64_t bad_sample_count_ = 0;
};

} // namespace yr
