#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include <yaoray/backends/cpu/cpu_material.hpp>
#include <yaoray/backends/cpu/cpu_sampler.hpp>
#include <yaoray/backends/cpu/cpu_surface.hpp>
#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/mis.hpp>
#include <yaoray/render/shading.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
constexpr float AbsorptionEpsilon = 1.0e-6f;
constexpr int MaxTransparentShadowHits = 16;

struct PreviousBounce {
    bool valid = false;
    bool delta = false;
    Point3f origin;
    float bsdf_pdf = 0.0f;
    int light_sample_count = 1;
};

struct PathMediumState {
    bool active = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

struct ShadowVisibility {
    bool visible = true;
    Color3f transmittance{1.0f, 1.0f, 1.0f};
};

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool IsNearBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float MaxComponent(Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
}

Color3f ClampMaxComponent(Color3f value, float limit) {
    if (limit <= 0.0f) {
        return value;
    }
    const float max_component = MaxComponent(value);
    if (max_component <= limit || max_component <= 0.0f) {
        return value;
    }
    return value * (limit / max_component);
}

float SafeAbsorptionChannel(float value) {
    return std::clamp(value, AbsorptionEpsilon, 1.0f);
}

Color3f BeerLambertTransmittance(Color3f absorption_color, float absorption_distance, float distance) {
    if (distance <= 0.0f || absorption_distance <= 0.0f) {
        return Color3f{1.0f, 1.0f, 1.0f};
    }
    const float inverse_distance = 1.0f / absorption_distance;
    return Color3f{
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.x)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.y)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.z)) * distance * inverse_distance)
    };
}

void ApplyMediumAttenuation(Color3f& throughput, const PathMediumState& medium, float distance) {
    if (!medium.active) {
        return;
    }
    throughput = Multiply(
        throughput,
        BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, distance)
    );
}

bool IsShadowTransmittanceBlack(Color3f color) {
    return MaxComponent(color) <= AbsorptionEpsilon;
}

float ClampTransmittanceChannel(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Color3f ClampTransmittance(Color3f value) {
    return Color3f{
        ClampTransmittanceChannel(value.x),
        ClampTransmittanceChannel(value.y),
        ClampTransmittanceChannel(value.z)
    };
}

bool IsShadowTransparentMaterial(const RenderMaterial& material) {
    return material.kind == RenderMaterialKind::Dielectric || material.kind == RenderMaterialKind::ThinDielectric;
}

Color3f ThinGlassShadowTransmittance(const RenderMaterial& material) {
    return ClampTransmittance(material.reflectance.value);
}

void ToggleShadowMedium(PathMediumState& medium, const RenderMaterial& material) {
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
}

bool IsThickDielectricTransmission(const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    return material.kind == RenderMaterialKind::Dielectric && Dot(wi, normal) < 0.0f;
}

void UpdateMediumStateAfterBsdf(PathMediumState& medium, const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    if (!IsThickDielectricTransmission(material, normal, wi)) {
        return;
    }
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
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

Color3f EnvironmentColor(const RenderSceneIR& scene, Vec3f direction) {
    return EvaluateEnvironment(scene, direction);
}

float EmissiveHitMisWeight(const RenderSceneIR& scene, const PreviousBounce& previous, Point3f hit_point, Vec3f light_normal) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfEmissiveLightSolidAngle(scene, previous.origin, hit_point, light_normal);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}

float EnvironmentHitMisWeight(const RenderSceneIR& scene, const PreviousBounce& previous, Vec3f direction) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfEnvironment(scene, direction);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}

bool IsValidMaterialIndex(const RenderSceneIR& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(CpuPathTraceStats& stats, const BvhTraceStats& trace_stats);

int DirectLightSampleCount(const RenderSceneIR& /*scene*/) {
    return 1;
}

ShadowVisibility TraceShadowVisibility(
    const CpuPreparedScene& prepared_scene,
    Ray3f ray,
    float max_distance,
    CpuPathTraceStats& stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    ShadowVisibility visibility;
    PathMediumState medium;
    float remaining_distance = max_distance;

    for (int transparent_hit_count = 0; transparent_hit_count < MaxTransparentShadowHits; ++transparent_hit_count) {
        BvhTraceStats shadow_trace;
        const CpuSurfaceHit surface_hit = TraceVisibleSurface(
            prepared_scene,
            ray,
            1.0e-5f,
            remaining_distance,
            &shadow_trace
        );
        AccumulateTraceStats(stats, shadow_trace);

        const bool finite_segment = std::isfinite(remaining_distance);
        if (surface_hit.exhausted) {
            return ShadowVisibility{false, Color3f{}};
        }
        const bool is_miss = !surface_hit.hit ||
            (surface_hit.geometry_hit.triangle_index < 0 && surface_hit.geometry_hit.sphere_index < 0);
        if (is_miss) {
            if (medium.active) {
                if (!finite_segment) {
                    return ShadowVisibility{false, Color3f{}};
                }
                visibility.transmittance = Multiply(
                    visibility.transmittance,
                    BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, remaining_distance)
                );
            }
            visibility.visible = !IsShadowTransmittanceBlack(visibility.transmittance);
            return visibility;
        }

        if (medium.active) {
            visibility.transmittance = Multiply(
                visibility.transmittance,
                BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, surface_hit.geometry_hit.t)
            );
            if (IsShadowTransmittanceBlack(visibility.transmittance)) {
                return ShadowVisibility{false, Color3f{}};
            }
        }

        // For sphere hits, the material was already resolved in the surface layer; validate
        // the triangle primitive material only for triangle hits.
        if (surface_hit.geometry_hit.primitive_index >= 0 &&
            !IsValidMaterialIndex(scene, scene.primitives[surface_hit.geometry_hit.primitive_index].material_index)) {
            return ShadowVisibility{false, Color3f{}};
        }

        const Point3f hit_point = ray.At(surface_hit.geometry_hit.t);
        const RenderMaterial& material = surface_hit.sample.material;
        if (!IsShadowTransparentMaterial(material)) {
            return ShadowVisibility{false, Color3f{}};
        }

        if (material.kind == RenderMaterialKind::ThinDielectric) {
            visibility.transmittance = Multiply(visibility.transmittance, ThinGlassShadowTransmittance(material));
            if (IsShadowTransmittanceBlack(visibility.transmittance)) {
                return ShadowVisibility{false, Color3f{}};
            }
        } else {
            ToggleShadowMedium(medium, material);
        }

        const float bias = SurfaceBias(hit_point);
        if (finite_segment) {
            remaining_distance -= surface_hit.geometry_hit.t + bias;
            if (remaining_distance <= 0.0f) {
                visibility.visible = !IsShadowTransmittanceBlack(visibility.transmittance);
                return visibility;
            }
        }
        ray = Ray3f{hit_point + ray.direction * bias, ray.direction};
    }

    return ShadowVisibility{false, Color3f{}};
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

Ray3f MakeCameraRay(const RenderSceneIR& scene, int x, int y, float pixel_u, float pixel_v) {
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

Color3f EstimateDirectEnvironmentLight(
    const CpuPreparedScene& prepared_scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    if (!HasSampleableEnvironment(scene)) {
        return Color3f{};
    }

    Color3f radiance;
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);
    for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
        const EnvironmentSample sample = SampleEnvironment(scene, sampler.NextLight2D(sample_index));
        if (!sample.valid || sample.pdf_solid_angle <= 0.0f || IsNearBlack(sample.radiance)) {
            continue;
        }

        const Vec3f wi = sample.direction;
        const float cos_surface = std::max(0.0f, Dot(normal, wi));
        if (cos_surface <= 0.0f) {
            continue;
        }

        const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
        if (IsNearBlack(bsdf)) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
        ++stats.shadow_rays;
        const ShadowVisibility visibility =
            TraceShadowVisibility(prepared_scene, Ray3f{shadow_origin, wi}, std::numeric_limits<float>::infinity(), stats);
        if (!visibility.visible) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float pdf_bsdf = PdfBsdf(material, wo, wi, normal);
        const float mis_weight = PowerHeuristic(light_sample_count, sample.pdf_solid_angle, 1, pdf_bsdf);
        const Color3f visible_radiance = Multiply(visibility.transmittance, sample.radiance);
        radiance = radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / sample.pdf_solid_angle);
    }

    return radiance * inverse_light_sample_count;
}

Color3f EstimateDirectLight(
    const CpuPreparedScene& prepared_scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    Color3f radiance;
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);

    if (!scene.emissive_primitives.empty()) {
        for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
            const auto sample = SampleEmissiveLights(scene, sampler.Next1D(), sampler.NextLight2D(sample_index));
            if (!sample.has_value() || sample->pdf <= 0.0f || IsNearBlack(sample->radiance)) {
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

            const float pdf_light = PdfEmissiveLightSolidAngle(scene, hit_point, sample->point, sample->normal);
            if (pdf_light <= 0.0f) {
                continue;
            }

            ++stats.shadow_rays;
            const Ray3f shadow_ray{shadow_origin, wi};
            const ShadowVisibility visibility =
                TraceShadowVisibility(prepared_scene, shadow_ray, shadow_distance - shadow_bias, stats);
            if (!visibility.visible) {
                ++stats.occluded_shadow_rays;
                continue;
            }

            const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
            if (IsNearBlack(bsdf)) {
                continue;
            }

            const float pdf_bsdf = PdfBsdf(material, wo, wi, normal);
            const float mis_weight = PowerHeuristic(light_sample_count, pdf_light, 1, pdf_bsdf);
            const Color3f visible_radiance = Multiply(visibility.transmittance, sample->radiance);
            radiance = radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / pdf_light);
        }
        radiance = radiance * inverse_light_sample_count;
    }

    // Analytic (non-area, non-environment) lights — deltas, no MIS vs BSDF.
    for (const AnalyticLight& light : scene.analytic_lights) {
        AnalyticLightSample sample;
        if (light.kind == AnalyticLightKind::Point) {
            sample = SampleAnalyticPoint(light, hit_point);
        } else {
            // Slice 1 only handles Point. Distant/Spot land in later slices.
            continue;
        }
        if (!sample.valid || IsNearBlack(sample.radiance)) {
            continue;
        }
        const float cos_surface = std::max(0.0f, Dot(normal, sample.wi));
        if (cos_surface <= 0.0f) {
            continue;
        }
        const Color3f bsdf = EvaluateBsdf(material, wo, sample.wi, normal);
        if (IsNearBlack(bsdf)) {
            continue;
        }
        const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
        ++stats.shadow_rays;
        const ShadowVisibility visibility = TraceShadowVisibility(
            prepared_scene,
            Ray3f{shadow_origin, sample.wi},
            sample.distance - SurfaceBias(hit_point),
            stats
        );
        if (!visibility.visible) {
            ++stats.occluded_shadow_rays;
            continue;
        }
        const Color3f visible_radiance = Multiply(visibility.transmittance, sample.radiance);
        radiance = radiance + Multiply(bsdf, visible_radiance) * cos_surface;
    }

    radiance = radiance + EstimateDirectEnvironmentLight(prepared_scene, material, hit_point, normal, wo, sampler, stats);
    return radiance;
}

Color3f TracePath(const CpuPreparedScene& prepared_scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    Color3f radiance;
    Color3f throughput{1.0f, 1.0f, 1.0f};
    PreviousBounce previous_bounce;
    PathMediumState medium;
    const int max_depth = std::max(1, scene.max_depth);

    for (int depth = 0; depth < max_depth; ++depth) {
        ++stats.rays_traced;

        BvhTraceStats trace_stats;
        const CpuSurfaceHit surface_hit = TraceVisibleSurface(
            prepared_scene,
            ray,
            1.0e-5f,
            std::numeric_limits<float>::infinity(),
            &trace_stats
        );
        AccumulateTraceStats(stats, trace_stats);
        const bool is_miss = !surface_hit.hit ||
            (surface_hit.geometry_hit.triangle_index < 0 && surface_hit.geometry_hit.sphere_index < 0);
        if (is_miss || surface_hit.exhausted) {
            ++stats.misses;
            const float environment_weight = EnvironmentHitMisWeight(scene, previous_bounce, ray.direction);
            radiance = radiance + Multiply(throughput, EnvironmentColor(scene, ray.direction)) * environment_weight;
            break;
        }

        ++stats.hits;
        ApplyMediumAttenuation(throughput, medium, surface_hit.geometry_hit.t);
        if (IsNearBlack(throughput)) {
            break;
        }

        // For sphere hits, the surface layer already resolved the material; for triangle hits
        // validate via the primitive table. Either way, surface_hit.sample.material is correct.
        if (surface_hit.geometry_hit.primitive_index >= 0 &&
            !IsValidMaterialIndex(scene, scene.primitives[surface_hit.geometry_hit.primitive_index].material_index)) {
            radiance = radiance + Multiply(throughput, Color3f{1.0f, 0.0f, 1.0f});
            break;
        }

        const Point3f hit_point = ray.At(surface_hit.geometry_hit.t);
        const Vec3f wo = -ray.direction;
        const RenderMaterial& material = surface_hit.sample.material;
        const Vec3f normal = surface_hit.sample.shading_normal;

        // Emissive contribution. For sphere hits, sphere emission is out of M1 Slice 1 scope,
        // so we guard LocateTriangle with triangle_index >= 0.
        if (!IsNearBlack(material.emission) && surface_hit.geometry_hit.triangle_index >= 0) {
            const TriangleRef hit_tri = LocateTriangle(scene, surface_hit.geometry_hit.triangle_index);
            const Vec3f light_geo_normal = GeometricNormal(scene, hit_tri);
            const float emission_weight = EmissiveHitMisWeight(scene, previous_bounce, hit_point, light_geo_normal);
            radiance = radiance + Multiply(throughput, material.emission) * emission_weight;
        }

        if (!IsDeltaBsdf(material)) {
            radiance = radiance + Multiply(throughput, EstimateDirectLight(prepared_scene, material, hit_point, normal, wo, sampler, stats));
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

        UpdateMediumStateAfterBsdf(medium, material, normal, bsdf_sample.wi);
        previous_bounce = PreviousBounce{
            true,
            bsdf_sample.specular,
            hit_point,
            bsdf_sample.pdf,
            DirectLightSampleCount(scene)
        };
        const Vec3f bias_normal = Dot(bsdf_sample.wi, normal) >= 0.0f ? normal : -normal;
        ray = Ray3f{hit_point + bias_normal * SurfaceBias(hit_point), bsdf_sample.wi};
    }

    return radiance;
}

} // namespace

CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene, const RenderRequest& request) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    CpuPathTraceResult result{request.resume_film == nullptr ? Film{scene.width, scene.height} : *request.resume_film, {}, true, {}};
    const CpuTileSchedule schedule = BuildCpuTileSchedule(scene.width, scene.height, scene.threads);
    result.stats.bvh_nodes = static_cast<int>(prepared_scene.bvh.nodes.size());
    result.stats.bvh_max_depth = prepared_scene.bvh.max_depth;
    result.stats.threads = schedule.worker_count;

    const int samples_per_pixel = std::max(1, scene.spp);
    if (request.resume_completed_spp < 0 || request.resume_completed_spp > samples_per_pixel) {
        result.ok = false;
        result.error = "invalid resume completed spp";
        return result;
    }
    if (request.resume_film != nullptr) {
        if (request.resume_film->Width() != scene.width || request.resume_film->Height() != scene.height) {
            result.ok = false;
            result.error = "resume film dimensions do not match render scene";
            return result;
        }
        for (int y = 0; y < request.resume_film->Height(); ++y) {
            for (int x = 0; x < request.resume_film->Width(); ++x) {
                if (request.resume_film->SampleCount(x, y) != static_cast<std::uint32_t>(request.resume_completed_spp)) {
                    result.ok = false;
                    result.error = "resume film sample counts do not match resume completed spp";
                    return result;
                }
            }
        }
    }

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t cumulative_rays = 0;
    for (int sample = request.resume_completed_spp; sample < samples_per_pixel; ++sample) {
        std::vector<CpuPathTraceStats> worker_stats(static_cast<std::size_t>(schedule.worker_count));
        ForEachCpuTile(schedule, [&](const CpuTile& tile, int worker_index) {
            CpuPathTraceStats& stats = worker_stats[static_cast<std::size_t>(worker_index)];
            for (int y = tile.y0; y < tile.y1; ++y) {
                for (int x = tile.x0; x < tile.x1; ++x) {
                    CpuSampler sampler{
                        scene.sampler,
                        SeedForPixelSample(scene.seed, x, y, sample),
                        sample,
                        samples_per_pixel,
                        DirectLightSampleCount(scene)
                    };
                    const Vec2f pixel_sample = sampler.NextPixel2D();
                    const Ray3f ray = MakeCameraRay(scene, x, y, pixel_sample.x, pixel_sample.y);
                    Color3f sample_radiance = TracePath(prepared_scene, ray, sampler, stats);
                    sample_radiance = ClampMaxComponent(sample_radiance, scene.radiance_clamp);
                    result.film.AddSample(x, y, sample_radiance);
                }
            }
        });

        for (const CpuPathTraceStats& stats : worker_stats) {
            MergeTraceStats(result.stats, stats);
        }
        cumulative_rays = result.stats.rays_traced;

        if (request.progress_callback) {
            const auto now = std::chrono::steady_clock::now();
            const int completed_spp = sample + 1;
            const RenderProgress progress{
                completed_spp,
                samples_per_pixel,
                static_cast<std::uint64_t>(completed_spp) * static_cast<std::uint64_t>(scene.width) *
                    static_cast<std::uint64_t>(scene.height),
                static_cast<std::uint64_t>(samples_per_pixel) * static_cast<std::uint64_t>(scene.width) *
                    static_cast<std::uint64_t>(scene.height),
                cumulative_rays,
                std::chrono::duration<double>(now - start).count()
            };
            const RenderProgressDecision decision = request.progress_callback(progress, result.film);
            if (decision.cancel) {
                result.ok = false;
                result.error = decision.error.empty() ? "render cancelled by progress callback" : decision.error;
                result.stats.elapsed_seconds = progress.elapsed_seconds;
                return result;
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
