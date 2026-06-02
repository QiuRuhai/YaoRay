#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene with one sphere that uses a "subsurface"
// named material with explicit sigma_a, sigma_s, and eta parameters.
yr::PbrtScene MakeSceneWithSubsurface(
    const std::string& sigma_a,
    const std::string& sigma_s,
    float eta
) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "subsurface_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtEntity mat;
    mat.type = "subsurface";
    // sigma_a: "0.0011 0.0024 0.014" as three floats
    mat.params.push_back(yr::PbrtParam{"rgb", "sigma_a",
        {0.0011f, 0.0024f, 0.014f}, {}, {}, {}});
    // sigma_s: "2.55 3.21 3.77" as three floats
    mat.params.push_back(yr::PbrtParam{"rgb", "sigma_s",
        {2.55f, 3.21f, 3.77f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "eta", {eta}, {}, {}, {}});
    pbrt.named_materials["subsurface_mat"] = mat;

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.material_name = "subsurface_mat";
    pbrt.shapes.push_back(shape);

    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_subsurface_compiles_to_subsurface_kind) {
    const yr::PbrtScene pbrt = MakeSceneWithSubsurface(
        "0.0011 0.0024 0.014",
        "2.55 3.21 3.77",
        1.33f
    );
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene->materials.empty());

    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::Subsurface);
    YR_EXPECT_TRUE(m.bssrdf_table != nullptr);
    YR_EXPECT_TRUE(m.sigma_s.x > 0.0f);
    YR_EXPECT_NEAR(m.bssrdf_eta, 1.33f, 1e-5f);
    YR_EXPECT_EQ(static_cast<int>(result.scene->bssrdf_tables.size()), 1);
}

YR_TEST(scene_compiler_subsurface_skin1_preset_with_scale) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "subsurface_skin1_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtEntity mat;
    mat.type = "subsurface";
    mat.params.push_back(yr::PbrtParam{"string", "name", {}, {}, {"Skin1"}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "scale", {50.0f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "eta", {1.5f}, {}, {}, {}});
    pbrt.named_materials["skin1_mat"] = mat;

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.material_name = "skin1_mat";
    pbrt.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene->materials.empty());

    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::Subsurface);
    YR_EXPECT_TRUE(m.bssrdf_table != nullptr);
    YR_EXPECT_NEAR(m.bssrdf_eta, 1.5f, 1e-6f);
    // Skin1 sigma_s.x = 0.74, scale = 50 -> 37.0
    YR_EXPECT_NEAR(m.sigma_s.x, 37.0f, 1e-2f);
    // Skin1 sigma_a.x = 0.032, scale = 50 -> 1.6
    YR_EXPECT_NEAR(m.sigma_a.x, 1.6f, 1e-2f);
}
