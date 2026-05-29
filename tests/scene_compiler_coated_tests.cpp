#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

namespace {

yr::PbrtScene MakeSceneWithCoatedConductor() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "coated.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Named material with coatedconductor parameters.
    yr::PbrtEntity mat;
    mat.type = "coatedconductor";
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.eta", {0.2f, 0.92f, 1.1f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"rgb", "conductor.k", {3.9f, 2.45f, 2.14f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "thickness", {0.02f}, {}, {}, {}});
    pbrt.named_materials["coated_test"] = mat;

    // A sphere referencing the named material.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.material_name = "coated_test";
    pbrt.shapes.push_back(shape);

    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_coatedconductor_reads_thickness) {
    const yr::PbrtScene pbrt = MakeSceneWithCoatedConductor();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!result.scene->materials.empty());
    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::CoatedConductor);
    YR_EXPECT_NEAR(m.coat_thickness, 0.02f, 1e-6f);
}

YR_TEST(scene_compiler_coatedconductor_derives_f0_from_eta_k) {
    // Schlick f0 = ((eta-1)^2 + k^2) / ((eta+1)^2 + k^2) per channel.
    const yr::PbrtScene pbrt = MakeSceneWithCoatedConductor();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials.front();
    auto f0 = [](float eta, float k) {
        const float num = (eta - 1.0f) * (eta - 1.0f) + k * k;
        const float den = (eta + 1.0f) * (eta + 1.0f) + k * k;
        return num / den;
    };
    YR_EXPECT_NEAR(m.reflectance.value.x, f0(0.2f, 3.9f), 1e-4f);
    YR_EXPECT_NEAR(m.reflectance.value.y, f0(0.92f, 2.45f), 1e-4f);
    YR_EXPECT_NEAR(m.reflectance.value.z, f0(1.1f, 2.14f), 1e-4f);
}
