#include <yaoray/accel/bvh.hpp>

#include "bvh_builder_internal.hpp"

#include <yaoray/geometry/intersection.hpp>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

namespace yr {
namespace {

using namespace bvh_builder_detail;

bool ValidateBuildOptions(const BvhBuildOptions& options, std::vector<std::string>& errors) {
    if (options.max_leaf_triangles < 1) {
        errors.push_back("BVH max_leaf_triangles must be at least 1");
    }
    if (options.thread_count < 0) {
        errors.push_back("BVH thread_count must be >= 0");
    }
    if (options.parallel_min_subtree_size < 1) {
        errors.push_back("BVH parallel_min_subtree_size must be >= 1");
    }
    return errors.empty();
}

bool AppendTriangleRefs(
    GeometryView geometry,
    int primitive_index,
    std::vector<BvhPrimRef>& refs,
    std::vector<std::string>& errors
) {
    const RenderPrimitive& primitive = geometry.primitives[static_cast<std::size_t>(primitive_index)];
    const std::size_t first_index = primitive.first_index;
    const std::size_t index_count = primitive.index_count;
    if (first_index > geometry.indices.size() || index_count > geometry.indices.size() - first_index) {
        errors.push_back("BVH build encountered a primitive with an invalid index range");
        return false;
    }
    if (index_count % 3 != 0) {
        errors.push_back("BVH build encountered a primitive with a non-triangular index count");
        return false;
    }

    const int triangle_count = static_cast<int>(index_count / 3);
    for (int local_triangle = 0; local_triangle < triangle_count; ++local_triangle) {
        const std::size_t triangle_offset = first_index + static_cast<std::size_t>(local_triangle) * 3;
        const std::uint32_t i0 = geometry.indices[triangle_offset];
        const std::uint32_t i1 = geometry.indices[triangle_offset + 1];
        const std::uint32_t i2 = geometry.indices[triangle_offset + 2];
        if (i0 >= geometry.vertices.size() || i1 >= geometry.vertices.size() || i2 >= geometry.vertices.size()) {
            errors.push_back("BVH build encountered a triangle with an invalid vertex index");
            return false;
        }

        const Point3f p0 = geometry.vertices[i0].position;
        const Point3f p1 = geometry.vertices[i1].position;
        const Point3f p2 = geometry.vertices[i2].position;
        const Bounds3f bounds = Union(Union(Bounds3f{p0, p0}, p1), p2);
        const Point3f centroid = (p0 + p1 + p2) * (1.0f / 3.0f);
        if (!IsFinite(bounds) || !IsFinite(centroid)) {
            errors.push_back("BVH build encountered non-finite triangle data");
            return false;
        }

        refs.push_back(BvhPrimRef{
            static_cast<int>(refs.size()),
            RenderBvhPrimitive{
                BvhPrimitiveKind::Triangle,
                MeshPrimitiveHandle{primitive_index},
                SphereHandle{},
                local_triangle,
                -1
            },
            bounds,
            centroid
        });
    }
    return true;
}

std::vector<BvhPrimRef> BuildPrimitiveRefs(
    GeometryView geometry,
    std::vector<std::string>& errors
) {
    std::vector<BvhPrimRef> refs;
    refs.reserve(geometry.indices.size() / 3 + geometry.spheres.size());
    int flat_triangle_index = 0;
    for (int primitive_index = 0;
         primitive_index < static_cast<int>(geometry.primitives.size());
         ++primitive_index) {
        const std::size_t first_new_ref = refs.size();
        if (!AppendTriangleRefs(geometry, primitive_index, refs, errors)) {
            return {};
        }
        for (std::size_t i = first_new_ref; i < refs.size(); ++i) {
            refs[i].primitive.flat_triangle_index = flat_triangle_index++;
        }
    }
    for (int sphere_index = 0; sphere_index < static_cast<int>(geometry.spheres.size()); ++sphere_index) {
        const RenderSphere& sphere = geometry.spheres[static_cast<std::size_t>(sphere_index)];
        const Bounds3f bounds = SphereBounds(sphere.center, sphere.radius);
        if (!(sphere.radius > 0.0f) || !IsFinite(bounds) || !IsFinite(sphere.center)) {
            errors.push_back("BVH build encountered invalid sphere data");
            return {};
        }
        refs.push_back(BvhPrimRef{
            static_cast<int>(refs.size()),
            RenderBvhPrimitive{
                BvhPrimitiveKind::Sphere,
                MeshPrimitiveHandle{},
                SphereHandle{sphere_index},
                -1,
                -1
            },
            bounds,
            sphere.center
        });
    }
    return refs;
}

void InitializePrimitiveLookup(const std::vector<BvhPrimRef>& refs, RenderBvh& bvh) {
    bvh.primitives.resize(refs.size());
    for (const BvhPrimRef& ref : refs) {
        bvh.primitives[static_cast<std::size_t>(ref.flat_index)] = ref.primitive;
        if (ref.primitive.kind == BvhPrimitiveKind::Triangle) {
            ++bvh.total_triangles;
        } else {
            ++bvh.total_spheres;
        }
    }
    bvh.total_primitives = static_cast<int>(refs.size());
}

int ResolveThreadCount(int requested_threads) {
    if (requested_threads > 0) return requested_threads;
    const int detected = static_cast<int>(std::thread::hardware_concurrency());
    return detected > 0 ? detected : 1;
}

SplitChooser ResolveSplitChooser(BvhSplitMethod method) {
    switch (method) {
        case BvhSplitMethod::SahBucketBinning:
            return ChooseSahSplit;
        case BvhSplitMethod::LongestAxisMedian:
            return ChooseMedianSplit;
    }
    return ChooseSahSplit;
}

bool BuildWideNodes(RenderBvh& bvh) {
    bvh.wide_nodes.clear();
    if (bvh.nodes.empty()) return true;
    std::function<int(int)> build = [&](int binary_root) {
        const int wide_index = static_cast<int>(bvh.wide_nodes.size());
        bvh.wide_nodes.push_back(RenderBvh4Node{});
        std::vector<int> frontier{binary_root};
        while (frontier.size() < 4) {
            auto expandable = std::find_if(frontier.begin(), frontier.end(), [&](int index) {
                return !bvh.nodes[static_cast<std::size_t>(index)].IsLeaf();
            });
            if (expandable == frontier.end()) break;
            const int index = *expandable;
            const RenderBvhNode& node = bvh.nodes[static_cast<std::size_t>(index)];
            const std::size_t position = static_cast<std::size_t>(expandable - frontier.begin());
            frontier[position] = node.LeftChild(index);
            frontier.insert(frontier.begin() + static_cast<std::ptrdiff_t>(position + 1), node.RightChild());
        }

        struct LaneData {
            Bounds3f bounds;
            int payload = -1;
            std::uint16_t count = 0;
        };
        LaneData lanes[4];
        for (std::size_t lane = 0; lane < frontier.size(); ++lane) {
            const RenderBvhNode& binary = bvh.nodes[static_cast<std::size_t>(frontier[lane])];
            lanes[lane].bounds = binary.bounds;
            if (binary.IsLeaf()) {
                if (binary.primitive_count > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
                    return -1;
                }
                lanes[lane].payload = binary.FirstPrimitive();
                lanes[lane].count = static_cast<std::uint16_t>(binary.primitive_count);
            } else {
                lanes[lane].payload = build(frontier[lane]);
                if (lanes[lane].payload < 0) return -1;
            }
        }
        RenderBvh4Node& wide = bvh.wide_nodes[static_cast<std::size_t>(wide_index)];
        for (int lane = 0; lane < 4; ++lane) {
            wide.min_x[lane] = lanes[lane].bounds.min.x;
            wide.min_y[lane] = lanes[lane].bounds.min.y;
            wide.min_z[lane] = lanes[lane].bounds.min.z;
            wide.max_x[lane] = lanes[lane].bounds.max.x;
            wide.max_y[lane] = lanes[lane].bounds.max.y;
            wide.max_z[lane] = lanes[lane].bounds.max.z;
            wide.payload[lane] = lanes[lane].payload;
            wide.primitive_count[lane] = lanes[lane].count;
        }
        return wide_index;
    };
    if (build(0) != 0) {
        bvh.wide_nodes.clear();
        return false;
    }
    return true;
}

int RunBuilder(
    std::vector<BvhPrimRef>& refs,
    const BvhBuildOptions& options,
    RenderBvh& bvh,
    std::vector<std::string>& errors
) {
    const SplitChooser chooser = ResolveSplitChooser(options.split_method);
    const int thread_count = ResolveThreadCount(options.thread_count);
    if (thread_count <= 1) {
        return BuildSubtreeSerial(
            refs,
            0,
            static_cast<int>(refs.size()),
            1,
            options.max_leaf_triangles,
            chooser,
            bvh,
            errors
        );
    }
    return BuildSubtreeParallel(
        refs,
        0,
        static_cast<int>(refs.size()),
        1,
        options.max_leaf_triangles,
        options.parallel_min_subtree_size,
        thread_count,
        chooser,
        bvh,
        errors
    );
}

} // namespace

namespace bvh_builder_detail {

bool BuildHierarchyFromRefs(
    std::vector<BvhPrimRef>& refs,
    const BvhBuildOptions& options,
    RenderBvh& bvh,
    std::vector<std::string>& errors
) {
    if (!ValidateBuildOptions(options, errors)) return false;
    if (refs.empty()) return true;
    const int root = RunBuilder(refs, options, bvh, errors);
    return root == 0 && errors.empty();
}

} // namespace bvh_builder_detail

BvhBuildResult BuildBvh(GeometryView geometry, const BvhBuildOptions& options) {
    BvhBuildResult result;

    std::vector<BvhPrimRef> refs = BuildPrimitiveRefs(geometry, result.errors);
    if (!result.errors.empty()) return result;
    if (refs.empty()) {
        BuildHierarchyFromRefs(refs, options, result.bvh, result.errors);
        return result;
    }

    InitializePrimitiveLookup(refs, result.bvh);
    if (!BuildHierarchyFromRefs(refs, options, result.bvh, result.errors)) {
        result.bvh = RenderBvh{};
    } else if (options.enable_bvh4) {
        BuildWideNodes(result.bvh);
    }
    return result;
}

} // namespace yr
