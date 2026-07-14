#include "yr_test.hpp"

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <filesystem>
#include <string>

namespace {

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = YAORAY_TEST_DATA_DIR;
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.material_name = "test_mat";
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& substring
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_normalmap_loads_png_into_material_normal_map) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.normal_map >= 0);
    YR_EXPECT_TRUE(static_cast<std::size_t>(m.normal_map) < result.scene->textures.size());
    // Normal maps are always loaded as linear data.
    YR_EXPECT_TRUE(result.scene->textures[m.normal_map].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_normalmap_honors_explicit_normalscale) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "normalscale", {0.5f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene->materials[0].normal_scale, 0.5f, 1.0e-6);
}

YR_TEST(scene_compiler_normalmap_without_param_keeps_index_minus_one) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials[0].normal_map, -1);
    YR_EXPECT_NEAR(result.scene->materials[0].normal_scale, 1.0f, 1.0e-6);
}

YR_TEST(scene_compiler_normalmap_missing_file_emits_error) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/no_such_normal.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_normalmap_works_on_conductor_material) {
    // Normal maps are not exclusive to diffuse — every material kind that uses
    // shading_normal should pick up the normal map.
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "conductor";
    mat.params.push_back(yr::PbrtParam{"rgb", "eta", {0.2f, 0.4f, 1.3f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"rgb", "k", {3.9f, 2.4f, 1.6f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->materials[0].normal_map >= 0);
}
