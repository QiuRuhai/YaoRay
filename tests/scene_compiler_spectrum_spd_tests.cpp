#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene with a coatedconductor material where
// conductor.eta and conductor.k are spectrum params referencing SPD filenames
// (single non-numeric string values — the pattern from killeroo-coated-gold.pbrt).
yr::PbrtScene MakeSceneWithSpdSpectrum() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "spd_spectrum_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Coatedconductor material with spectrum SPD filename params
    // (simulate what the killeroo-coated scene does: "spectrum conductor.k" ["spds/Au.k.spd"])
    yr::PbrtEntity mat;
    mat.type = "coatedconductor";
    // SPD filename stored as a string (single non-numeric entry).
    // After the parser fix, type="spectrum" + strings=["spds/Au.k.spd"] + floats empty.
    // Before the fix, type="spectrum" + floats=[0.0f, 0.0f, 0.0f] (silently zeroed).
    // The test needs to work with the fixed parser that routes the string to strings[].
    yr::PbrtParam eta_param;
    eta_param.type = "spectrum";
    eta_param.name = "conductor.eta";
    eta_param.strings = {"spds/Au.eta.spd"};  // simulate post-fix parser output
    // floats is empty — the fixed parser stores the filename in strings, not floats.
    mat.params.push_back(eta_param);

    yr::PbrtParam k_param;
    k_param.type = "spectrum";
    k_param.name = "conductor.k";
    k_param.strings = {"spds/Au.k.spd"};
    mat.params.push_back(k_param);

    pbrt.named_materials["gold_coated"] = mat;

    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    sphere.material_name = "gold_coated";
    pbrt.shapes.push_back(sphere);

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

} // namespace

// When a spectrum param contains a non-numeric string (SPD filename),
// the compiler must emit a Warning about it being unsupported.
YR_TEST(scene_compiler_spectrum_spd_filename_emits_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithSpdSpectrum();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // Must emit a warning about the SPD filename
    YR_EXPECT_TRUE(
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "spectrum") ||
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "SPD") ||
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "spd") ||
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, ".spd")
    );
}

// The fallback eta must be non-zero (not the zeroed-out value from the bad parse).
YR_TEST(scene_compiler_spectrum_spd_eta_fallback_nonzero) {
    const yr::PbrtScene pbrt = MakeSceneWithSpdSpectrum();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    // The first material should be CoatedConductor with non-zero eta.
    YR_EXPECT_TRUE(!result.scene->materials.empty());
    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::CoatedConductor);

    // eta must be non-zero (fallback applied)
    YR_EXPECT_TRUE(m.eta.value.x != 0.0f || m.eta.value.y != 0.0f || m.eta.value.z != 0.0f);
}

// The fallback k must be non-zero.
YR_TEST(scene_compiler_spectrum_spd_k_fallback_nonzero) {
    const yr::PbrtScene pbrt = MakeSceneWithSpdSpectrum();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    YR_EXPECT_TRUE(!result.scene->materials.empty());
    const yr::RenderMaterial& m = result.scene->materials.front();

    // k must be non-zero (fallback applied, not silently zeroed)
    YR_EXPECT_TRUE(m.k.value.x != 0.0f || m.k.value.y != 0.0f || m.k.value.z != 0.0f);
}

// Compilation must succeed (no errors).
YR_TEST(scene_compiler_spectrum_spd_compiles_without_errors) {
    const yr::PbrtScene pbrt = MakeSceneWithSpdSpectrum();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
}
