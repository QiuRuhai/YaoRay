#include "yr_test.hpp"

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace {

yr::PbrtScene MinimalSceneWithMaterial(const std::string& mat_type) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtEntity mat;
    mat.type = mat_type;
    pbrt.named_materials["test_mat"] = mat;

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

// Subsurface materials with non-zero default sigma_s now compile to
// RenderMaterialKind::Subsurface (M4 Slice 4). The old degradation-to-diffuse
// path only fires when sigma_s is explicitly zero on all channels.
YR_TEST(material_degradation_subsurface_with_defaults_compiles_to_subsurface) {
    yr::PbrtScene pbrt = MinimalSceneWithMaterial("subsurface");
    // No sigma_s override — the default {2.55, 3.21, 3.77} is non-zero, so
    // the material compiles to Subsurface with a precomputed diffusion table.
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Subsurface);
    YR_EXPECT_EQ(m.bssrdf_index, 0);
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(material_degradation_measured_becomes_default_conductor) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("measured");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& mat_out = result.scene->materials[0];
    YR_EXPECT_TRUE(mat_out.kind == yr::RenderMaterialKind::Conductor);
    YR_EXPECT_NEAR(mat_out.eta.value.x, 0.2f, 1.0e-6);
    YR_EXPECT_NEAR(mat_out.k.value.x, 1.0f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "measured"));
}

YR_TEST(material_degradation_hair_becomes_grey_diffuse) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("hair");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.5f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "hair"));
}

YR_TEST(material_degradation_unknown_kind_still_emits_catch_all_warning) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("totally_made_up_kind");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning,
                                       "totally_made_up_kind"));
}

YR_TEST(material_degradation_mix_uses_declared_reflectance) {
    yr::PbrtScene pbrt = MinimalSceneWithMaterial("mix");
    // Override the material to include a specific reflectance — mix mirrors
    // subsurface's "preserve declared reflectance if present" pattern.
    yr::PbrtEntity mat;
    mat.type = "mix";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.3f, 0.6f, 0.9f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.3f, 1.0e-6);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.6f, 1.0e-6);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.9f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "mix"));
}

YR_TEST(material_degradation_mix_without_reflectance_defaults_to_grey) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("mix");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.5f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "mix"));
}
