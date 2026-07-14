#pragma once

#include <string>
#include <vector>

#include <yaoray/accel/bvh.hpp>

namespace yr::bvh_builder_detail {

constexpr float ParallelEpsilon = 1.0e-8f;

struct BvhPrimRef {
    int flat_index = -1;
    RenderBvhPrimitive primitive;
    Bounds3f bounds;
    Point3f centroid;
};

struct SplitDecision {
    enum class Kind { MakeLeaf, Split };
    Kind kind = Kind::MakeLeaf;
    int mid = -1;
};

using SplitChooser = SplitDecision (*)(
    std::vector<BvhPrimRef>&,
    int,
    int,
    const Bounds3f&,
    const Bounds3f&,
    int
);

bool IsFinite(Point3f point);
bool IsFinite(const Bounds3f& bounds);
Bounds3f UnionBounds(const Bounds3f& a, const Bounds3f& b);
float AxisValue(Point3f point, int axis);
Vec3f Extent(const Bounds3f& bounds);
int LongestAxis(const Bounds3f& bounds);

void EmitLeafNode(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    int depth,
    int node_index,
    RenderBvh& bvh
);

bool ComputeRangeBounds(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    Bounds3f& node_bounds,
    Bounds3f& centroid_bounds,
    std::vector<std::string>& errors
);

int MergeSubtree(const RenderBvh& subtree, RenderBvh& bvh);

SplitDecision ChooseMedianSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
);

SplitDecision ChooseSahSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
);

int BuildSubtreeSerial(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    SplitChooser chooser,
    RenderBvh& bvh,
    std::vector<std::string>& errors
);

int BuildSubtreeParallel(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    int parallel_min_subtree_size,
    int fork_budget,
    SplitChooser chooser,
    RenderBvh& bvh,
    std::vector<std::string>& errors
);

bool BuildHierarchyFromRefs(
    std::vector<BvhPrimRef>& refs,
    const BvhBuildOptions& options,
    RenderBvh& bvh,
    std::vector<std::string>& errors
);

} // namespace yr::bvh_builder_detail
