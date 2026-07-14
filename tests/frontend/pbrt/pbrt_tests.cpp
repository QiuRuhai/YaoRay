#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>

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

bool ContainsPath(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& expected) {
    for (const std::filesystem::path& path : paths) {
        if (path == expected) {
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

YR_TEST(pbrt_resolves_included_plymesh_relative_to_include_directory) {
    const yr::PbrtSceneLoadResult load =
        yr::LoadPbrtScene(FixturePath("pbrt/include_resource_root.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path main_root =
        std::filesystem::weakly_canonical(FixturePath("pbrt"), ec);
    YR_EXPECT_TRUE(!ec);
    YR_EXPECT_TRUE(!load.scene->source_roots.empty());
    YR_EXPECT_TRUE(load.scene->source_roots[0] == main_root);

    ec.clear();
    const std::filesystem::path include_root =
        std::filesystem::weakly_canonical(FixturePath("pbrt/include_resource"), ec);
    YR_EXPECT_TRUE(!ec);
    YR_EXPECT_TRUE(ContainsPath(load.scene->source_roots, include_root));

    const yr::SceneCompileResult compiled = yr::CompilePbrtScene(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    if (!compiled.scene.has_value()) {
        return;
    }
    YR_EXPECT_TRUE(!compiled.scene.value().primitives.empty());
    YR_EXPECT_EQ(compiled.scene.value().vertices.size(), std::size_t{3});
    YR_EXPECT_NEAR(compiled.scene.value().vertices[0].position.x, -0.5, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[0].position.y, 0.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[0].position.z, 0.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[1].position.x, 0.5, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[1].position.y, 0.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[1].position.z, 0.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[2].position.x, 0.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[2].position.y, 1.0, 0.0001);
    YR_EXPECT_NEAR(compiled.scene.value().vertices[2].position.z, 0.0, 0.0001);
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
