#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

float Luminance(yr::Color3f color) {
    return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

bool ColorEqual(yr::Color3f a, yr::Color3f b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void RebuildBvh(yr::RenderScene& scene) {
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
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
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeEmissiveTriangleScene() {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.environment.radiance = yr::Color3f{};
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};
    RebuildBvh(scene);
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
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeIndirectBounceScene(int max_depth) {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.max_depth = max_depth;
    scene.environment.radiance = yr::Color3f{0.4f, 0.5f, 0.6f};
    scene.environment.strength = 1.0f;
    scene.materials[0].albedo = yr::Color3f{0.9f, 0.9f, 0.9f};
    scene.area_lights.clear();
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeDirectLightScene() {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.environment.radiance = yr::Color3f{};
    scene.materials[0].albedo = yr::Color3f{1.0f, 0.5f, 0.25f};
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 0.0f, 2.0f},
        1.0f,
        1.0f,
        yr::Color3f{4.0f, 4.0f, 4.0f}
    });
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeShadowedDirectLightScene() {
    yr::RenderScene scene = MakeDirectLightScene();
    scene.area_lights[0].position = yr::Point3f{0.0f, 2.0f, 2.0f};
    scene.area_lights[0].radiance = yr::Color3f{8.0f, 8.0f, 8.0f};
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.75f, 1.0f, 0.25f},
        yr::Point3f{0.75f, 1.0f, 0.25f},
        yr::Point3f{0.0f, 1.0f, 1.75f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        0
    });
    RebuildBvh(scene);
    return scene;
}

bool AnyPixelDifferent(const yr::Film& first, const yr::Film& second) {
    for (int y = 0; y < first.Height(); ++y) {
        for (int x = 0; x < first.Width(); ++x) {
            if (!ColorEqual(first.LinearPixel(x, y), second.LinearPixel(x, y))) {
                return true;
            }
        }
    }
    return false;
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
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
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

YR_TEST(cpu_path_tracer_uses_precompiled_bvh_stats) {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    for (int i = 0; i < 5; ++i) {
        const float x = 10.0f + static_cast<float>(i);
        scene.triangles.push_back(yr::RenderTriangle{
            yr::Point3f{x - 0.25f, -0.25f, 0.0f},
            yr::Point3f{x + 0.25f, -0.25f, 0.0f},
            yr::Point3f{x, 0.25f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        });
    }

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
}

YR_TEST(cpu_path_tracer_sees_emissive_surfaces) {
    const yr::RenderScene scene = MakeEmissiveTriangleScene();

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
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

    YR_EXPECT_TRUE(AnyPixelDifferent(first.film, second.film));
}

YR_TEST(cpu_path_tracer_adds_direct_area_light_contribution) {
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(MakeDirectLightScene());
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_counts_shadow_occlusion_and_dims_direct_light) {
    yr::RenderScene unblocked = MakeDirectLightScene();
    unblocked.area_lights[0].position = yr::Point3f{0.0f, 2.0f, 2.0f};
    unblocked.area_lights[0].radiance = yr::Color3f{8.0f, 8.0f, 8.0f};
    RebuildBvh(unblocked);
    const yr::RenderScene blocked = MakeShadowedDirectLightScene();

    const yr::CpuPathTraceResult unblocked_result = yr::RenderCpuPathTrace(unblocked);
    const yr::CpuPathTraceResult blocked_result = yr::RenderCpuPathTrace(blocked);

    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
    YR_EXPECT_TRUE(Luminance(blocked_result.film.LinearPixel(1, 1)) < Luminance(unblocked_result.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce) {
    const yr::CpuPathTraceResult depth_one = yr::RenderCpuPathTrace(MakeIndirectBounceScene(1));
    const yr::CpuPathTraceResult depth_two = yr::RenderCpuPathTrace(MakeIndirectBounceScene(2));

    YR_EXPECT_TRUE(Luminance(depth_two.film.LinearPixel(1, 1)) > Luminance(depth_one.film.LinearPixel(1, 1)));
}
