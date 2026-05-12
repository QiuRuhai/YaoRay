#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneLoadResult {
    std::optional<SceneDescription> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneLoadResult LoadSceneFile(const std::filesystem::path& path);
void ApplyBackendOverride(SceneDescription& scene, RenderBackendKind backend);

} // namespace yr
