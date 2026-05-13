#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
};

struct AssetLoadResult {
    std::optional<ImportedMesh> mesh;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);

} // namespace yr
