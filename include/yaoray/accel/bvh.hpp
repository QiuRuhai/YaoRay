#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/scene/geometry.hpp>

namespace yr {

enum class BvhSplitMethod { LongestAxisMedian, SahBucketBinning };
enum class BvhPrimitiveKind { Triangle, Sphere };

struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::SahBucketBinning;
    int max_leaf_triangles = 4;
    // Build the portable four-wide SoA traversal cache. It is opt-in because
    // scalar BVH4 traversal can be slower than the compact binary layout on
    // CPUs without a matching four-lane SIMD implementation.
    bool enable_bvh4 = false;
    // Number of worker threads for parallel BVH construction.
    // 0 = auto (std::thread::hardware_concurrency()).
    // 1 = serial (no threading).
    // >=2 = parallel with up to that many threads.
    int thread_count = 0;
    // Subtrees smaller than this many primitives build serially even when
    // parallel construction is active.
    int parallel_min_subtree_size = 1024;
};

struct alignas(32) RenderBvhNode {
    Bounds3f bounds;
    // Leaf: payload_offset is the first primitive and primitive_count > 0.
    // Interior: payload_offset is the right child; the left child is index + 1.
    int payload_offset = -1;
    int primitive_count = 0;

    bool IsLeaf() const { return primitive_count > 0; }
    int FirstPrimitive() const { return payload_offset; }
    int LeftChild(int node_index) const { return IsLeaf() ? -1 : node_index + 1; }
    int RightChild() const { return IsLeaf() ? -1 : payload_offset; }
};

static_assert(sizeof(RenderBvhNode) == 32);

struct RenderBvhPrimitive {
    BvhPrimitiveKind kind = BvhPrimitiveKind::Triangle;
    MeshPrimitiveHandle mesh_primitive;
    SphereHandle sphere;
    int local_triangle = -1;
    int flat_triangle_index = -1;
};

struct alignas(64) RenderBvh4Node {
    float min_x[4]{};
    float min_y[4]{};
    float min_z[4]{};
    float max_x[4]{};
    float max_y[4]{};
    float max_z[4]{};
    // primitive_count > 0: payload is first primitive. Otherwise payload is
    // the child BVH4 node. Negative payload marks an unused lane.
    int payload[4]{-1, -1, -1, -1};
    std::uint16_t primitive_count[4]{};
};

static_assert(sizeof(RenderBvh4Node) == 128);

struct RenderBvh {
    std::vector<RenderBvhNode> nodes;
    std::vector<RenderBvh4Node> wide_nodes;
    std::vector<int> primitive_indices;
    std::vector<RenderBvhPrimitive> primitives;
    int max_depth = 0;
    int total_primitives = 0;
    int total_triangles = 0;
    int total_spheres = 0;
};

struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t sphere_tests = 0;
};

struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    int triangle_index = -1;
    MeshPrimitiveHandle mesh_primitive;
    InstanceHandle instance;
    SphereHandle sphere;
    float bary_u = 0.0f;         // For sphere hits, this is the U on the sphere (azimuth).
    float bary_v = 0.0f;         // For sphere hits, this is the V on the sphere (zenith).
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(GeometryView geometry, const BvhBuildOptions& options = {});

BvhHit IntersectBvh(
    GeometryView geometry,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min = 1.0e-5f,
    float t_max = std::numeric_limits<float>::infinity()
);

// All intersections along `ray` (within [t_min, t_max]) that belong to one target
// object, collected by repeatedly calling IntersectBvh and advancing past each hit
// (no all-hits BVH traversal exists). A hit is kept when it is a triangle of
// `target_primitive_index` OR a hit on `target_sphere_index`; other geometry is
// transparent to the probe (skipped, not stopped). Hits are returned in
// increasing-t order, bounded by MaxHits.
struct BvhProbeHits {
    static constexpr int MaxHits = 64;
    int count = 0;
    bool exhausted = false;  // true if more than MaxHits target hits existed
    BvhHit hits[MaxHits];
};

BvhProbeHits IntersectBvhProbe(
    GeometryView geometry,
    const RenderBvh& bvh,
    const Ray3f& ray,
    MeshPrimitiveHandle target_primitive,
    SphereHandle target_sphere,
    float t_min,
    float t_max);

} // namespace yr
