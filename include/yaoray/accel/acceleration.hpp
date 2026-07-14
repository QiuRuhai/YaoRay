#pragma once

#include <string>
#include <vector>

#include <yaoray/accel/two_level_bvh.hpp>

namespace yr {

enum class RenderAccelerationKind { FlatReference, TwoLevel };

struct RenderAcceleration {
    RenderAccelerationKind kind = RenderAccelerationKind::FlatReference;
    RenderBvh flat;
    TwoLevelBvh two_level;

    int NodeCount() const;
    int MaxDepth() const;
};

struct RenderAccelerationBuildResult {
    RenderAcceleration acceleration;
    std::vector<std::string> errors;
};

// Explicit mesh instances select the two-level path. Scenes without them keep
// using the flat built-in SAH BVH as the reference implementation.
RenderAccelerationBuildResult BuildRenderAcceleration(
    GeometryView geometry,
    const BvhBuildOptions& options = {}
);

BvhHit IntersectAcceleration(
    GeometryView geometry,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min = 1.0e-5f,
    float t_max = std::numeric_limits<float>::infinity()
);

BvhProbeHits IntersectAccelerationProbe(
    GeometryView geometry,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    MeshPrimitiveHandle target_primitive,
    InstanceHandle target_instance,
    SphereHandle target_sphere,
    float t_min,
    float t_max
);

} // namespace yr
