#pragma once

#include <yaoray/backends/cpu/cpu_material.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>

namespace yr {

struct CpuSurfaceHit {
    bool hit = false;
    bool exhausted = false;
    BvhHit geometry_hit;
    ResolvedMaterialSample sample;
};

CpuSurfaceHit TraceVisibleSurface(
    const CpuPreparedScene& prepared_scene,
    const Ray3f& ray,
    float t_min,
    float t_max,
    BvhTraceStats* stats = nullptr);

} // namespace yr
