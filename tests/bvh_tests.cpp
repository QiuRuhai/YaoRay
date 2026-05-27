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
