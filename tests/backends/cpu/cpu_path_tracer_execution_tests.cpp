#include "cpu_path_tracer_test_support.hpp"
#include "yr_test.hpp"

#include <cstdint>
#include <string>
#include <stop_token>
#include <utility>
#include <vector>

YR_TEST(cpu_path_tracer_traces_one_sample_per_pixel) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(4, 3);
    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));

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
    yr::RenderJob job = yr::test_support::MakeBasePathJob(2, 2);
    job.settings.spp = 4;

    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));

    YR_EXPECT_EQ(result.film.Width(), 2);
    YR_EXPECT_EQ(result.film.Height(), 2);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 4);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{16});
}

YR_TEST(cpu_path_tracer_progress_callback_can_cancel_render) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(2, 2);
    job.settings.spp = 3;

    yr::RenderRequest request;
    request.progress_callback = [](const yr::RenderProgress&, const yr::Film&) {
        return yr::RenderProgressDecision{true, "stop after first pass"};
    };

    const yr::CpuPathTraceResult result =
        yr::test_support::RunPathTrace(std::move(job), request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("stop after first pass") != std::string::npos);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
}

YR_TEST(cpu_path_tracer_honors_pre_requested_stop_token_without_partial_pass) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(64, 64);
    job.settings.spp = 4;
    yr::CpuPreparedScene prepared =
        yr::test_support::PreparePathScene(std::move(job));

    std::stop_source stop_source;
    stop_source.request_stop();
    yr::RenderRequest request;
    request.stop_token = stop_source.get_token();

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(prepared, request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("cancelled") != std::string::npos);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), std::uint32_t{0});
    YR_EXPECT_EQ(result.film.SampleCount(63, 63), std::uint32_t{0});
}

YR_TEST(cpu_path_tracer_reuses_prepared_scene_workers_across_renders) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(32, 16);
    job.settings.threads = 2;
    yr::CpuPreparedScene prepared =
        yr::test_support::PreparePathScene(std::move(job));

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(prepared);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(prepared);

    YR_EXPECT_TRUE(first.ok);
    YR_EXPECT_TRUE(second.ok);
    YR_EXPECT_EQ(first.stats.threads, 2);
    YR_EXPECT_EQ(second.stats.threads, 2);
    YR_EXPECT_EQ(first.film.SampleCount(0, 0), std::uint32_t{1});
    YR_EXPECT_EQ(second.film.SampleCount(0, 0), std::uint32_t{1});
}

YR_TEST(cpu_path_tracer_reports_progress_after_each_sample_pass) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(2, 2);
    job.settings.spp = 3;
    std::vector<int> completed;

    yr::RenderRequest request;
    request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
        completed.push_back(progress.completed_spp);
        YR_EXPECT_EQ(progress.target_spp, 3);
        YR_EXPECT_EQ(film.SampleCount(0, 0), static_cast<std::uint32_t>(progress.completed_spp));
        return yr::RenderProgressDecision{};
    };

    const yr::CpuPathTraceResult result =
        yr::test_support::RunPathTrace(std::move(job), request);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(completed.size(), std::size_t{3});
    YR_EXPECT_EQ(completed[0], 1);
    YR_EXPECT_EQ(completed[1], 2);
    YR_EXPECT_EQ(completed[2], 3);
}

YR_TEST(cpu_path_tracer_executes_two_level_instance_acceleration) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(4, 3);
    job.scene.instances.push_back(yr::RenderInstance{
        yr::MeshPrimitiveHandle{0}, yr::Mat4f{}});
    yr::CpuPreparedScene prepared =
        yr::test_support::PreparePathScene(std::move(job));

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(prepared);

    YR_EXPECT_EQ(
        prepared.acceleration.kind, yr::RenderAccelerationKind::TwoLevel);
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(result.stats.bvh_nodes >= 2);
}

YR_TEST(cpu_path_tracer_adaptive_sampling_compacts_converged_pixels) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(4, 3);
    job.scene.vertices.clear();
    job.scene.indices.clear();
    job.scene.primitives.clear();
    job.settings.spp = 8;
    job.settings.adaptive_sampling.enabled = true;
    job.settings.adaptive_sampling.min_spp = 2;
    job.settings.adaptive_sampling.relative_error = 0.01f;
    job.settings.adaptive_sampling.absolute_error = 0.0f;

    std::vector<std::uint64_t> active_counts;
    yr::RenderRequest request;
    request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film&) {
        active_counts.push_back(progress.active_pixels);
        return yr::RenderProgressDecision{};
    };
    const yr::CpuPathTraceResult result =
        yr::test_support::RunPathTrace(std::move(job), request);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), std::uint32_t{2});
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), std::uint32_t{2});
    YR_EXPECT_EQ(result.stats.samples_rendered, std::uint64_t{24});
    YR_EXPECT_EQ(result.stats.converged_pixels, std::uint64_t{12});
    YR_EXPECT_EQ(active_counts.size(), std::size_t{2});
    YR_EXPECT_EQ(active_counts.back(), std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_adaptive_sampling_is_deterministic_across_thread_counts) {
    yr::RenderJob single = yr::test_support::MakeBasePathJob(17, 13);
    single.settings.spp = 6;
    single.settings.threads = 1;
    single.settings.adaptive_sampling.enabled = true;
    single.settings.adaptive_sampling.min_spp = 2;
    single.settings.adaptive_sampling.absolute_error = 0.001f;
    yr::RenderJob parallel = single;
    parallel.settings.threads = 4;

    const yr::CpuPathTraceResult first = yr::test_support::RunPathTrace(std::move(single));
    const yr::CpuPathTraceResult second = yr::test_support::RunPathTrace(std::move(parallel));

    YR_EXPECT_TRUE(first.ok);
    YR_EXPECT_TRUE(second.ok);
    YR_EXPECT_EQ(first.film.TotalSampleCount(), second.film.TotalSampleCount());
    for (int y = 0; y < first.film.Height(); ++y) {
        for (int x = 0; x < first.film.Width(); ++x) {
            YR_EXPECT_EQ(first.film.SampleCount(x, y), second.film.SampleCount(x, y));
            YR_EXPECT_EQ(first.film.LinearPixel(x, y).x, second.film.LinearPixel(x, y).x);
            YR_EXPECT_EQ(first.film.LinearPixel(x, y).y, second.film.LinearPixel(x, y).y);
            YR_EXPECT_EQ(first.film.LinearPixel(x, y).z, second.film.LinearPixel(x, y).z);
        }
    }
}

YR_TEST(cpu_path_tracer_records_first_hit_aovs) {
    yr::RenderJob job = yr::test_support::MakeBasePathJob(9, 9);
    const yr::CpuPathTraceResult result = yr::test_support::RunPathTrace(std::move(job));

    bool found_hit = false;
    for (int y = 0; y < result.film.Height(); ++y) {
        for (int x = 0; x < result.film.Width(); ++x) {
            if (result.film.DepthPixel(x, y) <= 0.0f) continue;
            found_hit = true;
            YR_EXPECT_NEAR(result.film.AlbedoPixel(x, y).x, 0.8, 1e-6);
            YR_EXPECT_NEAR(yr::Length(result.film.NormalPixel(x, y)), 1.0, 1e-6);
        }
    }
    YR_EXPECT_TRUE(found_hit);
}
