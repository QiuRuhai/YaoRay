#include "scene_compiler_internal.hpp"

namespace yr::pbrt_compile {
namespace {

std::string TextureNameInParam(const PbrtParam* param) {
    if (param == nullptr || param->type != "texture" || param->strings.empty()) return {};
    return param->strings.front();
}

template <typename Value>
const Value* FindBinding(
    const std::unordered_map<std::string, Value>& bindings,
    const std::string& name
) {
    const auto found = bindings.find(name);
    return found == bindings.end() ? nullptr : &found->second;
}

} // namespace

TexParam3f TexParam3fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    Color3f fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam3f result{fallback_value, -1};
    const PbrtParam* param = FindParam(params, param_name);
    if (param == nullptr) return result;

    const std::string texture_name = TextureNameInParam(param);
    if (!texture_name.empty()) {
        const int* texture_index = FindBinding(bindings.name_to_index, texture_name);
        if (texture_index == nullptr) {
            diagnostics.push_back(Warning(
                scene,
                "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"
            ));
        } else if (*texture_index >= 0) {
            result.texture = *texture_index;
        } else if (const Color3f* constant = FindBinding(bindings.constant_values, texture_name)) {
            result.value = *constant;
        }
        return result;
    }

    if (param->floats.size() >= 3) {
        result.value = {param->floats[0], param->floats[1], param->floats[2]};
    } else if (!param->floats.empty()) {
        result.value = {param->floats[0], param->floats[0], param->floats[0]};
    }
    return result;
}

TexParam1f TexParam1fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    float fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam1f result{fallback_value, -1};
    const PbrtParam* param = FindParam(params, param_name);
    if (param == nullptr) return result;

    const std::string texture_name = TextureNameInParam(param);
    if (!texture_name.empty()) {
        const int* texture_index = FindBinding(bindings.name_to_index, texture_name);
        if (texture_index == nullptr) {
            diagnostics.push_back(Warning(
                scene,
                "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"
            ));
        } else if (*texture_index >= 0) {
            result.texture = *texture_index;
        } else if (const Color3f* constant = FindBinding(bindings.constant_values, texture_name)) {
            result.value = constant->x;
        }
        return result;
    }

    if (!param->floats.empty()) result.value = param->floats.front();
    return result;
}

} // namespace yr::pbrt_compile
