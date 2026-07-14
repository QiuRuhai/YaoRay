#include <yaoray/accel/bvh.hpp>

#include <yaoray/geometry/intersection.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace yr {
namespace {

constexpr float ParallelEpsilon = 1.0e-8f;
constexpr std::size_t InlineTraversalDepth = 64;

class TraversalStack {
public:
    struct Entry {
        int node_index = -1;
        float entry_t = 0.0f;
    };

    [[nodiscard]] bool Empty() const {
        return inline_size_ == 0 && overflow_.empty();
    }

    void Push(int node_index, float entry_t) {
        const Entry entry{node_index, entry_t};
        if (inline_size_ < inline_nodes_.size() && overflow_.empty()) {
            inline_nodes_[inline_size_++] = entry;
            return;
        }
        overflow_.push_back(entry);
    }

    Entry Pop() {
        if (!overflow_.empty()) {
            const Entry entry = overflow_.back();
            overflow_.pop_back();
            return entry;
        }
        return inline_nodes_[--inline_size_];
    }

private:
    std::array<Entry, InlineTraversalDepth> inline_nodes_{};
    std::size_t inline_size_ = 0;
    std::vector<Entry> overflow_;
};

void IntersectTriangle(
    GeometryView geometry,
    const RenderBvhPrimitive& primitive_ref,
    const Ray3f& ray,
    float t_min,
    BvhHit& nearest
) {
    const RenderPrimitive* primitive = geometry.Find(primitive_ref.mesh_primitive);
    if (primitive == nullptr || primitive_ref.local_triangle < 0) {
        return;
    }

    const std::uint32_t base = primitive->first_index +
        static_cast<std::uint32_t>(primitive_ref.local_triangle) * 3;
    if (static_cast<std::size_t>(base) + 2 >= geometry.indices.size()) {
        return;
    }
    const std::uint32_t i0 = geometry.indices[base];
    const std::uint32_t i1 = geometry.indices[base + 1];
    const std::uint32_t i2 = geometry.indices[base + 2];
    if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() ||
        i2 >= geometry.vertices.size()) {
        return;
    }

    const Point3f p0 = geometry.vertices[i0].position;
    const Point3f p1 = geometry.vertices[i1].position;
    const Point3f p2 = geometry.vertices[i2].position;
    const Vec3f edge1 = p1 - p0;
    const Vec3f edge2 = p2 - p0;
    const Vec3f pvec = Cross(ray.direction, edge2);
    const float det = Dot(edge1, pvec);
    if (std::fabs(det) < ParallelEpsilon) {
        return;
    }

    const float inv_det = 1.0f / det;
    const Vec3f tvec = ray.origin - p0;
    const float u = Dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return;
    }

    const Vec3f qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return;
    }

    const float t = Dot(edge2, qvec) * inv_det;
    if (t <= t_min || t > nearest.t) {
        return;
    }
    // Coplanar primitives can produce the same hit distance while different
    // BVH layouts visit them in a different order. Keep the stable primitive
    // identity as the tie-breaker so builder and traversal choices do not
    // change the observable hit.
    if (t == nearest.t && nearest.hit && nearest.triangle_index >= 0 &&
        primitive_ref.flat_triangle_index >= nearest.triangle_index) {
        return;
    }

    nearest.hit = true;
    nearest.t = t;
    nearest.triangle_index = primitive_ref.flat_triangle_index;
    nearest.mesh_primitive = primitive_ref.mesh_primitive;
    nearest.sphere = SphereHandle{};
    nearest.bary_u = u;
    nearest.bary_v = v;
}

void IntersectAnalyticSphere(
    GeometryView geometry,
    const RenderBvhPrimitive& primitive_ref,
    const Ray3f& ray,
    float t_min,
    BvhHit& nearest
) {
    const RenderSphere* sphere = geometry.Find(primitive_ref.sphere);
    if (sphere == nullptr) {
        return;
    }
    const SphereHit hit = IntersectSphere(
        sphere->center, sphere->radius, ray, t_min, nearest.t);
    if (!hit.hit || hit.t >= nearest.t) {
        return;
    }

    const Point3f hit_point = ray.At(hit.t);
    const Vec3f normal = SphereNormal(sphere->center, sphere->radius, hit_point);
    const Vec2f uv = SphereUv(normal);
    nearest.hit = true;
    nearest.t = hit.t;
    nearest.triangle_index = -1;
    nearest.mesh_primitive = MeshPrimitiveHandle{};
    nearest.sphere = primitive_ref.sphere;
    nearest.bary_u = uv.x;
    nearest.bary_v = uv.y;
}

bool IntersectWideLane(const RenderBvh4Node& node, int lane,
    const RayBoundsPrecompute& ray, float t_min, float t_max, float& entry_t) {
    const float mins[3] = {node.min_x[lane], node.min_y[lane], node.min_z[lane]};
    const float maxs[3] = {node.max_x[lane], node.max_y[lane], node.max_z[lane]};
    for (int axis = 0; axis < 3; ++axis) {
        const float near_bound = ray.negative[axis] != 0 ? maxs[axis] : mins[axis];
        const float far_bound = ray.negative[axis] != 0 ? mins[axis] : maxs[axis];
        const float axis_near =
            (near_bound - ray.origin[axis]) * ray.inverse_direction[axis];
        const float axis_far =
            (far_bound - ray.origin[axis]) * ray.inverse_direction[axis];
        t_min = axis_near > t_min ? axis_near : t_min;
        t_max = axis_far < t_max ? axis_far : t_max;
    }
    entry_t = t_min;
    return t_max >= t_min;
}

BvhHit IntersectBvh4(GeometryView geometry, const RenderBvh& bvh,
    const Ray3f& ray, BvhTraceStats& stats, float t_min, float t_max) {
    BvhHit nearest;
    nearest.t = t_max;
    if (bvh.wide_nodes.empty()) return nearest;
    struct Work {
        int node_index = -1;
        int lane = -1;
        float entry_t = 0.0f;
    };
    std::array<Work, InlineTraversalDepth * 4> inline_stack{};
    std::vector<Work> overflow;
    std::size_t inline_size = 0;
    auto push = [&](Work work) {
        if (inline_size < inline_stack.size() && overflow.empty()) {
            inline_stack[inline_size++] = work;
        } else {
            overflow.push_back(work);
        }
    };
    auto pop = [&]() {
        if (!overflow.empty()) {
            const Work work = overflow.back();
            overflow.pop_back();
            return work;
        }
        return inline_stack[--inline_size];
    };
    const auto empty = [&] { return inline_size == 0 && overflow.empty(); };
    const RayBoundsPrecompute ray_bounds = PrecomputeRayBounds(ray);

    auto expand_node = [&](int node_index) {
        const RenderBvh4Node& node = bvh.wide_nodes[static_cast<std::size_t>(node_index)];
        Work hits[4];
        int hit_count = 0;
        for (int lane = 0; lane < 4; ++lane) {
            if (node.payload[lane] < 0) continue;
            float entry = t_min;
            ++stats.node_tests;
            if (IntersectWideLane(node, lane, ray_bounds, t_min, nearest.t, entry)) {
                hits[hit_count++] = Work{node_index, lane, entry};
            }
        }
        std::sort(hits, hits + hit_count,
            [](const Work& a, const Work& b) { return a.entry_t > b.entry_t; });
        for (int i = 0; i < hit_count; ++i) push(hits[i]);
    };

    expand_node(0);
    while (!empty()) {
        const Work work = pop();
        if (work.entry_t > nearest.t) continue;
        const RenderBvh4Node& node = bvh.wide_nodes[static_cast<std::size_t>(work.node_index)];
        const std::uint16_t primitive_count = node.primitive_count[work.lane];
        if (primitive_count == 0) {
            const int child = node.payload[work.lane];
            if (child >= 0 && static_cast<std::size_t>(child) < bvh.wide_nodes.size()) {
                expand_node(child);
            }
            continue;
        }
        const int first = node.payload[work.lane];
        for (int i = first; i < first + static_cast<int>(primitive_count); ++i) {
            if (i < 0 || static_cast<std::size_t>(i) >= bvh.primitive_indices.size()) continue;
            const int primitive_index = bvh.primitive_indices[static_cast<std::size_t>(i)];
            if (primitive_index < 0 ||
                static_cast<std::size_t>(primitive_index) >= bvh.primitives.size()) continue;
            const RenderBvhPrimitive& primitive =
                bvh.primitives[static_cast<std::size_t>(primitive_index)];
            if (primitive.kind == BvhPrimitiveKind::Triangle) {
                ++stats.triangle_tests;
                IntersectTriangle(geometry, primitive, ray, t_min, nearest);
            } else {
                ++stats.sphere_tests;
                IntersectAnalyticSphere(geometry, primitive, ray, t_min, nearest);
            }
        }
    }
    return nearest;
}

} // namespace

BvhHit IntersectBvh(
    GeometryView geometry,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min,
    float t_max
) {
    if (!bvh.wide_nodes.empty()) {
        return IntersectBvh4(geometry, bvh, ray, stats, t_min, t_max);
    }
    BvhHit nearest;
    nearest.t = t_max;
    if (bvh.nodes.empty()) {
        return nearest;
    }

    const RayBoundsPrecompute ray_bounds = PrecomputeRayBounds(ray);
    float root_entry = t_min;
    ++stats.node_tests;
    if (!IntersectBounds(bvh.nodes.front().bounds, ray_bounds, t_min, nearest.t, root_entry)) {
        return nearest;
    }
    TraversalStack stack;
    stack.Push(0, root_entry);
    while (!stack.Empty()) {
        const TraversalStack::Entry stack_entry = stack.Pop();
        const int node_index = stack_entry.node_index;
        if (stack_entry.entry_t > nearest.t) continue;
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= bvh.nodes.size()) {
            continue;
        }

        const RenderBvhNode& node = bvh.nodes[static_cast<std::size_t>(node_index)];
        if (node.primitive_count > 0) {
            for (int i = node.FirstPrimitive();
                 i < node.FirstPrimitive() + node.primitive_count; ++i) {
                if (i < 0 || static_cast<std::size_t>(i) >= bvh.primitive_indices.size()) {
                    continue;
                }
                const int primitive_index = bvh.primitive_indices[static_cast<std::size_t>(i)];
                if (primitive_index < 0 ||
                    static_cast<std::size_t>(primitive_index) >= bvh.primitives.size()) {
                    continue;
                }

                const RenderBvhPrimitive& primitive =
                    bvh.primitives[static_cast<std::size_t>(primitive_index)];
                if (primitive.kind == BvhPrimitiveKind::Triangle) {
                    ++stats.triangle_tests;
                    IntersectTriangle(geometry, primitive, ray, t_min, nearest);
                } else {
                    ++stats.sphere_tests;
                    IntersectAnalyticSphere(geometry, primitive, ray, t_min, nearest);
                }
            }
        } else {
            const int left_index = node.LeftChild(node_index);
            const int right_index = node.RightChild();
            float left_entry = t_min;
            float right_entry = t_min;
            bool hit_left = false;
            bool hit_right = false;
            if (left_index >= 0 && static_cast<std::size_t>(left_index) < bvh.nodes.size()) {
                ++stats.node_tests;
                hit_left = IntersectBounds(
                    bvh.nodes[static_cast<std::size_t>(left_index)].bounds,
                    ray_bounds, t_min, nearest.t, left_entry);
            }
            if (right_index >= 0 && static_cast<std::size_t>(right_index) < bvh.nodes.size()) {
                ++stats.node_tests;
                hit_right = IntersectBounds(
                    bvh.nodes[static_cast<std::size_t>(right_index)].bounds,
                    ray_bounds, t_min, nearest.t, right_entry);
            }
            if (hit_left && hit_right) {
                if (left_entry <= right_entry) {
                    stack.Push(right_index, right_entry);
                    stack.Push(left_index, left_entry);
                } else {
                    stack.Push(left_index, left_entry);
                    stack.Push(right_index, right_entry);
                }
            } else if (hit_left) {
                stack.Push(left_index, left_entry);
            } else if (hit_right) {
                stack.Push(right_index, right_entry);
            }
        }
    }

    return nearest;
}

BvhProbeHits IntersectBvhProbe(
    GeometryView geometry,
    const RenderBvh& bvh,
    const Ray3f& ray,
    MeshPrimitiveHandle target_primitive,
    SphereHandle target_sphere,
    float t_min,
    float t_max
) {
    BvhProbeHits result;
    BvhTraceStats stats;

    constexpr int MaxIterations = 4096;
    float cursor = t_min;
    for (int iter = 0; iter < MaxIterations; ++iter) {
        BvhHit hit = IntersectBvh(geometry, bvh, ray, stats, cursor, t_max);
        if (!hit.hit) {
            break;
        }

        const bool is_target =
            (target_primitive.IsValid() && hit.triangle_index >= 0 &&
             hit.mesh_primitive == target_primitive) ||
            (target_sphere.IsValid() && hit.sphere == target_sphere);

        if (is_target) {
            if (result.count < BvhProbeHits::MaxHits) {
                result.hits[result.count++] = hit;
            } else {
                result.exhausted = true;
                break;
            }
        }

        float next = hit.t + 1.0e-4f * (1.0f + std::fabs(hit.t));
        if (!(next > cursor)) {
            next = cursor + 1.0e-4f;
        }
        cursor = next;
        if (cursor >= t_max) {
            break;
        }
    }
    return result;
}

} // namespace yr
