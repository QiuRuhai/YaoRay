#include "yr_test.hpp"

#include "bvh_test_scene.hpp"

#include <cstddef>
#include <string>

#include <yaoray/accel/bvh.hpp>

namespace {

bool HasErrorContaining(const yr::BvhBuildResult& result, const char* text) {
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) return true;
    }
    return false;
}

} // namespace

YR_TEST(bvh_builder_returns_empty_bvh_for_empty_scene) {
    const yr::RenderSceneIR scene;
    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.primitive_indices.empty());
    YR_EXPECT_EQ(result.bvh.max_depth, 0);
}

YR_TEST(bvh_builder_builds_single_leaf_for_one_triangle) {
    const yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.primitive_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.primitive_indices[0], 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].LeftChild(0), -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].RightChild(), -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].FirstPrimitive(), 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].primitive_count, 1);
    YR_EXPECT_EQ(result.bvh.max_depth, 1);
}

YR_TEST(bvh_builder_places_analytic_sphere_in_unified_primitive_leaf) {
    yr::RenderSceneIR scene;
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{}, 1.0f, 0, -1, false});

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.primitive_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.primitives.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.total_primitives, 1);
    YR_EXPECT_EQ(result.bvh.total_triangles, 0);
    YR_EXPECT_EQ(result.bvh.total_spheres, 1);
    YR_EXPECT_EQ(result.bvh.nodes[0].primitive_count, 1);
    YR_EXPECT_EQ(result.bvh.primitives[0].kind, yr::BvhPrimitiveKind::Sphere);
    YR_EXPECT_EQ(result.bvh.primitives[0].sphere.Value(), 0);
}

YR_TEST(bvh_builder_splits_five_triangles) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    for (int i = 1; i < 5; ++i) yrtest::AddBvhTestTriangle(scene, static_cast<float>(i));

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.size() >= std::size_t{3});
    YR_EXPECT_EQ(result.bvh.total_triangles, 5);
    YR_EXPECT_TRUE(result.bvh.max_depth >= 2);
}

YR_TEST(bvh_builder_can_derive_four_wide_soa_nodes) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions options;
    options.enable_bvh4 = true;

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry(), options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!result.bvh.wide_nodes.empty());
    YR_EXPECT_TRUE(result.bvh.wide_nodes.size() <= result.bvh.nodes.size());
}

YR_TEST(bvh_sah_chooses_fewer_nodes_than_median_for_clusters) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;

    const yr::BvhBuildResult sah = yr::BuildBvh(scene.Geometry(), sah_options);
    const yr::BvhBuildResult median = yr::BuildBvh(scene.Geometry(), median_options);

    YR_EXPECT_TRUE(sah.errors.empty());
    YR_EXPECT_TRUE(median.errors.empty());
    YR_EXPECT_EQ(sah.bvh.total_triangles, 12);
    YR_EXPECT_EQ(median.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(sah.bvh.nodes.size() < median.bvh.nodes.size());
}

YR_TEST(bvh_sah_handles_degenerate_centroid_bounds) {
    yr::RenderSceneIR scene;
    for (int i = 0; i < 8; ++i) yrtest::AddBvhTestTriangle(scene, 0.0f);

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 8);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}

YR_TEST(bvh_sah_respects_max_leaf_triangles) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    yrtest::AddBvhTestTriangle(scene, 1.0f);
    yrtest::AddBvhTestTriangle(scene, 2.0f);

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.nodes[0].primitive_count, 3);
}

YR_TEST(bvh_builder_rejects_invalid_primitive_index_range) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.primitives[0].first_index = 1;

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(HasErrorContaining(result, "invalid index range"));
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
}

YR_TEST(bvh_builder_rejects_non_triangular_index_count) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.primitives[0].index_count = 2;

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(HasErrorContaining(result, "non-triangular index count"));
}

YR_TEST(bvh_builder_rejects_invalid_vertex_index) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.indices[2] = 99;

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(HasErrorContaining(result, "invalid vertex index"));
}

YR_TEST(bvh_builder_rejects_nonpositive_sphere_radius) {
    yr::RenderSceneIR scene;
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{}, 0.0f, 0, -1, false});

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry());

    YR_EXPECT_TRUE(HasErrorContaining(result, "invalid sphere data"));
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
}
