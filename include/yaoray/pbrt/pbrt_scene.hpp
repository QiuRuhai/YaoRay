#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace yr {

struct SceneWorldLoadResult {
    std::optional<SceneWorld> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path);

} // namespace yr
