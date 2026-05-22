#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <yaoray/assets/asset_resource.hpp>
#include <yaoray/assets/gltf_loader.hpp>
#include <yaoray/assets/obj_loader.hpp>

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

bool ErrorContains(const yr::AssetLoadResult& result, std::string_view text) {
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const yr::AssetResource& ResourceValue(const yr::AssetLoadResult& result) {
    if (!result.resource.has_value()) {
        throw std::runtime_error("expected AssetLoadResult::resource to contain a value");
    }
    return result.resource.value();
}

const yr::AssetPrimitive& FirstPrimitive(const yr::AssetResource& resource) {
    if (resource.meshes.empty() || resource.meshes[0].primitives.empty()) {
        throw std::runtime_error("expected first mesh primitive");
    }
    return resource.meshes[0].primitives[0];
}

const yr::AssetMaterial& FirstMaterial(const yr::AssetResource& resource) {
    if (resource.materials.empty()) {
        throw std::runtime_error("expected first material");
    }
    return resource.materials[0];
}

const yr::AssetTexture& TextureValue(const yr::AssetResource& resource, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= resource.textures.size()) {
        throw std::runtime_error("expected valid texture index");
    }
    return resource.textures[static_cast<std::size_t>(index)];
}

const yr::AssetImage& ImageValue(const yr::AssetResource& resource, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= resource.images.size()) {
        throw std::runtime_error("expected valid image index");
    }
    return resource.images[static_cast<std::size_t>(index)];
}

const yr::AssetSampler& SamplerValue(const yr::AssetResource& resource, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= resource.samplers.size()) {
        throw std::runtime_error("expected valid sampler index");
    }
    return resource.samplers[static_cast<std::size_t>(index)];
}

} // namespace

YR_TEST(asset_transform_defaults_to_identity_matrix) {
    const yr::AssetTransform transform;

    for (std::size_t i = 0; i < 16; ++i) {
        const double expected = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0 : 0.0;
        YR_EXPECT_NEAR(transform.local_to_parent[i], expected, 1e-6);
    }
}

YR_TEST(asset_resource_defaults_are_empty_static_scene_data) {
    const yr::AssetResource resource;
    const yr::AssetPrimitive primitive;

    YR_EXPECT_TRUE(resource.scenes.empty());
    YR_EXPECT_EQ(resource.default_scene, 0);
    YR_EXPECT_TRUE(resource.nodes.empty());
    YR_EXPECT_TRUE(resource.meshes.empty());
    YR_EXPECT_TRUE(resource.materials.empty());
    YR_EXPECT_TRUE(resource.textures.empty());
    YR_EXPECT_TRUE(resource.images.empty());
    YR_EXPECT_TRUE(resource.samplers.empty());
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_TRUE(primitive.positions.empty());
    YR_EXPECT_TRUE(primitive.normals.empty());
    YR_EXPECT_TRUE(primitive.texcoords0.empty());
    YR_EXPECT_TRUE(primitive.indices.empty());
    YR_EXPECT_EQ(primitive.material, -1);
}

YR_TEST(obj_loader_loads_triangle_obj_as_asset_resource) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_EQ(resource.scenes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.default_scene, 0);
    YR_EXPECT_EQ(resource.scenes[0].root_nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes[0].mesh, 0);

    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
    YR_EXPECT_NEAR(primitive.positions[0].x, -0.5, 1e-6);
    YR_EXPECT_NEAR(primitive.positions[1].x, 0.5, 1e-6);
    YR_EXPECT_NEAR(primitive.positions[2].y, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_obj) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/quad.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{6});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
}

YR_TEST(obj_loader_rejects_non_obj_extension) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".obj"));
}

YR_TEST(obj_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/missing.obj"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "OBJ file not found"));
}

YR_TEST(obj_loader_returns_error_when_obj_has_no_triangles) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/empty.obj"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "no triangles"));
}

YR_TEST(obj_loader_preserves_triangle_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/uv_triangle.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.texcoords0.size(), std::size_t{3});
    YR_EXPECT_TRUE(primitive.texcoords0.size() == primitive.positions.size());
    YR_EXPECT_NEAR(primitive.texcoords0[0].x, 0.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[1].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[2].y, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/uv_quad.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{6});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
    YR_EXPECT_TRUE(primitive.texcoords0.size() == primitive.positions.size());
    YR_EXPECT_NEAR(primitive.texcoords0[0].x, 0.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[1].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[2].x, 0.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[2].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[3].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[3].y, 0.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[4].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[4].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[5].x, 0.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[5].y, 1.0, 1e-6);
}

YR_TEST(obj_loader_preserves_triangle_vertex_normals) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/normal_triangle.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.normals.size(), std::size_t{3});
    YR_EXPECT_TRUE(primitive.normals.size() == primitive.positions.size());
    YR_EXPECT_NEAR(primitive.normals[0].z, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[1].y, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[1].z, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[2].x, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[2].z, 0.70710678, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs_and_normals) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/normal_quad.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{6});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
    YR_EXPECT_TRUE(primitive.texcoords0.size() == primitive.positions.size());
    YR_EXPECT_TRUE(primitive.normals.size() == primitive.positions.size());
    YR_EXPECT_NEAR(primitive.normals[0].z, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[1].y, 0.2, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[2].y, -0.2, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[3].y, 0.2, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[4].x, 0.2, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[5].y, -0.2, 1e-6);
}

YR_TEST(obj_loader_imports_basic_mtl_material) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/textured_quad.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetResource& resource = ResourceValue(result);
    const yr::AssetMaterial& material = FirstMaterial(resource);
    YR_EXPECT_EQ(resource.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(material.name, "checker");
    YR_EXPECT_NEAR(material.base_color.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(material.base_color.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(material.base_color.z, 0.75, 1e-6);
    YR_EXPECT_TRUE(material.base_color_texture >= 0);
    YR_EXPECT_EQ(resource.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.images.size(), std::size_t{1});
    YR_EXPECT_TRUE(ImageValue(resource, TextureValue(resource, material.base_color_texture).image).path.generic_string().find("checker_2x2.png") != std::string::npos);
    YR_EXPECT_EQ(FirstPrimitive(resource).material, 0);
}

YR_TEST(obj_loader_rejects_duplicate_mtl_material_names) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/duplicate_materials.obj"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "duplicate OBJ material"));
}

YR_TEST(gltf_loader_rejects_non_gltf_extension) {
    const yr::AssetLoadResult result = yr::LoadGltfResource(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".gltf or .glb"));
}

YR_TEST(gltf_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadGltfResource(FixturePath("assets/missing.gltf"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "glTF file not found"));
}

YR_TEST(gltf_loader_loads_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfResource(FixturePath("assets/gltf/Triangle/glTF/Triangle.gltf"));

    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!resource.scenes.empty());
    YR_EXPECT_TRUE(!resource.nodes.empty());
    YR_EXPECT_TRUE(!resource.meshes.empty());
    YR_EXPECT_EQ(resource.default_scene, 0);
    YR_EXPECT_EQ(resource.scenes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.scenes[0].root_nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.scenes[0].root_nodes[0], 0);
    YR_EXPECT_EQ(resource.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes[0].mesh, 0);
    for (std::size_t i = 0; i < 16; ++i) {
        const double expected = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0 : 0.0;
        YR_EXPECT_NEAR(resource.nodes[0].transform.local_to_parent[i], expected, 1e-6);
    }
    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
}

YR_TEST(gltf_loader_loads_non_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfResource(FixturePath("assets/gltf/TriangleWithoutIndices/glTF/TriangleWithoutIndices.gltf"));

    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!resource.scenes.empty());
    YR_EXPECT_TRUE(!resource.nodes.empty());
    YR_EXPECT_TRUE(!resource.meshes.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
}

YR_TEST(gltf_loader_loads_base_color_texture_material) {
    const yr::AssetLoadResult result =
        yr::LoadGltfResource(FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf"));

    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!resource.scenes.empty());
    YR_EXPECT_TRUE(!resource.nodes.empty());
    YR_EXPECT_TRUE(!resource.meshes.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
    const yr::AssetMaterial& material = FirstMaterial(resource);
    YR_EXPECT_TRUE(material.base_color_texture >= 0);
    const yr::AssetTexture& texture = TextureValue(resource, material.base_color_texture);
    YR_EXPECT_TRUE(texture.image >= 0);
    YR_EXPECT_TRUE(ImageValue(resource, texture.image).path.generic_string().find("testTexture.png") != std::string::npos);
    YR_EXPECT_TRUE(texture.sampler >= 0);
    const yr::AssetSampler& sampler = SamplerValue(resource, texture.sampler);
    YR_EXPECT_EQ(sampler.wrap_s, yr::TextureWrap::MirroredRepeat);
    YR_EXPECT_EQ(sampler.wrap_t, yr::TextureWrap::MirroredRepeat);
}

YR_TEST(gltf_loader_warns_and_defaults_for_unsupported_texture_wraps) {
    const yr::AssetLoadResult result =
        yr::LoadGltfResource(FixturePath("assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf"));

    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!resource.scenes.empty());
    YR_EXPECT_TRUE(!resource.nodes.empty());
    YR_EXPECT_TRUE(!resource.meshes.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
    YR_EXPECT_TRUE(!result.warnings.empty());
    YR_EXPECT_TRUE(result.warnings[0].find("unsupported glTF texture wrap") != std::string::npos);
    const yr::AssetTexture& texture = TextureValue(resource, FirstMaterial(resource).base_color_texture);
    YR_EXPECT_TRUE(texture.sampler >= 0);
    const yr::AssetSampler& sampler = SamplerValue(resource, texture.sampler);
    YR_EXPECT_EQ(sampler.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(sampler.wrap_t, yr::TextureWrap::Repeat);
}

YR_TEST(gltf_loader_rejects_invalid_base_color_texture_index) {
    const yr::AssetLoadResult result = yr::LoadGltfResource(
        FixturePath("assets/gltf/InvalidBaseColorTextureIndex/glTF/InvalidBaseColorTextureIndex.gltf")
    );

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "invalid base color texture"));
}

YR_TEST(gltf_loader_rejects_base_color_texture_without_image_source) {
    const yr::AssetLoadResult result = yr::LoadGltfResource(
        FixturePath("assets/gltf/BaseColorTextureMissingSource/glTF/BaseColorTextureMissingSource.gltf")
    );

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "base color texture image"));
}

YR_TEST(gltf_loader_rejects_base_color_texture_data_uri) {
    const yr::AssetLoadResult result = yr::LoadGltfResource(
        FixturePath("assets/gltf/BaseColorTextureDataUri/glTF/BaseColorTextureDataUri.gltf")
    );

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "data URI"));
}

YR_TEST(gltf_loader_rejects_binary_glb_with_embedded_base_color_texture) {
    const yr::AssetLoadResult result =
        yr::LoadGltfResource(FixturePath("assets/gltf/BoxTextured/glTF-Binary/BoxTextured.glb"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "external image URI"));
}
