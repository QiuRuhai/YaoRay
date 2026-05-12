#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

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
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

} // namespace

YR_TEST(cpu_debug_renderer_traces_one_ray_per_pixel) {
    const yr::RenderScene scene = MakeDebugTriangleScene(4, 3);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.triangle_tests, std::uint64_t{12});
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
