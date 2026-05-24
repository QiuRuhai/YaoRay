#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/frontends/scene_frontend.hpp>
#include <yaoray/scene/diagnostic.hpp>

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_frontend_loads_toml_as_scene_world) {
    const yr::SceneWorldLoadResult result =
        yr::LoadSceneWorldFile(FixturePath("scene/builtin_triangle.toml"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->assets.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->instances.size(), std::size_t{1});
}

YR_TEST(scene_frontend_loads_pbrt_as_scene_world) {
    const yr::SceneWorldLoadResult result =
        yr::LoadSceneWorldFile(FixturePath("pbrt/minimal_triangle.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->assets.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->instances.size(), std::size_t{1});
}

YR_TEST(scene_frontend_rejects_unsupported_extension) {
    const yr::SceneWorldLoadResult result =
        yr::LoadSceneWorldFile(FixturePath("scene/not_a_scene.txt"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "unsupported scene file extension"));
}

YR_TEST(scene_frontend_applies_backend_override_to_scene_world) {
    yr::SceneWorld world;

    yr::ApplyBackendOverride(world, yr::RenderBackendKind::Cuda);

    YR_EXPECT_EQ(world.render.backend, yr::RenderBackendKind::Cuda);
}
