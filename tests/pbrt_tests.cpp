#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(pbrt_frontend_loads_minimal_triangle_scene_world) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneWorld& world = result.scene.value();
    YR_EXPECT_EQ(world.render.width, 64);
    YR_EXPECT_EQ(world.render.height, 32);
    YR_EXPECT_EQ(world.render.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.z, 3.0, 1e-6);
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.materials[0].name, std::string{"white"});
    YR_EXPECT_EQ(world.materials[0].type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(world.materials[0].albedo.x, 0.7, 1e-6);
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes[0].positions.size(), std::size_t{3});
    YR_EXPECT_EQ(world.assets[0].meshes[0].indices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_minimal_triangle_compiles_to_render_scene) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompileSceneWorld(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    YR_EXPECT_EQ(compiled.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.scene.value().materials.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_reports_missing_file) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/no_such_scene.pbrt"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "PBRT file not found"));
}
