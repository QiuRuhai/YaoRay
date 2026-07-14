#pragma once

#include <cstdint>

#include <yaoray/scene/render_scene.hpp>

namespace yr {

std::uint64_t ComputeRenderSettingsHash(
    const RenderSceneIR& scene,
    const RenderSettings& settings);

} // namespace yr
