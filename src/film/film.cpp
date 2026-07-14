#include <yaoray/film/film.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yr {
namespace {

bool IsFinite(Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

float Luminance(Color3f c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

} // namespace

Film::Film(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Film dimensions must be positive");
    }
}

void Film::AddSample(int x, int y, Color3f radiance) {
    AddSample(x, y, FilmSample{radiance});
}

void Film::AddSample(int x, int y, const FilmSample& sample) {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film sample coordinate out of range");
    }
    if (!IsFinite(sample.beauty)) {
        ++bad_sample_count_;
        return;
    }
    FilmPixel& pixel = pixels_[Index(x, y)];
    pixel.sum = pixel.sum + sample.beauty;
    ++pixel.samples;
    const float luminance = Luminance(sample.beauty);
    const float delta = luminance - pixel.luminance_mean;
    pixel.luminance_mean += delta / static_cast<float>(pixel.samples);
    const float delta_after_mean = luminance - pixel.luminance_mean;
    pixel.luminance_m2 += delta * delta_after_mean;
    if (sample.primary_hit && IsFinite(sample.albedo) && IsFinite(sample.normal) &&
        std::isfinite(sample.depth) && sample.depth >= 0.0f) {
        pixel.albedo_sum = pixel.albedo_sum + sample.albedo;
        pixel.normal_sum = pixel.normal_sum + sample.normal;
        pixel.depth_sum += sample.depth;
        ++pixel.aov_samples;
    }
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

Color3f Film::AlbedoPixel(int x, int y) const {
    const FilmPixel& pixel = pixels_[Index(x, y)];
    return pixel.aov_samples > 0
        ? pixel.albedo_sum / static_cast<float>(pixel.aov_samples)
        : Color3f{};
}

Vec3f Film::NormalPixel(int x, int y) const {
    const FilmPixel& pixel = pixels_[Index(x, y)];
    if (pixel.aov_samples == 0) return Vec3f{};
    const Vec3f average = pixel.normal_sum / static_cast<float>(pixel.aov_samples);
    return LengthSquared(average) > 0.0f ? Normalize(average) : Vec3f{};
}

float Film::DepthPixel(int x, int y) const {
    const FilmPixel& pixel = pixels_[Index(x, y)];
    return pixel.aov_samples > 0
        ? pixel.depth_sum / static_cast<float>(pixel.aov_samples)
        : 0.0f;
}

std::uint32_t Film::SampleCount(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    return pixels_[Index(x, y)].samples;
}

float Film::LuminanceVariance(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    const FilmPixel& pixel = pixels_[Index(x, y)];
    return pixel.samples > 1
        ? pixel.luminance_m2 / static_cast<float>(pixel.samples - 1)
        : 0.0f;
}

float Film::LuminanceStandardError(int x, int y) const {
    const std::uint32_t count = SampleCount(x, y);
    return count > 1
        ? std::sqrt(std::max(0.0f, LuminanceVariance(x, y)) / static_cast<float>(count))
        : std::numeric_limits<float>::infinity();
}

bool Film::IsConverged(int x, int y, int min_samples, float relative_error,
    float absolute_error, float confidence) const {
    const FilmPixel& pixel = pixels_[Index(x, y)];
    if (pixel.samples < static_cast<std::uint32_t>(std::max(2, min_samples))) return false;
    const float error_bound = std::max(0.0f, confidence) * LuminanceStandardError(x, y);
    const float tolerance = std::max(0.0f, absolute_error) +
        std::max(0.0f, relative_error) * std::fabs(pixel.luminance_mean);
    return std::isfinite(error_bound) && error_bound <= tolerance;
}

std::uint64_t Film::TotalSampleCount() const {
    std::uint64_t total = 0;
    for (const FilmPixel& pixel : pixels_) total += pixel.samples;
    return total;
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
