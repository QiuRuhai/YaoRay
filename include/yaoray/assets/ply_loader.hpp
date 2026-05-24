#pragma once

#include <filesystem>

#include <yaoray/assets/asset_resource.hpp>

namespace yr {

AssetLoadResult LoadPlyResource(const std::filesystem::path& path);

} // namespace yr
