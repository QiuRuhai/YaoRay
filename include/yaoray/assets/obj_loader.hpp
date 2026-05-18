#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct ImportedMaterial {
    std::string name;
    Color3f diffuse{0.8f, 0.8f, 0.8f};
    std::filesystem::path diffuse_texture_path;
    bool has_diffuse_texture = false;
};

struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    Vec2f uv0;
    Vec2f uv1;
    Vec2f uv2;
    bool has_uv = false;
    int material_index = -1;
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
    std::vector<ImportedMaterial> materials;
};

struct AssetLoadResult {
    std::optional<ImportedMesh> mesh;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);

} // namespace yr
