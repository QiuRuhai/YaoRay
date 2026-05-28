#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene with one infinite light pointing at a
// filename whose extension (.exr) is NOT supported by LoadHdrTexture (which
// only accepts .hdr and .pfm). A sentinel sphere is included so the
// downstream "scene contains no geometry" guard does not fire.
yr::PbrtScene MakeSceneWithUnsupportedEnvmap() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "unsupported_envmap.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Sentinel sphere so the empty-geometry guard does not fire.
    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(sphere);

    // An infinite light pointing at a .exr file (unsupported format).
    yr::PbrtLightRecord light;
    light.light.type = "infinite";
    light.light.params.push_back(yr::PbrtParam{
        "string", "filename", {}, {}, {"sky.exr"}, {}});
    light.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(light);

    return pbrt;
}

bool HasDiagnosticContaining(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasFieldDiagnostic(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& field_needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.field.find(field_needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_envmap_unsupported_extension_degrades_to_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithUnsupportedEnvmap();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // No Error severity diagnostic referencing the infinite light path. We
    // check both message-text and field-tag to cover both phrasings used by
    // the compiler's diagnostic emitter.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, "infinite"));
    YR_EXPECT_TRUE(!HasFieldDiagnostic(
        result.diagnostics, yr::DiagnosticSeverity::Error, "infinite"));

    // A Warning was emitted whose field tags the infinite light so users
    // can grep for the failure mode.
    YR_EXPECT_TRUE(HasFieldDiagnostic(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "infinite"));

    // The Warning's body should mention the load-failed phrase for log
    // greppability.
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "HDR envmap load failed"));

    // Compilation succeeded because the broken envmap was degraded, not fatal.
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_envmap_unsupported_extension_creates_constant_fallback_texture) {
    const yr::PbrtScene pbrt = MakeSceneWithUnsupportedEnvmap();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }

    // The infinite light still registers an IR entry: a fallback texture
    // and env distribution were created so downstream lighting code has
    // something to sample (uniform sky).
    YR_EXPECT_TRUE(result.scene->environment_distributions.size() >= std::size_t{1});
    YR_EXPECT_TRUE(!result.scene->textures.empty());

    // The fallback texture should be a 1x1 RGB constant white.
    const yr::RenderTexture& tex = result.scene->textures.back();
    YR_EXPECT_EQ(tex.width, 1);
    YR_EXPECT_EQ(tex.height, 1);
    YR_EXPECT_EQ(tex.texels.size(), std::size_t{1});
    YR_EXPECT_NEAR(tex.texels[0].x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(tex.texels[0].y, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(tex.texels[0].z, 1.0f, 1.0e-6);

    // The environment record should be active and reference the synthesized
    // texture by the back-index used at registration time.
    YR_EXPECT_TRUE(result.scene->environment.active);
}
