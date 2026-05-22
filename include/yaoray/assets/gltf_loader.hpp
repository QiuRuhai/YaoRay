#pragma once

#include <filesystem>

#include <yaoray/assets/asset_resource.hpp>

namespace yr {

AssetLoadResult LoadGltfResource(const std::filesystem::path& path);

} // namespace yr
