#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <optional>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/texture.hpp>

namespace yr {

// --- Render enums (migrated from scene/scene.hpp) ---

enum class RenderBackendKind { Cpu, Cuda };
enum class RenderIntegratorKind { DebugDirect, Path };
enum class RenderSamplerKind { Independent, Stratified };
enum class ToneMapperKind { None, Reinhard, Aces };

std::string_view RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name);
std::string_view RenderIntegratorName(RenderIntegratorKind integrator);
std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name);
std::string_view RenderSamplerName(RenderSamplerKind sampler);
std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name);
std::string_view ToneMapperName(ToneMapperKind mapper);
std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name);

// --- Camera ---

struct RenderCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
};

// --- Environment ---

struct RenderEnvironment {
    bool active = false;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};

struct RenderEnvironmentDistribution {
    int width = 0;
    int height = 0;
    std::vector<float> texel_weights;
    std::vector<float> row_weights;
    std::vector<float> row_cdf;
    std::vector<float> conditional_cdfs;
    float total_weight = 0.0f;
    bool uniform = false;
};

// --- Materials ---

enum class RenderMaterialKind {
    Diffuse,
    Conductor,
    Dielectric,
    ThinDielectric,
    CoatedDiffuse,
    CoatedConductor,
    DiffuseTransmission,
    Mix,
};

struct TexParam1f {
    float value = 0.0f;
    int texture = -1;
};

struct TexParam3f {
    Color3f value{0.0f, 0.0f, 0.0f};
    int texture = -1;
};

struct RenderMaterial {
    RenderMaterialKind kind = RenderMaterialKind::Diffuse;

    TexParam3f reflectance{{0.5f, 0.5f, 0.5f}};

    TexParam3f eta;
    TexParam3f k;

    float ior = 1.5f;

    TexParam1f uroughness{0.0f};
    TexParam1f vroughness{0.0f};
    bool remap_roughness = true;

    int mix_material_a = -1;
    int mix_material_b = -1;
    TexParam1f mix_amount{0.5f};

    float coating_ior = 1.5f;
    TexParam1f coating_roughness{0.0f};

    int normal_map = -1;
    float normal_scale = 1.0f;

    Color3f emission{0.0f, 0.0f, 0.0f};

    TexParam1f alpha{1.0f};

    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

// --- Geometry ---

struct RenderVertex {
    Point3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent;
    float tangent_handedness = 1.0f;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool has_tangents = false;
};

// --- Lights ---

struct EmissivePrimitive {
    int primitive_index = 0;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float area = 0.0f;
};

enum class AnalyticLightKind { Point, Spot, Distant };

struct AnalyticLight {
    AnalyticLightKind kind = AnalyticLightKind::Point;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    float cone_angle = 0.0f;
};

// --- Film settings ---

struct FilmSettings {
    std::filesystem::path output;
    ToneMapperKind tone_mapper = ToneMapperKind::Aces;
    float exposure = 0.0f;
};

// --- Complete Render Scene IR ---

struct RenderSceneIR {
    RenderBackendKind requested_backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::Path;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    float radiance_clamp = 0.0f;

    RenderCamera camera;

    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;

    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;

    std::vector<EmissivePrimitive> emissive_primitives;
    RenderEnvironment environment;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<AnalyticLight> analytic_lights;

    FilmSettings film;
};

} // namespace yr
