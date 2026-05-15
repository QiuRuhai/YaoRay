#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;

struct Rng {
    explicit Rng(std::uint64_t seed)
        : state(seed == 0 ? 0x9E3779B97F4A7C15ull : seed) {}

    std::uint32_t NextU32() {
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return static_cast<std::uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 32);
    }

    float NextFloat() {
        constexpr float scale = 1.0f / 16777216.0f;
        return static_cast<float>(NextU32() >> 8) * scale;
    }

    std::uint64_t state;
};

std::uint64_t Mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

std::uint64_t SeedFor(const RenderScene& scene, int x, int y, int sample) {
    std::uint64_t seed = Mix64(scene.seed);
    seed ^= Mix64(static_cast<std::uint64_t>(x) + 0xA24BAED4963EE407ull);
    seed ^= Mix64(static_cast<std::uint64_t>(y) + 0x9FB21C651E98DF25ull);
    seed ^= Mix64(static_cast<std::uint64_t>(sample) + 0xC2B2AE3D27D4EB4Full);
    return Mix64(seed);
}

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool IsNearBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float MaxAbsComponent(Point3f point) {
    return std::max(std::fabs(point.x), std::max(std::fabs(point.y), std::fabs(point.z)));
}

float SurfaceBias(Point3f point) {
    return std::max(MinShadowBias, MaxAbsComponent(point) * ShadowBiasScale);
}

float ShadowBias(Point3f origin, Point3f target, float distance) {
    const float coordinate_scale = std::max(MaxAbsComponent(origin), MaxAbsComponent(target));
    const float scaled_bias = coordinate_scale * ShadowBiasScale;
    const float capped_bias = distance * MaxShadowBiasDistanceFraction;
    return std::min(std::max(MinShadowBias, scaled_bias), capped_bias);
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

Color3f EnvironmentColor(const RenderScene& scene) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

bool IsValidMaterialIndex(const RenderScene& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(CpuPathTraceStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
}

Ray3f MakeCameraRay(const RenderScene& scene, int x, int y, float pixel_u, float pixel_v) {
    const float width = static_cast<float>(scene.width);
    const float height = static_cast<float>(scene.height);
    const float aspect = width / height;
    const float half_height = std::tan(scene.camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * (static_cast<float>(x) + pixel_u) / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * (static_cast<float>(y) + pixel_v) / height) * half_height;
    const Vec3f direction = Normalize(
        scene.camera.forward +
        scene.camera.right * screen_x +
        scene.camera.up * screen_y
    );
    return Ray3f{scene.camera.origin, direction};
}

Vec3f SampleCosineHemisphere(Vec3f normal, Rng& rng) {
    const float u1 = rng.NextFloat();
    const float u2 = rng.NextFloat();
    const float radius = std::sqrt(u1);
    const float theta = 2.0f * Pi * u2;
    const float local_x = radius * std::cos(theta);
    const float local_y = radius * std::sin(theta);
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - u1));

    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

Color3f EstimateDirectLight(
    const RenderScene& scene,
    Point3f hit_point,
    Vec3f normal,
    Color3f albedo,
    CpuPathTraceStats& stats
) {
    Color3f radiance;
    for (const RenderAreaLight& light : scene.area_lights) {
        const float area = light.width * light.height;
        if (area <= 0.0f) {
            continue;
        }

        const Vec3f to_light = light.position - hit_point;
        const float distance_squared = LengthSquared(to_light);
        if (distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float distance = std::sqrt(distance_squared);
        const float shadow_bias = ShadowBias(hit_point, light.position, distance);
        if (distance <= shadow_bias) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * shadow_bias;
        const Vec3f shadow_to_light = light.position - shadow_origin;
        const float shadow_distance_squared = LengthSquared(shadow_to_light);
        if (shadow_distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float shadow_distance = std::sqrt(shadow_distance_squared);
        const Vec3f wi = shadow_to_light / shadow_distance;
        const float n_dot_l = std::max(0.0f, Dot(normal, wi));
        if (n_dot_l <= 0.0f) {
            continue;
        }

        const Vec3f light_normal{0.0f, 0.0f, -1.0f};
        const float light_facing_cosine = std::max(0.0f, Dot(light_normal, -wi));
        if (light_facing_cosine <= 0.0f) {
            continue;
        }

        ++stats.shadow_rays;
        BvhTraceStats shadow_trace;
        const Ray3f shadow_ray{shadow_origin, wi};
        const BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_trace);
        AccumulateTraceStats(stats, shadow_trace);
        if (shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float scale = area * n_dot_l * light_facing_cosine / distance_squared;
        radiance = radiance + Multiply(albedo, light.radiance) * scale;
    }
    return radiance;
}

Color3f TracePath(const RenderScene& scene, Ray3f ray, Rng& rng, CpuPathTraceStats& stats) {
    Color3f radiance;
    Color3f throughput{1.0f, 1.0f, 1.0f};
    const int max_depth = std::max(1, scene.max_depth);

    for (int depth = 0; depth < max_depth; ++depth) {
        ++stats.rays_traced;

        BvhTraceStats trace_stats;
        const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
        AccumulateTraceStats(stats, trace_stats);
        if (!hit.hit || hit.triangle == nullptr) {
            ++stats.misses;
            radiance = radiance + Multiply(throughput, EnvironmentColor(scene));
            break;
        }

        ++stats.hits;
        if (!IsValidMaterialIndex(scene, hit.triangle->material_index)) {
            radiance = radiance + Multiply(throughput, Color3f{1.0f, 0.0f, 1.0f});
            break;
        }

        const RenderTriangle& triangle = *hit.triangle;
        const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
        const Point3f hit_point = ray.At(hit.t);
        const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);

        radiance = radiance + Multiply(throughput, material.emission);
        radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, stats));

        if (depth + 1 >= max_depth || IsNearBlack(material.albedo)) {
            break;
        }

        throughput = Multiply(throughput, material.albedo);
        const Vec3f bounce_direction = SampleCosineHemisphere(normal, rng);
        ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bounce_direction};
    }

    return radiance;
}

} // namespace

CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene) {
    RenderScene trace_scene = scene;
    BvhBuildResult build = BuildBvh(trace_scene.triangles);
    if (build.errors.empty()) {
        trace_scene.bvh = std::move(build.bvh);
    } else {
        trace_scene.bvh = RenderBvh{};
    }

    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(trace_scene.bvh.nodes.size());
    result.stats.bvh_max_depth = trace_scene.bvh.max_depth;

    const auto start = std::chrono::steady_clock::now();
    const int samples_per_pixel = std::max(1, scene.spp);
    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            for (int sample = 0; sample < samples_per_pixel; ++sample) {
                Rng rng{SeedFor(scene, x, y, sample)};
                const float pixel_u = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
                const float pixel_v = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
                const Ray3f ray = MakeCameraRay(scene, x, y, pixel_u, pixel_v);
                result.film.AddSample(x, y, TracePath(trace_scene, ray, rng, result.stats));
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
