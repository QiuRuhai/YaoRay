#include "scene_compiler_internal.hpp"

#include <cmath>
#include <system_error>
#include <utility>

namespace yr::pbrt_compile {
namespace {

constexpr float Pi = 3.14159265358979323846f;

std::filesystem::path NormalizeExistingOrAbsolute(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical.lexically_normal();

    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

} // namespace

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const PbrtScene& scene, std::string field, std::string message) {
    return SceneDiagnostic{
        DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic Warning(const PbrtScene& scene, std::string field, std::string message) {
    return SceneDiagnostic{
        DiagnosticSeverity::Warning, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic MaterialFallbackWarning(const PbrtScene& scene, const std::string& declared_kind) {
    return Warning(scene, "Material",
        "material kind '" + declared_kind +
        "' is not directly supported; applying degradation policy substitution.");
}

const PbrtParam* FindParam(const std::vector<PbrtParam>& params, const std::string& name) {
    for (const PbrtParam& param : params) {
        if (param.name == name) return &param;
    }
    return nullptr;
}

float FloatParam(const PbrtParam* param, float fallback) {
    return param == nullptr || param->floats.empty() ? fallback : param->floats[0];
}

Color3f RgbParam(const PbrtParam* param, Color3f fallback) {
    if (param == nullptr || param->floats.size() < 3) return fallback;
    return Color3f{param->floats[0], param->floats[1], param->floats[2]};
}

std::string StringParam(const PbrtParam* param, const std::string& fallback) {
    return param == nullptr || param->strings.empty() ? fallback : param->strings[0];
}

int IntParam(const PbrtParam* param, int fallback) {
    return param == nullptr || param->ints.empty() ? fallback : param->ints[0];
}

bool IsFinite(float value) {
    return std::isfinite(value);
}

std::filesystem::path ResolveSceneResourcePath(
    const PbrtScene& scene,
    const std::filesystem::path& value,
    const std::filesystem::path& declaring_root) {
    if (value.is_absolute()) return NormalizeExistingOrAbsolute(value);

    const std::filesystem::path main_root = NormalizeExistingOrAbsolute(scene.source_root);
    const std::filesystem::path main_candidate = NormalizeExistingOrAbsolute(main_root / value);
    if (std::filesystem::exists(main_candidate)) return main_candidate;

    std::filesystem::path normalized_declaring_root;
    if (!declaring_root.empty()) {
        normalized_declaring_root = NormalizeExistingOrAbsolute(declaring_root);
        if (normalized_declaring_root != main_root) {
            const std::filesystem::path candidate =
                NormalizeExistingOrAbsolute(normalized_declaring_root / value);
            if (std::filesystem::exists(candidate)) return candidate;
        }
    }

    for (const std::filesystem::path& root : scene.source_roots) {
        const std::filesystem::path normalized_root = NormalizeExistingOrAbsolute(root);
        if (normalized_root == main_root || normalized_root == normalized_declaring_root) continue;
        const std::filesystem::path candidate = NormalizeExistingOrAbsolute(normalized_root / value);
        if (std::filesystem::exists(candidate)) return candidate;
    }
    return main_candidate;
}

float FloatOrIntParam(const PbrtParam* param, float fallback) {
    if (param == nullptr) return fallback;
    if (!param->floats.empty()) return param->floats[0];
    if (!param->ints.empty()) return static_cast<float>(param->ints[0]);
    return fallback;
}

std::optional<double> NumericParamValue(const PbrtParam* param) {
    if (param == nullptr) return std::nullopt;
    if (!param->floats.empty()) return static_cast<double>(param->floats[0]);
    if (!param->ints.empty()) return static_cast<double>(param->ints[0]);
    return std::nullopt;
}

int BoundedIntParam(
    const PbrtParam* param,
    int fallback,
    int below_minimum_fallback,
    int above_maximum_fallback,
    int minimum,
    int maximum,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    const std::string& field) {
    const std::optional<double> value = NumericParamValue(param);
    if (!value.has_value()) return fallback;
    if (!std::isfinite(*value)) {
        diagnostics.push_back(Warning(
            scene, field, "value is not finite; using " + std::to_string(fallback)));
        return fallback;
    }
    if (*value < static_cast<double>(minimum)) {
        diagnostics.push_back(Warning(scene, field,
            "value is below " + std::to_string(minimum) + "; using " +
            std::to_string(below_minimum_fallback)));
        return below_minimum_fallback;
    }
    if (*value > static_cast<double>(maximum)) {
        diagnostics.push_back(Warning(scene, field,
            "value exceeds " + std::to_string(maximum) + "; using " +
            std::to_string(above_maximum_fallback)));
        return above_maximum_fallback;
    }
    return static_cast<int>(*value);
}

Point3f Point3FromParam(const PbrtParam* param, Point3f fallback) {
    if (param == nullptr || param->floats.size() < 3) return fallback;
    return Point3f{param->floats[0], param->floats[1], param->floats[2]};
}

} // namespace yr::pbrt_compile
