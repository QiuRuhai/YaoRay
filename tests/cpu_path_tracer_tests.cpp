#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Rewrite path tracer tests for table-geometry + PBRT material model.

namespace {

yr::CpuPreparedScene PreparePathScene(yr::RenderSceneIR scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(std::move(scene));
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}

yr::CpuPathTraceResult RunPathTrace(yr::RenderSceneIR scene) {
    return yr::RenderCpuPathTrace(PreparePathScene(std::move(scene)));
}

yr::CpuPathTraceResult RunPathTrace(yr::RenderSceneIR scene, const yr::RenderRequest& request) {
    return yr::RenderCpuPathTrace(PreparePathScene(std::move(scene)), request);
}

yr::RenderSceneIR MakeBaseScene(int width, int height) {
    yr::RenderSceneIR scene;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = 7;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.8f;
    scene.environment.active = false;
    scene.environment.radiance = yr::Color3f{0.02f, 0.03f, 0.04f};
    scene.environment.strength = 1.0f;
    yr::RenderMaterial mat;
    mat.kind = yr::RenderMaterialKind::Diffuse;
    mat.reflectance = yr::TexParam3f{{0.8f, 0.8f, 0.8f}};
    scene.materials.push_back(mat);
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.5f, -1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.5f, -1.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.0f,  1.25f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    return scene;
}

} // namespace

YR_TEST(cpu_path_tracer_traces_one_sample_per_pixel) {
    yr::RenderSceneIR scene = MakeBaseScene(4, 3);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));

    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_accumulates_spp_samples) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 4;

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));

    YR_EXPECT_EQ(result.film.Width(), 2);
    YR_EXPECT_EQ(result.film.Height(), 2);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 4);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{16});
}

YR_TEST(cpu_path_tracer_progress_callback_can_cancel_render) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;

    yr::RenderRequest request;
    request.progress_callback = [](const yr::RenderProgress&, const yr::Film&) {
        return yr::RenderProgressDecision{true, "stop after first pass"};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene), request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("stop after first pass") != std::string::npos);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
}

YR_TEST(cpu_path_tracer_reports_progress_after_each_sample_pass) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;
    std::vector<int> completed;

    yr::RenderRequest request;
    request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
        completed.push_back(progress.completed_spp);
        YR_EXPECT_EQ(progress.target_spp, 3);
        YR_EXPECT_EQ(film.SampleCount(0, 0), static_cast<std::uint32_t>(progress.completed_spp));
        return yr::RenderProgressDecision{};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene), request);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(completed.size(), std::size_t{3});
    YR_EXPECT_EQ(completed[0], 1);
    YR_EXPECT_EQ(completed[1], 2);
    YR_EXPECT_EQ(completed[2], 3);
}

YR_TEST(cpu_path_tracer_emissive_surface_contributes_radiance) {
    yr::RenderSceneIR scene = MakeBaseScene(3, 3);
    scene.environment.radiance = yr::Color3f{};
    scene.materials[0].reflectance = yr::TexParam3f{{}};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}

YR_TEST(cpu_path_tracer_renders_sphere_in_an_emissive_room) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 3;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    // Diffuse sphere material.
    yr::RenderMaterial diffuse;
    diffuse.kind = yr::RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = yr::Color3f{0.8f, 0.4f, 0.2f};
    scene.materials.push_back(diffuse);

    // Emissive material for a ceiling quad.
    yr::RenderMaterial emissive;
    emissive.kind = yr::RenderMaterialKind::Diffuse;
    emissive.emission = yr::Color3f{5.0f, 5.0f, 5.0f};
    scene.materials.push_back(emissive);

    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);

    // Big emissive ceiling at y = 2.
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2,  0, 2, 3};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 1, true, false, false});

    yr::EmissivePrimitive ep;
    ep.primitive_index = 0;
    ep.radiance = yr::Color3f{5.0f, 5.0f, 5.0f};
    ep.area = 16.0f;
    scene.emissive_primitives.push_back(ep);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    // The center pixel must see the sphere (not the ceiling and not a miss).
    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
    // The sphere material is orange (0.8, 0.4, 0.2). After reflecting the white
    // ceiling light, the pixel should keep a clear orange tint — green and blue
    // should both be noticeably below red. If the sphere were not hit and the
    // ray fell through to the white ceiling, this check would fail.
    YR_EXPECT_TRUE(center.y < center.x);
    YR_EXPECT_TRUE(center.z < center.x);
}

YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_point_light) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 2;  // direct only (no need for indirect to see the light)
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    yr::RenderMaterial diffuse;
    diffuse.kind = yr::RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = yr::Color3f{0.8f, 0.8f, 0.8f};
    scene.materials.push_back(diffuse);

    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);

    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{0.0f, 2.0f, 1.0f};
    light.intensity = yr::Color3f{40.0f, 40.0f, 40.0f};
    scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    // The pixel at the sphere center must receive non-zero direct illumination.
    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
}

YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_distant_light) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 2;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    yr::RenderMaterial diffuse;
    diffuse.kind = yr::RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = yr::Color3f{0.8f, 0.8f, 0.8f};
    scene.materials.push_back(diffuse);

    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);

    // Strong distant light from above so the top of the sphere is well-lit.
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};  // propagating down
    light.intensity = yr::Color3f{5.0f, 5.0f, 5.0f};
    scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);
    // The top of the sphere (pixel near (16, 13)) should be brighter than the
    // bottom (pixel near (16, 19)) under a downward-shining distant light.
    const yr::Color3f top = result.film.LinearPixel(16, 13);
    const yr::Color3f bottom = result.film.LinearPixel(16, 19);
    YR_EXPECT_TRUE(top.x > bottom.x);
}
