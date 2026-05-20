#pragma once

#include <filesystem>

#include <yaoray/assets/imported_asset.hpp>

namespace yr {

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);

} // namespace yr
