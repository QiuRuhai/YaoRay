#include "yr_test.hpp"
#include "tensor_test_util.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace {

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

// Build a minimal compilable scene with one sphere that uses a "measured"
// named material pointing at the given filename. source_root controls where
// the compiler looks for the file.
yr::PbrtScene MakeSceneWithMeasured(const std::string& source_root,
                                    const std::string& bsdf_filename) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "measured_test.pbrt";
    pbrt.source_root = source_root;

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtEntity mat;
    mat.type = "measured";
    mat.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {bsdf_filename}, {}});
    pbrt.named_materials["measured_mat"] = mat;

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.material_name = "measured_mat";
    pbrt.shapes.push_back(shape);

    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_measured_loads_to_measured_kind) {
    // Write a valid isotropic .bsdf to a temp file.
    const std::string path = yrtest::WriteSyntheticBsdf("sc_measured_iso.bsdf", 2);

    // source_root = "." so the compiler resolves "./sc_measured_iso.bsdf".
    const yr::PbrtScene pbrt = MakeSceneWithMeasured(".", "sc_measured_iso.bsdf");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene->materials.empty());

    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::Measured);
    YR_EXPECT_TRUE(m.measured_index >= 0);
    YR_EXPECT_TRUE(!result.scene->measured_brdfs.empty());

    std::remove(path.c_str());
}

YR_TEST(scene_compiler_measured_missing_file_degrades) {
    // Filename that doesn't exist on disk → should degrade to Conductor + Warning.
    const yr::PbrtScene pbrt = MakeSceneWithMeasured(".", "this_bsdf_does_not_exist.bsdf");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene->materials.empty());

    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::Conductor);

    // A Warning must be present mentioning the material (either "measured" or the filename).
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "measured"));
    // No Error.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, ""));
}

YR_TEST(scene_compiler_measured_anisotropic_degrades) {
    // Write an anisotropic .bsdf (n_phi_i=4 -> LoadMeasuredBrdf returns nullopt).
    const std::string path = yrtest::WriteSyntheticBsdf("sc_measured_aniso.bsdf", 4);

    const yr::PbrtScene pbrt = MakeSceneWithMeasured(".", "sc_measured_aniso.bsdf");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene->materials.empty());

    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::Conductor);

    // A Warning must be present mentioning "measured".
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "measured"));
    // No Error.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, ""));

    std::remove(path.c_str());
}
