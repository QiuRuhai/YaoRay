#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/core/transform.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct PbrtParam {
    std::string type;
    std::string name;
    std::vector<float> floats;
    std::vector<int> ints;
    std::vector<std::string> strings;
    std::vector<bool> bools;
};

struct PbrtEntity {
    std::string type;
    std::vector<PbrtParam> params;
    std::filesystem::path source_root;
};

struct PbrtShapeRecord {
    PbrtEntity shape;
    std::string material_name;
    std::optional<PbrtEntity> inline_material;
    std::optional<PbrtEntity> area_light;
    Mat4f object_to_world;
};

struct PbrtLightRecord {
    PbrtEntity light;
    Mat4f light_to_world;
};

struct PbrtObjectInstance {
    std::string name;
    Mat4f instance_to_world;
};

struct PbrtScene {
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    std::vector<std::filesystem::path> source_roots;

    PbrtEntity camera;
    PbrtEntity sampler;
    PbrtEntity integrator;
    PbrtEntity film;
    PbrtEntity filter;
    Mat4f camera_transform;

    std::unordered_map<std::string, PbrtEntity> named_materials;
    std::unordered_map<std::string, PbrtEntity> named_textures;
    std::vector<PbrtShapeRecord> shapes;
    std::vector<PbrtLightRecord> lights;

    std::unordered_map<std::string, std::vector<PbrtShapeRecord>> object_definitions;
    std::vector<PbrtObjectInstance> instances;
};

struct PbrtSceneLoadResult {
    std::optional<PbrtScene> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

PbrtSceneLoadResult LoadPbrtScene(const std::filesystem::path& path);

} // namespace yr
