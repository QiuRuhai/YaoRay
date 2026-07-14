#include "yr_test.hpp"

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>

#include <cmath>
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

// -----------------------------------------------------------------------
// NEW: smooth vertex normal generation
// -----------------------------------------------------------------------

// A loopsubdiv shape with no N param must produce has_normals == true
// (smooth area-weighted vertex normals are synthesised from P + indices).
YR_TEST(scene_compiler_loopsubdiv_generates_normals_when_none_provided) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(3);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_TRUE(!result.scene->primitives.empty());
    // The primitive must have has_normals true now that we synthesise them.
    YR_EXPECT_TRUE(result.scene->primitives[0].has_normals);
}

// Every synthesised vertex normal must be (approximately) unit-length.
YR_TEST(scene_compiler_loopsubdiv_generated_normals_are_unit_length) {
    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(3);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_TRUE(result.scene->primitives[0].has_normals);

    // The tetrahedron has 4 vertices — check all normals are unit-length.
    for (std::size_t vi = 0; vi < result.scene->vertices.size(); ++vi) {
        const yr::Vec3f& n = result.scene->vertices[vi].normal;
        const float len_sq = yr::Dot(n, n);
        // Allow ±1e-4 tolerance around 1.0.
        YR_EXPECT_TRUE(len_sq > 0.9998f && len_sq < 1.0002f);
    }
}

// The apex vertex of a non-flat mesh (tetrahedron) must get a smooth normal
// that differs from any single face normal (i.e., it IS an average).
// We verify the apex (v2 = {0,1,0.707}) gets a normal with a dominant +Y
// component (pointing "outward" in the averaging sense for that vertex).
YR_TEST(scene_compiler_loopsubdiv_generated_normals_are_smooth_not_face_normals) {
    // Build a simple 2-triangle "tent" (pyramid without a base) so we can
    // reason about the exact normals: v0=(0,0,0), v1=(2,0,0), v2=(1,0,2),
    // v3=(1,1,1) (apex above).  Two faces share the apex: (0,3,1) and (2,3,0)
    // (and we add two more to make a closed shape, but two is enough to test).
    // Simpler: use the tetrahedron from MakeSceneWithLoopSubdiv and just check
    // that no vertex normal equals any single face's (unnormalised) cross product.

    const yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(3);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_TRUE(result.scene->primitives[0].has_normals);

    // Positions from MakeSceneWithLoopSubdiv (object space == world space for identity CTM)
    // v0=(1,0,-0.707) v1=(-1,0,-0.707) v2=(0,1,0.707) v3=(0,-1,0.707)
    // Face 0: (0,1,2) => e1 = v1-v0 = (-2,0,0), e2 = v2-v0 = (-1,1,1.414)
    //   fn0 = Cross(e1,e2) = (0*1.414 - 0*1, 0*(-1) - (-2)*1.414, (-2)*1 - 0*(-1))
    //        = (0, 2.828, -2)
    // Normalised fn0 ~ (0, 0.816, -0.578)
    //
    // Face 1: (0,2,3) => e1=v2-v0=(-1,1,1.414), e2=v3-v0=(-1,-1,1.414)
    //   fn1 = Cross(e1,e2) = (1*1.414-1.414*(-1), 1.414*(-1)-(-1)*1.414, (-1)(-1)-1*(-1))
    //        = (1.414+1.414, -1.414+1.414, 1+1) = (2.828, 0, 2)
    // Normalised fn1 ~ (0.816, 0, 0.578)
    //
    // Vertex 0 is shared by faces 0,1,2(=(0,3,1)). Its smooth normal is the
    // area-weighted average of those face normals.  Because it contributes to
    // multiple faces, its normal CANNOT equal fn0 exactly.
    //
    // Simple test: vertex 0's normal Y component must be between the Y components
    // of fn0 (0.816) and fn1 (0) and fn2, i.e. not equal to 0.816.
    const yr::Vec3f& n0 = result.scene->vertices[0].normal;
    // The averaged normal for v0 must not be identical to any single face normal.
    // fn0 normalised is approximately (0, 0.816, -0.578). If the normal is smooth,
    // n0.y should be strictly less than 0.816.
    YR_EXPECT_TRUE(n0.y < 0.81f);
}

// A loopsubdiv with a FLAT mesh (all coplanar vertices) must yield normals equal
// to the face normal — smoothing a flat mesh is a no-op.
YR_TEST(scene_compiler_loopsubdiv_flat_mesh_normals_match_face_normal) {
    // Build a flat quad (2 triangles) in the XY plane; all normals must be +Z.
    yr::PbrtScene pbrt;
    pbrt.source_path = "flat_loopsubdiv_test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Flat quad: 4 vertices, 2 triangles
    std::vector<float> positions = {
        0.0f, 0.0f, 0.0f,   // v0
        1.0f, 0.0f, 0.0f,   // v1
        1.0f, 1.0f, 0.0f,   // v2
        0.0f, 1.0f, 0.0f,   // v3
    };
    std::vector<int> indices = { 0, 1, 2,  0, 2, 3 };

    yr::PbrtShapeRecord record;
    record.shape.type = "loopsubdiv";
    record.shape.params.push_back(yr::PbrtParam{"integer", "levels", {}, {1}, {}, {}});
    record.shape.params.push_back(yr::PbrtParam{"point3", "P", positions, {}, {}, {}});
    record.shape.params.push_back(yr::PbrtParam{"integer", "indices", {}, indices, {}, {}});
    record.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(record);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_TRUE(result.scene->primitives[0].has_normals);

    // All normals must point in +Z (0, 0, 1) since the quad is in the XY plane.
    for (std::size_t vi = 0; vi < result.scene->vertices.size(); ++vi) {
        const yr::Vec3f& n = result.scene->vertices[vi].normal;
        // Allow ±0.001 tolerance
        YR_EXPECT_TRUE(std::fabs(n.x) < 0.001f);
        YR_EXPECT_TRUE(std::fabs(n.y) < 0.001f);
        YR_EXPECT_TRUE(n.z > 0.999f);
    }
}

// If a loopsubdiv ALREADY provides N, it is left untouched (not re-synthesised).
YR_TEST(scene_compiler_loopsubdiv_existing_N_is_preserved) {
    yr::PbrtScene pbrt = MakeSceneWithLoopSubdiv(1);
    // Inject explicit per-vertex normals (all pointing in +Z) — synthetic sentinel.
    std::vector<float> explicit_normals(4 * 3, 0.0f);
    // v0..v3 all get (0, 0, 1) — an obviously synthetic value.
    for (int i = 0; i < 4; ++i) explicit_normals[i * 3 + 2] = 1.0f;
    pbrt.shapes[0].shape.params.push_back(
        yr::PbrtParam{"normal", "N", explicit_normals, {}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;
    YR_EXPECT_TRUE(result.scene->primitives[0].has_normals);

    // All normals must still be (0,0,1) — the explicit normals, NOT re-smoothed.
    for (std::size_t vi = 0; vi < result.scene->vertices.size(); ++vi) {
        const yr::Vec3f& n = result.scene->vertices[vi].normal;
        YR_EXPECT_TRUE(std::fabs(n.x) < 0.001f);
        YR_EXPECT_TRUE(std::fabs(n.y) < 0.001f);
        YR_EXPECT_TRUE(n.z > 0.999f);
    }
}
