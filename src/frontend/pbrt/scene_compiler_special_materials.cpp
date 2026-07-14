#include "scene_compiler_internal.hpp"

#include <yaoray/shading/bssrdf.hpp>
#include <yaoray/shading/measured_brdf.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace yr::pbrt_compile {

void CompileSubsurfaceMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    const std::vector<PbrtParam>& params = entity.params;
    const std::string preset = StringParam(FindParam(params, "name"), "");
    const float scale = FloatParam(FindParam(params, "scale"), 1.0f);
    Color3f sigma_a;
    Color3f sigma_s;
    if (!preset.empty()) {
        if (preset != "Skin1") {
            diagnostics.push_back(Warning(
                scene,
                "Material.subsurface",
                "unsupported subsurface preset '" + preset + "'; using Skin1 coefficients."
            ));
        }
        sigma_a = {0.032f, 0.17f, 0.48f};
        sigma_s = {0.74f, 0.88f, 1.01f};
    } else {
        sigma_a = TexParam3fFromParams(
            params, "sigma_a", {0.0011f, 0.0024f, 0.014f},
            bindings, scene, diagnostics
        ).value;
        sigma_s = TexParam3fFromParams(
            params, "sigma_s", {2.55f, 3.21f, 3.77f},
            bindings, scene, diagnostics
        ).value;
    }
    sigma_a = sigma_a * scale;
    sigma_s = sigma_s * scale;
    const float eta = FloatParam(FindParam(params, "eta"), 1.33f);

    const bool scattering = sigma_s.x > 0.0f || sigma_s.y > 0.0f || sigma_s.z > 0.0f;
    if (!scattering) {
        diagnostics.push_back(MaterialFallbackWarning(scene, entity.type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = {0.5f, 0.5f, 0.5f};
        return;
    }

    auto table = std::make_unique<BSSRDFTable>(100, 64);
    ComputeBeamDiffusionBSSRDF(0.0f, eta, *table);
    material.kind = RenderMaterialKind::Subsurface;
    material.sigma_a = sigma_a;
    material.sigma_s = sigma_s;
    material.bssrdf_eta = eta;
    material.bssrdf_index = static_cast<int>(ir.bssrdf_tables.size());
    ir.bssrdf_tables.push_back(std::move(table));
}

void CompileMeasuredMaterial(
    const PbrtEntity& entity,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::Conductor;
    material.eta.value = {0.2f, 0.2f, 0.2f};
    material.k.value = {1.0f, 1.0f, 1.0f};

    const std::string filename = StringParam(FindParam(entity.params, "filename"), "");
    if (filename.empty()) {
        diagnostics.push_back(Warning(
            scene,
            "Material.measured",
            "measured material is missing 'string filename'; falling back to conductor."
        ));
        return;
    }

    const std::filesystem::path resolved = ResolveSceneResourcePath(
        scene, filename, entity.source_root
    );
    std::string error;
    std::optional<MeasuredBrdf> measured = LoadMeasuredBrdf(resolved.string(), error);
    if (!measured.has_value()) {
        diagnostics.push_back(Warning(
            scene,
            "Material.measured",
            "measured material: failed to load '" + resolved.string() + "': " + error +
                "; falling back to conductor."
        ));
        return;
    }

    material.kind = RenderMaterialKind::Measured;
    material.measured_index = static_cast<int>(ir.measured_brdfs.size());
    ir.measured_brdfs.push_back(
        std::make_unique<MeasuredBrdf>(std::move(*measured))
    );
}

} // namespace yr::pbrt_compile
