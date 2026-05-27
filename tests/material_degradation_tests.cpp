#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

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
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.8f, 0.4f, 0.2f}, {}, {}, {}});
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

YR_TEST(material_degradation_emits_warning_for_subsurface) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("subsurface");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "subsurface"));
}

YR_TEST(material_degradation_emits_warning_for_unknown_kind) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("totally_made_up_material_name");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "totally_made_up_material_name"));
}

YR_TEST(material_degradation_falls_back_to_diffuse) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("hair");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->materials[0].kind == yr::RenderMaterialKind::Diffuse);
}
