#include <yaoray/render/bvh.hpp>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace yr {
namespace {

constexpr float ParallelEpsilon = 1.0e-8f;

struct BvhPrimRef {
    int flat_index = -1;
    int primitive_index = -1;
    int local_triangle = -1;
    Bounds3f bounds;
    Point3f centroid;
};

bool IsFinite(float value) {
    return std::isfinite(value);
}

bool IsFinite(Point3f point) {
    return IsFinite(point.x) && IsFinite(point.y) && IsFinite(point.z);
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
    if (axis == 0) {
        return point.x;
    }
    if (axis == 1) {
        return point.y;
    }
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
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

struct SplitDecision {
    enum class Kind { MakeLeaf, Split };
    Kind kind = Kind::MakeLeaf;
    int mid = -1;  // partition point; only valid when kind == Split
};

void EmitLeafNode(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    int depth,
    int node_index,
    RenderBvh& bvh
) {
    const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
    for (int i = begin; i < end; ++i) {
        bvh.triangle_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
    }
    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds,
        -1,
        -1,
        first_triangle,
        end - begin
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
}

bool ComputeRangeBounds(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    Bounds3f* out_node_bounds,
    Bounds3f* out_centroid_bounds,
    std::vector<std::string>& errors
) {
    Bounds3f node_bounds;
    Bounds3f centroid_bounds;
    for (int i = begin; i < end; ++i) {
        node_bounds = UnionBounds(node_bounds, prims[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, prims[static_cast<std::size_t>(i)].centroid);
    }
    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return false;
    }
    *out_node_bounds = node_bounds;
    *out_centroid_bounds = centroid_bounds;
    return true;
}

SplitDecision ChooseMedianSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& /*node_bounds*/,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
) {
    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) {
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
    }

    const int axis = LongestAxis(centroid_bounds);
    const int mid = begin + primitive_count / 2;
    std::nth_element(
        prims.begin() + begin,
        prims.begin() + mid,
        prims.begin() + end,
        [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
            return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
        }
    );
    return SplitDecision{SplitDecision::Kind::Split, mid};
}

constexpr int kSahBucketCount = 12;
constexpr float kSahTraversalCost = 0.5f;

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
    const Vec3f e = Extent(bounds);
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

int CentroidToBucket(float centroid_axis, float bounds_min_axis, float extent_axis) {
    // Map centroid_axis in [bounds_min_axis, bounds_min_axis + extent_axis]
    // to bucket in [0, kSahBucketCount). Caller guarantees extent_axis > 0.
    const float t = (centroid_axis - bounds_min_axis) / extent_axis;
    int bucket = static_cast<int>(t * static_cast<float>(kSahBucketCount));
    if (bucket < 0) bucket = 0;
    if (bucket >= kSahBucketCount) bucket = kSahBucketCount - 1;
    return bucket;
}

// Returns true and populates *out_axis, *out_split_idx, *out_cost if a valid
// SAH split is found. Returns false when every axis has near-zero centroid
// extent (degenerate input) or every candidate split would leave one side
// empty.
bool ChooseBestSahSplit(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int* out_axis,
    int* out_split_idx,
    float* out_cost
) {
    const float parent_area = SurfaceArea(node_bounds);
    if (parent_area <= 0.0f) {
        return false;
    }
    const Vec3f centroid_extent = Extent(centroid_bounds);

    float best_cost = std::numeric_limits<float>::infinity();
    int best_axis = -1;
    int best_split = -1;

    for (int axis = 0; axis < 3; ++axis) {
        const float extent_axis =
            (axis == 0) ? centroid_extent.x :
            (axis == 1) ? centroid_extent.y : centroid_extent.z;
        if (extent_axis < ParallelEpsilon) {
            continue;
        }
        const float min_axis = AxisValue(centroid_bounds.min, axis);

        SahBucket buckets[kSahBucketCount];
        for (int i = begin; i < end; ++i) {
            const BvhPrimRef& p = prims[static_cast<std::size_t>(i)];
            const int b = CentroidToBucket(AxisValue(p.centroid, axis), min_axis, extent_axis);
            buckets[b].count += 1;
            buckets[b].bounds = UnionBounds(buckets[b].bounds, p.bounds);
        }

        // Forward sweep: prefix counts + prefix bounds (left side of each split).
        SahBucket left[kSahBucketCount];
        left[0] = buckets[0];
        for (int i = 1; i < kSahBucketCount; ++i) {
            left[i].count = left[i - 1].count + buckets[i].count;
            left[i].bounds = UnionBounds(left[i - 1].bounds, buckets[i].bounds);
        }

        // Backward sweep: suffix counts + suffix bounds (right side of each split).
        SahBucket right[kSahBucketCount];
        right[kSahBucketCount - 1] = buckets[kSahBucketCount - 1];
        for (int i = kSahBucketCount - 2; i >= 0; --i) {
            right[i].count = right[i + 1].count + buckets[i].count;
            right[i].bounds = UnionBounds(right[i + 1].bounds, buckets[i].bounds);
        }

        // Evaluate splits between bucket split_idx and split_idx + 1.
        for (int split_idx = 0; split_idx < kSahBucketCount - 1; ++split_idx) {
            const int n_left = left[split_idx].count;
            const int n_right = right[split_idx + 1].count;
            if (n_left == 0 || n_right == 0) {
                continue;
            }
            const float cost =
                kSahTraversalCost +
                (static_cast<float>(n_left) * SurfaceArea(left[split_idx].bounds) +
                 static_cast<float>(n_right) * SurfaceArea(right[split_idx + 1].bounds)) /
                parent_area;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = split_idx;
            }
        }
    }

    if (best_axis < 0) {
        return false;
    }
    *out_axis = best_axis;
    *out_split_idx = best_split;
    *out_cost = best_cost;
    return true;
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

    // Leaf criterion (a): small enough -> leaf regardless of SAH cost.
    if (primitive_count <= max_leaf_triangles) {
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
    }

    int best_axis = -1;
    int best_split = -1;
    float best_cost = std::numeric_limits<float>::infinity();
    const bool found_split = ChooseBestSahSplit(
        prims, begin, end, node_bounds, centroid_bounds,
        &best_axis, &best_split, &best_cost
    );

    // Leaf criterion (b): SAH says splitting is more expensive than leafing.
    const float leaf_cost = static_cast<float>(primitive_count);
    if (found_split && best_cost >= leaf_cost) {
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
    }

    int mid = -1;
    if (found_split) {
        // Partition by bucket assignment along best_axis.
        const Vec3f centroid_extent = Extent(centroid_bounds);
        const float extent_axis =
            (best_axis == 0) ? centroid_extent.x :
            (best_axis == 1) ? centroid_extent.y : centroid_extent.z;
        const float min_axis = AxisValue(centroid_bounds.min, best_axis);
        const int split_idx = best_split;

        auto partition_iter = std::partition(
            prims.begin() + begin, prims.begin() + end,
            [best_axis, min_axis, extent_axis, split_idx](const BvhPrimRef& p) {
                return CentroidToBucket(AxisValue(p.centroid, best_axis), min_axis, extent_axis) <= split_idx;
            }
        );
        mid = static_cast<int>(partition_iter - prims.begin());

        if (mid == begin || mid == end) {
            // Defensive: SAH should never pick a split that leaves a side
            // empty (the n_left==0 / n_right==0 skip in ChooseBestSahSplit
            // filters that), but float rounding in bucket mapping could
            // collapse the partition. Fall back to median.
            mid = begin + primitive_count / 2;
            std::nth_element(
                prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
                [best_axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                    return AxisValue(a.centroid, best_axis) < AxisValue(b.centroid, best_axis);
                }
            );
        }
    } else {
        // No valid SAH split anywhere -> degenerate centroid bounds. Force an
        // even partition along the longest centroid axis (extent may be tiny
        // or zero; the nth_element is a no-op when all centroids are equal,
        // and the index-based midpoint still partitions deterministically).
        const int axis = LongestAxis(centroid_bounds);
        mid = begin + primitive_count / 2;
        std::nth_element(
            prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
            [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
            }
        );
    }

    return SplitDecision{SplitDecision::Kind::Split, mid};
}

template <typename ChooserFn>
int BuildSubtreeSerial(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    ChooserFn chooser,
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
    if (!ComputeRangeBounds(prims, begin, end, &node_bounds, &centroid_bounds, errors)) {
        return -1;
    }

    const SplitDecision decision =
        chooser(prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles);

    if (decision.kind == SplitDecision::Kind::MakeLeaf) {
        EmitLeafNode(prims, begin, end, node_bounds, depth, node_index, bvh);
        return node_index;
    }

    const int mid = decision.mid;
    if (mid <= begin || mid >= end) {
        errors.push_back("BVH split produced an empty child range");
        return -1;
    }

    const int left_child = BuildSubtreeSerial(
        prims, begin, mid, depth + 1, max_leaf_triangles, chooser, bvh, errors);
    const int right_child = BuildSubtreeSerial(
        prims, mid, end, depth + 1, max_leaf_triangles, chooser, bvh, errors);
    if (left_child < 0 || right_child < 0) {
        return -1;
    }

    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds,
        left_child,
        right_child,
        0,
        0
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
    return node_index;
}

} // namespace

BvhBuildResult BuildBvh(const RenderSceneIR& scene, const BvhBuildOptions& options) {
    BvhBuildResult result;

    if (options.max_leaf_triangles < 1) {
        result.errors.push_back("BVH max_leaf_triangles must be at least 1");
        return result;
    }

    // Build flat triangle list from table geometry
    std::vector<BvhPrimRef> prims;
    int flat_tri = 0;
    for (int pi = 0; pi < static_cast<int>(scene.primitives.size()); ++pi) {
        const auto& prim = scene.primitives[pi];
        const int tri_count = static_cast<int>(prim.index_count / 3);
        for (int ti = 0; ti < tri_count; ++ti) {
            std::uint32_t i0 = scene.indices[prim.first_index + static_cast<std::uint32_t>(ti) * 3 + 0];
            std::uint32_t i1 = scene.indices[prim.first_index + static_cast<std::uint32_t>(ti) * 3 + 1];
            std::uint32_t i2 = scene.indices[prim.first_index + static_cast<std::uint32_t>(ti) * 3 + 2];
            Point3f p0 = scene.vertices[i0].position;
            Point3f p1 = scene.vertices[i1].position;
            Point3f p2 = scene.vertices[i2].position;

            Bounds3f tri_bounds = Union(Union(Bounds3f{p0, p0}, p1), p2);
            Point3f centroid{
                (p0.x + p1.x + p2.x) * (1.0f / 3.0f),
                (p0.y + p1.y + p2.y) * (1.0f / 3.0f),
                (p0.z + p1.z + p2.z) * (1.0f / 3.0f)
            };

            if (!IsFinite(tri_bounds) || !IsFinite(centroid)) {
                result.errors.push_back("BVH build encountered non-finite triangle data");
                return result;
            }

            BvhPrimRef ref;
            ref.flat_index = flat_tri;
            ref.primitive_index = pi;
            ref.local_triangle = ti;
            ref.bounds = tri_bounds;
            ref.centroid = centroid;
            prims.push_back(ref);
            ++flat_tri;
        }
    }

    if (prims.empty()) {
        return result;
    }

    // Build triangle_to_primitive lookup table
    result.bvh.triangle_to_primitive.resize(static_cast<std::size_t>(flat_tri));
    for (const auto& ref : prims) {
        result.bvh.triangle_to_primitive[static_cast<std::size_t>(ref.flat_index)] = {ref.primitive_index, ref.local_triangle};
    }
    result.bvh.total_triangles = flat_tri;

    int root = -1;
    switch (options.split_method) {
        case BvhSplitMethod::SahBucketBinning:
            root = BuildSubtreeSerial(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                ChooseSahSplit,
                result.bvh,
                result.errors
            );
            break;
        case BvhSplitMethod::LongestAxisMedian:
            root = BuildSubtreeSerial(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                ChooseMedianSplit,
                result.bvh,
                result.errors
            );
            break;
    }
    if (root != 0 || !result.errors.empty()) {
        result.bvh = RenderBvh{};
    }

    return result;
}

BvhHit IntersectBvh(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min,
    float t_max
) {
    BvhHit nearest;
    nearest.t = t_max;

    if (!bvh.nodes.empty()) {
        std::vector<int> stack;
        stack.push_back(0);
        while (!stack.empty()) {
            const int node_index = stack.back();
            stack.pop_back();
            if (node_index < 0 || static_cast<std::size_t>(node_index) >= bvh.nodes.size()) {
                continue;
            }

            const RenderBvhNode& node = bvh.nodes[static_cast<std::size_t>(node_index)];
            ++stats.node_tests;
            if (!node.bounds.Intersects(ray, t_min, nearest.t)) {
                continue;
            }

            if (node.triangle_count > 0) {
                for (int ti = node.first_triangle; ti < node.first_triangle + node.triangle_count; ++ti) {
                    if (ti < 0 || static_cast<std::size_t>(ti) >= bvh.triangle_indices.size()) {
                        continue;
                    }

                    ++stats.triangle_tests;
                    const int flat_idx = bvh.triangle_indices[static_cast<std::size_t>(ti)];
                    if (flat_idx < 0 || static_cast<std::size_t>(flat_idx) >= bvh.triangle_to_primitive.size()) {
                        continue;
                    }

                    auto [prim_idx, local_tri] = bvh.triangle_to_primitive[static_cast<std::size_t>(flat_idx)];
                    if (prim_idx < 0 || static_cast<std::size_t>(prim_idx) >= scene.primitives.size()) {
                        continue;
                    }

                    const auto& prim = scene.primitives[static_cast<std::size_t>(prim_idx)];
                    std::uint32_t base = prim.first_index + static_cast<std::uint32_t>(local_tri) * 3;
                    Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
                    Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
                    Point3f p2 = scene.vertices[scene.indices[base + 2]].position;

                    // Moller-Trumbore intersection
                    const Vec3f edge1 = p1 - p0;
                    const Vec3f edge2 = p2 - p0;
                    const Vec3f pvec = Cross(ray.direction, edge2);
                    const float det = Dot(edge1, pvec);
                    if (std::fabs(det) < ParallelEpsilon) {
                        continue;
                    }

                    const float inv_det = 1.0f / det;
                    const Vec3f tvec = ray.origin - p0;
                    const float u = Dot(tvec, pvec) * inv_det;
                    if (u < 0.0f || u > 1.0f) {
                        continue;
                    }

                    const Vec3f qvec = Cross(tvec, edge1);
                    const float v = Dot(ray.direction, qvec) * inv_det;
                    if (v < 0.0f || u + v > 1.0f) {
                        continue;
                    }

                    const float t = Dot(edge2, qvec) * inv_det;
                    if (t <= t_min || t >= nearest.t) {
                        continue;
                    }

                    nearest.hit = true;
                    nearest.t = t;
                    nearest.triangle_index = flat_idx;
                    nearest.primitive_index = prim_idx;
                    nearest.sphere_index = -1;          // defensive reset: triangle branch clears sphere slot
                    nearest.bary_u = u;
                    nearest.bary_v = v;
                }
            } else {
                if (node.right_child >= 0) {
                    stack.push_back(node.right_child);
                }
                if (node.left_child >= 0) {
                    stack.push_back(node.left_child);
                }
            }
        }
    } // end if (!bvh.nodes.empty())

    // Linear pass over analytic spheres. M1 keeps spheres out of the BVH because the
    // expected scene-level count is small (<= ~50); revisit if profiling demands it.
    for (std::size_t si = 0; si < scene.spheres.size(); ++si) {
        const RenderSphere& sphere = scene.spheres[si];
        const SphereHit s = IntersectSphere(sphere.center, sphere.radius, ray, t_min, nearest.t);
        if (!s.hit || s.t >= nearest.t) {
            continue;
        }
        const Point3f hit_point = ray.At(s.t);
        const Vec3f n = SphereNormal(sphere.center, sphere.radius, hit_point);
        const Vec2f uv = SphereUv(n);

        nearest.hit = true;
        nearest.t = s.t;
        nearest.triangle_index = -1;
        nearest.primitive_index = -1;
        nearest.sphere_index = static_cast<int>(si);
        nearest.bary_u = uv.x;
        nearest.bary_v = uv.y;
    }

    return nearest;
}

} // namespace yr
