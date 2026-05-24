#pragma once

#include <filesystem>

#include <yaoray/scene/scene_world.hpp>

namespace yr {

SceneWorldLoadResult LoadSceneWorldFile(const std::filesystem::path& path);
void ApplyBackendOverride(SceneWorld& scene, RenderBackendKind backend);

} // namespace yr
