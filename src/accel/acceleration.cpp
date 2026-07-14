#include <yaoray/accel/acceleration.hpp>

#include <algorithm>
#include <cmath>

namespace yr {

int RenderAcceleration::NodeCount() const {
    if (kind == RenderAccelerationKind::FlatReference) {
        return static_cast<int>(flat.nodes.size());
    }
    int count = static_cast<int>(two_level.tlas.nodes.size());
    for (const RenderBlas& blas : two_level.blases) {
        count += static_cast<int>(blas.bvh.nodes.size());
    }
    return count;
}

int RenderAcceleration::MaxDepth() const {
    if (kind == RenderAccelerationKind::FlatReference) return flat.max_depth;
    int blas_depth = 0;
    for (const RenderBlas& blas : two_level.blases) {
        blas_depth = std::max(blas_depth, blas.bvh.max_depth);
    }
    return two_level.tlas.max_depth + blas_depth;
}

RenderAccelerationBuildResult BuildRenderAcceleration(
    GeometryView geometry,
    const BvhBuildOptions& options
) {
    RenderAccelerationBuildResult result;
    if (geometry.instances.empty()) {
        BvhBuildResult flat = BuildBvh(geometry, options);
        result.errors = std::move(flat.errors);
        result.acceleration.kind = RenderAccelerationKind::FlatReference;
        result.acceleration.flat = std::move(flat.bvh);
        return result;
    }

    TwoLevelBvhBuildResult two_level = BuildTwoLevelBvh(geometry, options);
    result.errors = std::move(two_level.errors);
    result.acceleration.kind = RenderAccelerationKind::TwoLevel;
    result.acceleration.two_level = std::move(two_level.acceleration);
    return result;
}

BvhHit IntersectAcceleration(
    GeometryView geometry,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min,
    float t_max
) {
    if (acceleration.kind == RenderAccelerationKind::TwoLevel) {
        return IntersectTwoLevelBvh(
            geometry, acceleration.two_level, ray, stats, t_min, t_max);
    }
    return IntersectBvh(geometry, acceleration.flat, ray, stats, t_min, t_max);
}

BvhProbeHits IntersectAccelerationProbe(
    GeometryView geometry,
    const RenderAcceleration& acceleration,
    const Ray3f& ray,
    MeshPrimitiveHandle target_primitive,
    InstanceHandle target_instance,
    SphereHandle target_sphere,
    float t_min,
    float t_max
) {
    if (acceleration.kind == RenderAccelerationKind::FlatReference) {
        return IntersectBvhProbe(
            geometry, acceleration.flat, ray, target_primitive, target_sphere, t_min, t_max);
    }

    BvhProbeHits result;
    BvhTraceStats stats;
    constexpr int MaxIterations = 4096;
    float cursor = t_min;
    for (int iter = 0; iter < MaxIterations; ++iter) {
        const BvhHit hit = IntersectAcceleration(
            geometry, acceleration, ray, stats, cursor, t_max);
        if (!hit.hit) break;

        const bool instance_matches =
            !target_instance.IsValid() || hit.instance == target_instance;
        const bool is_target =
            (target_primitive.IsValid() && hit.triangle_index >= 0 &&
             hit.mesh_primitive == target_primitive && instance_matches) ||
            (target_sphere.IsValid() && hit.sphere == target_sphere);
        if (is_target) {
            if (result.count < BvhProbeHits::MaxHits) {
                result.hits[result.count++] = hit;
            } else {
                result.exhausted = true;
                break;
            }
        }

        float next = hit.t + 1.0e-4f * (1.0f + std::fabs(hit.t));
        if (!(next > cursor)) next = cursor + 1.0e-4f;
        cursor = next;
        if (cursor >= t_max) break;
    }
    return result;
}

} // namespace yr
