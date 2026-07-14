#pragma once

#include <cstdint>

#include <yaoray/accel/acceleration.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/core/rng.hpp>
#include <yaoray/sampling/sampler.hpp>

namespace yr {

struct RenderSceneIR;
struct RenderSettings;

struct PathTraceStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t shadow_rays = 0;
    std::uint64_t occluded_shadow_rays = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t sphere_tests = 0;
    std::uint64_t bvh_node_tests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

int PathLightSampleCount(const RenderSceneIR& scene);

struct PathSample {
    Color3f beauty;
    Color3f albedo;
    Vec3f normal;
    float depth = 0.0f;
    bool primary_hit = false;
};

PathSample IntegratePathSample(
    const RenderSceneIR& scene,
    const RenderSettings& settings,
    const RenderAcceleration& acceleration,
    Ray3f ray,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats);

Color3f IntegratePath(
    const RenderSceneIR& scene,
    const RenderSettings& settings,
    const RenderAcceleration& acceleration,
    Ray3f ray,
    Sampler& sampler,
    Rng& rng,
    PathTraceStats& stats);

} // namespace yr
