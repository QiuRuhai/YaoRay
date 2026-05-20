#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

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

} // namespace

YR_TEST(imported_triangle_defaults_do_not_claim_vertex_normals) {
    const yr::ImportedTriangle triangle;

    YR_EXPECT_TRUE(!triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.z, 0.0, 1e-6);
}

YR_TEST(obj_loader_loads_triangle_obj) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});

    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, -0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p1.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_obj) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});
}

YR_TEST(obj_loader_rejects_non_obj_extension) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".obj"));
}

YR_TEST(obj_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/missing.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "OBJ file not found"));
}

YR_TEST(obj_loader_returns_error_when_obj_has_no_triangles) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/empty.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "no triangles"));
}

YR_TEST(obj_loader_preserves_triangle_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/uv_triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_TRUE(triangle.has_uv);
    YR_EXPECT_NEAR(triangle.uv0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.uv1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.uv2.y, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/uv_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});
    YR_EXPECT_TRUE(result.mesh->triangles[0].has_uv);
    YR_EXPECT_TRUE(result.mesh->triangles[1].has_uv);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv2.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv0.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv0.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv1.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv2.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv2.y, 1.0, 1e-6);
}

YR_TEST(obj_loader_preserves_triangle_vertex_normals) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/normal_triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});

    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.z, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.x, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.z, 0.70710678, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs_and_normals) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/normal_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});

    const yr::ImportedTriangle& first = result.mesh->triangles[0];
    const yr::ImportedTriangle& second = result.mesh->triangles[1];
    YR_EXPECT_TRUE(first.has_uv);
    YR_EXPECT_TRUE(second.has_uv);
    YR_EXPECT_TRUE(first.has_vertex_normals);
    YR_EXPECT_TRUE(second.has_vertex_normals);
    YR_EXPECT_NEAR(first.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(first.n1.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(first.n2.y, -0.2, 1e-6);
    YR_EXPECT_NEAR(second.n0.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(second.n1.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(second.n2.y, -0.2, 1e-6);
}

YR_TEST(obj_loader_imports_basic_mtl_material) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/textured_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.mesh->materials[0].name, "checker");
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.z, 0.75, 1e-6);
    YR_EXPECT_TRUE(result.mesh->materials[0].has_diffuse_texture);
    YR_EXPECT_TRUE(result.mesh->materials[0].diffuse_texture_path.generic_string().find("checker_2x2.png") != std::string::npos);
    YR_EXPECT_EQ(result.mesh->triangles[0].material_index, 0);
}

YR_TEST(obj_loader_rejects_duplicate_mtl_material_names) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/duplicate_materials.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "duplicate OBJ material"));
}

YR_TEST(gltf_loader_rejects_non_gltf_extension) {
    const yr::AssetLoadResult result = yr::LoadGltfMesh(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".gltf or .glb"));
}

YR_TEST(gltf_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadGltfMesh(FixturePath("assets/missing.gltf"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "glTF file not found"));
}

YR_TEST(gltf_loader_loads_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/Triangle/glTF/Triangle.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.mesh->triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(gltf_loader_loads_non_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/TriangleWithoutIndices/glTF/TriangleWithoutIndices.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});
}

YR_TEST(gltf_loader_loads_base_color_texture_material) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!result.mesh->materials.empty());
    YR_EXPECT_TRUE(result.mesh->materials[0].has_diffuse_texture);
    YR_EXPECT_TRUE(result.mesh->materials[0].diffuse_texture_path.generic_string().find("testTexture.png") != std::string::npos);
}

YR_TEST(gltf_loader_loads_binary_glb) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/BoxTextured/glTF-Binary/BoxTextured.glb"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.mesh->triangles.size() >= std::size_t{12});
}
