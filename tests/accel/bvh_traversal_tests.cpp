#include "yr_test.hpp"

#include "bvh_test_scene.hpp"

#include <cstdint>

#include <yaoray/accel/bvh.hpp>
#include <yaoray/core/ray.hpp>

YR_TEST(bvh_traversal_hits_single_triangle) {
    const yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    const yr::BvhBuildResult build = yr::BuildBvh(scene.Geometry());
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(
        scene.Geometry(),
        build.bvh,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, 0);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
    YR_EXPECT_TRUE(stats.node_tests > 0);
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{1});
}

YR_TEST(bvh_traversal_returns_miss_for_empty_bvh) {
    const yr::RenderSceneIR scene;
    const yr::BvhBuildResult build = yr::BuildBvh(scene.Geometry());
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(
        scene.Geometry(),
        build.bvh,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats
    );

    YR_EXPECT_TRUE(!hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_EQ(stats.node_tests, std::uint64_t{0});
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{0});
}

YR_TEST(bvh_traversal_finds_sphere_without_triangle_bvh) {
    yr::RenderSceneIR scene;
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{}, 1.0f, 0, -1, false});
    scene.materials.push_back(yr::RenderMaterial{});
    const yr::BvhBuildResult build = yr::BuildBvh(scene.Geometry());
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(
        scene.Geometry(),
        build.bvh,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere.Value(), 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.0f, 1.0e-5);
    YR_EXPECT_TRUE(!build.bvh.nodes.empty());
    YR_EXPECT_EQ(build.bvh.total_spheres, 1);
    YR_EXPECT_TRUE(stats.node_tests > 0);
    YR_EXPECT_EQ(stats.sphere_tests, std::uint64_t{1});
}

YR_TEST(bvh_traversal_picks_closer_sphere_over_triangle) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    for (yr::RenderVertex& vertex : scene.vertices) vertex.position.z = -1.0f;
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{}, 0.5f, 0, -1, false});
    const yr::BvhBuildResult build = yr::BuildBvh(scene.Geometry());
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(
        scene.Geometry(),
        build.bvh,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats
    );

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere.Value(), 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.5f, 1.0e-5);
}

YR_TEST(bvh_sah_and_median_return_same_hit) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;
    const yr::BvhBuildResult sah = yr::BuildBvh(scene.Geometry(), sah_options);
    const yr::BvhBuildResult median = yr::BuildBvh(scene.Geometry(), median_options);
    const yr::Ray3f ray{yr::Point3f{5.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats sah_stats;
    yr::BvhTraceStats median_stats;

    const yr::BvhHit sah_hit = yr::IntersectBvh(scene.Geometry(), sah.bvh, ray, sah_stats);
    const yr::BvhHit median_hit = yr::IntersectBvh(scene.Geometry(), median.bvh, ray, median_stats);

    YR_EXPECT_TRUE(sah_hit.hit);
    YR_EXPECT_TRUE(median_hit.hit);
    YR_EXPECT_NEAR(sah_hit.t, median_hit.t, 1e-6f);
    YR_EXPECT_EQ(sah_hit.triangle_index, median_hit.triangle_index);
}

YR_TEST(bvh4_and_binary_traversal_return_the_same_hit) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions options;
    options.enable_bvh4 = true;
    const yr::BvhBuildResult build = yr::BuildBvh(scene.Geometry(), options);
    yr::RenderBvh binary = build.bvh;
    binary.wide_nodes.clear();
    const yr::Ray3f rays[] = {
        {yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        {yr::Point3f{5.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        {yr::Point3f{50.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
    };

    for (const yr::Ray3f& ray : rays) {
        yr::BvhTraceStats wide_stats;
        yr::BvhTraceStats binary_stats;
        const yr::BvhHit wide_hit = yr::IntersectBvh(
            scene.Geometry(), build.bvh, ray, wide_stats);
        const yr::BvhHit binary_hit = yr::IntersectBvh(
            scene.Geometry(), binary, ray, binary_stats);
        YR_EXPECT_EQ(wide_hit.hit, binary_hit.hit);
        if (wide_hit.hit) {
            YR_EXPECT_NEAR(wide_hit.t, binary_hit.t, 1.0e-6f);
            YR_EXPECT_EQ(wide_hit.triangle_index, binary_hit.triangle_index);
        }
    }
}
