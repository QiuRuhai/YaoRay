#include <yaoray/integrators/path_integrator.hpp>

#include "path_integrator_internal.hpp"

#include <yaoray/geometry/intersection.hpp>
#include <yaoray/integrators/bssrdf_probe.hpp>
#include <yaoray/integrators/mis.hpp>
#include <yaoray/integrators/surface_query.hpp>
#include <yaoray/lighting/environment.hpp>
#include <yaoray/lighting/light_sampling.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/shading/bsdf.hpp>
#include <yaoray/shading/bssrdf.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace yr {

PathSample IntegratePathSample(const RenderSceneIR& scene, const RenderSettings& settings,
    const RenderAcceleration& acceleration, Ray3f ray, Sampler& sampler, Rng& rng, PathTraceStats& stats) {
    PathSample path_sample;
    Color3f& radiance = path_sample.beauty;
    Color3f throughput{1.0f, 1.0f, 1.0f};
    PreviousBounce previous_bounce;
    PathMediumState medium;
    const int max_depth = std::max(1, settings.max_depth);

    for (int depth = 0; depth < max_depth; ++depth) {
        sampler.BeginBounce(depth);
        ++stats.rays_traced;

        BvhTraceStats trace_stats;
        const SurfaceHit surface_hit = TraceVisibleSurface(
            scene,
            acceleration,
            ray,
            1.0e-5f,
            std::numeric_limits<float>::infinity(),
            &trace_stats
        );
        AccumulateTraceStats(stats, trace_stats);
        const bool is_miss = !surface_hit.hit ||
            (surface_hit.geometry_hit.triangle_index < 0 &&
             !surface_hit.geometry_hit.sphere.IsValid());
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
        const RenderPrimitive* hit_primitive =
            scene.Geometry().Find(surface_hit.geometry_hit.mesh_primitive);
        if (hit_primitive != nullptr &&
            !IsValidMaterialIndex(scene, hit_primitive->material_index)) {
            radiance = radiance + Multiply(throughput, Color3f{1.0f, 0.0f, 1.0f});
            break;
        }

        const Point3f hit_point = ray.At(surface_hit.geometry_hit.t);
        const Vec3f wo = -ray.direction;
        const ShadingMaterial& material = surface_hit.sample.material;
        const Vec3f normal = surface_hit.sample.shading_normal;
        if (depth == 0) {
            path_sample.primary_hit = true;
            path_sample.albedo = material.reflectance.value;
            path_sample.normal = normal;
            path_sample.depth = surface_hit.geometry_hit.t;
        }

        // Emissive contribution. Sphere area lights are not yet supported,
        // so we guard LocateTriangle behind triangle_index >= 0 and skip the
        // MIS branch for sphere hits (their emission is treated as zero).
        if (!IsNearBlack(material.emission) && surface_hit.geometry_hit.triangle_index >= 0) {
            const TriangleRef hit_tri =
                LocateTriangle(scene.Geometry(), surface_hit.geometry_hit.triangle_index);
            const Vec3f light_geo_normal = GeometricNormal(scene.Geometry(), hit_tri);
            const float emission_weight = EmissiveHitMisWeight(
                scene, previous_bounce, hit_point, light_geo_normal, hit_tri.primitive.Value());
            radiance = radiance + Multiply(throughput, material.emission) * emission_weight;
        }

        if (!IsDeltaBsdf(material)) {
            radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, acceleration, material, hit_point, normal, wo, sampler, rng, stats));
        }

        if (depth + 1 >= max_depth) {
            break;
        }

        // --- Subsurface (BSSRDF) entry / exit / continue ---
        if (material.kind == RenderMaterialKind::Subsurface && material.bssrdf_table != nullptr) {
            const Vec3f ns = normal;
            // Build an orthonormal frame (ss, ts) about ns.
            Vec3f ss = (std::fabs(ns.x) > 0.9f)
                           ? Normalize(Cross(ns, Vec3f{0.0f, 1.0f, 0.0f}))
                           : Normalize(Cross(ns, Vec3f{1.0f, 0.0f, 0.0f}));
            const Vec3f ts = Cross(ns, ss);

            const float eta = material.bssrdf_eta;
            const float cos_theta_o = std::fabs(Dot(wo, ns));
            const float fr = FrDielectric(cos_theta_o, eta);

            if (sampler.Sample1D(SampleDimension::BssrdfReflect) < fr) {
                // Specular reflection at the entry interface (throughput unchanged:
                // the fr selection probability cancels the fr Fresnel factor).
                const Vec3f wr = ns * (2.0f * Dot(wo, ns)) - wo;
                previous_bounce = PreviousBounce{true, true, hit_point, 1.0f, PathLightSampleCount(scene)};
                const Vec3f bias_n = Dot(wr, ns) >= 0.0f ? ns : -ns;
                ray = Ray3f{hit_point + bias_n * SurfaceBias(hit_point), wr};
                continue;
            }

            // Enter the medium. (1 - fr) is consumed by the split, so it is NOT
            // multiplied again here; S = (1-fr)*Sp*Sw, and Sw is applied at the exit.
            const TabulatedBSSRDF bssrdf(material.sigma_a, material.sigma_s, eta, *material.bssrdf_table);
            const BssrdfProbeSample probe = SampleBssrdfProbe(
                bssrdf, scene, acceleration, hit_point, ss, ts, ns,
                surface_hit.geometry_hit.mesh_primitive.Value(),
                surface_hit.geometry_hit.instance.Value(),
                surface_hit.geometry_hit.sphere.Value(),
                sampler.Sample1D(SampleDimension::BssrdfAxis),
                sampler.Sample2D(SampleDimension::BssrdfDisk));
            if (!probe.hit || probe.pdf <= 0.0f) {
                break;
            }

            throughput = Multiply(throughput, probe.sp / probe.pdf);
            if (IsNearBlack(throughput)) {
                break;
            }

            // Exit interface as a normalized Fresnel-weighted cosine (Sw) lobe.
            ShadingMaterial exit_material;
            exit_material.kind = RenderMaterialKind::SubsurfaceExit;
            exit_material.ior = eta;
            const Vec3f ni = probe.ni;

            // Direct lighting at the exit point (reuses the standard NEE path).
            radiance = radiance + Multiply(throughput,
                EstimateDirectLight(scene, acceleration, exit_material, probe.pi, ni, ni, sampler, rng, stats));

            // Sample the continuation direction from the exit lobe.
            const BsdfSample exit_sample = SampleBsdf(exit_material, ni, ni,
                sampler.Sample2D(SampleDimension::BssrdfExitBsdf), rng);
            if (!exit_sample.valid || IsNearBlack(exit_sample.weight)) {
                break;
            }
            throughput = Multiply(throughput, exit_sample.weight);
            if (!SurviveRussianRoulette(depth, throughput, sampler)) {
                break;
            }

            previous_bounce = PreviousBounce{true, false, probe.pi, exit_sample.pdf, PathLightSampleCount(scene)};
            const Vec3f bias_n = Dot(exit_sample.wi, ni) >= 0.0f ? ni : -ni;
            ray = Ray3f{probe.pi + bias_n * SurfaceBias(probe.pi), exit_sample.wi};
            continue;
        }

        const BsdfSample bsdf_sample = SampleBsdf(material, wo, normal,
            sampler.Sample2D(SampleDimension::Bsdf), rng);
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
            PathLightSampleCount(scene)
        };
        const Vec3f bias_normal = Dot(bsdf_sample.wi, normal) >= 0.0f ? normal : -normal;
        ray = Ray3f{hit_point + bias_normal * SurfaceBias(hit_point), bsdf_sample.wi};
    }

    path_sample.beauty = ClampMaxComponent(radiance, settings.radiance_clamp);
    return path_sample;
}

Color3f IntegratePath(const RenderSceneIR& scene, const RenderSettings& settings,
    const RenderAcceleration& acceleration, Ray3f ray, Sampler& sampler, Rng& rng,
    PathTraceStats& stats) {
    return IntegratePathSample(scene, settings, acceleration, ray, sampler, rng, stats).beauty;
}

} // namespace yr
