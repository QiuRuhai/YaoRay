#include <yaoray/backends/backend.hpp>
#include <yaoray/core/diagnostic.hpp>
#include <yaoray/core/version.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void PrintHelp() {
    std::cout
        << "YaoRay " << yr::VersionString() << '\n'
        << '\n'
        << "Usage:\n"
        << "  yaoray --help\n"
        << "  yaoray --version\n"
        << "  yaoray render <scene.pbrt> [--backend cpu|cuda]\n";
}

void PrintRenderHelp() {
    std::cout
        << "Usage:\n"
        << "  yaoray render <scene.pbrt> [--backend cpu|cuda]\n"
        << '\n'
        << "The render command parses, compiles, and renders PBRT scenes.\n";
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

    // --- Parse PBRT scene ---
    yr::PbrtSceneLoadResult parse_result = yr::LoadPbrtScene(scene_path);
    if (yr::HasSceneErrors(parse_result.diagnostics) || !parse_result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(parse_result.diagnostics) << '\n';
        return 1;
    }

    // --- Compile to render IR ---
    yr::SceneCompileResult compile_result = yr::CompilePbrtScene(parse_result.scene.value());
    if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
        return 1;
    }

    yr::RenderSceneIR render_scene = std::move(compile_result.scene.value());

    // --- Print compile warnings (non-fatal) ---
    if (!compile_result.diagnostics.empty()) {
        std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
    }

    // --- Apply backend override ---
    if (backend_override) {
        render_scene.requested_backend = *backend_override;
    }

    // --- Print scene info ---
    int total_triangles = 0;
    for (const auto& prim : render_scene.primitives) {
        total_triangles += static_cast<int>(prim.index_count / 3);
    }

    std::cout << "Scene parsed successfully: " << scene_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
    std::cout << "Integrator: " << yr::RenderIntegratorName(render_scene.integrator) << '\n';
    std::cout << "Compiled triangles: " << total_triangles << '\n';
    std::cout << "Compiled materials: " << render_scene.materials.size() << '\n';
    std::cout << "Compiled textures: " << render_scene.textures.size() << '\n';
    std::cout << "Texture memory MiB: " << BytesToMiB(EstimateTextureMemoryBytes(render_scene)) << '\n';

    // --- Create backend and prepare ---
    const auto backend = yr::CreateRenderBackend(render_scene.requested_backend);
    if (!backend) {
        std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
        return 1;
    }

    yr::BackendPrepareResult prepare_result = backend->Prepare(std::move(render_scene));
    if (!prepare_result.ok || prepare_result.scene == nullptr) {
        std::cerr << "Render backend preparation failed: "
                  << (prepare_result.error.empty() ? "unknown error" : prepare_result.error)
                  << '\n';
        return 1;
    }
    std::cout << "Prepare seconds: " << prepare_result.elapsed_seconds << '\n';

    const yr::RenderSceneIR& prepared_scene_ir = prepare_result.scene->SourceScene();

    // --- Render ---
    yr::RenderRequest render_request;
    render_request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film&) -> yr::RenderProgressDecision {
        std::cout << "Progress: " << progress.completed_spp << "/" << progress.target_spp
                  << " spp elapsed=" << progress.elapsed_seconds << "s\n";
        return yr::RenderProgressDecision{};
    };

    const yr::RenderResult render_result = backend->Render(*prepare_result.scene, render_request);
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
    if (!render_result.film.has_value()) {
        std::cerr << "Render backend completed without a film.\n";
        return 1;
    }

    // --- Write output image ---
    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(prepared_scene_ir.film.tone_mapper),
        prepared_scene_ir.film.exposure
    };

    const yr::ImageWriteResult write_result = yr::WriteImage(*render_result.film, tone_map, prepared_scene_ir.film.output);
    if (!write_result.ok) {
        std::cerr << "Image write error: " << write_result.error << '\n';
        return 1;
    }

    // --- Print stats ---
    std::cout << "Rendered image: " << prepared_scene_ir.film.output.generic_string() << '\n';
    std::cout << "Threads: " << render_result.stats.threads << '\n';
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
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
