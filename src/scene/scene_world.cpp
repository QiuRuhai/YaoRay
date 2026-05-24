#include <yaoray/scene/scene_world.hpp>

#include <utility>

namespace yr {

SceneWorld BuildSceneWorld(const SceneDescription& scene) {
    SceneWorld world;
    world.source_path = scene.source_path;
    world.source_root = scene.source_path.parent_path();
    world.render = scene.render;
    world.film = scene.film;
    world.offline = scene.offline;
    world.camera = scene.camera;
    world.materials = scene.materials;
    world.lights = scene.lights;
    world.environment = scene.environment;

    world.assets.reserve(scene.assets.size());
    for (const AssetDescription& asset : scene.assets) {
        SceneWorldAsset world_asset;
        world_asset.name = asset.name;
        world_asset.path = asset.path;
        world_asset.quads = asset.quads;
        world.assets.push_back(std::move(world_asset));
    }

    world.instances.reserve(scene.instances.size());
    for (const InstanceDescription& instance : scene.instances) {
        SceneWorldInstance world_instance;
        world_instance.asset = instance.asset;
        world_instance.transform = instance.transform;
        world_instance.material = instance.material;
        world.instances.push_back(world_instance);
    }

    return world;
}

} // namespace yr
