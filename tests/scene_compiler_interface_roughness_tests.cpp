#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene with a coatedconductor material that
// declares "float interface.roughness" (the PBRT v4 param name for coat roughness).
yr::PbrtScene MakeSceneWithInterfaceRoughness(float roughness_value) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "interface_roughness_test.pbrt";
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
    mat.type = "coatedconductor";
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.eta", {0.2f, 0.2f, 0.2f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.k", {1.0f, 1.0f, 1.0f}, {}, {}, {}});
    // Use the PBRT v4 param name for coat roughness: "interface.roughness"
    mat.params.push_back(yr::PbrtParam{"float", "interface.roughness", {roughness_value}, {}, {}, {}});
    pbrt.named_materials["coated"] = mat;

    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    sphere.material_name = "coated";
    pbrt.shapes.push_back(sphere);

    return pbrt;
}

// Same but using the legacy param name "roughness" (without "interface." prefix).
yr::PbrtScene MakeSceneWithLegacyRoughness(float roughness_value) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "legacy_roughness_test.pbrt";
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
    mat.type = "coatedconductor";
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.eta", {0.2f, 0.2f, 0.2f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.k", {1.0f, 1.0f, 1.0f}, {}, {}, {}});
    // Legacy param name (no "interface." prefix)
    mat.params.push_back(yr::PbrtParam{"float", "roughness", {roughness_value}, {}, {}, {}});
    pbrt.named_materials["coated"] = mat;

    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    sphere.material_name = "coated";
    pbrt.shapes.push_back(sphere);

    return pbrt;
}

} // namespace

// The "interface.roughness" param must be read and stored as coating_roughness.
YR_TEST(scene_compiler_coatedconductor_reads_interface_roughness) {
    const yr::PbrtScene pbrt = MakeSceneWithInterfaceRoughness(0.02f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    YR_EXPECT_TRUE(!result.scene->materials.empty());
    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::CoatedConductor);

    // coating_roughness must reflect the interface.roughness value
    YR_EXPECT_NEAR(m.coating_roughness.value, 0.02f, 1e-5f);
}

// A non-zero interface.roughness must differ from the default 0.
YR_TEST(scene_compiler_coatedconductor_interface_roughness_nonzero_when_set) {
    const yr::PbrtScene pbrt = MakeSceneWithInterfaceRoughness(0.15f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    const yr::RenderMaterial& m = result.scene->materials.front();
    // Must be 0.15, not 0 (which would indicate the param was ignored)
    YR_EXPECT_NEAR(m.coating_roughness.value, 0.15f, 1e-5f);
}

// The legacy "roughness" param must still work as a fallback.
YR_TEST(scene_compiler_coatedconductor_legacy_roughness_still_works) {
    const yr::PbrtScene pbrt = MakeSceneWithLegacyRoughness(0.08f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    const yr::RenderMaterial& m = result.scene->materials.front();
    // The legacy "roughness" should still be accepted
    YR_EXPECT_NEAR(m.coating_roughness.value, 0.08f, 1e-5f);
}

// When both "interface.roughness" and "roughness" are present,
// "interface.roughness" takes precedence.
YR_TEST(scene_compiler_coatedconductor_interface_roughness_takes_precedence) {
    yr::PbrtScene pbrt = MakeSceneWithInterfaceRoughness(0.05f);
    // Also add the legacy param with a different value
    pbrt.named_materials["coated"].params.push_back(
        yr::PbrtParam{"float", "roughness", {0.99f}, {}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    const yr::RenderMaterial& m = result.scene->materials.front();
    // interface.roughness (0.05) must win over roughness (0.99)
    YR_EXPECT_NEAR(m.coating_roughness.value, 0.05f, 1e-5f);
}

// Compilation succeeds — no errors.
YR_TEST(scene_compiler_coatedconductor_interface_roughness_no_errors) {
    const yr::PbrtScene pbrt = MakeSceneWithInterfaceRoughness(0.02f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
}
