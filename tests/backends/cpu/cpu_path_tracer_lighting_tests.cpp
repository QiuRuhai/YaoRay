#include "cpu_path_tracer_test_support.hpp"
#include "yr_test.hpp"

#include <utility>

YR_TEST(cpu_path_tracer_emissive_surface_contributes_radiance) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(3, 3);
    job.scene.environment.radiance = yr::Color3f{};
    job.scene.materials[0].reflectance = yr::TexParam3f{{}};
    job.scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};

    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}

YR_TEST(cpu_path_tracer_renders_sphere_in_an_emissive_room) {
    yr::RenderJob job = yr::test_support::MakeLitSphereJob();
    job.settings.max_depth = 3;
    yr::RenderSceneIR& scene = job.scene;
    scene.materials[0].reflectance.value = yr::Color3f{0.8f, 0.4f, 0.2f};

    yr::RenderMaterial emissive;
    emissive.kind = yr::RenderMaterialKind::Diffuse;
    emissive.emission = yr::Color3f{5.0f, 5.0f, 5.0f};
    scene.materials.push_back(emissive);
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2, 0, 2, 3};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 1, true, false, false});
    scene.emissive_primitives.push_back(yr::EmissivePrimitive{
        0, yr::Color3f{5.0f, 5.0f, 5.0f}, 16.0f});

    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y < center.x);
    YR_EXPECT_TRUE(center.z < center.x);
}

YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_point_light) {
    yr::RenderJob job = yr::test_support::MakeLitSphereJob();
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{0.0f, 2.0f, 1.0f};
    light.intensity = yr::Color3f{40.0f, 40.0f, 40.0f};
    job.scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
}

YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_distant_light) {
    yr::RenderJob job = yr::test_support::MakeLitSphereJob();
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{5.0f, 5.0f, 5.0f};
    job.scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    const yr::Color3f top = result.film.LinearPixel(16, 13);
    const yr::Color3f bottom = result.film.LinearPixel(16, 19);
    YR_EXPECT_TRUE(top.x > bottom.x);
}
