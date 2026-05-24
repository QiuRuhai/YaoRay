#pragma once

#include <filesystem>

#include <yaoray/scene/scene_world.hpp>

namespace yr {

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path);

} // namespace yr
