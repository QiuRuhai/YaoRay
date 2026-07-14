#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include "cpu_worker_pool.hpp"

#include <yaoray/sampling/sampler.hpp>
#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>
#include <yaoray/core/ray.hpp>
#include <yaoray/core/rng.hpp>
#include <yaoray/integrators/path_integrator.hpp>
#include <yaoray/scene/camera.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace yr {
namespace {

constexpr std::size_t ActivePixelChunkSize = 256;

struct alignas(64) CpuWorkerScratch {
    CpuRenderStats stats;
};

void MergeTraceStats(CpuRenderStats& target, const CpuRenderStats& source) {
    target.rays_traced += source.rays_traced;
    target.shadow_rays += source.shadow_rays;
    target.occluded_shadow_rays += source.occluded_shadow_rays;
    target.triangle_tests += source.triangle_tests;
    target.sphere_tests += source.sphere_tests;
    target.bvh_node_tests += source.bvh_node_tests;
    target.hits += source.hits;
    target.misses += source.misses;
}

} // namespace

CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene, const RenderRequest& request) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    const RenderSettings& settings = prepared_scene.Settings();
    CpuPathTraceResult result{
        request.resume_film == nullptr ? Film{settings.width, settings.height} : *request.resume_film,
        {}, true, {}};
    const CpuTileSchedule schedule = BuildCpuTileSchedule(
        settings.width, settings.height, settings.threads);
    result.stats.bvh_nodes = prepared_scene.acceleration.NodeCount();
    result.stats.bvh_max_depth = prepared_scene.acceleration.MaxDepth();
    result.stats.threads = schedule.worker_count;

    const int samples_per_pixel = std::max(1, settings.spp);
    if (request.resume_completed_spp < 0 || request.resume_completed_spp > samples_per_pixel) {
        result.ok = false;
        result.error = "invalid resume completed spp";
        return result;
    }
    if (request.resume_film != nullptr) {
        if (request.resume_film->Width() != settings.width ||
            request.resume_film->Height() != settings.height) {
            result.ok = false;
            result.error = "resume film dimensions do not match render scene";
            return result;
        }
        for (int y = 0; y < request.resume_film->Height(); ++y) {
            for (int x = 0; x < request.resume_film->Width(); ++x) {
                const std::uint32_t count = request.resume_film->SampleCount(x, y);
                const bool invalid_count = settings.adaptive_sampling.enabled
                    ? count > static_cast<std::uint32_t>(request.resume_completed_spp)
                    : count != static_cast<std::uint32_t>(request.resume_completed_spp);
                if (invalid_count) {
                    result.ok = false;
                    result.error = "resume film sample counts do not match resume completed spp";
                    return result;
                }
            }
        }
    }

    const auto start = std::chrono::steady_clock::now();
    if (request.stop_token.stop_requested()) {
        result.ok = false;
        result.error = "render cancelled";
        return result;
    }

    const std::size_t pixel_count = static_cast<std::size_t>(settings.width) *
        static_cast<std::size_t>(settings.height);
    std::vector<FilmSample> pass_samples(pixel_count);
    std::vector<std::uint32_t> active_pixels;
    std::vector<std::uint32_t> next_active_pixels;
    std::vector<CpuWorkerScratch> worker_scratch(
        static_cast<std::size_t>(schedule.worker_count));
    active_pixels.reserve(pixel_count);
    next_active_pixels.reserve(pixel_count);
    const AdaptiveSamplingSettings& adaptive = settings.adaptive_sampling;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        const int x = static_cast<int>(index % static_cast<std::size_t>(settings.width));
        const int y = static_cast<int>(index / static_cast<std::size_t>(settings.width));
        const bool converged = adaptive.enabled && result.film.IsConverged(
            x, y, adaptive.min_spp, adaptive.relative_error,
            adaptive.absolute_error, adaptive.confidence);
        if (!converged && result.film.SampleCount(x, y) <
            static_cast<std::uint32_t>(samples_per_pixel)) {
            active_pixels.push_back(static_cast<std::uint32_t>(index));
        }
    }
    std::uint64_t cumulative_rays = 0;
    std::uint64_t total_samples = result.film.TotalSampleCount();
    for (int pass = request.resume_completed_spp;
         pass < samples_per_pixel && !active_pixels.empty(); ++pass) {
        for (CpuWorkerScratch& scratch : worker_scratch) scratch.stats = {};
        const std::size_t task_count =
            (active_pixels.size() + ActivePixelChunkSize - 1) / ActivePixelChunkSize;
        const bool pass_completed = prepared_scene.worker_pool->Run(
            task_count,
            request.stop_token,
            [&](std::size_t task_index, int worker_index) {
            CpuRenderStats& stats =
                worker_scratch[static_cast<std::size_t>(worker_index)].stats;
            const std::size_t begin = task_index * ActivePixelChunkSize;
            const std::size_t end = std::min(begin + ActivePixelChunkSize, active_pixels.size());
            for (std::size_t active_index = begin; active_index < end; ++active_index) {
                    const std::size_t pixel_index = active_pixels[active_index];
                    const int x = static_cast<int>(pixel_index % static_cast<std::size_t>(settings.width));
                    const int y = static_cast<int>(pixel_index / static_cast<std::size_t>(settings.width));
                    const int sample = static_cast<int>(result.film.SampleCount(x, y));
                    Sampler sampler{
                        settings.sampler,
                        settings.seed,
                        x,
                        y,
                        sample,
                        samples_per_pixel,
                        PathLightSampleCount(scene)
                    };
                    // Distinct RNG stream for stochastic BSDF evaluation
                    // (M3 LayeredBxDF). Salted so it does not correlate with
                    // Sampler's stratified dimensions. No consumer in
                    // Slice 1; load-bearing from Slice 2 on.
                    Rng bsdf_rng{
                        SeedForPixelSample(settings.seed, x, y, sample) ^ 0x9E3779B97F4A7C15ULL};
                    const Vec2f pixel_sample = sampler.Sample2D(SampleDimension::Pixel);
                    const Ray3f ray = GeneratePerspectiveCameraRay(
                        scene.camera,
                        settings.width,
                        settings.height,
                        x,
                        y,
                        pixel_sample
                    );
                    const PathSample path_sample = IntegratePathSample(
                        scene, settings, prepared_scene.acceleration, ray, sampler, bsdf_rng, stats);
                    pass_samples[pixel_index] = FilmSample{
                        path_sample.beauty,
                        path_sample.albedo,
                        path_sample.normal,
                        path_sample.depth,
                        path_sample.primary_hit};
            }
        });

        for (const CpuWorkerScratch& scratch : worker_scratch) {
            MergeTraceStats(result.stats, scratch.stats);
        }
        cumulative_rays = result.stats.rays_traced;

        if (!pass_completed) {
            result.ok = false;
            result.error = "render cancelled";
            result.stats.elapsed_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        }

        for (const std::uint32_t encoded_index : active_pixels) {
            const std::size_t pixel_index = encoded_index;
            const int x = static_cast<int>(pixel_index % static_cast<std::size_t>(settings.width));
            const int y = static_cast<int>(pixel_index / static_cast<std::size_t>(settings.width));
            result.film.AddSample(x, y, pass_samples[pixel_index]);
        }
        total_samples += static_cast<std::uint64_t>(active_pixels.size());

        next_active_pixels.clear();
        std::uint64_t converged_pixels = 0;
        for (std::size_t index = 0; index < pixel_count; ++index) {
            const int x = static_cast<int>(index % static_cast<std::size_t>(settings.width));
            const int y = static_cast<int>(index / static_cast<std::size_t>(settings.width));
            const bool converged = adaptive.enabled && result.film.IsConverged(
                x, y, adaptive.min_spp, adaptive.relative_error,
                adaptive.absolute_error, adaptive.confidence);
            if (converged) ++converged_pixels;
            if (!converged && result.film.SampleCount(x, y) <
                static_cast<std::uint32_t>(samples_per_pixel)) {
                next_active_pixels.push_back(static_cast<std::uint32_t>(index));
            }
        }
        active_pixels.swap(next_active_pixels);
        result.stats.samples_rendered = total_samples;
        result.stats.converged_pixels = converged_pixels;

        if (request.progress_callback) {
            const auto now = std::chrono::steady_clock::now();
            const int completed_spp = pass + 1;
            const RenderProgress progress{
                completed_spp,
                samples_per_pixel,
                total_samples,
                static_cast<std::uint64_t>(samples_per_pixel) *
                    static_cast<std::uint64_t>(settings.width) *
                    static_cast<std::uint64_t>(settings.height),
                active_pixels.size(),
                converged_pixels,
                cumulative_rays,
                std::chrono::duration<double>(now - start).count()
            };
            const RenderProgressDecision decision = request.progress_callback(progress, result.film);
            if (decision.cancel) {
                result.ok = false;
                result.error = decision.error.empty() ? "render cancelled by progress callback" : decision.error;
                result.stats.elapsed_seconds = progress.elapsed_seconds;
                return result;
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
