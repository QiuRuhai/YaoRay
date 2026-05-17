#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

void RebuildBvh(yr::RenderScene& scene) {
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
}

yr::RenderScene MakeDebugTriangleScene(int width = 5, int height = 5) {
    yr::RenderScene scene;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.05f, 0.10f, 0.15f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    RebuildBvh(scene);
    return scene;
}

void AddCenterLight(yr::RenderScene& scene, yr::Point3f position, yr::Color3f radiance) {
    scene.area_lights.push_back(yr::RenderAreaLight{
        position,
        1.0f,
        1.0f,
        radiance
    });
}

} // namespace

YR_TEST(cpu_debug_renderer_traces_one_ray_per_pixel) {
    const yr::RenderScene scene = MakeDebugTriangleScene(4, 3);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(result.stats.triangle_tests <= result.stats.rays_traced);
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
}

YR_TEST(cpu_debug_renderer_records_hits_and_misses) {
    const yr::RenderScene scene = MakeDebugTriangleScene(5, 5);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(result.stats.misses > 0);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(cpu_debug_renderer_shades_environment_misses) {
    yr::RenderScene scene = MakeDebugTriangleScene(2, 2);
    scene.triangles.clear();
    scene.bvh = yr::RenderBvh{};
    scene.materials.clear();
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_EQ(result.stats.hits, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{4});
    YR_EXPECT_NEAR(pixel.x, 0.4, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 0.6, 1e-6);
    YR_EXPECT_NEAR(pixel.z, 0.8, 1e-6);
}

YR_TEST(cpu_debug_renderer_uses_fallback_color_for_invalid_material_indices) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    scene.triangles[0].material_index = 99;
    scene.materials.clear();

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(center.x > 0.9f);
    YR_EXPECT_TRUE(center.y < 0.1f);
    YR_EXPECT_TRUE(center.z > 0.9f);
}

YR_TEST(cpu_debug_renderer_uses_bvh_to_reduce_triangle_tests) {
    yr::RenderScene scene = MakeDebugTriangleScene(5, 5);
    scene.triangles.clear();
    for (int i = 0; i < 5; ++i) {
        const float x = static_cast<float>(i) * 10.0f;
        scene.triangles.push_back(yr::RenderTriangle{
            yr::Point3f{x - 0.25f, -0.25f, 0.0f},
            yr::Point3f{x + 0.25f, -0.25f, 0.0f},
            yr::Point3f{x, 0.25f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        });
    }
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_TRUE(result.stats.triangle_tests < result.stats.rays_traced * std::uint64_t{scene.triangles.size()});
}

YR_TEST(cpu_debug_renderer_adds_material_emission_on_hits) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_lights_front_facing_diffuse_hits) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    AddCenterLight(scene, yr::Point3f{0.0f, 0.0f, 2.0f}, yr::Color3f{4.0f, 4.0f, 4.0f});

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 0.2, 1e-5);
    YR_EXPECT_NEAR(center.z, 0.1, 1e-5);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_ignores_lights_behind_surface) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    AddCenterLight(scene, yr::Point3f{0.0f, 0.0f, -2.0f}, yr::Color3f{10.0f, 10.0f, 10.0f});

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_shadow_ray_blocks_direct_light) {
    yr::RenderScene unblocked = MakeDebugTriangleScene(3, 3);
    AddCenterLight(unblocked, yr::Point3f{0.0f, 2.0f, 2.0f}, yr::Color3f{8.0f, 8.0f, 8.0f});

    yr::RenderScene blocked = unblocked;
    blocked.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.75f, 1.0f, 0.25f},
        yr::Point3f{0.75f, 1.0f, 0.25f},
        yr::Point3f{0.0f, 1.0f, 1.75f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        0
    });
    RebuildBvh(blocked);

    const yr::CpuDebugRenderResult unblocked_result = yr::RenderCpuDebug(unblocked);
    const yr::CpuDebugRenderResult blocked_result = yr::RenderCpuDebug(blocked);
    const yr::Color3f unblocked_center = unblocked_result.film.LinearPixel(1, 1);
    const yr::Color3f blocked_center = blocked_result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(unblocked_center.x > 0.5f);
    YR_EXPECT_TRUE(blocked_center.x < unblocked_center.x * 0.1f);
    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}

YR_TEST(cpu_debug_renderer_does_not_treat_visible_light_panel_as_shadow_occluder_at_large_scale) {
    yr::RenderScene scene;
    scene.width = 9;
    scene.height = 9;
    scene.spp = 1;
    scene.camera.origin = yr::Point3f{1'000'000.0f, 1'000.0f, 1'000'000.0f};
    scene.camera.forward = yr::Vec3f{0.0f, -1.0f, 0.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 0.0f, 1.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{}});
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{}, yr::Color3f{1.0f, 1.0f, 1.0f}});

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{999'000.0f, 0.0f, 999'000.0f},
        yr::Point3f{1'001'000.0f, 0.0f, 999'000.0f},
        yr::Point3f{1'001'000.0f, 0.0f, 1'001'000.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{999'000.0f, 0.0f, 999'000.0f},
        yr::Point3f{1'001'000.0f, 0.0f, 1'001'000.0f},
        yr::Point3f{999'000.0f, 0.0f, 1'001'000.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{999'935.0f, 2'000.0f, 999'947.5f},
        yr::Point3f{1'000'065.0f, 2'000.0f, 999'947.5f},
        yr::Point3f{1'000'065.0f, 2'000.0f, 1'000'052.5f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{999'935.0f, 2'000.0f, 999'947.5f},
        yr::Point3f{1'000'065.0f, 2'000.0f, 1'000'052.5f},
        yr::Point3f{999'935.0f, 2'000.0f, 1'000'052.5f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{1'000'000.0f, 2'000.0f, 1'000'000.0f},
        130.0f,
        105.0f,
        yr::Color3f{10.0f, 10.0f, 10.0f}
    });
    RebuildBvh(scene);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(4, 4);

    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(center.x > 0.0f);
}
