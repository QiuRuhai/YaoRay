#include <yaoray/accel/two_level_bvh.hpp>

#include "bvh_builder_internal.hpp"

#include <yaoray/geometry/intersection.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace yr {
namespace {

using bvh_builder_detail::BvhPrimRef;

constexpr std::size_t InlineTraversalDepth = 64;

Bounds3f UnionBounds(const Bounds3f& a, const Bounds3f& b) {
    return bvh_builder_detail::UnionBounds(a, b);
}

Point3f BoundsCentroid(const Bounds3f& bounds) {
    return (bounds.min + bounds.max) * 0.5f;
}

bool IsInvertibleAffine(const Mat4f& transform) {
    for (float value : transform.m) {
        if (!std::isfinite(value)) return false;
    }
    const float determinant =
        transform.m[0] * (transform.m[5] * transform.m[10] - transform.m[9] * transform.m[6]) -
        transform.m[4] * (transform.m[1] * transform.m[10] - transform.m[9] * transform.m[2]) +
        transform.m[8] * (transform.m[1] * transform.m[6] - transform.m[5] * transform.m[2]);
    return std::isfinite(determinant) && std::fabs(determinant) > 1.0e-12f;
}

Bounds3f TransformBounds(const Mat4f& transform, const Bounds3f& bounds) {
    Bounds3f result;
    for (int corner = 0; corner < 8; ++corner) {
        const Point3f point{
            (corner & 1) != 0 ? bounds.max.x : bounds.min.x,
            (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
            (corner & 4) != 0 ? bounds.max.z : bounds.min.z
        };
        result = Union(result, TransformPoint(transform, point));
    }
    return result;
}

bool BuildTlas(
    GeometryView geometry,
    const BvhBuildOptions& options,
    TwoLevelBvh& acceleration,
    std::vector<std::string>& errors
) {
    RenderTlas tlas;
    std::vector<BvhPrimRef> refs;
    refs.reserve(acceleration.instances.size() + geometry.spheres.size());

    for (int i = 0; i < static_cast<int>(acceleration.instances.size()); ++i) {
        const PreparedMeshInstance& instance = acceleration.instances[static_cast<std::size_t>(i)];
        const int flat_index = static_cast<int>(tlas.primitives.size());
        tlas.primitives.push_back(RenderTlasPrimitive{
            TlasPrimitiveKind::Instance,
            InstanceHandle{i},
            SphereHandle{},
            instance.world_bounds
        });
        refs.push_back(BvhPrimRef{
            flat_index,
            RenderBvhPrimitive{},
            instance.world_bounds,
            BoundsCentroid(instance.world_bounds)
        });
    }

    for (int i = 0; i < static_cast<int>(geometry.spheres.size()); ++i) {
        const RenderSphere& sphere = geometry.spheres[static_cast<std::size_t>(i)];
        const Bounds3f bounds = SphereBounds(sphere.center, sphere.radius);
        if (!(sphere.radius > 0.0f) || !bvh_builder_detail::IsFinite(bounds)) {
            errors.push_back("TLAS build encountered invalid sphere data");
            return false;
        }
        const int flat_index = static_cast<int>(tlas.primitives.size());
        tlas.primitives.push_back(RenderTlasPrimitive{
            TlasPrimitiveKind::Sphere,
            InstanceHandle{},
            SphereHandle{i},
            bounds
        });
        refs.push_back(BvhPrimRef{
            flat_index,
            RenderBvhPrimitive{},
            bounds,
            sphere.center
        });
    }

    RenderBvh hierarchy;
    if (!bvh_builder_detail::BuildHierarchyFromRefs(refs, options, hierarchy, errors)) {
        return false;
    }
    tlas.nodes = std::move(hierarchy.nodes);
    tlas.primitive_indices = std::move(hierarchy.primitive_indices);
    tlas.max_depth = hierarchy.max_depth;
    acceleration.tlas = std::move(tlas);
    return true;
}

bool UpdateInstanceTransforms(
    std::span<const Mat4f> transforms,
    TwoLevelBvh& acceleration,
    std::string& error
) {
    if (transforms.size() != acceleration.instances.size()) {
        error = "instance transform count does not match prepared TLAS instance count";
        return false;
    }
    std::vector<Mat4f> inverses(transforms.size());
    std::vector<Bounds3f> world_bounds(transforms.size());
    for (std::size_t i = 0; i < transforms.size(); ++i) {
        if (!IsInvertibleAffine(transforms[i])) {
            error = "instance transform is singular or non-finite";
            return false;
        }
        const PreparedMeshInstance& instance = acceleration.instances[i];
        if (instance.blas_index < 0 ||
            static_cast<std::size_t>(instance.blas_index) >= acceleration.blases.size()) {
            error = "instance references an invalid BLAS";
            return false;
        }
        inverses[i] = Inverse(transforms[i]);
        world_bounds[i] = TransformBounds(
            transforms[i], acceleration.blases[static_cast<std::size_t>(instance.blas_index)].object_bounds);
        if (!bvh_builder_detail::IsFinite(world_bounds[i])) {
            error = "instance transform produced non-finite world bounds";
            return false;
        }
    }
    for (std::size_t i = 0; i < transforms.size(); ++i) {
        PreparedMeshInstance& instance = acceleration.instances[i];
        instance.object_to_world = transforms[i];
        instance.world_to_object = inverses[i];
        instance.world_bounds = world_bounds[i];
    }
    return true;
}

Bounds3f TlasPrimitiveBounds(const TwoLevelBvh& acceleration, int primitive_index) {
    if (primitive_index < 0 ||
        static_cast<std::size_t>(primitive_index) >= acceleration.tlas.primitives.size()) {
        return Bounds3f{};
    }
    return acceleration.tlas.primitives[static_cast<std::size_t>(primitive_index)].world_bounds;
}

void RefitTlasNodes(TwoLevelBvh& acceleration) {
    for (std::size_t i = 0; i < acceleration.instances.size(); ++i) {
        acceleration.tlas.primitives[i].world_bounds = acceleration.instances[i].world_bounds;
    }

    for (std::size_t reverse = acceleration.tlas.nodes.size(); reverse > 0; --reverse) {
        RenderBvhNode& node = acceleration.tlas.nodes[reverse - 1];
        Bounds3f bounds;
        if (node.primitive_count > 0) {
            for (int i = node.FirstPrimitive();
                 i < node.FirstPrimitive() + node.primitive_count;
                 ++i) {
                if (i < 0 || static_cast<std::size_t>(i) >= acceleration.tlas.primitive_indices.size()) {
                    continue;
                }
                bounds = UnionBounds(
                    bounds,
                    TlasPrimitiveBounds(
                        acceleration,
                        acceleration.tlas.primitive_indices[static_cast<std::size_t>(i)]));
            }
        } else {
            const int node_index = static_cast<int>(reverse - 1);
            const int left_child = node.LeftChild(node_index);
            const int right_child = node.RightChild();
            if (left_child >= 0) {
                bounds = UnionBounds(
                    bounds,
                    acceleration.tlas.nodes[static_cast<std::size_t>(left_child)].bounds);
            }
            if (right_child >= 0) {
                bounds = UnionBounds(
                    bounds,
                    acceleration.tlas.nodes[static_cast<std::size_t>(right_child)].bounds);
            }
        }
        node.bounds = bounds;
    }
}

class TraversalStack {
public:
    struct Entry {
        int node_index = -1;
        float entry_t = 0.0f;
    };

    bool Empty() const { return inline_size_ == 0 && overflow_.empty(); }

    void Push(int value, float entry_t) {
        const Entry entry{value, entry_t};
        if (inline_size_ < inline_values_.size() && overflow_.empty()) {
            inline_values_[inline_size_++] = entry;
        } else {
            overflow_.push_back(entry);
        }
    }

    Entry Pop() {
        if (!overflow_.empty()) {
            const Entry value = overflow_.back();
            overflow_.pop_back();
            return value;
        }
        return inline_values_[--inline_size_];
    }

private:
    std::array<Entry, InlineTraversalDepth> inline_values_{};
    std::size_t inline_size_ = 0;
    std::vector<Entry> overflow_;
};

void IntersectTlasSphere(
    GeometryView geometry,
    SphereHandle handle,
    const Ray3f& ray,
    float t_min,
    BvhHit& nearest
) {
    const RenderSphere* sphere = geometry.Find(handle);
    if (sphere == nullptr) return;
    const SphereHit hit = IntersectSphere(sphere->center, sphere->radius, ray, t_min, nearest.t);
    if (!hit.hit || hit.t >= nearest.t) return;

    const Vec3f normal = SphereNormal(sphere->center, sphere->radius, ray.At(hit.t));
    const Vec2f uv = SphereUv(normal);
    nearest.hit = true;
    nearest.t = hit.t;
    nearest.triangle_index = -1;
    nearest.mesh_primitive = MeshPrimitiveHandle{};
    nearest.instance = InstanceHandle{};
    nearest.sphere = handle;
    nearest.bary_u = uv.x;
    nearest.bary_v = uv.y;
}

} // namespace

TwoLevelBvhBuildResult BuildTwoLevelBvh(
    GeometryView geometry,
    const BvhBuildOptions& options
) {
    TwoLevelBvhBuildResult result;
    result.acceleration.blases.reserve(geometry.primitives.size());

    int flat_triangle_offset = 0;
    for (int i = 0; i < static_cast<int>(geometry.primitives.size()); ++i) {
        const RenderPrimitive& primitive = geometry.primitives[static_cast<std::size_t>(i)];
        GeometryView primitive_geometry{
            geometry.vertices,
            geometry.indices,
            std::span<const RenderPrimitive>{&primitive, 1},
            {},
            {}
        };
        BvhBuildResult build = BuildBvh(primitive_geometry, options);
        if (!build.errors.empty()) {
            result.errors.insert(result.errors.end(), build.errors.begin(), build.errors.end());
            return result;
        }
        if (build.bvh.nodes.empty()) {
            result.errors.push_back("BLAS build encountered an empty mesh primitive");
            return result;
        }
        for (RenderBvhPrimitive& triangle : build.bvh.primitives) {
            triangle.mesh_primitive = MeshPrimitiveHandle{i};
            triangle.flat_triangle_index += flat_triangle_offset;
        }
        result.acceleration.blases.push_back(RenderBlas{
            MeshPrimitiveHandle{i},
            build.bvh.nodes.front().bounds,
            std::move(build.bvh)
        });
        flat_triangle_offset += static_cast<int>(primitive.index_count / 3);
    }

    if (geometry.instances.empty()) {
        result.acceleration.instances.reserve(geometry.primitives.size());
        for (int i = 0; i < static_cast<int>(geometry.primitives.size()); ++i) {
            result.acceleration.instances.push_back(PreparedMeshInstance{
                MeshPrimitiveHandle{i}, i, Mat4f{}, Mat4f{},
                result.acceleration.blases[static_cast<std::size_t>(i)].object_bounds
            });
        }
    } else {
        result.acceleration.instances.reserve(geometry.instances.size());
        for (const RenderInstance& source : geometry.instances) {
            const int primitive_index = source.primitive.Value();
            if (primitive_index < 0 ||
                static_cast<std::size_t>(primitive_index) >= result.acceleration.blases.size()) {
                result.errors.push_back("instance references an invalid mesh primitive");
                return result;
            }
            if (!IsInvertibleAffine(source.object_to_world)) {
                result.errors.push_back("instance transform is singular or non-finite");
                return result;
            }
            result.acceleration.instances.push_back(PreparedMeshInstance{
                source.primitive,
                primitive_index,
                source.object_to_world,
                Inverse(source.object_to_world),
                TransformBounds(
                    source.object_to_world,
                    result.acceleration.blases[static_cast<std::size_t>(primitive_index)].object_bounds)
            });
        }
    }

    BuildTlas(geometry, options, result.acceleration, result.errors);
    if (!result.errors.empty()) result.acceleration = TwoLevelBvh{};
    return result;
}

bool RefitTwoLevelBvh(
    GeometryView geometry,
    std::span<const Mat4f> object_to_world,
    TwoLevelBvh& acceleration,
    std::string& error
) {
    (void)geometry;
    error.clear();
    if (!UpdateInstanceTransforms(object_to_world, acceleration, error)) return false;
    RefitTlasNodes(acceleration);
    return true;
}

bool RebuildTwoLevelTlas(
    GeometryView geometry,
    std::span<const Mat4f> object_to_world,
    const BvhBuildOptions& options,
    TwoLevelBvh& acceleration,
    std::string& error
) {
    error.clear();
    const std::vector<PreparedMeshInstance> original_instances = acceleration.instances;
    if (!UpdateInstanceTransforms(object_to_world, acceleration, error)) return false;
    std::vector<std::string> errors;
    if (!BuildTlas(geometry, options, acceleration, errors)) {
        acceleration.instances = original_instances;
        error = errors.empty() ? "TLAS rebuild failed" : errors.front();
        return false;
    }
    return true;
}

BvhHit IntersectTwoLevelBvh(
    GeometryView geometry,
    const TwoLevelBvh& acceleration,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min,
    float t_max
) {
    BvhHit nearest;
    nearest.t = t_max;
    if (acceleration.tlas.nodes.empty()) return nearest;

    const RayBoundsPrecompute ray_bounds = PrecomputeRayBounds(ray);
    float root_entry = t_min;
    ++stats.node_tests;
    if (!IntersectBounds(
            acceleration.tlas.nodes.front().bounds, ray_bounds,
            t_min, nearest.t, root_entry)) return nearest;
    TraversalStack stack;
    stack.Push(0, root_entry);
    while (!stack.Empty()) {
        const TraversalStack::Entry stack_entry = stack.Pop();
        const int node_index = stack_entry.node_index;
        if (stack_entry.entry_t > nearest.t) continue;
        if (node_index < 0 ||
            static_cast<std::size_t>(node_index) >= acceleration.tlas.nodes.size()) {
            continue;
        }
        const RenderBvhNode& node = acceleration.tlas.nodes[static_cast<std::size_t>(node_index)];
        if (node.primitive_count == 0) {
            const int left_index = node.LeftChild(node_index);
            const int right_index = node.RightChild();
            float left_entry = t_min;
            float right_entry = t_min;
            bool hit_left = false;
            bool hit_right = false;
            if (left_index >= 0 &&
                static_cast<std::size_t>(left_index) < acceleration.tlas.nodes.size()) {
                ++stats.node_tests;
                hit_left = IntersectBounds(
                    acceleration.tlas.nodes[static_cast<std::size_t>(left_index)].bounds,
                    ray_bounds, t_min, nearest.t, left_entry);
            }
            if (right_index >= 0 &&
                static_cast<std::size_t>(right_index) < acceleration.tlas.nodes.size()) {
                ++stats.node_tests;
                hit_right = IntersectBounds(
                    acceleration.tlas.nodes[static_cast<std::size_t>(right_index)].bounds,
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
            continue;
        }

        for (int i = node.FirstPrimitive();
             i < node.FirstPrimitive() + node.primitive_count;
             ++i) {
            if (i < 0 || static_cast<std::size_t>(i) >= acceleration.tlas.primitive_indices.size()) {
                continue;
            }
            const int primitive_index = acceleration.tlas.primitive_indices[static_cast<std::size_t>(i)];
            if (primitive_index < 0 ||
                static_cast<std::size_t>(primitive_index) >= acceleration.tlas.primitives.size()) {
                continue;
            }
            const RenderTlasPrimitive& primitive =
                acceleration.tlas.primitives[static_cast<std::size_t>(primitive_index)];
            if (primitive.kind == TlasPrimitiveKind::Sphere) {
                ++stats.sphere_tests;
                IntersectTlasSphere(geometry, primitive.sphere, ray, t_min, nearest);
                continue;
            }

            const int instance_index = primitive.instance.Value();
            if (instance_index < 0 ||
                static_cast<std::size_t>(instance_index) >= acceleration.instances.size()) {
                continue;
            }
            const PreparedMeshInstance& instance =
                acceleration.instances[static_cast<std::size_t>(instance_index)];
            if (instance.blas_index < 0 ||
                static_cast<std::size_t>(instance.blas_index) >= acceleration.blases.size()) {
                continue;
            }
            const Ray3f object_ray{
                TransformPoint(instance.world_to_object, ray.origin),
                TransformVector(instance.world_to_object, ray.direction),
                ray.time
            };
            BvhHit hit = IntersectBvh(
                geometry,
                acceleration.blases[static_cast<std::size_t>(instance.blas_index)].bvh,
                object_ray,
                stats,
                t_min,
                nearest.t
            );
            if (hit.hit && hit.t < nearest.t) {
                hit.instance = primitive.instance;
                nearest = hit;
            }
        }
    }
    return nearest;
}

} // namespace yr
