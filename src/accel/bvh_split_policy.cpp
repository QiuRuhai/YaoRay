#include "bvh_builder_internal.hpp"

#include <algorithm>
#include <limits>

namespace yr::bvh_builder_detail {
namespace {

constexpr int SahBucketCount = 12;
constexpr float SahTraversalCost = 0.5f;

struct SahBucket {
    int count = 0;
    Bounds3f bounds;
};

float SurfaceArea(const Bounds3f& bounds) {
    if (bounds.min.x > bounds.max.x ||
        bounds.min.y > bounds.max.y ||
        bounds.min.z > bounds.max.z) {
        return 0.0f;
    }
    const Vec3f extent = Extent(bounds);
    return 2.0f * (
        extent.x * extent.y + extent.y * extent.z + extent.z * extent.x
    );
}

int CentroidToBucket(float centroid, float minimum, float extent) {
    const float offset = (centroid - minimum) / extent;
    return std::clamp(
        static_cast<int>(offset * static_cast<float>(SahBucketCount)),
        0,
        SahBucketCount - 1
    );
}

bool ChooseBestSahSplit(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int& best_axis,
    int& best_split,
    float& best_cost
) {
    const float parent_area = SurfaceArea(node_bounds);
    if (parent_area <= 0.0f) return false;

    const Vec3f centroid_extent = Extent(centroid_bounds);
    for (int axis = 0; axis < 3; ++axis) {
        const float extent = AxisValue(centroid_extent, axis);
        if (extent < ParallelEpsilon) continue;
        const float minimum = AxisValue(centroid_bounds.min, axis);

        SahBucket buckets[SahBucketCount];
        for (int i = begin; i < end; ++i) {
            const BvhPrimRef& primitive = prims[static_cast<std::size_t>(i)];
            SahBucket& bucket = buckets[CentroidToBucket(
                AxisValue(primitive.centroid, axis), minimum, extent
            )];
            ++bucket.count;
            bucket.bounds = UnionBounds(bucket.bounds, primitive.bounds);
        }

        SahBucket left[SahBucketCount];
        SahBucket right[SahBucketCount];
        left[0] = buckets[0];
        for (int i = 1; i < SahBucketCount; ++i) {
            left[i].count = left[i - 1].count + buckets[i].count;
            left[i].bounds = UnionBounds(left[i - 1].bounds, buckets[i].bounds);
        }
        right[SahBucketCount - 1] = buckets[SahBucketCount - 1];
        for (int i = SahBucketCount - 2; i >= 0; --i) {
            right[i].count = right[i + 1].count + buckets[i].count;
            right[i].bounds = UnionBounds(right[i + 1].bounds, buckets[i].bounds);
        }

        for (int split = 0; split < SahBucketCount - 1; ++split) {
            const int left_count = left[split].count;
            const int right_count = right[split + 1].count;
            if (left_count == 0 || right_count == 0) continue;
            const float cost = SahTraversalCost +
                (static_cast<float>(left_count) * SurfaceArea(left[split].bounds) +
                 static_cast<float>(right_count) * SurfaceArea(right[split + 1].bounds)) /
                parent_area;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = split;
            }
        }
    }
    return best_axis >= 0;
}

void MedianPartition(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int axis,
    int mid
) {
    std::nth_element(
        prims.begin() + begin,
        prims.begin() + mid,
        prims.begin() + end,
        [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
            return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
        }
    );
}

} // namespace

SplitDecision ChooseMedianSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f&,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
) {
    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) return {};

    const int mid = begin + primitive_count / 2;
    MedianPartition(prims, begin, end, LongestAxis(centroid_bounds), mid);
    return SplitDecision{SplitDecision::Kind::Split, mid};
}

SplitDecision ChooseSahSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
) {
    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) return {};

    int best_axis = -1;
    int best_split = -1;
    float best_cost = std::numeric_limits<float>::infinity();
    const bool found = ChooseBestSahSplit(
        prims,
        begin,
        end,
        node_bounds,
        centroid_bounds,
        best_axis,
        best_split,
        best_cost
    );
    if (found && best_cost >= static_cast<float>(primitive_count)) return {};

    int mid = begin + primitive_count / 2;
    if (!found) {
        MedianPartition(prims, begin, end, LongestAxis(centroid_bounds), mid);
        return SplitDecision{SplitDecision::Kind::Split, mid};
    }

    const float extent = AxisValue(Extent(centroid_bounds), best_axis);
    const float minimum = AxisValue(centroid_bounds.min, best_axis);
    const auto partition = std::partition(
        prims.begin() + begin,
        prims.begin() + end,
        [best_axis, best_split, minimum, extent](const BvhPrimRef& primitive) {
            return CentroidToBucket(
                AxisValue(primitive.centroid, best_axis), minimum, extent
            ) <= best_split;
        }
    );
    mid = static_cast<int>(partition - prims.begin());
    if (mid == begin || mid == end) {
        mid = begin + primitive_count / 2;
        MedianPartition(prims, begin, end, best_axis, mid);
    }
    return SplitDecision{SplitDecision::Kind::Split, mid};
}

} // namespace yr::bvh_builder_detail
