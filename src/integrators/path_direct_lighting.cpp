#include "path_integrator_internal.hpp"

#include <yaoray/integrators/mis.hpp>
#include <yaoray/integrators/surface_query.hpp>
#include <yaoray/lighting/environment.hpp>
#include <yaoray/lighting/light_sampling.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/shading/bsdf.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace yr {
namespace {

constexpr float MinShadowBias = 1.0e-4f;
constexpr int MaxTransparentShadowHits = 16;

} // namespace

ShadowVisibility TraceShadowVisibility(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    Ray3f ray,
    float max_distance,
    PathTraceStats& stats
) {
    ShadowVisibility visibility;
    PathMediumState medium;
    float remaining_distance = max_distance;

    for (int transparent_hit_count = 0; transparent_hit_count < MaxTransparentShadowHits; ++transparent_hit_count) {
        BvhTraceStats shadow_trace;
        const SurfaceHit surface_hit = TraceVisibleSurface(
            scene,
            acceleration,
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
            (surface_hit.geometry_hit.triangle_index < 0 &&
             !surface_hit.geometry_hit.sphere.IsValid());
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
        const RenderPrimitive* hit_primitive =
            scene.Geometry().Find(surface_hit.geometry_hit.mesh_primitive);
        if (hit_primitive != nullptr &&
            !IsValidMaterialIndex(scene, hit_primitive->material_index)) {
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

Color3f EstimateDirectEnvironmentLight(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const ShadingMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats
) {
    const LightSceneView lights = MakeLightSceneView(scene);
    if (!HasSampleableEnvironment(lights)) {
        return Color3f{};
    }

    Color3f radiance;
    const int light_sample_count = PathLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);
    for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
        const EnvironmentSample sample = SampleEnvironment(
            lights, sampler.Sample2D(SampleDimension::DirectLightSurface, sample_index));
        if (!sample.valid || sample.pdf_solid_angle <= 0.0f || IsNearBlack(sample.radiance)) {
            continue;
        }

        const Vec3f wi = sample.direction;
        const float cos_surface = std::max(0.0f, Dot(normal, wi));
        if (cos_surface <= 0.0f) {
            continue;
        }

        const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal, rng);
        if (IsNearBlack(bsdf)) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
        ++stats.shadow_rays;
        const ShadowVisibility visibility =
            TraceShadowVisibility(scene, acceleration, Ray3f{shadow_origin, wi}, std::numeric_limits<float>::infinity(), stats);
        if (!visibility.visible) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float pdf_bsdf = PdfBsdf(material, wo, wi, normal, rng);
        const float mis_weight = PowerHeuristic(light_sample_count, sample.pdf_solid_angle, 1, pdf_bsdf);
        const Color3f visible_radiance = Multiply(visibility.transmittance, sample.radiance);
        radiance = radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / sample.pdf_solid_angle);
    }

    return radiance * inverse_light_sample_count;
}

Color3f EstimateDirectLight(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const ShadingMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats
) {
    Color3f radiance;
    const int light_sample_count = PathLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);

    if (!scene.emissive_primitives.empty()) {
        for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
            const auto sample = SampleEmissiveLights(
                MakeLightSceneView(scene),
                sampler.Sample1D(SampleDimension::DirectLightSelect, sample_index),
                sampler.Sample2D(SampleDimension::DirectLightSurface, sample_index));
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

            const float pdf_light = PdfEmissiveLightSolidAngle(
                MakeLightSceneView(scene), hit_point, sample->point, sample->normal,
                sample->emissive_index);
            if (pdf_light <= 0.0f) {
                continue;
            }

            ++stats.shadow_rays;
            const Ray3f shadow_ray{shadow_origin, wi};
            const ShadowVisibility visibility =
                TraceShadowVisibility(scene, acceleration, shadow_ray, shadow_distance - shadow_bias, stats);
            if (!visibility.visible) {
                ++stats.occluded_shadow_rays;
                continue;
            }

            const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal, rng);
            if (IsNearBlack(bsdf)) {
                continue;
            }

            const float pdf_bsdf = PdfBsdf(material, wo, wi, normal, rng);
            const float mis_weight = PowerHeuristic(light_sample_count, pdf_light, 1, pdf_bsdf);
            const Color3f visible_radiance = Multiply(visibility.transmittance, sample->radiance);
            radiance = radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / pdf_light);
        }
        radiance = radiance * inverse_light_sample_count;
    }

    // Analytic (non-area, non-environment) lights — deltas, no MIS vs BSDF.
    if (!scene.analytic_lights.empty()) {
        const AnalyticLightSample sample = SampleAnalyticLight(
            MakeLightSceneView(scene), hit_point,
            sampler.Sample1D(SampleDimension::AnalyticLightSelect));
        if (sample.valid && sample.selection_pdf > 0.0f && !IsNearBlack(sample.radiance)) {
        const float cos_surface = std::max(0.0f, Dot(normal, sample.wi));
            if (cos_surface > 0.0f) {
                const Color3f bsdf = EvaluateBsdf(material, wo, sample.wi, normal, rng);
                if (!IsNearBlack(bsdf)) {
                    const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
                    ++stats.shadow_rays;
                    const ShadowVisibility visibility = TraceShadowVisibility(
                        scene, acceleration,
                        Ray3f{shadow_origin, sample.wi},
                        sample.distance - SurfaceBias(hit_point),
                        stats
                    );
                    if (!visibility.visible) {
                        ++stats.occluded_shadow_rays;
                    } else {
                        const Color3f visible_radiance = Multiply(
                            visibility.transmittance, sample.radiance);
                        radiance = radiance + Multiply(bsdf, visible_radiance) *
                            (cos_surface / sample.selection_pdf);
                    }
                }
            }
        }
    }

    radiance = radiance + EstimateDirectEnvironmentLight(scene, acceleration, material, hit_point, normal, wo, sampler, rng, stats);
    return radiance;
}


} // namespace yr
