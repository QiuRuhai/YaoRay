#include "bvh_builder_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <thread>

namespace yr::bvh_builder_detail {

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
) {
    const int primitive_count = end - begin;
    if (primitive_count <= parallel_min_subtree_size || fork_budget <= 1) {
        return BuildSubtreeSerial(
            prims, begin, end, depth, max_leaf_triangles, chooser, bvh, errors
        );
    }
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

    RenderBvh left_subtree;
    RenderBvh right_subtree;
    std::vector<std::string> left_errors;
    std::vector<std::string> right_errors;
    const int left_budget = (fork_budget + 1) / 2;
    const int right_budget = fork_budget - left_budget;

    std::thread left_thread([&] {
        BuildSubtreeParallel(
            prims,
            begin,
            decision.mid,
            depth + 1,
            max_leaf_triangles,
            parallel_min_subtree_size,
            left_budget,
            chooser,
            left_subtree,
            left_errors
        );
    });
    std::thread right_thread([&] {
        BuildSubtreeParallel(
            prims,
            decision.mid,
            end,
            depth + 1,
            max_leaf_triangles,
            parallel_min_subtree_size,
            right_budget,
            chooser,
            right_subtree,
            right_errors
        );
    });
    left_thread.join();
    right_thread.join();

    errors.insert(errors.end(), left_errors.begin(), left_errors.end());
    errors.insert(errors.end(), right_errors.begin(), right_errors.end());
    if (!left_errors.empty() || !right_errors.empty()) return -1;
    if (left_subtree.nodes.empty() || right_subtree.nodes.empty()) {
        errors.push_back("BVH parallel subtree returned empty");
        return -1;
    }

    const int left_child = MergeSubtree(left_subtree, bvh);
    const int right_child = MergeSubtree(right_subtree, bvh);
    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds, right_child, 0
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
    return node_index;
}

} // namespace yr::bvh_builder_detail
