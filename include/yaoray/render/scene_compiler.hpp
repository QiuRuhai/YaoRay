#pragma once

#include <optional>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderSceneIR> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompileSceneWorld(const SceneWorld& scene);
SceneCompileResult CompileScene(const SceneDescription& scene);

} // namespace yr
