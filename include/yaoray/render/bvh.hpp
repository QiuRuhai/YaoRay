#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>

namespace yr {

struct RenderSceneIR;

enum class BvhSplitMethod { LongestAxisMedian, SahBucketBinning };

struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::SahBucketBinning;
    int max_leaf_triangles = 4;
    // Number of worker threads for parallel BVH construction.
    // 0 = auto (std::thread::hardware_concurrency()).
    // 1 = serial (no threading).
    // >=2 = parallel with up to that many threads.
    int thread_count = 0;
    // Subtrees smaller than this many primitives build serially even when
    // parallel construction is active.
    int parallel_min_subtree_size = 1024;
};

struct RenderBvhNode {
    Bounds3f bounds;
    int left_child = -1;
    int right_child = -1;
    int first_triangle = 0;
    int triangle_count = 0;
};

struct RenderBvh {
    std::vector<RenderBvhNode> nodes;
    std::vector<int> triangle_indices;
    std::vector<std::pair<int, int>> triangle_to_primitive;  // flat tri → (primitive_index, local_tri)
    int max_depth = 0;
    int total_triangles = 0;
};

struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
};

struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    int triangle_index = -1;
    int primitive_index = -1;
    int sphere_index = -1;       // -1 for triangle hits; >= 0 for sphere hits
    float bary_u = 0.0f;         // For sphere hits, this is the U on the sphere (azimuth).
    float bary_v = 0.0f;         // For sphere hits, this is the V on the sphere (zenith).
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(const RenderSceneIR& scene, const BvhBuildOptions& options = {});

BvhHit IntersectBvh(
    const RenderSceneIR& scene,
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
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    int target_primitive_index,   // -1 to not match triangles
    int target_sphere_index,      // -1 to not match spheres
    float t_min,
    float t_max);

} // namespace yr
