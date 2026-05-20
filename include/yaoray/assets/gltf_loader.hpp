#pragma once

#include <filesystem>

#include <yaoray/assets/imported_asset.hpp>

namespace yr {

AssetLoadResult LoadGltfMesh(const std::filesystem::path& path);

} // namespace yr
