#pragma once

#include <stdexcept>
#include <utility>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/runtime/render_job.hpp>

namespace yr::test_support {

inline CpuPreparedScene PreparePathScene(RenderJob job) {
    CpuPrepareResult prepared = PrepareCpuScene(std::move(job));
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

inline CpuPathTraceResult RunPathTrace(RenderJob job) {
    return RenderCpuPathTrace(PreparePathScene(std::move(job)));
}

inline CpuPathTraceResult RunPathTrace(RenderJob job, const RenderRequest& request) {
    return RenderCpuPathTrace(PreparePathScene(std::move(job)), request);
}

inline RenderJob MakeBasePathJob(int width, int height) {
    RenderJob job;
    job.settings.width = width;
    job.settings.height = height;
    job.settings.spp = 1;
    job.settings.max_depth = 1;
    job.settings.seed = 7;

    RenderSceneIR& scene = job.scene;
    scene.camera.origin = Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.8f;
    scene.environment.active = false;
    scene.environment.radiance = Color3f{0.02f, 0.03f, 0.04f};
    scene.environment.strength = 1.0f;

    RenderMaterial material;
    material.kind = RenderMaterialKind::Diffuse;
    material.reflectance = TexParam3f{{0.8f, 0.8f, 0.8f}};
    scene.materials.push_back(material);
    scene.vertices = {
        RenderVertex{Point3f{-1.5f, -1.0f, 0.0f}, Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        RenderVertex{Point3f{ 1.5f, -1.0f, 0.0f}, Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        RenderVertex{Point3f{ 0.0f,  1.25f, 0.0f}, Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(RenderPrimitive{0, 3, 0, true, false, false});
    return job;
}

inline RenderJob MakeLitSphereJob() {
    RenderJob job;
    job.settings.width = 32;
    job.settings.height = 32;
    job.settings.spp = 8;
    job.settings.max_depth = 2;

    RenderSceneIR& scene = job.scene;
    scene.camera.origin = Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    RenderMaterial diffuse;
    diffuse.kind = RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = Color3f{0.8f, 0.8f, 0.8f};
    scene.materials.push_back(diffuse);

    RenderSphere sphere;
    sphere.center = Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);
    return job;
}

} // namespace yr::test_support
