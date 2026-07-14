#include "scene_compiler_internal.hpp"

#include <yaoray/io/image_loader.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace yr::pbrt_compile {

bool CompileNormalMap(
    const PbrtEntity& entity,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    RenderMaterial& material,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* normal_map = FindParam(entity.params, "normalmap");
    if (normal_map == nullptr || normal_map->strings.empty()) return false;

    const std::filesystem::path resolved = ResolveSceneResourcePath(
        scene, normal_map->strings.front(), entity.source_root
    );
    std::string extension = resolved.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
        diagnostics.push_back(Error(
            scene,
            "Material.normalmap",
            "normal map must be a PNG or JPEG: " + resolved.generic_string()
        ));
        return false;
    }

    TextureLoadResult load = LoadLdrTexture(resolved, TextureColorSpace::Linear);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "Material.normalmap", load.error));
        return false;
    }

    material.normal_map = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    material.normal_scale = FloatParam(FindParam(entity.params, "normalscale"), 1.0f);
    return true;
}

} // namespace yr::pbrt_compile
