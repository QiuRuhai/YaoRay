#include <yaoray/backends/backend.hpp>
#include <yaoray/core/version.hpp>
#include <yaoray/film/film_checkpoint.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/render/render_scene_hash.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene_parser.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

void PrintHelp() {
    std::cout
        << "YaoRay " << yr::VersionString() << '\n'
        << '\n'
        << "Usage:\n"
        << "  yaoray --help\n"
        << "  yaoray --version\n"
        << "  yaoray render <scene.toml> [--backend cpu|cuda]\n";
}

void PrintRenderHelp() {
    std::cout
        << "Usage:\n"
        << "  yaoray render <scene.toml> [--backend cpu|cuda]\n"
        << '\n'
        << "The render command parses, compiles, and renders scenes with scene-selected CPU integrators.\n";
}

yr::ToneMapper ToFilmToneMapper(yr::ToneMapperKind mapper) {
    switch (mapper) {
        case yr::ToneMapperKind::None:
            return yr::ToneMapper::None;
        case yr::ToneMapperKind::Reinhard:
            return yr::ToneMapper::Reinhard;
        case yr::ToneMapperKind::Aces:
            return yr::ToneMapper::Aces;
    }
    return yr::ToneMapper::Aces;
}

double SafeRate(double numerator, double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) {
        return 0.0;
    }
    return numerator / elapsed_seconds;
}

bool OfflineRequested(const yr::SceneDescription& scene) {
    return scene.offline.progress ||
           !scene.offline.checkpoint_png.empty() ||
           !scene.offline.checkpoint_state.empty() ||
           scene.offline.resume;
}

std::optional<std::string> ValidateOfflineWorkflow(
    const yr::SceneDescription& scene,
    const yr::RenderSceneIR& render_scene
) {
    if (!OfflineRequested(scene)) {
        return std::nullopt;
    }
    if (render_scene.requested_backend != yr::RenderBackendKind::Cpu ||
        render_scene.integrator != yr::RenderIntegratorKind::Path) {
        return "offline workflow supports only cpu path renders";
    }
    return std::nullopt;
}

double PercentComplete(const yr::RenderProgress& progress) {
    if (progress.target_samples == 0) {
        return 100.0;
    }
    return 100.0 * static_cast<double>(progress.completed_samples) / static_cast<double>(progress.target_samples);
}

double SafeEtaSeconds(const yr::RenderProgress& progress) {
    if (progress.elapsed_seconds <= 0.0 ||
        progress.completed_samples == 0 ||
        progress.completed_samples >= progress.target_samples) {
        return 0.0;
    }
    const double samples_per_second = static_cast<double>(progress.completed_samples) / progress.elapsed_seconds;
    return static_cast<double>(progress.target_samples - progress.completed_samples) / samples_per_second;
}

std::uint64_t TotalSamples(const yr::Film& film) {
    std::uint64_t total = 0;
    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            total += film.SampleCount(x, y);
        }
    }
    return total;
}

std::uint64_t EstimateTextureMemoryBytes(const yr::RenderSceneIR& scene) {
    std::uint64_t total = 0;
    for (const yr::RenderTexture& texture : scene.textures) {
        total += static_cast<std::uint64_t>(texture.texels.size() * sizeof(yr::Color4f));
    }
    return total;
}

double BytesToMiB(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

int RunRender(int argc, char** argv) {
    if (argc == 3 && (std::string_view{argv[2]} == "--help" || std::string_view{argv[2]} == "-h")) {
        PrintRenderHelp();
        return 0;
    }
    if (argc < 3) {
        std::cerr << "Missing scene file path.\n";
        PrintRenderHelp();
        return 2;
    }

    const std::filesystem::path scene_path = argv[2];
    std::optional<yr::RenderBackendKind> backend_override;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --backend.\n";
                return 2;
            }
            const auto backend = yr::ParseRenderBackendName(argv[++i]);
            if (!backend) {
                std::cerr << "Unknown backend: " << argv[i] << '\n';
                return 2;
            }
            backend_override = *backend;
        } else {
            std::cerr << "Unknown render argument: " << arg << '\n';
            PrintRenderHelp();
            return 2;
        }
    }

    yr::SceneLoadResult result = yr::LoadSceneFile(scene_path);
    if (yr::HasSceneErrors(result.diagnostics) || !result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(result.diagnostics) << '\n';
        return 1;
    }

    yr::SceneDescription scene = std::move(result.scene.value());
    if (backend_override) {
        yr::ApplyBackendOverride(scene, *backend_override);
    }

    const yr::SceneCompileResult compile_result = yr::CompileScene(scene);
    if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
        return 1;
    }

    const yr::RenderSceneIR& render_scene = compile_result.scene.value();
    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
    std::cout << "Integrator: " << yr::RenderIntegratorName(render_scene.integrator) << '\n';
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
    std::cout << "Compiled materials: " << render_scene.materials.size() << '\n';
    std::cout << "Compiled textures: " << render_scene.textures.size() << '\n';
    std::cout << "Texture memory MiB: " << BytesToMiB(EstimateTextureMemoryBytes(render_scene)) << '\n';

    if (const std::optional<std::string> offline_error = ValidateOfflineWorkflow(scene, render_scene)) {
        std::cerr << *offline_error << '\n';
        return 1;
    }

    const auto backend = yr::CreateRenderBackend(render_scene.requested_backend);
    if (!backend) {
        std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
        return 1;
    }

    yr::BackendPrepareResult prepare_result = backend->Prepare(render_scene);
    if (!prepare_result.ok || prepare_result.scene == nullptr) {
        std::cerr << "Render backend preparation failed: "
                  << (prepare_result.error.empty() ? "unknown error" : prepare_result.error)
                  << '\n';
        return 1;
    }
    std::cout << "Prepare seconds: " << prepare_result.elapsed_seconds << '\n';

    const std::uint64_t settings_hash = yr::ComputeRenderSettingsHash(render_scene);
    std::optional<yr::Film> resume_film;
    int resume_completed_spp = 0;
    if (scene.offline.resume) {
        yr::FilmCheckpointLoadResult checkpoint = yr::LoadFilmCheckpoint(
            scene.offline.checkpoint_state,
            render_scene.width,
            render_scene.height,
            render_scene.spp,
            settings_hash
        );
        if (!checkpoint.ok || !checkpoint.film.has_value()) {
            std::cerr << "Checkpoint load failed: " << checkpoint.error << '\n';
            return 1;
        }
        resume_completed_spp = checkpoint.metadata.completed_spp;
        resume_film.emplace(std::move(checkpoint.film.value()));
        std::cout << "Resumed checkpoint: " << scene.offline.checkpoint_state.generic_string()
                  << " at " << resume_completed_spp << "/" << render_scene.spp << " spp\n";
    }

    yr::RenderRequest render_request;
    if (resume_film.has_value()) {
        render_request.resume_film = &resume_film.value();
        render_request.resume_completed_spp = resume_completed_spp;
    }

    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(scene.film.tone_mapper),
        scene.film.exposure
    };

    auto last_progress = std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.progress_interval_seconds);
    auto last_checkpoint_png =
        std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.checkpoint_png_interval_seconds);
    auto last_checkpoint_state =
        std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.checkpoint_state_interval_seconds);
    bool wrote_final_checkpoint_png = false;
    bool wrote_final_checkpoint_state = false;

    if (OfflineRequested(scene)) {
        render_request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
            const auto now = std::chrono::steady_clock::now();
            const bool final_pass = progress.completed_spp >= progress.target_spp;
            if (scene.offline.progress &&
                (final_pass || now - last_progress >= std::chrono::seconds(scene.offline.progress_interval_seconds))) {
                last_progress = now;
                std::cout << "Progress: " << progress.completed_spp << "/" << progress.target_spp
                          << " spp (" << PercentComplete(progress) << "%)"
                          << " elapsed=" << progress.elapsed_seconds << "s"
                          << " eta=" << SafeEtaSeconds(progress) << "s"
                          << " samples/sec=" << SafeRate(static_cast<double>(progress.completed_samples), progress.elapsed_seconds)
                          << " rays/sec=" << SafeRate(static_cast<double>(progress.rays_traced), progress.elapsed_seconds)
                          << '\n';
            }
            if (!scene.offline.checkpoint_png.empty() &&
                (final_pass || now - last_checkpoint_png >= std::chrono::seconds(scene.offline.checkpoint_png_interval_seconds))) {
                last_checkpoint_png = now;
                const yr::ImageWriteResult image = yr::WriteImage(film, tone_map, scene.offline.checkpoint_png);
                if (!image.ok) {
                    return yr::RenderProgressDecision{true, "checkpoint image write failed: " + image.error};
                }
                wrote_final_checkpoint_png = final_pass;
                std::cout << "Checkpoint image: " << scene.offline.checkpoint_png.generic_string() << '\n';
            }
            if (!scene.offline.checkpoint_state.empty() &&
                (final_pass || now - last_checkpoint_state >= std::chrono::seconds(scene.offline.checkpoint_state_interval_seconds))) {
                last_checkpoint_state = now;
                const yr::FilmCheckpointMetadata metadata{
                    render_scene.width,
                    render_scene.height,
                    render_scene.spp,
                    progress.completed_spp,
                    settings_hash
                };
                const yr::FilmCheckpointWriteResult state = yr::WriteFilmCheckpoint(scene.offline.checkpoint_state, film, metadata);
                if (!state.ok) {
                    return yr::RenderProgressDecision{true, "checkpoint state write failed: " + state.error};
                }
                wrote_final_checkpoint_state = final_pass;
                std::cout << "Checkpoint state: " << scene.offline.checkpoint_state.generic_string() << '\n';
            }
            return yr::RenderProgressDecision{};
        };
    }

    const yr::RenderResult render_result = backend->Render(*prepare_result.scene, render_request);
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
    if (!render_result.film.has_value()) {
        std::cerr << "Render backend completed without a film.\n";
        return 1;
    }

    if (!scene.offline.checkpoint_png.empty() && !wrote_final_checkpoint_png) {
        const yr::ImageWriteResult image = yr::WriteImage(*render_result.film, tone_map, scene.offline.checkpoint_png);
        if (!image.ok) {
            std::cerr << "Checkpoint image write failed: " << image.error << '\n';
            return 1;
        }
        std::cout << "Checkpoint image: " << scene.offline.checkpoint_png.generic_string() << '\n';
    }
    if (!scene.offline.checkpoint_state.empty() && !wrote_final_checkpoint_state) {
        const yr::FilmCheckpointMetadata metadata{
            render_scene.width,
            render_scene.height,
            render_scene.spp,
            render_scene.spp,
            settings_hash
        };
        const yr::FilmCheckpointWriteResult state =
            yr::WriteFilmCheckpoint(scene.offline.checkpoint_state, *render_result.film, metadata);
        if (!state.ok) {
            std::cerr << "Checkpoint state write failed: " << state.error << '\n';
            return 1;
        }
        std::cout << "Checkpoint state: " << scene.offline.checkpoint_state.generic_string() << '\n';
    }

    const yr::ImageWriteResult write_result = yr::WriteImage(*render_result.film, tone_map, scene.film.output);
    if (!write_result.ok) {
        std::cerr << "Image write error: " << write_result.error << '\n';
        return 1;
    }

    const std::uint64_t total_samples = TotalSamples(*render_result.film);
    std::cout << "Rendered image: " << scene.film.output.generic_string() << '\n';
    std::cout << "Threads: " << render_result.stats.threads << '\n';
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
    std::cout << "Samples/sec: " << SafeRate(static_cast<double>(total_samples), render_result.stats.elapsed_seconds) << '\n';
    std::cout << "Rays/sec: " << SafeRate(static_cast<double>(render_result.stats.rays_traced), render_result.stats.elapsed_seconds) << '\n';
    std::cout << "BVH nodes: " << render_result.stats.bvh_nodes << '\n';
    std::cout << "BVH max depth: " << render_result.stats.bvh_max_depth << '\n';
    std::cout << "Shadow rays: " << render_result.stats.shadow_rays << '\n';
    std::cout << "Occluded shadow rays: " << render_result.stats.occluded_shadow_rays << '\n';
    std::cout << "BVH node tests: " << render_result.stats.bvh_node_tests << '\n';
    std::cout << "Triangle tests: " << render_result.stats.triangle_tests << '\n';
    std::cout << "Hits: " << render_result.stats.hits << '\n';
    std::cout << "Misses: " << render_result.stats.misses << '\n';
    std::cout << "Elapsed seconds: " << render_result.stats.elapsed_seconds << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        PrintHelp();
        return 0;
    }

    const std::string_view arg = argv[1];
    if (arg == "--help" || arg == "-h") {
        PrintHelp();
        return 0;
    }
    if (arg == "--version") {
        std::cout << yr::VersionString() << '\n';
        return 0;
    }
    if (arg == "render") {
        return RunRender(argc, argv);
    }

    std::cerr << "Unknown argument: " << arg << '\n';
    PrintHelp();
    return 2;
}
