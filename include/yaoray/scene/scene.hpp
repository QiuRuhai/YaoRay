#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

enum class RenderBackendKind {
    Cpu,
    Cuda,
};

enum class RenderIntegratorKind {
    DebugDirect,
    Path,
};

enum class ToneMapperKind {
    None,
    Reinhard,
    Aces,
};

enum class CameraKind {
    Perspective,
};

enum class LightKind {
    Area,
};

enum class EnvironmentKind {
    None,
    Constant,
    Hdri,
};

struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
};

struct FilmSettings {
    std::filesystem::path output;
    ToneMapperKind tone_mapper = ToneMapperKind::Aces;
    float exposure = 0.0f;
    int checkpoint_interval_s = 0;
    std::filesystem::path checkpoint_path;
};

struct CameraDescription {
    CameraKind type = CameraKind::Perspective;
    Point3f position;
    Point3f target;
    float fov_y = 45.0f;
    float aperture = 0.0f;
    float focus_distance = 1.0f;
};

struct QuadDescription {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Point3f p3;
};

struct AssetDescription {
    std::string name;
    std::filesystem::path path;
    std::vector<QuadDescription> quads;
};

struct TransformDescription {
    Vec3f translate;
    Vec3f rotate_degrees;
    Vec3f scale{1.0f, 1.0f, 1.0f};
};

struct MaterialDescription {
    std::string name;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};

struct InstanceDescription {
    std::string asset;
    TransformDescription transform;
    std::string material;
};

struct AreaLightDescription {
    Point3f position;
    std::array<float, 2> size{1.0f, 1.0f};
    Color3f radiance{1.0f, 1.0f, 1.0f};
};

struct LightDescription {
    LightKind type = LightKind::Area;
    AreaLightDescription area;
};

struct EnvironmentDescription {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    std::filesystem::path path;
    float strength = 1.0f;
};

struct SceneDescription {
    std::filesystem::path source_path;
    RenderSettings render;
    FilmSettings film;
    std::optional<CameraDescription> camera;
    std::vector<AssetDescription> assets;
    std::vector<MaterialDescription> materials;
    std::vector<InstanceDescription> instances;
    std::vector<LightDescription> lights;
    EnvironmentDescription environment;
};

std::string_view RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name);
std::string_view RenderIntegratorName(RenderIntegratorKind integrator);
std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name);

std::string_view ToneMapperName(ToneMapperKind mapper);
std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name);

std::string_view CameraKindName(CameraKind kind);
std::optional<CameraKind> ParseCameraKindName(std::string_view name);

std::string_view LightKindName(LightKind kind);
std::optional<LightKind> ParseLightKindName(std::string_view name);

std::string_view EnvironmentKindName(EnvironmentKind kind);
std::optional<EnvironmentKind> ParseEnvironmentKindName(std::string_view name);

} // namespace yr
