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
