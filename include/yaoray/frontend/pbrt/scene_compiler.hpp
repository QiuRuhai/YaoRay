#pragma once

#include <optional>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderSceneIR> scene;
    RenderSettings settings;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompilePbrtScene(const PbrtScene& scene);

} // namespace yr
