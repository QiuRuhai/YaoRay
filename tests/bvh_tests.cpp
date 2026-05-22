#include "yr_test.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderTriangle MakeTriangle(float x_offset) {
    return yr::RenderTriangle{
        yr::Point3f{x_offset - 0.25f, -0.25f, 0.0f},
        yr::Point3f{x_offset + 0.25f, -0.25f, 0.0f},
        yr::Point3f{x_offset, 0.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    };
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

YR_TEST(bvh_builder_returns_empty_bvh_for_empty_triangle_list) {
    const std::vector<yr::RenderTriangle> triangles;

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.bvh.max_depth, 0);
}

YR_TEST(bvh_builder_builds_single_leaf_for_one_triangle) {
    const std::vector<yr::RenderTriangle> triangles{MakeTriangle(0.0f)};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

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

YR_TEST(bvh_builder_keeps_four_triangles_in_default_leaf) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{4});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 4);
}

YR_TEST(bvh_builder_splits_five_triangles) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f),
        MakeTriangle(4.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.size() >= std::size_t{3});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{5});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 0);
    YR_EXPECT_TRUE(result.bvh.nodes[0].left_child > 0);
    YR_EXPECT_TRUE(result.bvh.nodes[0].right_child > 0);
    YR_EXPECT_TRUE(result.bvh.max_depth >= 2);
}

YR_TEST(bvh_builder_honors_max_leaf_triangles_one) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f)
    };
    const yr::BvhBuildOptions options{yr::BvhSplitMethod::LongestAxisMedian, 1};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{3});
    for (const yr::RenderBvhNode& node : result.bvh.nodes) {
        if (node.triangle_count > 0) {
            YR_EXPECT_EQ(node.triangle_count, 1);
        }
    }
}

YR_TEST(bvh_builder_rejects_invalid_leaf_size) {
    const std::vector<yr::RenderTriangle> triangles{MakeTriangle(0.0f)};
    const yr::BvhBuildOptions options{yr::BvhSplitMethod::LongestAxisMedian, 0};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles, options);

    YR_EXPECT_TRUE(!result.errors.empty());
    YR_EXPECT_TRUE(HasErrorContaining(result, "max_leaf_triangles"));
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
}

YR_TEST(bvh_builder_leaf_indices_are_valid_triangle_indices) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f),
        MakeTriangle(4.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    for (const int triangle_index : result.bvh.triangle_indices) {
        YR_EXPECT_TRUE(triangle_index >= 0);
        YR_EXPECT_TRUE(static_cast<std::size_t>(triangle_index) < triangles.size());
    }
}

struct TestBvhScene {
    yr::RenderSceneIR scene;
    yr::RenderBvh bvh;
};

TestBvhScene MakeBvhScene(std::vector<yr::RenderTriangle> triangles) {
    TestBvhScene test_scene;
    test_scene.scene.triangles = std::move(triangles);
    const yr::BvhBuildResult build = yr::BuildBvh(test_scene.scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    test_scene.bvh = build.bvh;
    return test_scene;
}

YR_TEST(bvh_traversal_returns_miss_for_empty_bvh) {
    const TestBvhScene scene = MakeBvhScene({});
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene.scene, scene.bvh, ray, stats);

    YR_EXPECT_TRUE(!hit.hit);
    YR_EXPECT_TRUE(hit.triangle == nullptr);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_EQ(stats.node_tests, std::uint64_t{0});
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{0});
}

YR_TEST(bvh_traversal_hits_single_triangle) {
    const TestBvhScene scene = MakeBvhScene({MakeTriangle(0.0f)});
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene.scene, scene.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_TRUE(hit.triangle != nullptr);
    YR_EXPECT_EQ(hit.triangle_index, 0);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
    YR_EXPECT_TRUE(stats.node_tests > 0);
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{1});
}

YR_TEST(bvh_traversal_returns_nearest_hit) {
    std::vector<yr::RenderTriangle> triangles;
    triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.25f, -0.25f, -2.0f},
        yr::Point3f{0.25f, -0.25f, -2.0f},
        yr::Point3f{0.0f, 0.25f, -2.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    triangles.push_back(MakeTriangle(0.0f));
    const TestBvhScene scene = MakeBvhScene(std::move(triangles));
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene.scene, scene.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, 1);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
}

YR_TEST(bvh_traversal_culls_sparse_triangles) {
    const TestBvhScene scene = MakeBvhScene({
        MakeTriangle(0.0f),
        MakeTriangle(10.0f),
        MakeTriangle(20.0f),
        MakeTriangle(30.0f),
        MakeTriangle(40.0f)
    });
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene.scene, scene.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_TRUE(stats.triangle_tests < std::uint64_t{5});
}
