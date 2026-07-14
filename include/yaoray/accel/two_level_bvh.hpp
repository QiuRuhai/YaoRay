#pragma once

#include <span>
#include <string>
#include <vector>

#include <yaoray/accel/bvh.hpp>
#include <yaoray/core/transform.hpp>

namespace yr {

enum class TlasPrimitiveKind { Instance, Sphere };

struct RenderBlas {
    MeshPrimitiveHandle primitive;
    Bounds3f object_bounds;
    RenderBvh bvh;
};

struct PreparedMeshInstance {
    MeshPrimitiveHandle primitive;
    int blas_index = -1;
    Mat4f object_to_world;
    Mat4f world_to_object;
    Bounds3f world_bounds;
};

struct RenderTlasPrimitive {
    TlasPrimitiveKind kind = TlasPrimitiveKind::Instance;
    InstanceHandle instance;
    SphereHandle sphere;
    Bounds3f world_bounds;
};

struct RenderTlas {
    std::vector<RenderBvhNode> nodes;
    std::vector<int> primitive_indices;
    std::vector<RenderTlasPrimitive> primitives;
    int max_depth = 0;
};

struct TwoLevelBvh {
    std::vector<RenderBlas> blases;
    std::vector<PreparedMeshInstance> instances;
    RenderTlas tlas;
};

struct TwoLevelBvhBuildResult {
    TwoLevelBvh acceleration;
    std::vector<std::string> errors;
};

// If GeometryView::instances is empty, one identity instance is synthesized
// for every mesh primitive. Otherwise the explicit instance table is used.
TwoLevelBvhBuildResult BuildTwoLevelBvh(
    GeometryView geometry,
    const BvhBuildOptions& options = {}
);

// Refit keeps TLAS topology and updates only instance transforms and ancestor
// bounds. Rebuild recomputes the SAH TLAS topology while reusing immutable BLASes.
bool RefitTwoLevelBvh(
    GeometryView geometry,
    std::span<const Mat4f> object_to_world,
    TwoLevelBvh& acceleration,
    std::string& error
);

bool RebuildTwoLevelTlas(
    GeometryView geometry,
    std::span<const Mat4f> object_to_world,
    const BvhBuildOptions& options,
    TwoLevelBvh& acceleration,
    std::string& error
);

BvhHit IntersectTwoLevelBvh(
    GeometryView geometry,
    const TwoLevelBvh& acceleration,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min = 1.0e-5f,
    float t_max = std::numeric_limits<float>::infinity()
);

} // namespace yr
