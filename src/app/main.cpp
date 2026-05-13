#include <yaoray/backends/backend.hpp>
#include <yaoray/core/version.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene_parser.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
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
        << "The render command currently parses, compiles, and renders CPU debug PNG/PPM images.\n";
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

    const yr::RenderScene& render_scene = compile_result.scene.value();
    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
    std::cout << "BVH nodes: " << render_scene.bvh.nodes.size() << '\n';
    std::cout << "BVH max depth: " << render_scene.bvh.max_depth << '\n';

    const auto backend = yr::CreateRenderBackend(render_scene.backend);
    if (!backend) {
        std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.backend) << '\n';
        return 1;
    }

    const yr::RenderResult render_result = backend->Render(render_scene, yr::RenderRequest{});
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
    if (!render_result.film.has_value()) {
        std::cerr << "Render backend completed without a film.\n";
        return 1;
    }

    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(scene.film.tone_mapper),
        scene.film.exposure
    };
    const yr::ImageWriteResult write_result = yr::WriteImage(*render_result.film, tone_map, scene.film.output);
    if (!write_result.ok) {
        std::cerr << "Image write error: " << write_result.error << '\n';
        return 1;
    }

    std::cout << "Rendered image: " << scene.film.output.generic_string() << '\n';
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
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
