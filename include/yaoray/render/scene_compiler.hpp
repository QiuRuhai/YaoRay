#pragma once

#include <optional>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderSceneIR> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompilePbrtScene(const PbrtScene& scene);

} // namespace yr
