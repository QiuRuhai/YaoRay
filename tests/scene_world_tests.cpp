#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

#include <yaoray/scene/scene.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace {

yr::SceneDescription MakeTomlStyleScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated.toml";
    scene.render.backend = yr::RenderBackendKind::Cpu;
    scene.render.integrator = yr::RenderIntegratorKind::Path;
    scene.render.width = 64;
    scene.render.height = 32;
    scene.render.spp = 2;
    scene.film.output = "out/generated.png";
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 1.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 1.0f, 0.0f};

    scene.materials.push_back(yr::MaterialDescription{
        "white",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.7f, 0.7f, 0.7f},
        yr::Color3f{}
    });
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "white";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    scene.instances.push_back(instance);

    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{0.0f, 3.0f, 0.0f};
    light.area.size = {2.0f, 2.0f};
    light.area.radiance = yr::Color3f{8.0f, 7.0f, 6.0f};
    scene.lights.push_back(light);
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.01f, 0.02f, 0.03f};
    return scene;
}

} // namespace

YR_TEST(scene_world_defaults_are_empty_frontend_output) {
    const yr::SceneWorld world;

    YR_EXPECT_TRUE(world.source_path.empty());
    YR_EXPECT_TRUE(world.source_root.empty());
    YR_EXPECT_EQ(world.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_TRUE(!world.camera.has_value());
    YR_EXPECT_TRUE(world.assets.empty());
    YR_EXPECT_TRUE(world.materials.empty());
    YR_EXPECT_TRUE(world.instances.empty());
    YR_EXPECT_TRUE(world.lights.empty());
}

YR_TEST(scene_world_adapter_preserves_toml_scene_fields) {
    const yr::SceneDescription scene = MakeTomlStyleScene();

    const yr::SceneWorld world = yr::BuildSceneWorld(scene);

    YR_EXPECT_EQ(world.source_path.generic_string(), scene.source_path.generic_string());
    YR_EXPECT_EQ(world.source_root.generic_string(), scene.source_path.parent_path().generic_string());
    YR_EXPECT_EQ(world.render.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(world.render.width, 64);
    YR_EXPECT_EQ(world.render.height, 32);
    YR_EXPECT_EQ(world.film.output.generic_string(), std::string{"out/generated.png"});
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.y, 1.0, 1e-6);
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.materials[0].name, std::string{"white"});
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].name, std::string{"triangle"});
    YR_EXPECT_EQ(world.assets[0].path.generic_string(), std::string{"builtin:triangle"});
    YR_EXPECT_TRUE(world.assets[0].meshes.empty());
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
    YR_EXPECT_EQ(world.instances[0].asset, std::string{"triangle"});
    YR_EXPECT_EQ(world.instances[0].material, std::string{"white"});
    YR_EXPECT_NEAR(world.instances[0].transform.translate.z, 3.0, 1e-6);
    YR_EXPECT_EQ(world.lights.size(), std::size_t{1});
    YR_EXPECT_NEAR(world.lights[0].area.radiance.x, 8.0, 1e-6);
    YR_EXPECT_EQ(world.environment.type, yr::EnvironmentKind::Constant);
}
