#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>

namespace yr {

struct RenderScene;
struct RenderTriangle;

enum class BvhSplitMethod {
    LongestAxisMedian,
};

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
    int max_depth = 0;
};

struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
};

struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    const RenderTriangle* triangle = nullptr;
    int triangle_index = -1;
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(
    const std::vector<RenderTriangle>& triangles,
    const BvhBuildOptions& options = {}
);

BvhHit IntersectBvh(
    const RenderScene& scene,
    const Ray3f& ray,
    BvhTraceStats& stats
);

} // namespace yr
