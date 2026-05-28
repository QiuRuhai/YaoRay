#include "yr_test.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Rewrite BVH tests for table-geometry API (BuildBvh takes RenderSceneIR).

namespace {

yr::RenderSceneIR MakeSingleTriangleScene(float x_offset = 0.0f) {
    yr::RenderSceneIR scene;
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{x_offset - 0.25f, -0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{x_offset + 0.25f, -0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{x_offset,          0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    scene.materials.push_back(yr::RenderMaterial{});
    return scene;
}

void AddTriangle(yr::RenderSceneIR& scene, float x_offset) {
    const auto base = static_cast<std::uint32_t>(scene.vertices.size());
    scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x_offset - 0.25f, -0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f});
    scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x_offset + 0.25f, -0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f});
    scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x_offset,          0.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f});
    scene.indices.push_back(base);
    scene.indices.push_back(base + 1);
    scene.indices.push_back(base + 2);
    scene.primitives.push_back(yr::RenderPrimitive{base, 3, 0, true, false, false});
}

yr::RenderSceneIR MakeClusteredTriangleScene() {
    // Three clusters of 4 triangles each, along the X axis at x = 0, 5, 10.
    // SAH should isolate one cluster on the first split (4 vs 8 partition),
    // producing a smaller tree than the median-split builder's balanced
    // 6-vs-6 partition.
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 0.05f);
    AddTriangle(scene, 0.10f);
    AddTriangle(scene, 0.15f);
    AddTriangle(scene, 5.00f);
    AddTriangle(scene, 5.05f);
    AddTriangle(scene, 5.10f);
    AddTriangle(scene, 5.15f);
    AddTriangle(scene, 10.00f);
    AddTriangle(scene, 10.05f);
    AddTriangle(scene, 10.10f);
    AddTriangle(scene, 10.15f);
    return scene;  // 12 triangles total
}

bool HasErrorContaining(const yr::BvhBuildResult& result, const char* text) {
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(bvh_builder_returns_empty_bvh_for_empty_scene) {
    yr::RenderSceneIR scene;
    const yr::BvhBuildResult result = yr::BuildBvh(scene);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.bvh.max_depth, 0);
}

YR_TEST(bvh_builder_builds_single_leaf_for_one_triangle) {
    yr::RenderSceneIR scene = MakeSingleTriangleScene();
    const yr::BvhBuildResult result = yr::BuildBvh(scene);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices[0], 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].left_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].right_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].first_triangle, 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 1);
    YR_EXPECT_EQ(result.bvh.max_depth, 1);
}

YR_TEST(bvh_builder_splits_five_triangles) {
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 1.0f);
    AddTriangle(scene, 2.0f);
    AddTriangle(scene, 3.0f);
    AddTriangle(scene, 4.0f);

    const yr::BvhBuildResult result = yr::BuildBvh(scene);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.size() >= std::size_t{3});
    YR_EXPECT_EQ(result.bvh.total_triangles, 5);
    YR_EXPECT_TRUE(result.bvh.max_depth >= 2);
}

YR_TEST(bvh_traversal_hits_single_triangle) {
    yr::RenderSceneIR scene = MakeSingleTriangleScene();
    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    YR_EXPECT_TRUE(build.errors.empty());

    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, 0);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
    YR_EXPECT_TRUE(stats.node_tests > 0);
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{1});
}

YR_TEST(bvh_traversal_returns_miss_for_empty_bvh) {
    yr::RenderSceneIR scene;
    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(!hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_EQ(stats.node_tests, std::uint64_t{0});
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{0});
}

YR_TEST(bvh_intersect_finds_sphere_when_no_triangles) {
    yr::RenderSceneIR scene;
    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 1.0f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);
    scene.materials.push_back(yr::RenderMaterial{});

    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    YR_EXPECT_TRUE(build.errors.empty());

    yr::BvhTraceStats stats;
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere_index, 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.0f, 1.0e-5);
}

YR_TEST(bvh_intersect_picks_closer_of_sphere_and_triangle) {
    yr::RenderSceneIR scene;

    // Triangle at z = -1 (further from camera at z=3).
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.0f, -1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, -1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.0f,  1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});

    // Sphere at z = 0 (closer to camera).
    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);
    scene.materials.push_back(yr::RenderMaterial{});

    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    yr::BvhTraceStats stats;
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere_index, 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.5f, 1.0e-5);
}

YR_TEST(bvh_sah_builds_valid_bvh_for_five_triangles) {
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 1.0f);
    AddTriangle(scene, 2.0f);
    AddTriangle(scene, 3.0f);
    AddTriangle(scene, 4.0f);

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 5);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.max_depth >= 1);
}

YR_TEST(bvh_sah_chooses_better_split_than_median_for_clusters) {
    // Asymmetric input: SAH isolates a 4-triangle cluster on the first
    // split, producing fewer total nodes than median's balanced 6-vs-6
    // split. Expected: SAH = 5 nodes, median = 7 nodes.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult sah = yr::BuildBvh(scene, sah_options);

    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;
    const yr::BvhBuildResult median = yr::BuildBvh(scene, median_options);

    YR_EXPECT_TRUE(sah.errors.empty());
    YR_EXPECT_TRUE(median.errors.empty());
    YR_EXPECT_EQ(sah.bvh.total_triangles, 12);
    YR_EXPECT_EQ(median.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(sah.bvh.nodes.size() < median.bvh.nodes.size());
}

YR_TEST(bvh_sah_and_median_return_same_hit_for_simple_ray) {
    // Both split methods must produce a BVH that, traced with the same
    // ray, returns the same hit. Geometry organization may differ but
    // the per-pixel intersection result must not.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult sah = yr::BuildBvh(scene, sah_options);

    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;
    const yr::BvhBuildResult median = yr::BuildBvh(scene, median_options);

    // Ray straight down through the middle cluster at x = 5.
    const yr::Ray3f ray{yr::Point3f{5.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats sah_stats;
    yr::BvhTraceStats median_stats;
    const yr::BvhHit sah_hit = yr::IntersectBvh(scene, sah.bvh, ray, sah_stats);
    const yr::BvhHit median_hit = yr::IntersectBvh(scene, median.bvh, ray, median_stats);

    YR_EXPECT_TRUE(sah_hit.hit);
    YR_EXPECT_TRUE(median_hit.hit);
    YR_EXPECT_NEAR(sah_hit.t, median_hit.t, 1e-6f);
    // Both must select the same triangle (the closest-t resolution
    // should agree even though the BVH organization differs).
    YR_EXPECT_EQ(sah_hit.triangle_index, median_hit.triangle_index);
}

YR_TEST(bvh_sah_handles_degenerate_centroid_bounds) {
    // 8 triangles all at the same centroid -> centroid bounds is
    // degenerate on every axis -> SAH cannot find a valid split.
    // Builder must still produce a valid BVH via the median fallback.
    yr::RenderSceneIR scene;
    for (int i = 0; i < 8; ++i) {
        AddTriangle(scene, 0.0f);
    }

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 8);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}

YR_TEST(bvh_sah_respects_max_leaf_triangles) {
    // With max_leaf_triangles = 4 and 3 well-separated triangles, SAH
    // must produce a single leaf (count <= max_leaf_triangles triggers
    // the leaf criterion regardless of SAH cost).
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 1.0f);
    AddTriangle(scene, 2.0f);

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 3);
    YR_EXPECT_EQ(result.bvh.nodes[0].left_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].right_child, -1);
}

YR_TEST(bvh_parallel_matches_serial_byte_for_byte) {
    // Same scene, two builds: one forced serial (thread_count=1), one
    // forced parallel with a lowered subtree-threshold so the 12-triangle
    // cluster scene actually engages the parallel code path. The two
    // builds must produce bitwise-identical RenderBvh output.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions serial_opts;
    serial_opts.split_method = yr::BvhSplitMethod::SahBucketBinning;
    serial_opts.thread_count = 1;
    serial_opts.parallel_min_subtree_size = 4;
    const yr::BvhBuildResult serial = yr::BuildBvh(scene, serial_opts);

    yr::BvhBuildOptions parallel_opts;
    parallel_opts.split_method = yr::BvhSplitMethod::SahBucketBinning;
    parallel_opts.thread_count = 4;
    parallel_opts.parallel_min_subtree_size = 4;
    const yr::BvhBuildResult parallel = yr::BuildBvh(scene, parallel_opts);

    YR_EXPECT_TRUE(serial.errors.empty());
    YR_EXPECT_TRUE(parallel.errors.empty());
    YR_EXPECT_EQ(serial.bvh.nodes.size(), parallel.bvh.nodes.size());
    YR_EXPECT_EQ(serial.bvh.triangle_indices.size(), parallel.bvh.triangle_indices.size());
    YR_EXPECT_EQ(serial.bvh.max_depth, parallel.bvh.max_depth);
    YR_EXPECT_EQ(serial.bvh.total_triangles, parallel.bvh.total_triangles);

    for (std::size_t i = 0; i < serial.bvh.nodes.size(); ++i) {
        const yr::RenderBvhNode& s = serial.bvh.nodes[i];
        const yr::RenderBvhNode& p = parallel.bvh.nodes[i];
        YR_EXPECT_EQ(s.bounds.min.x, p.bounds.min.x);
        YR_EXPECT_EQ(s.bounds.min.y, p.bounds.min.y);
        YR_EXPECT_EQ(s.bounds.min.z, p.bounds.min.z);
        YR_EXPECT_EQ(s.bounds.max.x, p.bounds.max.x);
        YR_EXPECT_EQ(s.bounds.max.y, p.bounds.max.y);
        YR_EXPECT_EQ(s.bounds.max.z, p.bounds.max.z);
        YR_EXPECT_EQ(s.left_child, p.left_child);
        YR_EXPECT_EQ(s.right_child, p.right_child);
        YR_EXPECT_EQ(s.first_triangle, p.first_triangle);
        YR_EXPECT_EQ(s.triangle_count, p.triangle_count);
    }
    for (std::size_t i = 0; i < serial.bvh.triangle_indices.size(); ++i) {
        YR_EXPECT_EQ(serial.bvh.triangle_indices[i], parallel.bvh.triangle_indices[i]);
    }
}

YR_TEST(bvh_parallel_handles_below_threshold_serially) {
    // With parallel_min_subtree_size larger than the scene, the parallel
    // builder must fall through to the serial path on the first call and
    // still produce a valid BVH. This verifies the fast-path early-out
    // doesn't break small-scene behavior.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();  // 12 triangles

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    options.thread_count = 8;
    options.parallel_min_subtree_size = 1024;  // > 12, so no parallel
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}
