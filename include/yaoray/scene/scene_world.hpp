#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneWorldMesh {
    std::string material;
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> texcoords0;
    std::vector<std::uint32_t> indices;
};

struct SceneWorldAsset {
    std::string name;
    std::filesystem::path path;
    std::vector<QuadDescription> quads;
    std::vector<SceneWorldMesh> meshes;
};

struct SceneWorldInstance {
    std::string asset;
    TransformDescription transform;
    std::string material;
};

struct SceneWorld {
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    RenderSettings render;
    FilmSettings film;
    OfflineSettings offline;
    std::optional<CameraDescription> camera;
    std::vector<SceneWorldAsset> assets;
    std::vector<MaterialDescription> materials;
    std::vector<SceneWorldInstance> instances;
    std::vector<LightDescription> lights;
    EnvironmentDescription environment;
};

SceneWorld BuildSceneWorld(const SceneDescription& scene);

} // namespace yr
