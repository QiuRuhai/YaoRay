#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
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

} // namespace

YR_TEST(pbrt_frontend_loads_minimal_triangle_scene_world) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneWorld& world = result.scene.value();
    YR_EXPECT_EQ(world.render.width, 64);
    YR_EXPECT_EQ(world.render.height, 32);
    YR_EXPECT_EQ(world.render.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.z, 3.0, 1e-6);
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.materials[0].name, std::string{"white"});
    YR_EXPECT_EQ(world.materials[0].type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(world.materials[0].albedo.x, 0.7, 1e-6);
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes[0].positions.size(), std::size_t{3});
    YR_EXPECT_EQ(world.assets[0].meshes[0].indices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_minimal_triangle_compiles_to_render_scene) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompileSceneWorld(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    YR_EXPECT_EQ(compiled.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.scene.value().materials.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_reports_missing_file) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/no_such_scene.pbrt"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "PBRT file not found"));
}

YR_TEST(pbrt_frontend_resolves_include_with_transform_and_material_scope) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/include_root.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneWorld& world = load.scene.value();
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes[0].material, std::string{"red"});
    YR_EXPECT_NEAR(world.assets[0].meshes[0].positions[0].x, 1.0, 1e-6);

    const yr::SceneCompileResult compiled = yr::CompileSceneWorld(world);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    if (!compiled.scene.has_value()) {
        return;
    }
    const yr::RenderSceneIR& scene = compiled.scene.value();
    YR_EXPECT_EQ(scene.triangles.size(), std::size_t{1});
    YR_EXPECT_NEAR(scene.triangles[0].p0.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.materials[scene.triangles[0].material_index].albedo.x, 1.0, 1e-6);
}

YR_TEST(pbrt_frontend_loads_plymesh_shape) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/ply_scene.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneWorld& world = load.scene.value();
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    const yr::SceneWorldMesh& mesh = world.assets[0].meshes[0];
    YR_EXPECT_EQ(mesh.material, std::string{"plymat"});
    YR_EXPECT_EQ(mesh.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(mesh.normals.size(), std::size_t{3});
    YR_EXPECT_EQ(mesh.texcoords0.size(), std::size_t{3});
    YR_EXPECT_EQ(mesh.indices.size(), std::size_t{3});

    const yr::SceneCompileResult compiled = yr::CompileSceneWorld(world);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    if (!compiled.scene.has_value()) {
        return;
    }
    const yr::RenderTriangle& triangle = compiled.scene->triangles[0];
    YR_EXPECT_TRUE(triangle.has_uv);
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
}

YR_TEST(pbrt_frontend_builds_camera_from_active_transform_and_resets_world_transform) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/camera_transform.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(load.diagnostics));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneWorld& world = load.scene.value();
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.y, 2.0, 1e-6);
    YR_EXPECT_NEAR(world.camera->position.z, 5.0, 1e-6);
    YR_EXPECT_NEAR(world.camera->target.z, 6.0, 1e-6);
    YR_EXPECT_NEAR(world.camera->fov_y, 30.0, 1e-6);
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_NEAR(world.assets[0].meshes[0].positions[0].x, 0.0, 1e-6);
    YR_EXPECT_NEAR(world.assets[0].meshes[0].positions[0].y, 0.0, 1e-6);
    YR_EXPECT_NEAR(world.assets[0].meshes[0].positions[0].z, 0.0, 1e-6);
}
