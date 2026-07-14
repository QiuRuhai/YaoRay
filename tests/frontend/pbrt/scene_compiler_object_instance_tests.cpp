#include "yr_test.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/accel/acceleration.hpp>

namespace {

yr::Mat4f Identity() {
    return yr::Mat4f{};
}

yr::Mat4f Translation(float x, float y, float z) {
    yr::Mat4f m{};
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "object_instance_test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = Identity();
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";
    return pbrt;
}

yr::PbrtShapeRecord SphereRecord() {
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = Identity();
    return shape;
}

yr::PbrtShapeRecord TriangleRecord() {
    yr::PbrtShapeRecord shape;
    shape.shape.type = "trianglemesh";
    shape.shape.params.push_back(yr::PbrtParam{
        "point3", "P", {-0.25f, -0.25f, 0.0f, 0.25f, -0.25f, 0.0f,
                         0.0f, 0.25f, 0.0f}, {}, {}, {}});
    shape.shape.params.push_back(
        yr::PbrtParam{"integer", "indices", {}, {0, 1, 2}, {}, {}});
    shape.object_to_world = Identity();
    return shape;
}

bool HasWarningContaining(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view needle
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == yr::DiagnosticSeverity::Warning &&
            diagnostic.message.find(std::string{needle}) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_object_instance_compiles_sphere_with_default_material) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.object_definitions["ball"].push_back(SphereRecord());
    pbrt.instances.push_back(yr::PbrtObjectInstance{"ball", Translation(3.0f, 0.0f, 0.0f)});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->spheres[0].material_index, 0);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.x, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].radius, 0.5f, 1.0e-6);
}

YR_TEST(scene_compiler_object_instance_undefined_material_warns_and_uses_default) {
    yr::PbrtScene pbrt = MinimalScene();
    yr::PbrtShapeRecord sphere = SphereRecord();
    sphere.material_name = "missing_material";
    pbrt.object_definitions["ball"].push_back(sphere);
    pbrt.instances.push_back(yr::PbrtObjectInstance{"ball", Translation(0.0f, 1.0f, 0.0f)});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningContaining(result.diagnostics, "undefined material: missing_material"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->spheres[0].material_index, 0);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.y, 1.0f, 1.0e-6);
}

YR_TEST(scene_compiler_object_instance_undefined_material_does_not_use_existing_named_material) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity red;
    red.type = "diffuse";
    red.params.push_back(yr::PbrtParam{"rgb", "reflectance", {1.0f, 0.0f, 0.0f}, {}, {}, {}});
    pbrt.named_materials["red"] = red;

    yr::PbrtShapeRecord sphere = SphereRecord();
    sphere.material_name = "missing_material";
    pbrt.object_definitions["ball"].push_back(sphere);
    pbrt.instances.push_back(yr::PbrtObjectInstance{"ball", Translation(0.0f, 0.0f, 2.0f)});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningContaining(result.diagnostics, "undefined material: missing_material"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_TRUE(result.scene->materials.size() >= std::size_t{2});
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});

    const int sphere_material_index = result.scene->spheres[0].material_index;
    YR_EXPECT_TRUE(sphere_material_index >= 0);
    YR_EXPECT_TRUE(static_cast<std::size_t>(sphere_material_index) < result.scene->materials.size());

    const yr::RenderMaterial& sphere_material =
        result.scene->materials[static_cast<std::size_t>(sphere_material_index)];
    YR_EXPECT_TRUE(sphere_material.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(sphere_material.reflectance.value.x, 0.5f, 1.0e-6);
    YR_EXPECT_NEAR(sphere_material.reflectance.value.y, 0.5f, 1.0e-6);
    YR_EXPECT_NEAR(sphere_material.reflectance.value.z, 0.5f, 1.0e-6);
}

YR_TEST(scene_compiler_mesh_instances_reuse_one_primitive) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.object_definitions["triangle"].push_back(TriangleRecord());
    pbrt.instances.push_back(
        yr::PbrtObjectInstance{"triangle", Translation(-1.0f, 0.0f, 0.0f)});
    pbrt.instances.push_back(
        yr::PbrtObjectInstance{"triangle", Translation(1.0f, 0.0f, 0.0f)});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_EQ(result.scene->primitives.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene->vertices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene->instances.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene->instances[0].primitive.Value(), 0);
    YR_EXPECT_EQ(result.scene->instances[1].primitive.Value(), 0);

    const yr::RenderAccelerationBuildResult acceleration =
        yr::BuildRenderAcceleration(result.scene->Geometry());
    yr::BvhTraceStats stats;
    const yr::BvhHit hit = yr::IntersectAcceleration(
        result.scene->Geometry(),
        acceleration.acceleration,
        yr::Ray3f{yr::Point3f{1.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}},
        stats);
    YR_EXPECT_TRUE(acceleration.errors.empty());
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.instance.Value(), 1);
}

YR_TEST(scene_compiler_keeps_direct_mesh_as_identity_instance) {
    yr::PbrtScene pbrt = MinimalScene();
    yr::PbrtShapeRecord direct = TriangleRecord();
    direct.object_to_world = Translation(3.0f, 0.0f, 0.0f);
    pbrt.shapes.push_back(direct);
    pbrt.object_definitions["triangle"].push_back(TriangleRecord());
    pbrt.instances.push_back(
        yr::PbrtObjectInstance{"triangle", Translation(-1.0f, 0.0f, 0.0f)});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_EQ(result.scene->primitives.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene->instances.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene->instances[0].primitive.Value(), 1);
    YR_EXPECT_EQ(result.scene->instances[1].primitive.Value(), 0);
    YR_EXPECT_NEAR(result.scene->instances[1].object_to_world.m[12], 0.0f, 1.0e-6f);
}
