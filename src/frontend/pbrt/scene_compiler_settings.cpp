#include "scene_compiler_internal.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace yr::pbrt_compile {
namespace {

constexpr int MaxFilmResolution = 16384;

int SampleCountParam(
    const PbrtParam* param,
    int fallback,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    const std::string& field) {
    return BoundedIntParam(param, fallback, fallback, fallback, 1,
        std::numeric_limits<int>::max(), scene, diagnostics, field);
}

int MultiplySampleCounts(
    int x,
    int y,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics) {
    const long long product = static_cast<long long>(x) * static_cast<long long>(y);
    if (product < 1 || product > static_cast<long long>(std::numeric_limits<int>::max())) {
        diagnostics.push_back(Warning(scene, "Sampler.samples",
            "sample count product is outside supported integer range; using 1"));
        return 1;
    }
    return static_cast<int>(product);
}

void EnforceSobolSampleCount(
    const PbrtScene& scene,
    RenderSettings& settings,
    std::vector<SceneDiagnostic>& diagnostics) {
    if (settings.sampler != RenderSamplerKind::OwenSobol &&
        settings.sampler != RenderSamplerKind::ZSobol) return;
    const unsigned spp = static_cast<unsigned>(std::max(1, settings.spp));
    if (std::has_single_bit(spp)) return;
    const unsigned rounded = std::bit_ceil(spp);
    if (rounded > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        settings.spp = 1;
    } else {
        settings.spp = static_cast<int>(rounded);
    }
    diagnostics.push_back(Warning(scene, "Sampler.pixelsamples",
        "Sobol sample count must be a power of two; rounding up to " +
            std::to_string(settings.spp)));
}

} // namespace

void CompileFilm(
    const PbrtScene& scene,
    RenderSettings& settings,
    std::vector<SceneDiagnostic>& diagnostics) {
    const auto& params = scene.film.params;
    settings.width = BoundedIntParam(FindParam(params, "xresolution"), 1280, 1,
        MaxFilmResolution, 1, MaxFilmResolution, scene, diagnostics, "Film.xresolution");
    settings.height = BoundedIntParam(FindParam(params, "yresolution"), 720, 1,
        MaxFilmResolution, 1, MaxFilmResolution, scene, diagnostics, "Film.yresolution");

    const PbrtParam* filename = FindParam(params, "filename");
    settings.film.output = filename != nullptr && !filename->strings.empty()
        ? scene.source_root / filename->strings[0]
        : scene.source_root / "out" / (scene.source_path.stem().string() + ".png");

    const float iso = FloatParam(FindParam(params, "iso"), 100.0f);
    if (iso > 0.0f) settings.film.exposure = std::log2(iso / 100.0f);
}

void CompileCamera(
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics) {
    float fov = FloatOrIntParam(FindParam(scene.camera.params, "fov"), 45.0f);
    if (!IsFinite(fov) || fov <= 0.0f || fov >= 180.0f) {
        diagnostics.push_back(Warning(scene, "Camera.fov",
            "field of view is outside renderable range (0, 180); using 45"));
        fov = 45.0f;
    }
    ir.camera.fov_y_radians = DegreesToRadians(fov);

    const Mat4f world_from_camera = Inverse(scene.camera_transform);
    ir.camera.origin = Point3f{
        world_from_camera.m[12], world_from_camera.m[13], world_from_camera.m[14]};
    ir.camera.right = Normalize(Vec3f{
        world_from_camera.m[0], world_from_camera.m[1], world_from_camera.m[2]});
    ir.camera.up = Normalize(Vec3f{
        world_from_camera.m[4], world_from_camera.m[5], world_from_camera.m[6]});
    ir.camera.forward = Normalize(Vec3f{
        world_from_camera.m[8], world_from_camera.m[9], world_from_camera.m[10]});
}

void CompileIntegrator(
    const PbrtScene& scene,
    RenderSettings& settings,
    std::vector<SceneDiagnostic>& diagnostics) {
    settings.integrator = RenderIntegratorKind::Path;
    settings.max_depth = BoundedIntParam(FindParam(scene.integrator.params, "maxdepth"),
        5, 0, 5, 0, std::numeric_limits<int>::max(), scene, diagnostics,
        "Integrator.maxdepth");
}

void CompileSampler(
    const PbrtScene& scene,
    RenderSettings& settings,
    std::vector<SceneDiagnostic>& diagnostics) {
    if (scene.sampler.type == "stratified") {
        settings.sampler = RenderSamplerKind::Stratified;
    } else if (scene.sampler.type == "sobol" || scene.sampler.type == "paddedsobol") {
        settings.sampler = RenderSamplerKind::OwenSobol;
    } else if (scene.sampler.type == "zsobol") {
        settings.sampler = RenderSamplerKind::ZSobol;
    } else {
        settings.sampler = RenderSamplerKind::Independent;
    }

    const auto& params = scene.sampler.params;
    const PbrtParam* pixel_samples = FindParam(params, "pixelsamples");
    if (pixel_samples != nullptr) {
        settings.spp = SampleCountParam(pixel_samples, 1, scene, diagnostics, "Sampler.pixelsamples");
        EnforceSobolSampleCount(scene, settings, diagnostics);
        return;
    }

    const PbrtParam* x_samples = FindParam(params, "xsamples");
    const PbrtParam* y_samples = FindParam(params, "ysamples");
    if (x_samples == nullptr && y_samples == nullptr) {
        EnforceSobolSampleCount(scene, settings, diagnostics);
        return;
    }

    const int x = SampleCountParam(x_samples, 1, scene, diagnostics, "Sampler.xsamples");
    const int y = SampleCountParam(y_samples, 1, scene, diagnostics, "Sampler.ysamples");
    settings.spp = MultiplySampleCounts(x, y, scene, diagnostics);
    EnforceSobolSampleCount(scene, settings, diagnostics);
}

} // namespace yr::pbrt_compile
