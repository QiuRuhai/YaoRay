#include <yaoray/frontends/scene_frontend.hpp>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/scene_parser.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace yr {
namespace {

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

SceneDiagnostic FrontendError(const std::filesystem::path& path, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, path, "", std::move(message)};
}

} // namespace

SceneWorldLoadResult LoadSceneWorldFile(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    if (extension == ".pbrt") {
        return LoadPbrtSceneFile(path);
    }
    if (extension == ".toml") {
        SceneWorldLoadResult result;
        SceneLoadResult loaded = LoadSceneFile(path);
        result.diagnostics = std::move(loaded.diagnostics);
        if (!HasSceneErrors(result.diagnostics) && loaded.scene.has_value()) {
            result.scene = BuildSceneWorld(loaded.scene.value());
        }
        return result;
    }

    SceneWorldLoadResult result;
    result.diagnostics.push_back(FrontendError(path, "unsupported scene file extension: " + extension));
    return result;
}

void ApplyBackendOverride(SceneWorld& scene, RenderBackendKind backend) {
    scene.render.backend = backend;
}

} // namespace yr
