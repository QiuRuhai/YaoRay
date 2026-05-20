#include "yr_test.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace {

yr::SceneDescription MakeBaseScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated.toml";
    scene.render.backend = yr::RenderBackendKind::Cuda;
    scene.render.integrator = yr::RenderIntegratorKind::Path;
    scene.render.sampler = yr::RenderSamplerKind::Stratified;
    scene.render.width = 320;
    scene.render.height = 180;
    scene.render.spp = 4;
    scene.render.max_depth = 6;
    scene.render.seed = std::uint64_t{123};
    scene.render.threads = 4;
    scene.render.light_samples = 4;
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera->fov_y = 60.0f;
    return scene;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

} // namespace

YR_TEST(render_scene_defaults_are_backend_friendly) {
    const yr::RenderScene scene;

    YR_EXPECT_EQ(scene.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.integrator, yr::RenderIntegratorKind::DebugDirect);
    YR_EXPECT_EQ(scene.sampler, yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(scene.width, 0);
    YR_EXPECT_EQ(scene.height, 0);
    YR_EXPECT_EQ(scene.spp, 1);
    YR_EXPECT_EQ(scene.max_depth, 5);
    YR_EXPECT_EQ(scene.seed, std::uint64_t{0});
    YR_EXPECT_EQ(scene.threads, 0);
    YR_EXPECT_EQ(scene.light_samples, 1);
    YR_EXPECT_TRUE(scene.triangles.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.area_lights.empty());
    YR_EXPECT_TRUE(scene.bvh.nodes.empty());
    YR_EXPECT_TRUE(scene.bvh.triangle_indices.empty());
    YR_EXPECT_EQ(scene.bvh.max_depth, 0);

    const yr::RenderMaterial material;
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
}

YR_TEST(scene_compiler_copies_render_settings) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.backend, yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(compiled.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(compiled.sampler, yr::RenderSamplerKind::Stratified);
    YR_EXPECT_EQ(compiled.width, 320);
    YR_EXPECT_EQ(compiled.height, 180);
    YR_EXPECT_EQ(compiled.spp, 4);
    YR_EXPECT_EQ(compiled.max_depth, 6);
    YR_EXPECT_EQ(compiled.seed, std::uint64_t{123});
    YR_EXPECT_EQ(compiled.threads, 4);
    YR_EXPECT_EQ(compiled.light_samples, 4);
}

YR_TEST(scene_compiler_builds_camera_basis) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderCamera& camera = result.scene.value().camera;
    YR_EXPECT_NEAR(camera.origin.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.z, 4.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.fov_y_radians, 1.04719758, 1e-6);
}

YR_TEST(scene_compiler_copies_constant_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.z, 0.4, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.strength, 2.0, 1e-6);
}

YR_TEST(scene_compiler_copies_area_lights) {
    yr::SceneDescription scene = MakeBaseScene();
    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.area.size = {4.0f, 5.0f};
    light.area.radiance = yr::Color3f{6.0f, 7.0f, 8.0f};
    scene.lights.push_back(light);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().area_lights.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].position.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].width, 4.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].height, 5.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].radiance.z, 8.0, 1e-6);
}

YR_TEST(scene_compiler_expands_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});

    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, -0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p0.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.p1.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-6);
    YR_EXPECT_EQ(triangle.material_index, 0);
}

YR_TEST(scene_compiler_compiles_named_materials) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "red",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.9f, 0.1f, 0.05f},
        yr::Color3f{0.0f, 0.0f, 0.0f}
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_NEAR(compiled.materials[0].albedo.x, 0.9, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.y, 0.1, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.z, 0.05, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[0].emission.x, 0.0, 1e-6);
}

YR_TEST(scene_compiler_copies_material_type) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "mirror",
        yr::MaterialKind::Mirror,
        yr::Color3f{0.95f, 0.95f, 0.95f},
        yr::Color3f{}
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials[0].type, yr::MaterialKind::Mirror);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.x, 0.95, 1e-6);
}

YR_TEST(scene_compiler_copies_material_scalars) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "plastic",
        yr::MaterialKind::Plastic,
        yr::Color3f{0.8f, 0.05f, 0.03f},
        yr::Color3f{},
        0.25f,
        0.08f
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials[0].type, yr::MaterialKind::Plastic);
    YR_EXPECT_NEAR(compiled.materials[0].roughness, 0.25, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[0].specular, 0.08, 1e-6);
}

YR_TEST(scene_compiler_binds_builtin_instance_to_named_material) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.materials.push_back(yr::MaterialDescription{
        "green",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.1f, 0.65f, 0.2f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "green";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.y, 0.65, 1e-6);
}

YR_TEST(scene_compiler_shares_named_material_between_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.materials.push_back(yr::MaterialDescription{
        "white",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.75f, 0.75f, 0.75f},
        yr::Color3f{}
    });
    yr::InstanceDescription first;
    first.asset = "triangle";
    first.material = "white";
    scene.instances.push_back(first);

    yr::InstanceDescription second;
    second.asset = "triangle";
    second.material = "white";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_EQ(compiled.triangles[1].material_index, 0);
}

YR_TEST(scene_compiler_preserves_default_material_for_unbound_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.materials.push_back(yr::MaterialDescription{
        "red",
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{}
    });
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 1);
    YR_EXPECT_EQ(compiled.materials[1].type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(compiled.materials[1].albedo.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[1].albedo.y, 0.8, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[1].albedo.z, 0.8, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[1].roughness, 0.0, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[1].specular, 0.04, 1e-6);
}

YR_TEST(scene_compiler_rejects_unknown_material_reference) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.materials.push_back(yr::MaterialDescription{
        "red",
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "missing";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "instances.material", "references unknown material"));
}

YR_TEST(scene_compiler_applies_builtin_triangle_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 2.0, 1e-5);
}

YR_TEST(scene_compiler_rejects_external_assets) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"model", "assets/model.glb"});
    scene.instances.push_back(yr::InstanceDescription{"model", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.path", "asset import not implemented yet"));
}

YR_TEST(scene_compiler_rejects_hdri_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.path = "assets/hdri/studio.hdr";

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.path", "HDRI environment import not implemented yet"));
}

YR_TEST(scene_compiler_expands_obj_asset) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_NEAR(result.scene.value().triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(scene_compiler_preserves_obj_vertex_normals) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/normal_triangle.obj")});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.x, 0.70710678, 1e-6);
}

YR_TEST(scene_compiler_transforms_obj_vertex_normals) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/normal_triangle.obj")});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 90.0f, 0.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_TRUE(triangle.n0.x > 0.99f);
    YR_EXPECT_NEAR(triangle.n0.z, 0.0, 1e-5);
}

YR_TEST(scene_compiler_imports_obj_material_texture_and_uvs) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/textured_quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.x, 0.25, 1e-6);
    YR_EXPECT_TRUE(compiled.triangles[0].has_uv);
    YR_EXPECT_NEAR(compiled.triangles[0].uv1.x, 1.0, 1e-6);
}

YR_TEST(scene_compiler_caches_duplicate_obj_textures) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"first", FixturePath("assets/textured_quad.obj")});
    scene.assets.push_back(yr::AssetDescription{"second", FixturePath("assets/textured_quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"first", {}});
    yr::InstanceDescription second;
    second.asset = "second";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
    YR_EXPECT_EQ(compiled.materials[1].albedo_texture, 0);
}

YR_TEST(scene_compiler_scene_material_overrides_imported_obj_material) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/textured_quad.obj")});
    scene.materials.push_back(yr::MaterialDescription{
        "override",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.9f, 0.1f, 0.2f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "quad";
    instance.material = "override";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_TRUE(compiled.textures.empty());
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, -1);
}

YR_TEST(scene_compiler_reports_missing_obj_texture) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"bad", FixturePath("assets/missing_texture.obj")});
    scene.instances.push_back(yr::InstanceDescription{"bad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.path", "texture file not found"));
}

YR_TEST(scene_compiler_expands_inline_quad_asset) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            }
        }
    });
    scene.materials.push_back(yr::MaterialDescription{
        "white",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.7f, 0.7f, 0.7f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "panel";
    instance.material = "white";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_EQ(compiled.triangles[1].material_index, 0);
    YR_EXPECT_NEAR(compiled.triangles[0].p0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].p1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p1.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(scene_compiler_expands_multiple_inline_quads) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panels",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            },
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 1.0f},
                yr::Point3f{1.0f, 0.0f, 1.0f},
                yr::Point3f{1.0f, 1.0f, 1.0f},
                yr::Point3f{0.0f, 1.0f, 1.0f}
            }
        }
    });
    scene.instances.push_back(yr::InstanceDescription{"panels", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{4});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_EQ(result.scene.value().triangles[2].material_index, 0);
}

YR_TEST(scene_compiler_applies_inline_quad_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            }
        }
    });
    yr::InstanceDescription instance;
    instance.asset = "panel";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 2.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 4.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 4.0, 1e-5);
}

YR_TEST(scene_compiler_rejects_degenerate_inline_quad) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "bad_panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f}
            }
        }
    });
    scene.instances.push_back(yr::InstanceDescription{"bad_panel", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.quads", "quad produces degenerate triangle"));
}

YR_TEST(scene_compiler_applies_obj_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/triangle.obj")});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});

    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 2.0, 1e-5);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-5);
}

YR_TEST(scene_compiler_expands_two_obj_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    yr::InstanceDescription second;
    second.asset = "quad";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{4});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_EQ(result.scene.value().triangles[2].material_index, 1);
    YR_EXPECT_NEAR(result.scene.value().triangles[2].p0.x, 1.5, 1e-6);
}

YR_TEST(scene_compiler_builds_empty_bvh_for_empty_scene) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene.value().triangles.empty());
    YR_EXPECT_TRUE(result.scene.value().bvh.nodes.empty());
    YR_EXPECT_TRUE(result.scene.value().bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.scene.value().bvh.max_depth, 0);
}

YR_TEST(scene_compiler_builds_bvh_for_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices[0], 0);
}

YR_TEST(scene_compiler_builds_bvh_for_obj_quad) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices.size(), std::size_t{2});
}
