#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

float Luminance(yr::Color3f color) {
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

bool ColorEqual(yr::Color3f a, yr::Color3f b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

yr::RenderScene MakeBaseScene(int width, int height) {
    yr::RenderScene scene;
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
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.02f, 0.03f, 0.04f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{0.8f, 0.8f, 0.8f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-1.5f, -1.0f, 0.0f},
        yr::Point3f{1.5f, -1.0f, 0.0f},
        yr::Point3f{0.0f, 1.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

yr::RenderScene MakeEmissiveTriangleScene() {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.environment.radiance = yr::Color3f{};
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};
    return scene;
}

yr::RenderScene MakeStochasticEdgeScene(std::uint64_t seed) {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.spp = 8;
    scene.seed = seed;
    scene.environment.radiance = yr::Color3f{0.0f, 0.0f, 0.0f};
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{1.0f, 0.25f, 0.125f};
    scene.triangles.clear();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.06f, -0.8f, 0.0f},
        yr::Point3f{0.9f, -0.8f, 0.0f},
        yr::Point3f{-0.06f, 0.8f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

yr::RenderScene MakeIndirectBounceScene(int max_depth) {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.max_depth = max_depth;
    scene.environment.radiance = yr::Color3f{0.4f, 0.5f, 0.6f};
    scene.environment.strength = 1.0f;
    scene.materials[0].albedo = yr::Color3f{0.9f, 0.9f, 0.9f};
    scene.area_lights.clear();
    return scene;
}

} // namespace

YR_TEST(cpu_path_tracer_traces_one_sample_per_pixel) {
    const yr::RenderScene scene = MakeBaseScene(4, 3);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_TRUE(result.stats.bvh_nodes > 0);
    YR_EXPECT_TRUE(result.stats.bvh_max_depth >= 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(cpu_path_tracer_accumulates_spp_samples) {
    yr::RenderScene scene = MakeBaseScene(2, 2);
    scene.spp = 4;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.film.Width(), 2);
    YR_EXPECT_EQ(result.film.Height(), 2);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 4);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{16});
}

YR_TEST(cpu_path_tracer_sees_emissive_surfaces) {
    const yr::RenderScene scene = MakeEmissiveTriangleScene();

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
}

YR_TEST(cpu_path_tracer_is_deterministic_for_same_seed) {
    const yr::RenderScene scene = MakeStochasticEdgeScene(123);

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_changes_stochastic_result_for_different_seed) {
    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(MakeStochasticEdgeScene(123));
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(MakeStochasticEdgeScene(456));

    YR_EXPECT_TRUE(!ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce) {
    const yr::CpuPathTraceResult depth_one = yr::RenderCpuPathTrace(MakeIndirectBounceScene(1));
    const yr::CpuPathTraceResult depth_two = yr::RenderCpuPathTrace(MakeIndirectBounceScene(2));

    YR_EXPECT_TRUE(Luminance(depth_two.film.LinearPixel(1, 1)) > Luminance(depth_one.film.LinearPixel(1, 1)));
}
