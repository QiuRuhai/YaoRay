#include "scene_compiler_internal.hpp"

#include <algorithm>

namespace yr::pbrt_compile {
namespace {

void CompileDiffuseMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::Diffuse;
    material.reflectance = TexParam3fFromParams(
        params, "reflectance", {0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics
    );
    if (FindParam(params, "Kd") != nullptr) {
        material.reflectance = TexParam3fFromParams(
            params, "Kd", material.reflectance.value, bindings, scene, diagnostics
        );
    }
}

void CompileDielectricMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::Dielectric;
    material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
    material.ior = FloatParam(FindParam(params, "index"), material.ior);
    material.uroughness = TexParam1fFromParams(
        params, "uroughness", 0.0f, bindings, scene, diagnostics
    );
    material.vroughness = TexParam1fFromParams(
        params, "vroughness", material.uroughness.value, bindings, scene, diagnostics
    );
}

void CompileCoatedDiffuseMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::CoatedDiffuse;
    material.reflectance = TexParam3fFromParams(
        params, "reflectance", {0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics
    );
    material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
    const std::string roughness_name = FindParam(params, "interface.roughness") != nullptr
        ? "interface.roughness"
        : "roughness";
    material.coating_roughness = TexParam1fFromParams(
        params, roughness_name, 0.0f, bindings, scene, diagnostics
    );
    material.coat_thickness = FloatParam(FindParam(params, "thickness"), 0.01f);
    material.coat_maxdepth = IntParam(FindParam(params, "maxdepth"), 10);
    material.coat_nsamples = std::max(1, IntParam(FindParam(params, "nsamples"), 1));
}

void CompileDiffuseFallback(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    diagnostics.push_back(MaterialFallbackWarning(scene, entity.type));
    material.kind = RenderMaterialKind::Diffuse;
    material.reflectance = TexParam3fFromParams(
        entity.params, "reflectance", {0.5f, 0.5f, 0.5f},
        bindings, scene, diagnostics
    );
}

} // namespace

int CompileMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    RenderMaterial material;
    const std::vector<PbrtParam>& params = entity.params;
    const std::string& type = entity.type;

    if (type == "matte" || type == "diffuse") {
        CompileDiffuseMaterial(params, bindings, scene, diagnostics, material);
    } else if (type == "conductor" || type == "metal") {
        CompileConductorMaterial(params, bindings, scene, diagnostics, material);
    } else if (type == "dielectric" || type == "glass") {
        CompileDielectricMaterial(params, bindings, scene, diagnostics, material);
    } else if (type == "thindielectric") {
        material.kind = RenderMaterialKind::ThinDielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
    } else if (type == "coateddiffuse") {
        CompileCoatedDiffuseMaterial(params, bindings, scene, diagnostics, material);
    } else if (type == "coatedconductor") {
        CompileCoatedConductorMaterial(params, bindings, scene, diagnostics, material);
    } else if (type == "diffusetransmission") {
        material.kind = RenderMaterialKind::DiffuseTransmission;
        material.reflectance = TexParam3fFromParams(
            params, "reflectance", {0.25f, 0.25f, 0.25f}, bindings, scene, diagnostics
        );
    } else if (type == "plastic" || type == "uber" || type == "substrate") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(
            params, "Kd", {0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics
        );
        if (FindParam(params, "reflectance") != nullptr) {
            material.reflectance = TexParam3fFromParams(
                params, "reflectance", material.reflectance.value,
                bindings, scene, diagnostics
            );
        }
    } else if (type == "subsurface") {
        CompileSubsurfaceMaterial(entity, bindings, ir, scene, diagnostics, material);
    } else if (type == "measured") {
        CompileMeasuredMaterial(entity, ir, scene, diagnostics, material);
    } else {
        CompileDiffuseFallback(entity, bindings, scene, diagnostics, material);
    }

    CompileNormalMap(entity, scene, ir, material, diagnostics);
    const int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
}

int EnsureDefaultMaterial(RenderSceneIR& ir, int& default_material_index) {
    if (default_material_index < 0) {
        default_material_index = static_cast<int>(ir.materials.size());
        ir.materials.push_back(RenderMaterial{});
    }
    return default_material_index;
}

int CloneMaterialWithEmission(RenderSceneIR& ir, int material_index, Color3f radiance) {
    RenderMaterial material = ir.materials[material_index];
    material.emission = radiance;
    const int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
}

int ResolveMaterialIndexForShape(
    const PbrtShapeRecord& record,
    const std::unordered_map<std::string, int>& material_name_to_index,
    const TextureBindings& texture_bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    int& default_material_index,
    const std::string& diagnostic_field
) {
    if (!record.material_name.empty()) {
        const auto found = material_name_to_index.find(record.material_name);
        if (found != material_name_to_index.end()) return found->second;
        diagnostics.push_back(Warning(
            scene, diagnostic_field, "undefined material: " + record.material_name
        ));
        return EnsureDefaultMaterial(ir, default_material_index);
    }
    if (record.inline_material.has_value()) {
        return CompileMaterial(
            *record.inline_material, texture_bindings, ir, scene, diagnostics
        );
    }
    return EnsureDefaultMaterial(ir, default_material_index);
}

} // namespace yr::pbrt_compile
