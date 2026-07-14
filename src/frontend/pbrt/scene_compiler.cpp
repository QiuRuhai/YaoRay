#include <yaoray/frontend/pbrt/scene_compiler.hpp>

#include "scene_compiler_internal.hpp"

#include <string>
#include <unordered_map>

namespace yr {
using namespace pbrt_compile;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SceneCompileResult CompilePbrtScene(const PbrtScene& scene) {
    SceneCompileResult result;
    RenderSceneIR ir;
    auto& diagnostics = result.diagnostics;
    int default_material_index = -1;

    // 1. Film, camera, integrator, sampler
    CompileFilm(scene, result.settings, diagnostics);
    CompileCamera(scene, ir, diagnostics);
    CompileIntegrator(scene, result.settings, diagnostics);
    CompileSampler(scene, result.settings, diagnostics);

    // 2. Compile named textures -> name-to-index map (+ constant value side-map).
    TextureBindings texture_bindings = CompileTextures(scene, ir, diagnostics);

    // Early-exit if texture loading failed (e.g. missing imagemap files).
    if (HasSceneErrors(diagnostics)) {
        return result;
    }

    // 3. Compile named materials -> build name->index map
    std::unordered_map<std::string, int> material_name_to_index;
    for (const auto& [name, entity] : scene.named_materials) {
        int idx = CompileMaterial(entity, texture_bindings, ir, scene, diagnostics);
        material_name_to_index[name] = idx;
    }

    // 4. Compile shapes
    for (const PbrtShapeRecord& record : scene.shapes) {
        int mat_idx = ResolveMaterialIndexForShape(
            record, material_name_to_index, texture_bindings, ir, scene, diagnostics,
            default_material_index, "Shape");

        CompileShape(record, mat_idx, ir, scene, diagnostics, "Shape");
    }

    // 5. Compile object instances
    CompileInstances(scene, material_name_to_index, texture_bindings, ir, diagnostics, default_material_index);

    // 6. Compile analytic light sources
    CompileLights(scene, ir, diagnostics);

    if (ir.primitives.empty() && ir.spheres.empty()) {
        diagnostics.push_back(Error(scene, "Shape", "scene contains no geometry"));
    }

    if (HasSceneErrors(diagnostics)) {
        return result;
    }

    result.scene = std::move(ir);
    return result;
}

} // namespace yr
