#pragma once

#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/texture.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct RenderCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
    float aperture = 0.0f;
    float focus_distance = 1.0f;
};

struct RenderEnvironment {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
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

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

struct RenderTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    int material_index = 0;
    Vec2f uv0;
    Vec2f uv1;
    Vec2f uv2;
    bool has_uv = false;
    Vec3f n0;
    Vec3f n1;
    Vec3f n2;
    bool has_vertex_normals = false;
};

struct RenderAreaLight {
    Point3f position;
    float width = 1.0f;
    float height = 1.0f;
    Color3f radiance{1.0f, 1.0f, 1.0f};
};

struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    float radiance_clamp = 0.0f;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
    RenderBvh bvh;
};

} // namespace yr
