#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct ImportedMaterial {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f diffuse{0.8f, 0.8f, 0.8f};
    Color3f emission{};
    float roughness = 0.0f;
    float specular = 0.04f;
    std::filesystem::path diffuse_texture_path;
    bool has_diffuse_texture = false;
    TextureWrap diffuse_texture_wrap_s = TextureWrap::Repeat;
    TextureWrap diffuse_texture_wrap_t = TextureWrap::Repeat;
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
    Vec3f n0;
    Vec3f n1;
    Vec3f n2;
    bool has_vertex_normals = false;
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

} // namespace yr
