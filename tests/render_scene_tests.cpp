#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/render_scene_hash.hpp>
#include <yaoray/render/scene_compiler.hpp>

// TODO(Task 11): Rewrite scene compiler tests for PBRT-only pipeline (CompilePbrtScene).

YR_TEST(render_scene_ir_defaults_are_backend_friendly) {
    const yr::RenderSceneIR scene;

    YR_EXPECT_EQ(scene.requested_backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(scene.sampler, yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(scene.width, 0);
    YR_EXPECT_EQ(scene.height, 0);
    YR_EXPECT_EQ(scene.spp, 1);
    YR_EXPECT_EQ(scene.max_depth, 5);
    YR_EXPECT_EQ(scene.seed, std::uint64_t{0});
    YR_EXPECT_EQ(scene.threads, 0);
    YR_EXPECT_NEAR(scene.radiance_clamp, 0.0, 1e-6);
    YR_EXPECT_TRUE(scene.vertices.empty());
    YR_EXPECT_TRUE(scene.indices.empty());
    YR_EXPECT_TRUE(scene.primitives.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.emissive_primitives.empty());
}

YR_TEST(render_settings_hash_is_stable_for_identical_scene) {
    yr::RenderSceneIR scene;
    scene.width = 320;
    scene.height = 180;
    scene.spp = 4;

    yr::RenderSceneIR same;
    same.width = 320;
    same.height = 180;
    same.spp = 4;

    YR_EXPECT_EQ(yr::ComputeRenderSettingsHash(scene), yr::ComputeRenderSettingsHash(same));
}

YR_TEST(render_settings_hash_changes_for_different_settings) {
    yr::RenderSceneIR base;
    base.width = 320;
    base.height = 180;
    base.spp = 4;

    yr::RenderSceneIR changed_spp = base;
    changed_spp.spp = 8;

    yr::RenderSceneIR changed_seed = base;
    changed_seed.seed = 42;

    const std::uint64_t base_hash = yr::ComputeRenderSettingsHash(base);
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(changed_spp));
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(changed_seed));
}

YR_TEST(render_settings_hash_changes_for_geometry_table_counts) {
    yr::RenderSceneIR base;
    base.width = 64;
    base.height = 64;

    yr::RenderSceneIR changed = base;
    changed.vertices.push_back(yr::RenderVertex{});

    YR_EXPECT_TRUE(yr::ComputeRenderSettingsHash(base) != yr::ComputeRenderSettingsHash(changed));
}

YR_TEST(scene_compiler_compiles_shape_sphere) {
    // Build a minimal PbrtScene with a Shape "sphere" of radius 0.5 translated to (1, 2, 3).
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};  // identity
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord record;
    record.shape.type = "sphere";
    record.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    // object_to_world that translates by (1,2,3)
    record.object_to_world = yr::Mat4f{};
    record.object_to_world.m[12] = 1.0f;
    record.object_to_world.m[13] = 2.0f;
    record.object_to_world.m[14] = 3.0f;
    pbrt.shapes.push_back(record);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene->spheres[0].center.x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.y, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.z, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].radius, 0.5f, 1.0e-6);
}
