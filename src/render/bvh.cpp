#include <yaoray/render/bvh.hpp>

#include <yaoray/render/render_scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

int BuildRecursive(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
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
    for (int i = begin; i < end; ++i) {
        node_bounds = UnionBounds(node_bounds, prims[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, prims[static_cast<std::size_t>(i)].centroid);
    }

    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return -1;
    }

    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) {
        const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
        for (int i = begin; i < end; ++i) {
            bvh.triangle_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
        }
        bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
            node_bounds,
            -1,
            -1,
            first_triangle,
            primitive_count
        };
        bvh.max_depth = std::max(bvh.max_depth, depth);
        return node_index;
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

    if (mid == begin || mid == end) {
        errors.push_back("BVH median split produced an empty child range");
        return -1;
    }

    const int left_child = BuildRecursive(prims, begin, mid, depth + 1, max_leaf_triangles, bvh, errors);
    const int right_child = BuildRecursive(prims, mid, end, depth + 1, max_leaf_triangles, bvh, errors);
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

    const int root = BuildRecursive(
        prims,
        0,
        static_cast<int>(prims.size()),
        1,
        options.max_leaf_triangles,
        result.bvh,
        result.errors
    );
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
    if (bvh.nodes.empty()) {
        return nearest;
    }

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

    return nearest;
}

} // namespace yr
