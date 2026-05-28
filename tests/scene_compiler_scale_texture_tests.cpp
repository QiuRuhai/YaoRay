#include "yr_test.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

namespace {

// Minimal compilable scene shell -- same shape as the imagemap_degrade fixture
// (relative source_root so it does not depend on YAORAY_TEST_DATA_DIR), with
// one sphere so the empty-geometry guard does not trigger.
yr::PbrtScene MakeBaseSceneShell() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "scale_test.pbrt";
    pbrt.source_root = ".";

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
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    return pbrt;
}

void AddConstantTexture(yr::PbrtScene& pbrt, const std::string& name, float r, float g, float b) {
    yr::PbrtEntity e;
    e.type = "constant";
    e.params.push_back(yr::PbrtParam{"rgb", "value", {r, g, b}, {}, {}, {}});
    pbrt.named_textures.emplace(name, e);
}

void AddScaleTexture(yr::PbrtScene& pbrt,
                     const std::string& name,
                     const std::string& inner_name,
                     float scale) {
    yr::PbrtEntity e;
    e.type = "scale";
    e.params.push_back(yr::PbrtParam{"float", "scale", {scale}, {}, {}, {}});
    e.params.push_back(yr::PbrtParam{"texture", "tex", {}, {}, {inner_name}, {}});
    pbrt.named_textures.emplace(name, e);
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

bool HasFieldContaining(
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

// ---------------------------------------------------------------------------
// Folding observable through a downstream material binding.
//
// The scale's effect is most cleanly observable by hanging a diffuse material
// off the scaled texture and checking the folded constant arrives at the
// material's reflectance.value field.
// ---------------------------------------------------------------------------
YR_TEST(scene_compiler_scale_texture_folds_constant_inner_into_material) {
    // scale = 0.5 applied to a constant inner of {1.0, 0.8, 0.4}
    // expected folded constant: {0.5, 0.4, 0.2}
    yr::PbrtScene pbrt = MakeBaseSceneShell();
    AddConstantTexture(pbrt, "inner", 1.0f, 0.8f, 0.4f);
    AddScaleTexture(pbrt, "outer", "inner", 0.5f);

    // Wire a diffuse material that pulls reflectance from the scaled outer
    // texture, so the fold's output becomes visible via the material binding.
    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"outer"}, {}});
    pbrt.named_materials["test_mat"] = mat;
    pbrt.shapes[0].material_name = "test_mat";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning,
        "unsupported texture class 'scale'"));

    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];

    // Folded constant: .texture == -1 and .value == inner_value * scale.
    YR_EXPECT_EQ(m.reflectance.texture, -1);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.5f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.4f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.2f, 1.0e-5);
}

YR_TEST(scene_compiler_scale_texture_missing_inner_degrades_to_warning) {
    yr::PbrtScene pbrt = MakeBaseSceneShell();
    // "inner_does_not_exist" is NOT declared, only referenced.
    AddScaleTexture(pbrt, "outer", "inner_does_not_exist", 0.5f);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));

    // A Warning specifically referencing the outer scale texture's name.
    YR_EXPECT_TRUE(HasFieldContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "outer"));

    // And the message should mention the unknown inner reference.
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning,
        "inner_does_not_exist"));
}

YR_TEST(scene_compiler_scale_texture_no_longer_emits_unsupported_warning) {
    // Specifically verify the catch-all "unsupported texture class 'scale'"
    // warning is GONE after the patch -- this is the observable that
    // distinguishes pre-patch from post-patch behavior.
    yr::PbrtScene pbrt = MakeBaseSceneShell();
    AddConstantTexture(pbrt, "inner", 1.0f, 1.0f, 1.0f);
    AddScaleTexture(pbrt, "outer", "inner", 0.5f);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    for (const yr::SceneDiagnostic& d : result.diagnostics) {
        const bool is_unsupported_scale_warning =
            d.message.find("unsupported texture class 'scale'") != std::string::npos;
        YR_EXPECT_TRUE(!is_unsupported_scale_warning);
    }
}
