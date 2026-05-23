#pragma once

#include <cstdint>

#include <yaoray/render/render_scene.hpp>

namespace yr {

std::uint64_t ComputeRenderSettingsHash(const RenderSceneIR& scene);

} // namespace yr
