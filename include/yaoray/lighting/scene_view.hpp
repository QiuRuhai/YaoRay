#pragma once

#include <span>

#include <yaoray/scene/geometry.hpp>
#include <yaoray/scene/handles.hpp>
#include <yaoray/scene/light.hpp>
#include <yaoray/scene/texture.hpp>

namespace yr {

struct RenderSceneIR;

struct LightSceneView {
    GeometryView geometry;
    std::span<const RenderTexture> textures;
    std::span<const EmissivePrimitive> emissive_primitives;
    const RenderEnvironment& environment;
    std::span<const RenderEnvironmentDistribution> environment_distributions;
    std::span<const AnalyticLight> analytic_lights;
    const LightSamplingCache& sampling_cache;

    const RenderTexture* Find(TextureHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < textures.size()
            ? &textures[static_cast<std::size_t>(index)]
            : nullptr;
    }

    const EmissivePrimitive* Find(EmissiveLightHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < emissive_primitives.size()
            ? &emissive_primitives[static_cast<std::size_t>(index)]
            : nullptr;
    }
};

LightSceneView MakeLightSceneView(const RenderSceneIR& scene);

} // namespace yr
