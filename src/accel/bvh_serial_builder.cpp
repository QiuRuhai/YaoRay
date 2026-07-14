#include "bvh_builder_internal.hpp"

#include <algorithm>
#include <cstddef>

namespace yr::bvh_builder_detail {

int BuildSubtreeSerial(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    SplitChooser chooser,
    RenderBvh& bvh,
    std::vector<std::string>& errors
) {
    if (begin >= end) {
        errors.push_back("BVH build produced an empty primitive range");
        return -1;
    }

    const int node_index = static_cast<int>(bvh.nodes.size());
    bvh.nodes.push_back(RenderBvhNode{});

    Bounds3f node_bounds;
    Bounds3f centroid_bounds;
    if (!ComputeRangeBounds(prims, begin, end, node_bounds, centroid_bounds, errors)) {
        return -1;
    }

    const SplitDecision decision = chooser(
        prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles
    );
    if (decision.kind == SplitDecision::Kind::MakeLeaf) {
        EmitLeafNode(prims, begin, end, node_bounds, depth, node_index, bvh);
        return node_index;
    }
    if (decision.mid <= begin || decision.mid >= end) {
        errors.push_back("BVH split produced an empty child range");
        return -1;
    }

    const int left_child = BuildSubtreeSerial(
        prims, begin, decision.mid, depth + 1, max_leaf_triangles, chooser, bvh, errors
    );
    const int right_child = BuildSubtreeSerial(
        prims, decision.mid, end, depth + 1, max_leaf_triangles, chooser, bvh, errors
    );
    if (left_child < 0 || right_child < 0) return -1;

    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds, right_child, 0
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
    return node_index;
}

} // namespace yr::bvh_builder_detail
