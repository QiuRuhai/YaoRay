#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <string>
#include <vector>

namespace {

// Build a minimal compilable scene that declares one disk shape.
// The disk is at z=0 in object space, radius=1.0, no inner radius.
// After the patch, it must produce at least one primitive with triangles.
yr::PbrtScene MakeSceneWithDisk(float radius = 1.0f, float inner_radius = 0.0f, float height = 0.0f) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "disk_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord record;
    record.shape.type = "disk";
    record.shape.params.push_back(yr::PbrtParam{"float", "radius", {radius}, {}, {}, {}});
    if (inner_radius > 0.0f) {
        record.shape.params.push_back(yr::PbrtParam{"float", "innerradius", {inner_radius}, {}, {}, {}});
    }
    record.shape.params.push_back(yr::PbrtParam{"float", "height", {height}, {}, {}, {}});
    record.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(record);

    return pbrt;
}

// Build a scene with a disk that has an AreaLightSource attached.
yr::PbrtScene MakeSceneWithEmissiveDisk() {
    yr::PbrtScene pbrt = MakeSceneWithDisk(150.0f, 0.0f, 0.0f);

    // Attach an area light to the disk
    yr::PbrtEntity area_light;
    area_light.type = "diffuse";
    area_light.params.push_back(yr::PbrtParam{"rgb", "L", {50.0f, 50.0f, 50.0f}, {}, {}, {}});
    pbrt.shapes.back().area_light = area_light;

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

// A disk shape compiles to at least one primitive with triangles.
YR_TEST(scene_compiler_disk_produces_triangles) {
    const yr::PbrtScene pbrt = MakeSceneWithDisk();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    // Must have at least one primitive
    YR_EXPECT_TRUE(!result.scene->primitives.empty());
    // Must have indices (triangles)
    YR_EXPECT_TRUE(!result.scene->indices.empty());
    // indices must be divisible by 3 (triangles)
    YR_EXPECT_TRUE(result.scene->indices.size() % 3 == 0);
}

// A disk with no geometry warning — it must not emit "unsupported shape type: disk"
YR_TEST(scene_compiler_disk_no_unsupported_warning) {
    const yr::PbrtScene pbrt = MakeSceneWithDisk();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // Should not have an unsupported-shape warning for disk
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics, yr::DiagnosticSeverity::Warning, "unsupported shape type: disk"));
}

// A disk with r=150 (like the killeroo scene) produces a meaningful area
YR_TEST(scene_compiler_disk_large_radius_has_area) {
    const yr::PbrtScene pbrt = MakeSceneWithDisk(150.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    YR_EXPECT_TRUE(!result.scene->primitives.empty());
    // A disk with r=150 should have positive area
    YR_EXPECT_TRUE(!result.scene->indices.empty());
}

// An emissive disk becomes an emissive primitive
YR_TEST(scene_compiler_disk_area_light_creates_emissive_primitive) {
    const yr::PbrtScene pbrt = MakeSceneWithEmissiveDisk();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) return;

    // The disk should produce at least one emissive primitive
    YR_EXPECT_TRUE(!result.scene->emissive_primitives.empty());

    // The emissive primitive should have non-zero radiance (L=[50,50,50])
    if (!result.scene->emissive_primitives.empty()) {
        const yr::EmissivePrimitive& ep = result.scene->emissive_primitives[0];
        YR_EXPECT_NEAR(ep.radiance.x, 50.0f, 1e-4f);
        YR_EXPECT_NEAR(ep.radiance.y, 50.0f, 1e-4f);
        YR_EXPECT_NEAR(ep.radiance.z, 50.0f, 1e-4f);
        YR_EXPECT_TRUE(ep.area > 0.0f);
    }
}

// Compilation succeeds — no errors
YR_TEST(scene_compiler_disk_compiles_without_errors) {
    const yr::PbrtScene pbrt = MakeSceneWithDisk();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
}
