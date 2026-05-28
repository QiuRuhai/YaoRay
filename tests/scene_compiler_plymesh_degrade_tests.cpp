#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene that declares one plymesh shape pointing at
// a path that does NOT exist on disk. A sentinel sphere is included so the
// downstream "scene contains no geometry" guard does not fire — the plymesh
// itself is expected to degrade and add zero triangles.
yr::PbrtScene MakeSceneWithMissingPly() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "missing_plymesh.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // Sentinel sphere so the empty-geometry guard does not fire after the
    // plymesh is degraded.
    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(sphere);

    // A plymesh referencing a file that doesn't exist.
    yr::PbrtShapeRecord record;
    record.shape.type = "plymesh";
    record.shape.params.push_back(yr::PbrtParam{
        "string", "filename", {}, {}, {"this_ply_does_not_exist_on_disk.ply"}, {}});
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

YR_TEST(scene_compiler_plymesh_missing_file_degrades_to_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithMissingPly();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // No Error severity diagnostic mentioning PLY-related failures.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, "PLY"));
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Error, "ply"));

    // A Warning was emitted referencing the PLY load failure so users can grep.
    YR_EXPECT_TRUE(HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "PLY"));

    // Compilation succeeded because the broken plymesh was degraded, not fatal.
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_plymesh_missing_file_skips_shape_in_ir) {
    const yr::PbrtScene pbrt = MakeSceneWithMissingPly();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }

    // The missing plymesh contributes no triangle IR: zero primitives, zero
    // vertices, zero indices. The sentinel sphere lives in ir.spheres, not
    // ir.primitives, so these counts must stay at zero.
    YR_EXPECT_EQ(result.scene->primitives.size(), std::size_t{0});
    YR_EXPECT_EQ(result.scene->vertices.size(), std::size_t{0});
    YR_EXPECT_EQ(result.scene->indices.size(), std::size_t{0});

    // Sanity: the sentinel sphere did register.
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
}
