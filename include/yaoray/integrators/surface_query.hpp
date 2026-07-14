#pragma once

#include <yaoray/shading/material_evaluator.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/accel/acceleration.hpp>

namespace yr {

struct RenderSceneIR;

struct SurfaceHit {
    bool hit = false;
    bool exhausted = false;
    BvhHit geometry_hit;
    ResolvedMaterial sample;
};

SurfaceHit TraceVisibleSurface(
    const RenderSceneIR& scene,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    float t_min,
    float t_max,
    BvhTraceStats* stats = nullptr);

} // namespace yr
