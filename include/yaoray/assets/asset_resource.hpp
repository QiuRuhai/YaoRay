#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

enum class AssetPrimitiveTopology {
    Triangles,
};

enum class AssetAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct AssetTangent {
    Vec3f direction;
    float handedness = 1.0f;
};

struct AssetScene {
    std::string name;
    std::vector<int> root_nodes;
};

struct AssetTransform {
    std::array<float, 16> local_to_parent{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

struct AssetNode {
    std::string name;
    AssetTransform transform;
    int mesh = -1;
    std::vector<int> children;
};

struct AssetPrimitive {
    AssetPrimitiveTopology topology = AssetPrimitiveTopology::Triangles;
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> texcoords0;
    std::vector<AssetTangent> tangents;
    std::vector<std::uint32_t> indices;
    int material = -1;
};

struct AssetMesh {
    std::string name;
    std::vector<AssetPrimitive> primitives;
};

struct AssetMaterial {
    std::string name;
    MaterialKind approximate_type = MaterialKind::Diffuse;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    float base_color_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int occlusion_texture = -1;
    int emissive_texture = -1;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    AssetAlphaMode alpha_mode = AssetAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};

struct AssetTexture {
    int image = -1;
    int sampler = -1;
};

struct AssetImage {
    std::filesystem::path path;
};

struct AssetSampler {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};

struct AssetResource {
    std::vector<AssetScene> scenes;
    int default_scene = 0;
    std::vector<AssetNode> nodes;
    std::vector<AssetMesh> meshes;
    std::vector<AssetMaterial> materials;
    std::vector<AssetTexture> textures;
    std::vector<AssetImage> images;
    std::vector<AssetSampler> samplers;
};

struct AssetLoadResult {
    std::optional<AssetResource> resource;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

} // namespace yr
