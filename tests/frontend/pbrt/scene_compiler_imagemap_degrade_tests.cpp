#include "yr_test.hpp"

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene that declares one imagemap texture pointing
// at a path that does NOT exist on disk. The fixture is intentionally similar
// to MinimalScene() in scene_compiler_texture_tests.cpp but uses a relative
// source_root ("."), so the test does not depend on YAORAY_TEST_DATA_DIR.
yr::PbrtScene MakeSceneWithMissingImagemap() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "missing_imagemap.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere so the empty-geometry guard passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    // Declare one imagemap texture pointing at a non-existent file.
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{
        "string", "filename", {}, {}, {"this_file_does_not_exist_on_disk.png"}, {}
    });
    pbrt.named_textures["missing_tex"] = tex;

    return pbrt;
}

bool HasDiagnosticContaining(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasFieldDiagnostic(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& field_needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.field.find(field_needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_imagemap_missing_file_degrades_to_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithMissingImagemap();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // No Error severity referencing the missing texture name (neither in the
    // message body nor in the diagnostic.field).
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, "missing_tex"));
    YR_EXPECT_TRUE(!HasFieldDiagnostic(
        result.diagnostics, yr::DiagnosticSeverity::Error, "missing_tex"));

    // A Warning was emitted whose field tags the texture name so users can grep.
    YR_EXPECT_TRUE(HasFieldDiagnostic(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "missing_tex"));

    // The Warning's body mentions the "imagemap load failed" phrase so the
    // failure mode is grep-friendly across logs.
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "imagemap load failed"));

    // Compilation succeeded because the broken texture was degraded, not fatal.
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));

    // The degraded binding did NOT add a RenderTexture entry; downstream
    // consumers see a folded constant. With only one (broken) texture declared,
    // ir.textures must stay empty.
    YR_EXPECT_EQ(result.scene->textures.size(), std::size_t{0});
}

YR_TEST(scene_compiler_imagemap_missing_file_binds_neutral_constant_to_material) {
    yr::PbrtScene pbrt = MakeSceneWithMissingImagemap();

    // Wire a diffuse material that pulls reflectance from the missing texture.
    // The folded-constant path inside TexParam3fFromParams should resolve to
    // Color3f{0.5, 0.5, 0.5} (the patch's neutral fallback).
    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{
        "texture", "reflectance", {}, {}, {"missing_tex"}, {}
    });
    pbrt.named_materials["broken_surface"] = mat;
    pbrt.shapes[0].material_name = "broken_surface";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});

    const yr::RenderMaterial& m = result.scene->materials[0];
    // Folded constant: .texture == -1 and .value == neutral grey.
    YR_EXPECT_EQ(m.reflectance.texture, -1);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.5f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.5f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.5f, 1.0e-5);
}
