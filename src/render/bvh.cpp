#include <yaoray/render/bvh.hpp>

#include <yaoray/render/render_scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace yr {
namespace {

constexpr float MinHitT = 1.0e-5f;
constexpr float ParallelEpsilon = 1.0e-8f;

struct BvhPrimitive {
    int triangle_index = -1;
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

Bounds3f TriangleBounds(const RenderTriangle& triangle) {
    Bounds3f bounds;
    bounds = Union(bounds, triangle.p0);
    bounds = Union(bounds, triangle.p1);
    bounds = Union(bounds, triangle.p2);
    return bounds;
}

Point3f TriangleCentroid(const RenderTriangle& triangle) {
    return Point3f{
        (triangle.p0.x + triangle.p1.x + triangle.p2.x) / 3.0f,
        (triangle.p0.y + triangle.p1.y + triangle.p2.y) / 3.0f,
        (triangle.p0.z + triangle.p1.z + triangle.p2.z) / 3.0f
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
    std::vector<BvhPrimitive>& primitives,
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
        node_bounds = UnionBounds(node_bounds, primitives[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, primitives[static_cast<std::size_t>(i)].centroid);
    }

    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return -1;
    }

    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) {
        const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
        for (int i = begin; i < end; ++i) {
            bvh.triangle_indices.push_back(primitives[static_cast<std::size_t>(i)].triangle_index);
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
        primitives.begin() + begin,
        primitives.begin() + mid,
        primitives.begin() + end,
        [axis](const BvhPrimitive& a, const BvhPrimitive& b) {
            return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
        }
    );

    if (mid == begin || mid == end) {
        errors.push_back("BVH median split produced an empty child range");
        return -1;
    }

    const int left_child = BuildRecursive(primitives, begin, mid, depth + 1, max_leaf_triangles, bvh, errors);
    const int right_child = BuildRecursive(primitives, mid, end, depth + 1, max_leaf_triangles, bvh, errors);
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

bool IntersectTriangle(const Ray3f& ray, const RenderTriangle& triangle, float& t_out) {
    const Vec3f edge1 = triangle.p1 - triangle.p0;
    const Vec3f edge2 = triangle.p2 - triangle.p0;
    const Vec3f pvec = Cross(ray.direction, edge2);
    const float det = Dot(edge1, pvec);
    if (std::fabs(det) < ParallelEpsilon) {
        return false;
    }

    const float inv_det = 1.0f / det;
    const Vec3f tvec = ray.origin - triangle.p0;
    const float u = Dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3f qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float t = Dot(edge2, qvec) * inv_det;
    if (t <= MinHitT) {
        return false;
    }

    t_out = t;
    return true;
}

} // namespace

BvhBuildResult BuildBvh(const std::vector<RenderTriangle>& triangles, const BvhBuildOptions& options) {
    BvhBuildResult result;

    if (options.max_leaf_triangles < 1) {
        result.errors.push_back("BVH max_leaf_triangles must be at least 1");
        return result;
    }

    if (triangles.empty()) {
        return result;
    }

    std::vector<BvhPrimitive> primitives;
    primitives.reserve(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const RenderTriangle& triangle = triangles[i];
        const Bounds3f bounds = TriangleBounds(triangle);
        const Point3f centroid = TriangleCentroid(triangle);
        if (!IsFinite(bounds) || !IsFinite(centroid)) {
            result.errors.push_back("BVH build encountered non-finite triangle data");
            return result;
        }
        primitives.push_back(BvhPrimitive{static_cast<int>(i), bounds, centroid});
    }

    const int root = BuildRecursive(
        primitives,
        0,
        static_cast<int>(primitives.size()),
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

BvhHit IntersectBvh(const RenderScene& scene, const Ray3f& ray, BvhTraceStats& stats) {
    BvhHit nearest;
    if (scene.bvh.nodes.empty()) {
        return nearest;
    }

    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        const int node_index = stack.back();
        stack.pop_back();
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= scene.bvh.nodes.size()) {
            continue;
        }

        const RenderBvhNode& node = scene.bvh.nodes[static_cast<std::size_t>(node_index)];
        ++stats.node_tests;
        if (!node.bounds.Intersects(ray, MinHitT, nearest.t)) {
            continue;
        }

        if (node.triangle_count > 0) {
            for (int i = 0; i < node.triangle_count; ++i) {
                const int index_position = node.first_triangle + i;
                if (index_position < 0 ||
                    static_cast<std::size_t>(index_position) >= scene.bvh.triangle_indices.size()) {
                    continue;
                }

                const int triangle_index = scene.bvh.triangle_indices[static_cast<std::size_t>(index_position)];
                if (triangle_index < 0 ||
                    static_cast<std::size_t>(triangle_index) >= scene.triangles.size()) {
                    continue;
                }

                ++stats.triangle_tests;
                float t = 0.0f;
                const RenderTriangle& triangle = scene.triangles[static_cast<std::size_t>(triangle_index)];
                if (IntersectTriangle(ray, triangle, t) && t < nearest.t) {
                    nearest.hit = true;
                    nearest.t = t;
                    nearest.triangle = &triangle;
                    nearest.triangle_index = triangle_index;
                }
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
