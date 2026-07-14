#include "image_loader_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace yr::image_loader_detail {

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool IsFiniteColor(Color3f color) {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}

} // namespace yr::image_loader_detail
