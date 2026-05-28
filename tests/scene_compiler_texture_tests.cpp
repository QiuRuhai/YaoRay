#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

const std::filesystem::path& TestDataDir() {
    static const std::filesystem::path dir{YAORAY_TEST_DATA_DIR};
    return dir;
}

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = TestDataDir();
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere so the empty-geometry check passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_loads_imagemap_texture_into_ir_textures) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["checker"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});

    const yr::RenderTexture& loaded = result.scene->textures[0];
    YR_EXPECT_EQ(loaded.width, 2);
    YR_EXPECT_EQ(loaded.height, 2);
    YR_EXPECT_TRUE(!loaded.texels.empty());
    // PNG defaults to sRGB.
    YR_EXPECT_TRUE(loaded.color_space == yr::TextureColorSpace::Srgb);
}

YR_TEST(scene_compiler_imagemap_respects_explicit_linear_encoding) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    tex.params.push_back(yr::PbrtParam{"string", "encoding", {}, {}, {"linear"}, {}});
    pbrt.named_textures["linear_data"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    YR_EXPECT_TRUE(result.scene->textures[0].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_imagemap_hdr_loads_as_linear) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/tiny_env.hdr"}, {}});
    pbrt.named_textures["env"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    YR_EXPECT_TRUE(result.scene->textures[0].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_constant_texture_does_not_allocate_render_texture) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "constant";
    tex.params.push_back(yr::PbrtParam{"rgb", "value", {0.7f, 0.2f, 0.1f}, {}, {}, {}});
    pbrt.named_textures["solid_red"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // Constant textures fold to values and do NOT push a RenderTexture.
    YR_EXPECT_EQ(result.scene->textures.size(), std::size_t{0});
}

YR_TEST(scene_compiler_imagemap_missing_file_emits_error_diagnostic) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/no_such_texture.png"}, {}});
    pbrt.named_textures["broken"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    // Missing texture is an Error — compilation fails.
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_binds_imagemap_to_diffuse_reflectance) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["wood"] = tex;

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"wood"}, {}});
    pbrt.named_materials["floor"] = mat;
    pbrt.shapes[0].material_name = "floor";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.reflectance.texture, 0);
}

YR_TEST(scene_compiler_binds_constant_texture_to_diffuse_reflectance_value) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "constant";
    tex.params.push_back(yr::PbrtParam{"rgb", "value", {0.4f, 0.5f, 0.6f}, {}, {}, {}});
    pbrt.named_textures["bluish"] = tex;

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"bluish"}, {}});
    pbrt.named_materials["wall"] = mat;
    pbrt.shapes[0].material_name = "wall";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    // Constant texture folds into .value; .texture stays -1.
    YR_EXPECT_EQ(m.reflectance.texture, -1);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.4f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.5f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.6f, 1.0e-5);
}

YR_TEST(scene_compiler_binds_imagemap_to_conductor_roughness) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["microfacets"] = tex;

    yr::PbrtEntity mat;
    mat.type = "conductor";
    mat.params.push_back(yr::PbrtParam{"texture", "uroughness", {}, {}, {"microfacets"}, {}});
    pbrt.named_materials["bumpy_gold"] = mat;
    pbrt.shapes[0].material_name = "bumpy_gold";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.uroughness.texture, 0);
}

YR_TEST(scene_compiler_unknown_texture_name_emits_warning_and_uses_fallback) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    // No Texture directive created, but the material references "missing".
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"missing"}, {}});
    pbrt.named_materials["broken"] = mat;
    pbrt.shapes[0].material_name = "broken";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // No fatal error; compilation succeeds with the default reflectance and a Warning.
    bool found_warning = false;
    for (const yr::SceneDiagnostic& d : result.diagnostics) {
        if (d.severity == yr::DiagnosticSeverity::Warning &&
            d.message.find("missing") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    YR_EXPECT_TRUE(found_warning);
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.reflectance.texture, -1);
}
