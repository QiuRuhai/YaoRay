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
    BvhSplitMethod split_method = BvhSplitMethod::LongestAxisMedian;
    int max_leaf_triangles = 4;
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

} // namespace yr
