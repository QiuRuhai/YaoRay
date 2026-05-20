#include <yaoray/render/environment.hpp>

#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = 2.0f * Pi;
constexpr float MinSinTheta = 1.0e-6f;

float Fract(float value) {
    return value - std::floor(value);
}

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, std::nextafter(1.0f, 0.0f));
}

float Luminance(Color3f color) {
    return std::max(0.0f, 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z);
}

float RowTheta(int y, int height) {
    return (static_cast<float>(y) + 0.5f) * Pi / static_cast<float>(height);
}

std::size_t TexelIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

int FindCdfIndex(const std::vector<float>& cdf, float target) {
    const auto found = std::lower_bound(cdf.begin(), cdf.end(), target);
    if (found == cdf.end()) {
        return static_cast<int>(cdf.size()) - 1;
    }
    return static_cast<int>(found - cdf.begin());
}

bool ValidTextureIndex(const RenderScene& scene, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < scene.textures.size();
}

bool ValidDistributionIndex(const RenderScene& scene, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < scene.environment_distributions.size();
}

} // namespace

Vec2f DirectionToEnvironmentUv(Vec3f direction, float rotation_radians) {
    const Vec3f d = Normalize(direction);
    const float phi = std::atan2(d.x, d.z);
    const float u = Fract(0.5f + (phi + rotation_radians) / TwoPi);
    const float theta = std::acos(std::clamp(d.y, -1.0f, 1.0f));
    return Vec2f{u, std::clamp(theta / Pi, 0.0f, 1.0f)};
}

Vec3f EnvironmentUvToDirection(Vec2f uv, float rotation_radians) {
    const float phi = (uv.x - 0.5f) * TwoPi - rotation_radians;
    const float theta = std::clamp(uv.y, 0.0f, 1.0f) * Pi;
    const float sin_theta = std::sin(theta);
    return Normalize(Vec3f{
        sin_theta * std::sin(phi),
        std::cos(theta),
        sin_theta * std::cos(phi)
    });
}

RenderEnvironmentDistribution BuildEnvironmentDistribution(const RenderTexture& texture) {
    RenderEnvironmentDistribution distribution;
    distribution.width = texture.width;
    distribution.height = texture.height;
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        distribution.uniform = true;
        distribution.width = std::max(1, texture.width);
        distribution.height = std::max(1, texture.height);
    }

    const int width = std::max(1, distribution.width);
    const int height = std::max(1, distribution.height);
    distribution.texel_weights.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    distribution.row_weights.assign(static_cast<std::size_t>(height), 0.0f);
    distribution.row_cdf.assign(static_cast<std::size_t>(height), 0.0f);
    distribution.conditional_cdfs.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

    for (int y = 0; y < height; ++y) {
        const float sin_theta = std::max(MinSinTheta, std::sin(RowTheta(y, height)));
        float row_sum = 0.0f;
        for (int x = 0; x < width; ++x) {
            const std::size_t index = TexelIndex(x, y, width);
            const Color3f color = index < texture.texels.size() ? texture.texels[index] : Color3f{};
            const float weight = Luminance(color) * sin_theta;
            distribution.texel_weights[index] = weight;
            row_sum += weight;
            distribution.conditional_cdfs[index] = row_sum;
        }
        distribution.row_weights[static_cast<std::size_t>(y)] = row_sum;
        distribution.total_weight += row_sum;
        distribution.row_cdf[static_cast<std::size_t>(y)] = distribution.total_weight;
    }

    if (distribution.total_weight <= 0.0f || !std::isfinite(distribution.total_weight)) {
        distribution.uniform = true;
        distribution.total_weight = static_cast<float>(width * height);
        for (int y = 0; y < height; ++y) {
            float row_sum = 0.0f;
            for (int x = 0; x < width; ++x) {
                const std::size_t index = TexelIndex(x, y, width);
                distribution.texel_weights[index] = 1.0f;
                row_sum += 1.0f;
                distribution.conditional_cdfs[index] = row_sum;
            }
            distribution.row_weights[static_cast<std::size_t>(y)] = row_sum;
            distribution.row_cdf[static_cast<std::size_t>(y)] = static_cast<float>((y + 1) * width);
        }
    }

    return distribution;
}

Color3f EvaluateEnvironment(const RenderScene& scene, Vec3f direction) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    if (scene.environment.type != EnvironmentKind::Hdri ||
        scene.environment.strength <= 0.0f ||
        !ValidTextureIndex(scene, scene.environment.texture_index)) {
        return Color3f{};
    }

    const RenderTexture& texture = scene.textures[static_cast<std::size_t>(scene.environment.texture_index)];
    const Vec2f uv = DirectionToEnvironmentUv(direction, scene.environment.rotation_radians);
    return SampleTexture(texture, uv) * scene.environment.strength;
}

bool HasSampleableEnvironment(const RenderScene& scene) {
    return scene.environment.type == EnvironmentKind::Hdri &&
           scene.environment.strength > 0.0f &&
           ValidTextureIndex(scene, scene.environment.texture_index) &&
           ValidDistributionIndex(scene, scene.environment.distribution_index);
}

EnvironmentSample SampleEnvironment(const RenderScene& scene, Vec2f sample) {
    if (!HasSampleableEnvironment(scene)) {
        return EnvironmentSample{};
    }

    const RenderEnvironmentDistribution& distribution =
        scene.environment_distributions[static_cast<std::size_t>(scene.environment.distribution_index)];
    const int width = distribution.width;
    const int height = distribution.height;
    if (width <= 0 || height <= 0 || distribution.total_weight <= 0.0f) {
        return EnvironmentSample{};
    }

    const float row_target = ClampUnit(sample.y) * distribution.total_weight;
    const int y = FindCdfIndex(distribution.row_cdf, row_target);
    const float row_previous = y == 0 ? 0.0f : distribution.row_cdf[static_cast<std::size_t>(y - 1)];
    const float row_weight = std::max(distribution.row_weights[static_cast<std::size_t>(y)], MinSinTheta);
    const float row_fraction = std::clamp((row_target - row_previous) / row_weight, 0.0f, 1.0f);

    const std::size_t row_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    const float column_target = ClampUnit(sample.x) * row_weight;
    const auto begin = distribution.conditional_cdfs.begin() + static_cast<std::ptrdiff_t>(row_offset);
    const auto end = begin + width;
    const auto found = std::lower_bound(begin, end, column_target);
    const int x = found == end ? width - 1 : static_cast<int>(found - begin);
    const float column_previous =
        x == 0 ? 0.0f : distribution.conditional_cdfs[row_offset + static_cast<std::size_t>(x - 1)];
    const float texel_weight = std::max(distribution.texel_weights[row_offset + static_cast<std::size_t>(x)], MinSinTheta);
    const float column_fraction = std::clamp((column_target - column_previous) / texel_weight, 0.0f, 1.0f);

    const Vec2f uv{
        (static_cast<float>(x) + column_fraction) / static_cast<float>(width),
        (static_cast<float>(y) + row_fraction) / static_cast<float>(height)
    };
    const Vec3f sampled_direction = EnvironmentUvToDirection(uv, scene.environment.rotation_radians);
    const float pdf = PdfEnvironment(scene, sampled_direction);
    if (pdf <= 0.0f) {
        return EnvironmentSample{};
    }
    return EnvironmentSample{sampled_direction, EvaluateEnvironment(scene, sampled_direction), pdf, true};
}

float PdfEnvironment(const RenderScene& scene, Vec3f direction) {
    if (!HasSampleableEnvironment(scene)) {
        return 0.0f;
    }
    const RenderEnvironmentDistribution& distribution =
        scene.environment_distributions[static_cast<std::size_t>(scene.environment.distribution_index)];
    const int width = distribution.width;
    const int height = distribution.height;
    if (width <= 0 || height <= 0 || distribution.total_weight <= 0.0f) {
        return 0.0f;
    }

    const Vec2f uv = DirectionToEnvironmentUv(direction, scene.environment.rotation_radians);
    const int x = std::clamp(static_cast<int>(std::floor(uv.x * static_cast<float>(width))), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(uv.y * static_cast<float>(height))), 0, height - 1);
    const std::size_t index = TexelIndex(x, y, width);
    const float probability = distribution.texel_weights[index] / distribution.total_weight;
    const float sin_theta = std::max(MinSinTheta, std::sin(RowTheta(y, height)));
    const float texel_solid_angle = (TwoPi / static_cast<float>(width)) * (Pi / static_cast<float>(height)) * sin_theta;
    return probability / texel_solid_angle;
}

} // namespace yr
