#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include <yaoray/backends/cpu/cpu_sampler.hpp>
#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/mis.hpp>
#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace yr {
namespace {

constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;
constexpr int RussianRouletteStartDepth = 3;
constexpr float RussianRouletteMinSurvival = 0.05f;
constexpr float RussianRouletteMaxSurvival = 0.95f;

struct PreviousBounce {
    bool valid = false;
    bool delta = false;
    Point3f origin;
    float bsdf_pdf = 0.0f;
    int light_sample_count = 1;
};

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3f Barycentric(Point3f point, const RenderTriangle& triangle) {
    const Vec3f v0 = triangle.p1 - triangle.p0;
    const Vec3f v1 = triangle.p2 - triangle.p0;
    const Vec3f v2 = point - triangle.p0;
    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) <= 1.0e-12f) {
        return Vec3f{1.0f, 0.0f, 0.0f};
    }
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    const float u = 1.0f - v - w;
    return Vec3f{u, v, w};
}

Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric) {
    return Vec2f{
        triangle.uv0.x * barycentric.x + triangle.uv1.x * barycentric.y + triangle.uv2.x * barycentric.z,
        triangle.uv0.y * barycentric.x + triangle.uv1.y * barycentric.y + triangle.uv2.y * barycentric.z
    };
}

RenderMaterial ResolveHitMaterial(
    const RenderScene& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& material,
    Point3f hit_point
) {
    RenderMaterial resolved = material;
    if (material.albedo_texture < 0 || !triangle.has_uv) {
        return resolved;
    }
    const std::size_t texture_index = static_cast<std::size_t>(material.albedo_texture);
    if (texture_index >= scene.textures.size()) {
        return resolved;
    }
    const Vec3f barycentric = Barycentric(hit_point, triangle);
    const Vec2f uv = InterpolateUv(triangle, barycentric);
    resolved.albedo = SampleTextureNearest(scene.textures[texture_index], uv);
    return resolved;
}

bool IsNearBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float MaxComponent(Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
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

float EmissiveHitMisWeight(const RenderScene& scene, const PreviousBounce& previous, Point3f hit_point) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfAreaLightsForPointSolidAngle(scene, previous.origin, hit_point);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}

bool IsValidMaterialIndex(const RenderScene& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

int DirectLightSampleCount(const RenderScene& scene) {
    return std::max(1, scene.light_samples);
}

bool SurviveRussianRoulette(int depth, Color3f& throughput, CpuSampler& sampler) {
    if (depth < RussianRouletteStartDepth) {
        return true;
    }

    const float survival = std::clamp(
        MaxComponent(throughput),
        RussianRouletteMinSurvival,
        RussianRouletteMaxSurvival
    );
    if (sampler.Next1D() >= survival) {
        return false;
    }

    throughput = throughput / survival;
    return true;
}

void AccumulateTraceStats(CpuPathTraceStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
}

void MergeTraceStats(CpuPathTraceStats& target, const CpuPathTraceStats& source) {
    target.rays_traced += source.rays_traced;
    target.shadow_rays += source.shadow_rays;
    target.occluded_shadow_rays += source.occluded_shadow_rays;
    target.triangle_tests += source.triangle_tests;
    target.bvh_node_tests += source.bvh_node_tests;
    target.hits += source.hits;
    target.misses += source.misses;
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

Color3f EstimateDirectLight(
    const RenderScene& scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
) {
    Color3f radiance;
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);

    for (const RenderAreaLight& light : scene.area_lights) {
        Color3f light_radiance;
        for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
            const std::optional<AreaLightSample> sample = SampleAreaLight(light, sampler.NextLight2D(sample_index));
            if (!sample.has_value()) {
                continue;
            }

            const Vec3f to_light = sample->point - hit_point;
            const float distance_squared = LengthSquared(to_light);
            if (distance_squared <= MinShadowBias * MinShadowBias) {
                continue;
            }

            const float distance = std::sqrt(distance_squared);
            const float shadow_bias = ShadowBias(hit_point, sample->point, distance);
            if (distance <= shadow_bias) {
                continue;
            }

            const Point3f shadow_origin = hit_point + normal * shadow_bias;
            const Vec3f shadow_to_light = sample->point - shadow_origin;
            const float shadow_distance_squared = LengthSquared(shadow_to_light);
            if (shadow_distance_squared <= MinShadowBias * MinShadowBias) {
                continue;
            }

            const float shadow_distance = std::sqrt(shadow_distance_squared);
            const Vec3f wi = shadow_to_light / shadow_distance;
            const float cos_surface = std::max(0.0f, Dot(normal, wi));
            if (cos_surface <= 0.0f) {
                continue;
            }

            const float pdf_light = PdfAreaLightSampleSolidAngle(light, hit_point, sample->point);
            if (pdf_light <= 0.0f) {
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

            const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
            if (IsNearBlack(bsdf)) {
                continue;
            }

            const float pdf_bsdf = PdfBsdf(material, wo, wi, normal);
            const float mis_weight = PowerHeuristic(light_sample_count, pdf_light, 1, pdf_bsdf);
            light_radiance = light_radiance + Multiply(bsdf, sample->radiance) * (cos_surface * mis_weight / pdf_light);
        }

        radiance = radiance + light_radiance * inverse_light_sample_count;
    }
    return radiance;
}

Color3f TracePath(const RenderScene& scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats) {
    Color3f radiance;
    Color3f throughput{1.0f, 1.0f, 1.0f};
    PreviousBounce previous_bounce;
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
        const RenderMaterial& base_material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
        const Point3f hit_point = ray.At(hit.t);
        const RenderMaterial material = ResolveHitMaterial(scene, triangle, base_material, hit_point);
        const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);
        const Vec3f wo = -ray.direction;

        if (!IsNearBlack(material.emission)) {
            const float emission_weight = EmissiveHitMisWeight(scene, previous_bounce, hit_point);
            radiance = radiance + Multiply(throughput, material.emission) * emission_weight;
        }

        if (!IsDeltaBsdf(material)) {
            radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, material, hit_point, normal, wo, sampler, stats));
        }

        if (depth + 1 >= max_depth) {
            break;
        }

        const BsdfSample bsdf_sample = SampleBsdf(material, wo, normal, sampler.Next2D());
        if (!bsdf_sample.valid || IsNearBlack(bsdf_sample.weight)) {
            break;
        }

        throughput = Multiply(throughput, bsdf_sample.weight);
        if (!SurviveRussianRoulette(depth, throughput, sampler)) {
            break;
        }

        previous_bounce = PreviousBounce{
            true,
            bsdf_sample.specular,
            hit_point,
            bsdf_sample.pdf,
            DirectLightSampleCount(scene)
        };
        ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bsdf_sample.wi};
    }

    return radiance;
}

} // namespace

CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene) {
    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    const CpuTileSchedule schedule = BuildCpuTileSchedule(scene.width, scene.height, scene.threads);
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;
    result.stats.threads = schedule.worker_count;

    const auto start = std::chrono::steady_clock::now();
    const int samples_per_pixel = std::max(1, scene.spp);
    std::vector<CpuPathTraceStats> worker_stats(static_cast<std::size_t>(schedule.worker_count));
    ForEachCpuTile(schedule, [&](const CpuTile& tile, int worker_index) {
        CpuPathTraceStats& stats = worker_stats[static_cast<std::size_t>(worker_index)];
        for (int y = tile.y0; y < tile.y1; ++y) {
            for (int x = tile.x0; x < tile.x1; ++x) {
                for (int sample = 0; sample < samples_per_pixel; ++sample) {
                    CpuSampler sampler{
                        scene.sampler,
                        SeedForPixelSample(scene.seed, x, y, sample),
                        sample,
                        samples_per_pixel,
                        DirectLightSampleCount(scene)
                    };
                    const Vec2f pixel_sample = sampler.NextPixel2D();
                    const Ray3f ray = MakeCameraRay(scene, x, y, pixel_sample.x, pixel_sample.y);
                    result.film.AddSample(x, y, TracePath(scene, ray, sampler, stats));
                }
            }
        }
    });

    for (const CpuPathTraceStats& stats : worker_stats) {
        MergeTraceStats(result.stats, stats);
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
