#include "yr_test.hpp"

#include "bvh_test_scene.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <yaoray/accel/two_level_bvh.hpp>
#include <yaoray/accel/acceleration.hpp>

namespace {

yr::BvhHit Trace(
    const yr::RenderSceneIR& scene,
    const yr::TwoLevelBvh& acceleration,
    float x
) {
    yr::BvhTraceStats stats;
    return yr::IntersectTwoLevelBvh(
        scene.Geometry(),
        acceleration,
        yr::Ray3f{yr::Point3f{x, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats
    );
}

} // namespace

YR_TEST(two_level_bvh_synthesizes_identity_instances) {
    const yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    const yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());

    YR_EXPECT_TRUE(build.errors.empty());
    YR_EXPECT_EQ(build.acceleration.blases.size(), std::size_t{1});
    YR_EXPECT_EQ(build.acceleration.instances.size(), std::size_t{1});
    YR_EXPECT_TRUE(!build.acceleration.tlas.nodes.empty());

    const yr::BvhHit hit = Trace(scene, build.acceleration, 0.0f);
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.mesh_primitive.Value(), 0);
    YR_EXPECT_EQ(hit.instance.Value(), 0);
    YR_EXPECT_NEAR(hit.t, 1.0f, 1.0e-6f);
}

YR_TEST(two_level_bvh_reuses_one_blas_for_multiple_instances) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::TranslationMatrix(yr::Vec3f{-1.0f, 0.0f, 0.0f})});
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::TranslationMatrix(yr::Vec3f{1.0f, 0.0f, 0.0f})});

    const yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());

    YR_EXPECT_TRUE(build.errors.empty());
    YR_EXPECT_EQ(build.acceleration.blases.size(), std::size_t{1});
    YR_EXPECT_EQ(build.acceleration.instances.size(), std::size_t{2});
    const yr::BvhHit left = Trace(scene, build.acceleration, -1.0f);
    const yr::BvhHit right = Trace(scene, build.acceleration, 1.0f);
    YR_EXPECT_TRUE(left.hit);
    YR_EXPECT_TRUE(right.hit);
    YR_EXPECT_EQ(left.instance.Value(), 0);
    YR_EXPECT_EQ(right.instance.Value(), 1);
}

YR_TEST(two_level_bvh_refit_updates_bounds_without_rebuilding_topology) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.instances.push_back(yr::RenderInstance{yr::MeshPrimitiveHandle{0}, yr::Mat4f{}});
    yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());
    const std::vector<int> original_indices = build.acceleration.tlas.primitive_indices;
    const std::size_t original_node_count = build.acceleration.tlas.nodes.size();
    const std::size_t original_blas_count = build.acceleration.blases.size();
    const std::vector<yr::Mat4f> transforms{
        yr::TranslationMatrix(yr::Vec3f{2.0f, 0.0f, 0.0f})
    };
    std::string error;

    const bool ok = yr::RefitTwoLevelBvh(
        scene.Geometry(), transforms, build.acceleration, error);

    YR_EXPECT_TRUE(ok);
    YR_EXPECT_TRUE(error.empty());
    YR_EXPECT_EQ(build.acceleration.tlas.nodes.size(), original_node_count);
    YR_EXPECT_EQ(build.acceleration.tlas.primitive_indices, original_indices);
    YR_EXPECT_EQ(build.acceleration.blases.size(), original_blas_count);
    YR_EXPECT_TRUE(!Trace(scene, build.acceleration, 0.0f).hit);
    YR_EXPECT_TRUE(Trace(scene, build.acceleration, 2.0f).hit);
}

YR_TEST(two_level_bvh_rebuild_reuses_blas_and_recomputes_tlas) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.instances.push_back(yr::RenderInstance{yr::MeshPrimitiveHandle{0}, yr::Mat4f{}});
    yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());
    const std::size_t original_blas_nodes = build.acceleration.blases[0].bvh.nodes.size();
    const std::vector<yr::Mat4f> transforms{
        yr::TranslationMatrix(yr::Vec3f{-2.0f, 0.0f, 0.0f})
    };
    std::string error;

    const bool ok = yr::RebuildTwoLevelTlas(
        scene.Geometry(), transforms, yr::BvhBuildOptions{}, build.acceleration, error);

    YR_EXPECT_TRUE(ok);
    YR_EXPECT_TRUE(error.empty());
    YR_EXPECT_EQ(build.acceleration.blases[0].bvh.nodes.size(), original_blas_nodes);
    YR_EXPECT_TRUE(Trace(scene, build.acceleration, -2.0f).hit);
    YR_EXPECT_TRUE(!Trace(scene, build.acceleration, 0.0f).hit);
}

YR_TEST(two_level_bvh_keeps_spheres_in_the_tlas) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    for (yr::RenderVertex& vertex : scene.vertices) vertex.position.z = -2.0f;
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{}, 0.5f, 0, -1, false});
    const yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());

    const yr::BvhHit hit = Trace(scene, build.acceleration, 0.0f);

    YR_EXPECT_TRUE(build.errors.empty());
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere.Value(), 0);
    YR_EXPECT_TRUE(!hit.instance.IsValid());
    YR_EXPECT_NEAR(hit.t, 0.5f, 1.0e-6f);
}

YR_TEST(two_level_bvh_rejects_singular_instance_transform) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::ScaleMatrix(yr::Vec3f{0.0f, 1.0f, 1.0f})});

    const yr::TwoLevelBvhBuildResult build = yr::BuildTwoLevelBvh(scene.Geometry());

    YR_EXPECT_TRUE(!build.errors.empty());
    YR_EXPECT_TRUE(build.acceleration.tlas.nodes.empty());
}

YR_TEST(two_level_probe_filters_other_instances_of_the_same_blas) {
    yr::RenderSceneIR scene = yrtest::MakeSingleBvhTestTriangle();
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::TranslationMatrix(yr::Vec3f{0.0f, 0.0f, 0.0f})});
    scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::TranslationMatrix(yr::Vec3f{0.0f, 0.0f, 1.0f})});
    const yr::RenderAccelerationBuildResult build =
        yr::BuildRenderAcceleration(scene.Geometry());

    const yr::BvhProbeHits hits = yr::IntersectAccelerationProbe(
        scene.Geometry(),
        build.acceleration,
        yr::Ray3f{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        yr::MeshPrimitiveHandle{0},
        yr::InstanceHandle{0},
        yr::SphereHandle{},
        1.0e-5f,
        10.0f);

    YR_EXPECT_TRUE(build.errors.empty());
    YR_EXPECT_EQ(hits.count, 1);
    YR_EXPECT_EQ(hits.hits[0].instance.Value(), 0);
    YR_EXPECT_NEAR(hits.hits[0].t, 3.0f, 1.0e-5f);
}
