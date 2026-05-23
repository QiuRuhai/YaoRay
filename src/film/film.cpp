#include <yaoray/film/film.hpp>

#include <cmath>
#include <stdexcept>

namespace yr {
namespace {

bool IsFinite(Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

} // namespace

Film::Film(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Film dimensions must be positive");
    }
}

void Film::AddSample(int x, int y, Color3f radiance) {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film sample coordinate out of range");
    }
    if (!IsFinite(radiance)) {
        ++bad_sample_count_;
        return;
    }
    FilmPixel& pixel = pixels_[Index(x, y)];
    pixel.sum = pixel.sum + radiance;
    ++pixel.samples;
}

Color3f Film::LinearPixel(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    const FilmPixel& pixel = pixels_[Index(x, y)];
    if (pixel.samples == 0) {
        return Color3f{};
    }
    return pixel.sum / static_cast<float>(pixel.samples);
}

std::uint32_t Film::SampleCount(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    return pixels_[Index(x, y)].samples;
}

void Film::SetPixelForCheckpoint(int x, int y, FilmPixel pixel) {
    if (!InBounds(x, y)) {
        return;
    }
    pixels_[Index(x, y)] = pixel;
}

std::size_t Film::Index(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
}

bool Film::InBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

} // namespace yr
