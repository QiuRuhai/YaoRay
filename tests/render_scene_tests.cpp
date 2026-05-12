#include "yr_test.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace {

yr::SceneDescription MakeBaseScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated.toml";
    scene.render.backend = yr::RenderBackendKind::Cuda;
    scene.render.width = 320;
    scene.render.height = 180;
    scene.render.spp = 4;
    scene.render.max_depth = 6;
    scene.render.seed = std::uint64_t{123};
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera->fov_y = 60.0f;
    return scene;
}

} // namespace

YR_TEST(render_scene_defaults_are_backend_friendly) {
    const yr::RenderScene scene;

    YR_EXPECT_EQ(scene.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.width, 0);
    YR_EXPECT_EQ(scene.height, 0);
    YR_EXPECT_EQ(scene.spp, 1);
    YR_EXPECT_EQ(scene.max_depth, 5);
    YR_EXPECT_EQ(scene.seed, std::uint64_t{0});
    YR_EXPECT_TRUE(scene.triangles.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.area_lights.empty());
}

YR_TEST(scene_compiler_copies_render_settings) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.backend, yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(compiled.width, 320);
    YR_EXPECT_EQ(compiled.height, 180);
    YR_EXPECT_EQ(compiled.spp, 4);
    YR_EXPECT_EQ(compiled.max_depth, 6);
    YR_EXPECT_EQ(compiled.seed, std::uint64_t{123});
}

YR_TEST(scene_compiler_builds_camera_basis) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderCamera& camera = result.scene.value().camera;
    YR_EXPECT_NEAR(camera.origin.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.z, 4.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.fov_y_radians, 1.04719758, 1e-6);
}

YR_TEST(scene_compiler_copies_constant_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.z, 0.4, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.strength, 2.0, 1e-6);
}

YR_TEST(scene_compiler_copies_area_lights) {
    yr::SceneDescription scene = MakeBaseScene();
    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.area.size = {4.0f, 5.0f};
    light.area.radiance = yr::Color3f{6.0f, 7.0f, 8.0f};
    scene.lights.push_back(light);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().area_lights.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].position.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].width, 4.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].height, 5.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].radiance.z, 8.0, 1e-6);
}
