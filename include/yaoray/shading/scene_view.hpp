#pragma once

#include <memory>
#include <span>

#include <yaoray/scene/geometry.hpp>
#include <yaoray/scene/handles.hpp>
#include <yaoray/scene/material.hpp>
#include <yaoray/scene/texture.hpp>

namespace yr {

struct BSSRDFTable;
struct MeasuredBrdf;
struct RenderSceneIR;

struct ShadingSceneView {
    GeometryView geometry;
    std::span<const RenderMaterial> materials;
    std::span<const RenderTexture> textures;
    std::span<const std::shared_ptr<MeasuredBrdf>> measured_brdfs;
    std::span<const std::shared_ptr<BSSRDFTable>> bssrdf_tables;

    const RenderMaterial* Find(MaterialHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < materials.size()
            ? &materials[static_cast<std::size_t>(index)]
            : nullptr;
    }

    const RenderTexture* Find(TextureHandle handle) const {
        const int index = handle.Value();
        return index >= 0 && static_cast<std::size_t>(index) < textures.size()
            ? &textures[static_cast<std::size_t>(index)]
            : nullptr;
    }
};

ShadingSceneView MakeShadingSceneView(const RenderSceneIR& scene);

} // namespace yr
