#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

// TODO(Task 11): Expand PBRT tests for new PbrtScene + CompilePbrtScene pipeline.

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

YR_TEST(pbrt_loads_minimal_triangle_scene) {
    const yr::PbrtSceneLoadResult result =
        yr::LoadPbrtScene(FixturePath("pbrt/minimal_triangle.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
}

YR_TEST(pbrt_minimal_triangle_compiles_to_render_scene) {
    const yr::PbrtSceneLoadResult load =
        yr::LoadPbrtScene(FixturePath("pbrt/minimal_triangle.pbrt"));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompilePbrtScene(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    YR_EXPECT_TRUE(!compiled.scene.value().primitives.empty());
    YR_EXPECT_TRUE(!compiled.scene.value().materials.empty());
}

YR_TEST(pbrt_reports_missing_file) {
    const yr::PbrtSceneLoadResult result =
        yr::LoadPbrtScene(FixturePath("pbrt/no_such_scene.pbrt"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
}

YR_TEST(pbrt_resolves_include_with_transform) {
    const yr::PbrtSceneLoadResult load =
        yr::LoadPbrtScene(FixturePath("pbrt/include_root.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompilePbrtScene(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    if (!compiled.scene.has_value()) {
        return;
    }
    YR_EXPECT_TRUE(!compiled.scene.value().primitives.empty());
}

YR_TEST(pbrt_loads_plymesh_shape) {
    const yr::PbrtSceneLoadResult load =
        yr::LoadPbrtScene(FixturePath("pbrt/ply_scene.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompilePbrtScene(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    if (!compiled.scene.has_value()) {
        return;
    }
    YR_EXPECT_TRUE(!compiled.scene.value().primitives.empty());
}

YR_TEST(pbrt_builds_camera_from_transform) {
    const yr::PbrtSceneLoadResult load =
        yr::LoadPbrtScene(FixturePath("pbrt/camera_transform.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompilePbrtScene(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
}
