#include "yr_test.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

namespace {

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "input_validation_test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.camera.type = "perspective";
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord sphere;
    sphere.shape.type = "sphere";
    sphere.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    sphere.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(sphere);
    return pbrt;
}

bool HasWarningField(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == yr::DiagnosticSeverity::Warning &&
            diagnostic.field == field) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_clamps_invalid_render_settings_with_warnings) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {0}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {-4}, {}, {}});
    pbrt.sampler.params.push_back(yr::PbrtParam{"integer", "pixelsamples", {}, {0}, {}, {}});
    pbrt.integrator.params.push_back(yr::PbrtParam{"float", "maxdepth", {-2.0f}, {}, {}, {}});
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {0.0f}, {}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->width, 1);
    YR_EXPECT_EQ(result.scene->height, 1);
    YR_EXPECT_EQ(result.scene->spp, 1);
    YR_EXPECT_EQ(result.scene->max_depth, 0);
    YR_EXPECT_NEAR(result.scene->camera.fov_y_radians, 0.785398185f, 1.0e-6);

    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Film.xresolution"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Film.yresolution"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Sampler.pixelsamples"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Integrator.maxdepth"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Camera.fov"));
}

YR_TEST(scene_compiler_clamps_huge_film_resolution_with_warnings) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.film.params.push_back(yr::PbrtParam{
        "float", "xresolution", {1.0e30f}, {}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{
        "integer", "yresolution", {}, {std::numeric_limits<int>::max()}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Film.xresolution"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Film.yresolution"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->width, 16384);
    YR_EXPECT_EQ(result.scene->height, 16384);
}

YR_TEST(scene_compiler_skips_invalid_sphere_radius_without_invalid_ir) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtShapeRecord invalid;
    invalid.shape.type = "sphere";
    invalid.shape.params.push_back(yr::PbrtParam{"float", "radius", {-2.0f}, {}, {}, {}});
    invalid.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(invalid);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Shape.sphere.radius"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->spheres[0].radius > 0.0f);
    YR_EXPECT_TRUE(std::isfinite(result.scene->spheres[0].radius));
}

YR_TEST(scene_compiler_clamps_huge_numeric_settings_before_int_conversion) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.sampler.params.push_back(yr::PbrtParam{"float", "pixelsamples", {1.0e30f}, {}, {}, {}});
    pbrt.integrator.params.push_back(yr::PbrtParam{"float", "maxdepth", {1.0e30f}, {}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Sampler.pixelsamples"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Integrator.maxdepth"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spp, 1);
    YR_EXPECT_EQ(result.scene->max_depth, 5);
}

YR_TEST(scene_compiler_clamps_nonfinite_numeric_settings_before_int_conversion) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.sampler.params.push_back(yr::PbrtParam{
        "float", "pixelsamples", {std::numeric_limits<float>::infinity()}, {}, {}, {}});
    pbrt.integrator.params.push_back(yr::PbrtParam{
        "float", "maxdepth", {std::numeric_limits<float>::quiet_NaN()}, {}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Sampler.pixelsamples"));
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Integrator.maxdepth"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spp, 1);
    YR_EXPECT_EQ(result.scene->max_depth, 5);
}

YR_TEST(scene_compiler_safely_handles_stratified_sample_overflow) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.sampler.type = "stratified";
    pbrt.sampler.params.push_back(yr::PbrtParam{"integer", "xsamples", {}, {std::numeric_limits<int>::max()}, {}, {}});
    pbrt.sampler.params.push_back(yr::PbrtParam{"integer", "ysamples", {}, {std::numeric_limits<int>::max()}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Sampler.samples"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spp, 1);
}

YR_TEST(scene_compiler_honors_ysamples_without_xsamples) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.sampler.type = "stratified";
    pbrt.sampler.params.push_back(yr::PbrtParam{"integer", "ysamples", {}, {4}, {}, {}});

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spp, 4);
}

YR_TEST(scene_compiler_skips_sphere_with_invalid_effective_radius_from_transform) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtShapeRecord invalid;
    invalid.shape.type = "sphere";
    invalid.shape.params.push_back(yr::PbrtParam{"float", "radius", {1.0f}, {}, {}, {}});
    invalid.object_to_world.m = {
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    pbrt.shapes.push_back(invalid);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(HasWarningField(result.diagnostics, "Shape.sphere.radius"));
    if (!result.scene.has_value()) {
        return;
    }

    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
}
