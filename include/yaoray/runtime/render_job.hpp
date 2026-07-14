#pragma once

#include <utility>

#include <yaoray/scene/render_options.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace yr {

struct RenderJob {
    RenderSceneIR scene;
    RenderSettings settings;
};

} // namespace yr
