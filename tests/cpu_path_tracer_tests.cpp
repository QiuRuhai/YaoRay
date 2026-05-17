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
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{0.8f, 0.8f, 0.8f}, yr::Color3f{}});
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

yr::RenderScene MakeDiffuseFloorScene(std::uint64_t seed = 7) {
    yr::RenderScene scene;
    scene.width = 3;
    scene.height = 3;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = seed;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        2.0f,
        2.0f,
        yr::Color3f{4.0f, 4.0f, 4.0f}
    });
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeBlockedDiffuseFloorScene() {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 1.0f, -3.0f},
        yr::Point3f{0.0f, 1.0f, 3.0f},
        yr::Point3f{3.0f, 1.0f, -3.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
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
    return MakeDiffuseFloorScene();
}

yr::RenderScene MakeShadowedDirectLightScene() {
    return MakeBlockedDiffuseFloorScene();
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

bool FilmsEqual(const yr::Film& first, const yr::Film& second) {
    if (first.Width() != second.Width() || first.Height() != second.Height()) {
        return false;
    }
    if (first.BadSampleCount() != second.BadSampleCount()) {
        return false;
    }
    for (int y = 0; y < first.Height(); ++y) {
        for (int x = 0; x < first.Width(); ++x) {
            if (first.SampleCount(x, y) != second.SampleCount(x, y)) {
                return false;
            }
            if (!ColorEqual(first.LinearPixel(x, y), second.LinearPixel(x, y))) {
                return false;
            }
        }
    }
    return true;
}

bool CoreStatsEqual(const yr::CpuPathTraceStats& first, const yr::CpuPathTraceStats& second) {
    return first.rays_traced == second.rays_traced &&
           first.shadow_rays == second.shadow_rays &&
           first.occluded_shadow_rays == second.occluded_shadow_rays &&
           first.triangle_tests == second.triangle_tests &&
           first.bvh_node_tests == second.bvh_node_tests &&
           first.bvh_nodes == second.bvh_nodes &&
           first.bvh_max_depth == second.bvh_max_depth &&
           first.hits == second.hits &&
           first.misses == second.misses;
}

yr::RenderScene MakeThreadedDeterminismScene() {
    yr::RenderScene scene = MakeBaseScene(40, 24);
    scene.spp = 3;
    scene.max_depth = 2;
    scene.seed = 99;
    scene.sampler = yr::RenderSamplerKind::Stratified;
    scene.light_samples = 4;
    scene.environment.radiance = yr::Color3f{0.05f, 0.06f, 0.07f};
    scene.materials[0].albedo = yr::Color3f{0.7f, 0.6f, 0.5f};
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 0.5f, 2.0f},
        1.0f,
        1.0f,
        yr::Color3f{2.0f, 2.0f, 2.0f}
    });
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
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.threads, 1);
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

YR_TEST(cpu_path_tracer_stratified_sampler_changes_stochastic_result) {
    yr::RenderScene independent = MakeStochasticEdgeScene(123);
    independent.sampler = yr::RenderSamplerKind::Independent;

    yr::RenderScene stratified = independent;
    stratified.sampler = yr::RenderSamplerKind::Stratified;

    const yr::CpuPathTraceResult independent_result = yr::RenderCpuPathTrace(independent);
    const yr::CpuPathTraceResult stratified_result = yr::RenderCpuPathTrace(stratified);

    YR_EXPECT_TRUE(AnyPixelDifferent(independent_result.film, stratified_result.film));
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
    const yr::RenderScene unblocked = MakeDiffuseFloorScene();
    const yr::RenderScene blocked = MakeShadowedDirectLightScene();

    const yr::CpuPathTraceResult unblocked_result = yr::RenderCpuPathTrace(unblocked);
    const yr::CpuPathTraceResult blocked_result = yr::RenderCpuPathTrace(blocked);

    YR_EXPECT_TRUE(unblocked_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(unblocked_result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
    YR_EXPECT_TRUE(Luminance(blocked_result.film.LinearPixel(1, 1)) < Luminance(unblocked_result.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce) {
    const yr::CpuPathTraceResult depth_one = yr::RenderCpuPathTrace(MakeIndirectBounceScene(1));
    const yr::CpuPathTraceResult depth_two = yr::RenderCpuPathTrace(MakeIndirectBounceScene(2));

    YR_EXPECT_TRUE(Luminance(depth_two.film.LinearPixel(1, 1)) > Luminance(depth_one.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_direct_light_uses_diffuse_brdf_weight) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.area_lights[0].width = 2.0f;
    scene.area_lights[0].height = 2.0f;
    scene.area_lights[0].radiance = yr::Color3f{4.0f, 4.0f, 4.0f};
    RebuildBvh(scene);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(center.x < 2.0f);
    YR_EXPECT_TRUE(center.y < 2.0f);
    YR_EXPECT_TRUE(center.z < 2.0f);
}

YR_TEST(cpu_path_tracer_area_light_sampling_changes_with_seed) {
    yr::RenderScene first_scene = MakeDiffuseFloorScene(11);
    yr::RenderScene second_scene = MakeDiffuseFloorScene(12);
    first_scene.spp = 1;
    second_scene.spp = 1;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(first_scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(second_scene);

    YR_EXPECT_TRUE(!ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_ignores_invalid_area_light_size) {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.area_lights[0].width = 0.0f;
    scene.area_lights[0].height = 2.0f;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_ignores_area_light_behind_surface) {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.area_lights[0].position = yr::Point3f{0.0f, -2.0f, 0.0f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_light_samples_increase_shadow_rays) {
    yr::RenderScene one_sample = MakeDiffuseFloorScene(7);
    one_sample.light_samples = 1;

    yr::RenderScene four_samples = one_sample;
    four_samples.light_samples = 4;

    const yr::CpuPathTraceResult one_result = yr::RenderCpuPathTrace(one_sample);
    const yr::CpuPathTraceResult four_result = yr::RenderCpuPathTrace(four_samples);

    YR_EXPECT_TRUE(one_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(four_result.stats.shadow_rays, one_result.stats.shadow_rays * std::uint64_t{4});
    YR_EXPECT_EQ(four_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_is_deterministic_with_multiple_light_samples) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}

YR_TEST(cpu_path_tracer_stratified_sampler_is_deterministic) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.sampler = yr::RenderSamplerKind::Stratified;
    scene.spp = 4;
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}

YR_TEST(cpu_path_tracer_reports_single_thread_when_requested) {
    yr::RenderScene scene = MakeThreadedDeterminismScene();
    scene.threads = 1;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_reports_capped_requested_threads) {
    yr::RenderScene scene = MakeBaseScene(4, 4);
    scene.threads = 32;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_is_bitwise_identical_across_thread_counts) {
    yr::RenderScene single_thread = MakeThreadedDeterminismScene();
    single_thread.threads = 1;
    yr::RenderScene two_threads = single_thread;
    two_threads.threads = 2;
    yr::RenderScene four_threads = single_thread;
    four_threads.threads = 4;

    const yr::CpuPathTraceResult single_result = yr::RenderCpuPathTrace(single_thread);
    const yr::CpuPathTraceResult two_result = yr::RenderCpuPathTrace(two_threads);
    const yr::CpuPathTraceResult four_result = yr::RenderCpuPathTrace(four_threads);

    YR_EXPECT_EQ(single_result.stats.threads, 1);
    YR_EXPECT_EQ(two_result.stats.threads, 2);
    YR_EXPECT_EQ(four_result.stats.threads, 4);
    YR_EXPECT_TRUE(FilmsEqual(single_result.film, two_result.film));
    YR_EXPECT_TRUE(FilmsEqual(single_result.film, four_result.film));
    YR_EXPECT_TRUE(CoreStatsEqual(single_result.stats, two_result.stats));
    YR_EXPECT_TRUE(CoreStatsEqual(single_result.stats, four_result.stats));
}
