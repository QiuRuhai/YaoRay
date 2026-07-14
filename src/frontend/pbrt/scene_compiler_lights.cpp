#include "scene_compiler_internal.hpp"

#include <yaoray/io/image_loader.hpp>
#include <yaoray/lighting/environment.hpp>
#include <yaoray/shading/texture.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace yr::pbrt_compile {
namespace {

Color3f ReadScale(const std::vector<PbrtParam>& params, bool accept_scalar) {
    const PbrtParam* scale_param = FindParam(params, "scale");
    if (scale_param == nullptr) return Color3f{1.0f, 1.0f, 1.0f};
    if (scale_param->floats.size() >= 3) {
        return Color3f{
            scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
    }
    if (accept_scalar && !scale_param->floats.empty()) {
        const float scale = scale_param->floats[0];
        return Color3f{scale, scale, scale};
    }
    return Color3f{1.0f, 1.0f, 1.0f};
}

Color3f Multiply(Color3f lhs, Color3f rhs) {
    return Color3f{lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z};
}

bool CompileEnvironmentLight(
    const PbrtLightRecord& record,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics) {
    const PbrtParam* filename = FindParam(record.light.params, "filename");
    if (filename == nullptr || filename->strings.empty()) {
        filename = FindParam(record.light.params, "mapname");
    }
    if (filename == nullptr || filename->strings.empty()) {
        diagnostics.push_back(Error(scene, "LightSource.infinite",
            "infinite light requires a filename (or mapname)"));
        return false;
    }

    const std::filesystem::path resolved = ResolveSceneResourcePath(
        scene, filename->strings[0], record.light.source_root);
    TextureLoadResult load = LoadHdrTexture(resolved);
    if (!load.ok) {
        diagnostics.push_back(Warning(scene, "LightSource.infinite",
            "HDR envmap load failed (" + load.error +
            "); degrading to constant 1x1 white sky (L and scale params still apply)"));
        load.texture.kind = RenderTextureKind::Image;
        load.texture.width = 1;
        load.texture.height = 1;
        load.texture.texels = {Color4f{1.0f, 1.0f, 1.0f, 1.0f}};
        load.texture.color_space = TextureColorSpace::Linear;
    }

    const int texture_index = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    const int distribution_index = static_cast<int>(ir.environment_distributions.size());
    ir.environment_distributions.push_back(
        BuildEnvironmentDistribution(ir.textures[texture_index]));

    const Color3f radiance = RgbParam(
        FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
    ir.environment.active = true;
    ir.environment.radiance = Multiply(radiance, ReadScale(record.light.params, true));
    ir.environment.strength = 1.0f;
    ir.environment.rotation_radians = 0.0f;
    ir.environment.texture_index = texture_index;
    ir.environment.distribution_index = distribution_index;

    if (FindParam(record.light.params, "portals") != nullptr) {
        diagnostics.push_back(Warning(scene, "LightSource.infinite",
            "portal sampling is not supported in M1; portals parameter ignored"));
    }
    return true;
}

AnalyticLight CompilePointLight(const PbrtLightRecord& record) {
    AnalyticLight light;
    light.kind = AnalyticLightKind::Point;
    const Point3f from = Point3FromParam(
        FindParam(record.light.params, "from"), Point3f{0.0f, 0.0f, 0.0f});
    light.position = TransformPoint(record.light_to_world, from);
    const Color3f intensity = RgbParam(
        FindParam(record.light.params, "I"), Color3f{1.0f, 1.0f, 1.0f});
    light.intensity = Multiply(intensity, ReadScale(record.light.params, false));
    return light;
}

AnalyticLight CompileDistantLight(
    const PbrtLightRecord& record,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics) {
    AnalyticLight light;
    light.kind = AnalyticLightKind::Distant;
    const Point3f from = Point3FromParam(
        FindParam(record.light.params, "from"), Point3f{0.0f, 0.0f, 0.0f});
    const Point3f to = Point3FromParam(
        FindParam(record.light.params, "to"), Point3f{0.0f, 0.0f, 1.0f});
    Vec3f direction{to.x - from.x, to.y - from.y, to.z - from.z};
    if (LengthSquared(direction) == 0.0f) {
        diagnostics.push_back(Warning(scene, "LightSource.distant",
            "distant light has zero direction; defaulting to (0,-1,0)"));
        direction = Vec3f{0.0f, -1.0f, 0.0f};
    }
    light.direction = Normalize(TransformVector(record.light_to_world, direction));
    const Color3f radiance = RgbParam(
        FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
    light.intensity = Multiply(radiance, ReadScale(record.light.params, true));
    return light;
}

AnalyticLight CompileSpotLight(
    const PbrtLightRecord& record,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics) {
    AnalyticLight light;
    light.kind = AnalyticLightKind::Spot;
    const Point3f from = Point3FromParam(
        FindParam(record.light.params, "from"), Point3f{0.0f, 0.0f, 0.0f});
    const Point3f to = Point3FromParam(
        FindParam(record.light.params, "to"), Point3f{0.0f, 0.0f, 1.0f});
    light.position = TransformPoint(record.light_to_world, from);

    Vec3f direction{to.x - from.x, to.y - from.y, to.z - from.z};
    if (LengthSquared(direction) == 0.0f) {
        diagnostics.push_back(Warning(scene, "LightSource.spot",
            "spot light has zero direction; defaulting to (0,0,1)"));
        direction = Vec3f{0.0f, 0.0f, 1.0f};
    }
    light.direction = Normalize(TransformVector(record.light_to_world, direction));
    const Color3f intensity = RgbParam(
        FindParam(record.light.params, "I"), Color3f{1.0f, 1.0f, 1.0f});
    light.intensity = Multiply(intensity, ReadScale(record.light.params, true));

    const float cone = FloatParam(FindParam(record.light.params, "coneangle"), 30.0f);
    const float delta = FloatParam(FindParam(record.light.params, "conedeltaangle"), 5.0f);
    light.cone_angle = std::cos(DegreesToRadians(cone));
    light.cone_cos_inner = std::cos(DegreesToRadians(std::max(0.0f, cone - delta)));
    return light;
}

} // namespace

void CompileLights(
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics) {
    for (const PbrtLightRecord& record : scene.lights) {
        if (record.light.type == "point") {
            ir.analytic_lights.push_back(CompilePointLight(record));
        } else if (record.light.type == "infinite") {
            CompileEnvironmentLight(record, scene, ir, diagnostics);
        } else if (record.light.type == "distant") {
            ir.analytic_lights.push_back(CompileDistantLight(record, scene, diagnostics));
        } else if (record.light.type == "spot") {
            ir.analytic_lights.push_back(CompileSpotLight(record, scene, diagnostics));
        } else {
            diagnostics.push_back(Warning(scene, "LightSource",
                "unsupported LightSource type: " + record.light.type));
        }
    }
}

} // namespace yr::pbrt_compile
