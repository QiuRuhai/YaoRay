#pragma once

#include <optional>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderScene> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompileScene(const SceneDescription& scene);

} // namespace yr
