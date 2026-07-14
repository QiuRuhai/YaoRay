#include "bvh_builder_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr::bvh_builder_detail {

bool IsFinite(Point3f point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool IsFinite(const Bounds3f& bounds) {
    return IsFinite(bounds.min) && IsFinite(bounds.max);
}

Bounds3f UnionBounds(const Bounds3f& a, const Bounds3f& b) {
    return Bounds3f{
        Point3f{
            std::min(a.min.x, b.min.x),
            std::min(a.min.y, b.min.y),
            std::min(a.min.z, b.min.z),
        },
        Point3f{
            std::max(a.max.x, b.max.x),
            std::max(a.max.y, b.max.y),
            std::max(a.max.z, b.max.z),
        },
    };
}

float AxisValue(Point3f point, int axis) {
    if (axis == 0) return point.x;
    if (axis == 1) return point.y;
    return point.z;
}

Vec3f Extent(const Bounds3f& bounds) {
    return Vec3f{
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z
    };
}

int LongestAxis(const Bounds3f& bounds) {
    const Vec3f extent = Extent(bounds);
    if (extent.x >= extent.y && extent.x >= extent.z) return 0;
    if (extent.y >= extent.z) return 1;
    return 2;
}

void EmitLeafNode(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    int depth,
    int node_index,
    RenderBvh& bvh
) {
    const int first_primitive = static_cast<int>(bvh.primitive_indices.size());
    for (int i = begin; i < end; ++i) {
        bvh.primitive_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
    }
    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds, first_primitive, end - begin};
    bvh.max_depth = std::max(bvh.max_depth, depth);
}

bool ComputeRangeBounds(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    Bounds3f& node_bounds,
    Bounds3f& centroid_bounds,
    std::vector<std::string>& errors
) {
    for (int i = begin; i < end; ++i) {
        node_bounds = UnionBounds(node_bounds, prims[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, prims[static_cast<std::size_t>(i)].centroid);
    }
    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return false;
    }
    return true;
}

int MergeSubtree(const RenderBvh& subtree, RenderBvh& bvh) {
    const int node_offset = static_cast<int>(bvh.nodes.size());
    const int primitive_offset = static_cast<int>(bvh.primitive_indices.size());

    bvh.nodes.reserve(bvh.nodes.size() + subtree.nodes.size());
    for (const RenderBvhNode& node : subtree.nodes) {
        RenderBvhNode adjusted = node;
        if (adjusted.IsLeaf()) {
            adjusted.payload_offset += primitive_offset;
        } else {
            if (adjusted.payload_offset >= 0) adjusted.payload_offset += node_offset;
        }
        bvh.nodes.push_back(adjusted);
    }

    bvh.primitive_indices.insert(
        bvh.primitive_indices.end(),
        subtree.primitive_indices.begin(),
        subtree.primitive_indices.end()
    );
    bvh.max_depth = std::max(bvh.max_depth, subtree.max_depth);
    return node_offset;
}

} // namespace yr::bvh_builder_detail
