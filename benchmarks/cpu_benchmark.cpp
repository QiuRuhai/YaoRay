#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/runtime/render_job.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif

namespace {

struct BenchmarkCase {
    std::string_view name;
    std::filesystem::path scene_path;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 1;
};

struct BenchmarkRecord {
    BenchmarkCase benchmark;
    int iteration = 1;
    double prepare_seconds = 0.0;
    double bvh_build_seconds = 0.0;
    double render_seconds = 0.0;
    double rays_per_second = 0.0;
    std::uint64_t rays_traced = 0;
    std::uint64_t bvh_node_tests = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t sphere_tests = 0;
    std::string_view acceleration_kind = "flat_sah";
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
    int blas_count = 0;
    int tlas_nodes = 0;
    int threads = 0;
    double peak_rss_mib = 0.0;
    double split_seed_rmse = 0.0;
    std::string_view sampler = "independent";
};

std::string_view SamplerName(yr::RenderSamplerKind sampler) {
    switch (sampler) {
        case yr::RenderSamplerKind::Independent: return "independent";
        case yr::RenderSamplerKind::Stratified: return "stratified";
        case yr::RenderSamplerKind::OwenSobol: return "owen_sobol";
        case yr::RenderSamplerKind::ZSobol: return "zsobol";
    }
    return "unknown";
}

std::optional<yr::RenderSamplerKind> ParseSampler(std::string_view name) {
    if (name == "independent") return yr::RenderSamplerKind::Independent;
    if (name == "stratified") return yr::RenderSamplerKind::Stratified;
    if (name == "sobol" || name == "owen_sobol") return yr::RenderSamplerKind::OwenSobol;
    if (name == "zsobol") return yr::RenderSamplerKind::ZSobol;
    return std::nullopt;
}

double PeakRssMiB() {
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#else
    return 0.0;
#endif
}

double FilmRmse(const yr::Film& a, const yr::Film& b) {
    if (a.Width() != b.Width() || a.Height() != b.Height() ||
        a.Width() <= 0 || a.Height() <= 0) {
        return 0.0;
    }
    double squared_error = 0.0;
    for (int y = 0; y < a.Height(); ++y) {
        for (int x = 0; x < a.Width(); ++x) {
            const yr::Color3f difference = a.LinearPixel(x, y) - b.LinearPixel(x, y);
            squared_error += static_cast<double>(difference.x) * difference.x;
            squared_error += static_cast<double>(difference.y) * difference.y;
            squared_error += static_cast<double>(difference.z) * difference.z;
        }
    }
    const double value_count = static_cast<double>(a.Width()) * a.Height() * 3.0;
    return std::sqrt(squared_error / value_count);
}

std::optional<yr::RenderJob> LoadJob(
    const BenchmarkCase& benchmark,
    std::optional<yr::RenderSamplerKind> sampler,
    int thread_count,
    std::string& error
) {
    yr::PbrtSceneLoadResult parsed = yr::LoadPbrtScene(benchmark.scene_path);
    if (!parsed.scene.has_value() || yr::HasSceneErrors(parsed.diagnostics)) {
        error = yr::FormatSceneDiagnostics(parsed.diagnostics);
        return std::nullopt;
    }
    yr::SceneCompileResult compiled = yr::CompilePbrtScene(*parsed.scene);
    if (!compiled.scene.has_value() || yr::HasSceneErrors(compiled.diagnostics)) {
        error = yr::FormatSceneDiagnostics(compiled.diagnostics);
        return std::nullopt;
    }

    compiled.settings.requested_backend = yr::RenderBackendKind::Cpu;
    compiled.settings.integrator = yr::RenderIntegratorKind::Path;
    compiled.settings.width = benchmark.width;
    compiled.settings.height = benchmark.height;
    compiled.settings.spp = benchmark.spp;
    compiled.settings.max_depth = benchmark.max_depth;
    compiled.settings.seed = 0x59414f524159ULL;
    if (sampler.has_value()) compiled.settings.sampler = *sampler;
    if (thread_count >= 0) compiled.settings.threads = thread_count;
    return yr::RenderJob{std::move(*compiled.scene), std::move(compiled.settings)};
}

std::optional<BenchmarkRecord> RunCase(
    const BenchmarkCase& benchmark,
    std::optional<yr::RenderSamplerKind> sampler,
    int thread_count,
    std::string& error
) {
    const std::optional<yr::RenderJob> loaded =
        LoadJob(benchmark, sampler, thread_count, error);
    if (!loaded.has_value()) {
        return std::nullopt;
    }

    yr::RenderJob primary_job = *loaded;
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(std::move(primary_job));
    if (!prepared.ok || !prepared.scene.has_value()) {
        error = prepared.error.empty() ? "CPU prepare failed" : prepared.error;
        return std::nullopt;
    }
    yr::CpuPathTraceResult primary = yr::RenderCpuPathTrace(*prepared.scene);
    if (!primary.ok) {
        error = primary.error.empty() ? "CPU render failed" : primary.error;
        return std::nullopt;
    }

    yr::RenderJob comparison_job = *loaded;
    comparison_job.settings.seed ^= 0x9e3779b97f4a7c15ULL;
    yr::CpuPrepareResult comparison_prepared = yr::PrepareCpuScene(std::move(comparison_job));
    if (!comparison_prepared.ok || !comparison_prepared.scene.has_value()) {
        error = comparison_prepared.error.empty()
            ? "comparison CPU prepare failed"
            : comparison_prepared.error;
        return std::nullopt;
    }
    yr::CpuPathTraceResult comparison =
        yr::RenderCpuPathTrace(*comparison_prepared.scene);
    if (!comparison.ok) {
        error = comparison.error.empty() ? "comparison CPU render failed" : comparison.error;
        return std::nullopt;
    }

    BenchmarkRecord record;
    record.benchmark = benchmark;
    record.sampler = SamplerName(loaded->settings.sampler);
    record.prepare_seconds = prepared.elapsed_seconds;
    record.bvh_build_seconds = prepared.bvh_build_seconds;
    record.render_seconds = primary.stats.elapsed_seconds;
    record.rays_traced = primary.stats.rays_traced;
    record.rays_per_second = record.render_seconds > 0.0
        ? static_cast<double>(record.rays_traced) / record.render_seconds
        : 0.0;
    record.bvh_node_tests = primary.stats.bvh_node_tests;
    record.triangle_tests = primary.stats.triangle_tests;
    record.sphere_tests = primary.stats.sphere_tests;
    const yr::RenderAcceleration& acceleration = prepared.scene->acceleration;
    if (acceleration.kind == yr::RenderAccelerationKind::TwoLevel) {
        record.acceleration_kind = "two_level_sah";
        record.blas_count = static_cast<int>(acceleration.two_level.blases.size());
        record.tlas_nodes = static_cast<int>(acceleration.two_level.tlas.nodes.size());
    }
    record.bvh_nodes = primary.stats.bvh_nodes;
    record.bvh_max_depth = primary.stats.bvh_max_depth;
    record.threads = primary.stats.threads;
    record.peak_rss_mib = PeakRssMiB();
    record.split_seed_rmse = FilmRmse(primary.film, comparison.film);
    return record;
}

void WriteJson(std::ostream& output, const BenchmarkRecord& record) {
    output << std::setprecision(10)
        << "{\"suite\":\"performance\""
        << ",\"case\":\"" << record.benchmark.name << "\""
        << ",\"iteration\":" << record.iteration
        << ",\"scene\":\"" << record.benchmark.scene_path.generic_string() << "\""
        << ",\"width\":" << record.benchmark.width
        << ",\"height\":" << record.benchmark.height
        << ",\"spp\":" << record.benchmark.spp
        << ",\"max_depth\":" << record.benchmark.max_depth
        << ",\"sampler\":\"" << record.sampler << "\""
        << ",\"prepare_seconds\":" << record.prepare_seconds
        << ",\"bvh_build_seconds\":" << record.bvh_build_seconds
        << ",\"render_seconds\":" << record.render_seconds
        << ",\"rays_per_second\":" << record.rays_per_second
        << ",\"rays_traced\":" << record.rays_traced
        << ",\"bvh_node_tests\":" << record.bvh_node_tests
        << ",\"triangle_tests\":" << record.triangle_tests
        << ",\"sphere_tests\":" << record.sphere_tests
        << ",\"acceleration_kind\":\"" << record.acceleration_kind << "\""
        << ",\"bvh_nodes\":" << record.bvh_nodes
        << ",\"bvh_max_depth\":" << record.bvh_max_depth
        << ",\"blas_count\":" << record.blas_count
        << ",\"tlas_nodes\":" << record.tlas_nodes
        << ",\"threads\":" << record.threads
        << ",\"peak_rss_mib\":" << record.peak_rss_mib
        << ",\"noise_split_seed_rmse\":" << record.split_seed_rmse
        << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string selected_case = "all";
    int repeat_count = 1;
    int thread_count = -1;
    std::optional<yr::RenderSamplerKind> sampler;
    std::filesystem::path output_path;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--case" && i + 1 < argc) {
            selected_case = argv[++i];
        } else if (argument == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (argument == "--repeat" && i + 1 < argc) {
            repeat_count = std::max(1, std::stoi(argv[++i]));
        } else if (argument == "--threads" && i + 1 < argc) {
            thread_count = std::max(0, std::stoi(argv[++i]));
        } else if (argument == "--sampler" && i + 1 < argc) {
            const std::string_view name = argv[++i];
            sampler = ParseSampler(name);
            if (!sampler.has_value()) {
                std::cerr << "Unknown sampler: " << name << '\n';
                return 2;
            }
        } else if (argument == "--help") {
            std::cout << "Usage: yaoray_cpu_benchmark [--case small|medium|large|all] "
                         "[--repeat count] [--threads count] "
                         "[--sampler independent|stratified|sobol|zsobol] "
                         "[--output results.jsonl]\n";
            return 0;
        } else {
            std::cerr << "Unknown benchmark argument: " << argument << '\n';
            return 2;
        }
    }

    const std::filesystem::path source_root = YAORAY_SOURCE_DIR;
    const std::vector<BenchmarkCase> cases{
        {"small", source_root / "scenes/pbrt/hello_emissive/hello_emissive.pbrt", 96, 96, 4, 3},
        {"medium", source_root / "scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt", 192, 192, 8, 6},
        {"large", source_root / "scenes/pbrt/coated_showcase/coated_showcase.pbrt", 640, 240, 16, 12},
    };

    std::ofstream output_file;
    if (!output_path.empty()) {
        output_file.open(output_path, std::ios::trunc);
        if (!output_file) {
            std::cerr << "Failed to open benchmark output: " << output_path << '\n';
            return 1;
        }
    }

    bool matched = false;
    for (const BenchmarkCase& benchmark : cases) {
        if (selected_case != "all" && selected_case != benchmark.name) {
            continue;
        }
        matched = true;
        for (int iteration = 1; iteration <= repeat_count; ++iteration) {
            std::string error;
            std::optional<BenchmarkRecord> record =
                RunCase(benchmark, sampler, thread_count, error);
            if (!record.has_value()) {
                std::cerr << "Benchmark " << benchmark.name << " failed: " << error << '\n';
                return 1;
            }
            record->iteration = iteration;
            WriteJson(std::cout, *record);
            if (output_file) {
                WriteJson(output_file, *record);
            }
        }
    }
    if (!matched) {
        std::cerr << "Unknown benchmark case: " << selected_case << '\n';
        return 2;
    }
    return 0;
}
