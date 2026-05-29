#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene that declares one loopsubdiv shape.
// The base mesh is a tetrahedron (4 vertices, 4 triangles = 12 indices).
yr::PbrtScene MakeSceneWithLoopSubdiv(int levels = 1) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "loopsubdiv_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Base mesh: a tetrahedron.
    // Vertices (4 vertices * 3 floats = 12 floats)
    std::vector<float> positions = {
         1.0f,  0.0f, -0.707f,   // v0
        -1.0f,  0.0f, -0.707f,   // v1
         0.0f,  1.0f,  0.707f,   // v2
         0.0f, -1.0f,  0.707f,   // v3
    };
    // Indices (4 triangles * 3 indices = 12 ints)
    std::vector<int> indices = {
        0, 1, 2,
        0, 2, 3,
        0, 3, 1,
        1, 3, 2,
    };

    yr::PbrtShapeRecord record;
    record.shape.type = "loopsubdiv";
    record.shape.params.push_back(yr::PbrtParam{"integer", "levels", {}, {levels}, {}, {}});
    record.shape.params.push_back(yr::PbrtParam{"point3", "P", positions, {}, {}, {}});
    record.shape.params.push_back(yr::PbrtParam{"integer", "indices", {}, indices, {}, {}});
    record.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(record);

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

// A loopsubdiv shape compiles to triangle primitives (base mesh passthrough).
YR_TEST(scene_compiler_loopsubdiv_produces_triangles) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(3);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    // The base tetrahedron has 4 triangles = 12 indices.
    YR_EXPECT_TRUE(!result.scene->primitives.empty());
    YR_EXPECT_TRUE(!result.scene->indices.empty());
    YR_EXPECT_TRUE(result.scene->indices.size() % 3 == 0);

    // 4 triangles in the base mesh => 12 indices
    YR_EXPECT_EQ(result.scene->indices.size(), std::size_t{12});
}

// A loopsubdiv must emit a Warning that subdivision is not applied.
YR_TEST(scene_compiler_loopsubdiv_emits_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(3);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // Must emit a warning mentioning subdivision is not applied
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "subdivision"));
}

// loopsubdiv must NOT emit an "unsupported shape type" warning (it's now handled).
YR_TEST(scene_compiler_loopsubdiv_no_unsupported_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(1);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "unsupported shape type: loopsubdiv"));
}

// Compilation must succeed (no errors).
YR_TEST(scene_compiler_loopsubdiv_compiles_without_errors) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(1);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
}

// loopsubdiv with no indices falls back gracefully (Warning, no crash).
YR_TEST(scene_compiler_loopsubdiv_missing_indices_warns) {
    yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(1);
    // Remove the indices param so the compiler has no index data.
    pbrt.shapes[0].shape.params.erase(
        std::remove_if(pbrt.shapes[0].shape.params.begin(), pbrt.shapes[0].shape.params.end(),
            [](const yr::PbrtParam& p) { return p.name == "indices"; }),
        pbrt.shapes[0].shape.params.end());

    // Add a sentinel sphere so the scene is not empty.
    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(sphere);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // No errors (the missing-indices case is a warning, not fatal)
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    // Must emit a warning about missing P or indices
    YR_EXPECT_TRUE(
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "loopsubdiv") ||
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "requires P") ||
        HasDiagnosticContaining(result.diagnostics, yr::DiagnosticSeverity::Warning, "subdivision")
    );
}
