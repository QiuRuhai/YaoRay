#pragma once

#include <filesystem>
#include <string>

#include <yaoray/io/image_loader.hpp>

namespace yr::image_loader_detail {

std::string LowerExtension(const std::filesystem::path& path);
bool IsFiniteColor(Color3f color);
TextureLoadResult LoadExrTexture(const std::filesystem::path& path);

} // namespace yr::image_loader_detail
