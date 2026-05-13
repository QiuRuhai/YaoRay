#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

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
