#include "yr_test.hpp"

#include "bvh_test_scene.hpp"

#include <cstddef>

#include <yaoray/accel/bvh.hpp>

namespace {

void ExpectSameBvh(const yr::RenderBvh& actual, const yr::RenderBvh& expected) {
    YR_EXPECT_EQ(actual.nodes.size(), expected.nodes.size());
    YR_EXPECT_EQ(actual.primitive_indices.size(), expected.primitive_indices.size());
    YR_EXPECT_EQ(actual.max_depth, expected.max_depth);
    YR_EXPECT_EQ(actual.total_triangles, expected.total_triangles);
    for (std::size_t i = 0; i < actual.nodes.size(); ++i) {
        const yr::RenderBvhNode& a = actual.nodes[i];
        const yr::RenderBvhNode& e = expected.nodes[i];
        YR_EXPECT_EQ(a.bounds.min.x, e.bounds.min.x);
        YR_EXPECT_EQ(a.bounds.min.y, e.bounds.min.y);
        YR_EXPECT_EQ(a.bounds.min.z, e.bounds.min.z);
        YR_EXPECT_EQ(a.bounds.max.x, e.bounds.max.x);
        YR_EXPECT_EQ(a.bounds.max.y, e.bounds.max.y);
        YR_EXPECT_EQ(a.bounds.max.z, e.bounds.max.z);
        YR_EXPECT_EQ(a.payload_offset, e.payload_offset);
        YR_EXPECT_EQ(a.primitive_count, e.primitive_count);
    }
    for (std::size_t i = 0; i < actual.primitive_indices.size(); ++i) {
        YR_EXPECT_EQ(actual.primitive_indices[i], expected.primitive_indices[i]);
    }
}

} // namespace

YR_TEST(bvh_parallel_matches_serial_byte_for_byte) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions serial_options;
    serial_options.thread_count = 1;
    serial_options.parallel_min_subtree_size = 4;
    yr::BvhBuildOptions parallel_options = serial_options;
    parallel_options.thread_count = 4;

    const yr::BvhBuildResult serial = yr::BuildBvh(scene.Geometry(), serial_options);
    const yr::BvhBuildResult parallel = yr::BuildBvh(scene.Geometry(), parallel_options);

    YR_EXPECT_TRUE(serial.errors.empty());
    YR_EXPECT_TRUE(parallel.errors.empty());
    ExpectSameBvh(parallel.bvh, serial.bvh);
}

YR_TEST(bvh_parallel_falls_back_to_serial_below_threshold) {
    const yr::RenderSceneIR scene = yrtest::MakeClusteredBvhTestScene();
    yr::BvhBuildOptions options;
    options.thread_count = 8;
    options.parallel_min_subtree_size = 1024;

    const yr::BvhBuildResult result = yr::BuildBvh(scene.Geometry(), options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}
