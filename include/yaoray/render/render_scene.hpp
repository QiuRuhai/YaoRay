#pragma once

#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/bvh.hpp>
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
};

struct RenderMaterial {
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};

struct RenderTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    int material_index = 0;
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
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
    RenderBvh bvh;
};

} // namespace yr
