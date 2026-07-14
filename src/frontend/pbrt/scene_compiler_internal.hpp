#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace yr::pbrt_compile {

struct TextureBindings {
    std::unordered_map<std::string, int> name_to_index;
    std::unordered_map<std::string, Color3f> constant_values;
};

float DegreesToRadians(float degrees);
SceneDiagnostic Error(const PbrtScene& scene, std::string field, std::string message);
SceneDiagnostic Warning(const PbrtScene& scene, std::string field, std::string message);
SceneDiagnostic MaterialFallbackWarning(const PbrtScene& scene, const std::string& declared_kind);

const PbrtParam* FindParam(const std::vector<PbrtParam>& params, const std::string& name);
float FloatParam(const PbrtParam* param, float fallback);
Color3f RgbParam(const PbrtParam* param, Color3f fallback);
std::string StringParam(const PbrtParam* param, const std::string& fallback);
int IntParam(const PbrtParam* param, int fallback);
bool IsFinite(float value);
float FloatOrIntParam(const PbrtParam* param, float fallback);
std::optional<double> NumericParamValue(const PbrtParam* param);
Point3f Point3FromParam(const PbrtParam* param, Point3f fallback);

std::filesystem::path ResolveSceneResourcePath(
    const PbrtScene& scene,
    const std::filesystem::path& value,
    const std::filesystem::path& declaring_root = {});

int BoundedIntParam(
    const PbrtParam* param,
    int fallback,
    int below_minimum_fallback,
    int above_maximum_fallback,
    int minimum,
    int maximum,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    const std::string& field);

void CompileFilm(const PbrtScene& scene, RenderSettings& settings,
                 std::vector<SceneDiagnostic>& diagnostics);
void CompileCamera(const PbrtScene& scene, RenderSceneIR& ir,
                   std::vector<SceneDiagnostic>& diagnostics);
void CompileIntegrator(const PbrtScene& scene, RenderSettings& settings,
                       std::vector<SceneDiagnostic>& diagnostics);
void CompileSampler(const PbrtScene& scene, RenderSettings& settings,
                    std::vector<SceneDiagnostic>& diagnostics);
void CompileLights(const PbrtScene& scene, RenderSceneIR& ir,
                   std::vector<SceneDiagnostic>& diagnostics);

TextureBindings CompileTextures(const PbrtScene& scene, RenderSceneIR& ir,
                                std::vector<SceneDiagnostic>& diagnostics);
TexParam3f TexParam3fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    Color3f fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
TexParam1f TexParam1fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    float fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompileNormalMap(
    const PbrtEntity& entity,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    RenderMaterial& material,
    std::vector<SceneDiagnostic>& diagnostics);

void CompileConductorMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material);
void CompileCoatedConductorMaterial(
    const std::vector<PbrtParam>& params,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material);
void CompileSubsurfaceMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material);
void CompileMeasuredMaterial(
    const PbrtEntity& entity,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    RenderMaterial& material);

int CompileMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
int EnsureDefaultMaterial(RenderSceneIR& ir, int& default_material_index);
int CloneMaterialWithEmission(RenderSceneIR& ir, int material_index, Color3f radiance);
int ResolveMaterialIndexForShape(
    const PbrtShapeRecord& record,
    const std::unordered_map<std::string, int>& material_name_to_index,
    const TextureBindings& texture_bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    int& default_material_index,
    const std::string& diagnostic_field);

bool CompileShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    const std::string& diagnostic_field);
bool CompileTriangleMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompileLoopSubdivShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompilePlyMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompileSphereShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompileDiskShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics);
bool CompileInstances(
    const PbrtScene& scene,
    const std::unordered_map<std::string, int>& material_name_to_index,
    const TextureBindings& texture_bindings,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics,
    int& default_material_index);

} // namespace yr::pbrt_compile
