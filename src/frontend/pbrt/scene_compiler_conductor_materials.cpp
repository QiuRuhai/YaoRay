#include "scene_compiler_internal.hpp"

#include <algorithm>

namespace yr::pbrt_compile {
namespace {

struct MetalEtaK {
    const char* symbol;
    Color3f eta;
    Color3f k;
};

constexpr MetalEtaK KnownMetals[] = {
    {"Au", {0.143f, 0.375f, 1.442f}, {3.983f, 2.386f, 1.603f}},
    {"Ag", {0.155f, 0.116f, 0.138f}, {4.818f, 3.122f, 2.146f}},
    {"Cu", {0.200f, 0.924f, 1.102f}, {3.912f, 2.448f, 2.137f}},
    {"Al", {1.657f, 0.881f, 0.521f}, {9.224f, 6.270f, 4.837f}},
};

std::string SpdSymbol(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::string basename = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = basename.find('.');
    return dot == std::string::npos ? basename : basename.substr(0, dot);
}

bool ResolveSpectrumFilename(
    const PbrtScene& scene,
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    std::vector<SceneDiagnostic>& diagnostics,
    Color3f& eta,
    Color3f& k
) {
    const PbrtParam* param = FindParam(params, param_name);
    if (param == nullptr || param->type != "spectrum" ||
        param->strings.empty() || !param->floats.empty()) {
        return false;
    }

    const std::string symbol = SpdSymbol(param->strings.front());
    for (const MetalEtaK& metal : KnownMetals) {
        if (symbol != metal.symbol) continue;
        eta = metal.eta;
        k = metal.k;
        diagnostics.push_back(Warning(
            scene,
            "Material." + param_name,
            "spectrum SPD file reference '" + param->strings.front() +
                "' not supported; approximating '" + symbol + "' as RGB eta/k"
        ));
        return true;
    }

    eta = {0.2f, 0.2f, 0.2f};
    k = {1.0f, 1.0f, 1.0f};
    diagnostics.push_back(Warning(
        scene,
        "Material." + param_name,
        "spectrum SPD file reference '" + param->strings.front() +
            "' not supported; using generic metal fallback value"
    ));
    return true;
}

void ResolveEtaK(
    const std::vector<PbrtParam>& params,
    const std::string& eta_name,
    const std::string& k_name,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    Color3f eta{0.2f, 0.2f, 0.2f};
    Color3f k{1.0f, 1.0f, 1.0f};
    const bool eta_is_spd = ResolveSpectrumFilename(
        scene, params, eta_name, diagnostics, eta, k
    );
    const bool k_is_spd = ResolveSpectrumFilename(
        scene, params, k_name, diagnostics, eta, k
    );
    if (eta_is_spd || k_is_spd) {
        material.eta.value = eta;
        material.k.value = k;
        return;
    }
    material.eta = TexParam3fFromParams(
        params, eta_name, eta, bindings, scene, diagnostics
    );
    material.k = TexParam3fFromParams(
        params, k_name, k, bindings, scene, diagnostics
    );
}

void CompileCoat(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
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

float ConductorF0(float eta, float k) {
    const float numerator = (eta - 1.0f) * (eta - 1.0f) + k * k;
    const float denominator = (eta + 1.0f) * (eta + 1.0f) + k * k;
    return denominator > 0.0f ? numerator / denominator : 1.0f;
}

} // namespace

void CompileConductorMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::Conductor;
    ResolveEtaK(params, "eta", "k", bindings, scene, diagnostics, material);
    const float roughness = FloatParam(FindParam(params, "roughness"), 0.0f);
    material.uroughness = TexParam1fFromParams(
        params, "uroughness", roughness, bindings, scene, diagnostics
    );
    material.vroughness = TexParam1fFromParams(
        params, "vroughness", material.uroughness.value, bindings, scene, diagnostics
    );
}

void CompileCoatedConductorMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material
) {
    material.kind = RenderMaterialKind::CoatedConductor;
    ResolveEtaK(
        params, "conductor.eta", "conductor.k", bindings, scene, diagnostics, material
    );
    material.reflectance.value = {
        ConductorF0(material.eta.value.x, material.k.value.x),
        ConductorF0(material.eta.value.y, material.k.value.y),
        ConductorF0(material.eta.value.z, material.k.value.z)
    };
    material.uroughness = TexParam1fFromParams(
        params, "conductor.roughness", 0.0f, bindings, scene, diagnostics
    );
    material.vroughness = material.uroughness;
    CompileCoat(params, bindings, scene, diagnostics, material);
}

} // namespace yr::pbrt_compile
