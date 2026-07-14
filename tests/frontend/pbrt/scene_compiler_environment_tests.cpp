#include "yr_test.hpp"

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/scene/render_scene.hpp>

#include <filesystem>
#include <string>

namespace {

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

const std::filesystem::path& TestDataDir() {
    static const std::filesystem::path dir{YAORAY_TEST_DATA_DIR};
    return dir;
}

yr::PbrtScene MinimalSceneWithInfinite(const std::string& filename, yr::Color3f L_value, float scale_value) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = TestDataDir();
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtLightRecord lr;
    lr.light.type = "infinite";
    lr.light.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {filename}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "L", {L_value.x, L_value.y, L_value.z}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"float", "scale", {scale_value}, {}, {}, {}});
    lr.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(lr);

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_lightsource_infinite_activates_environment) {
    const yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/tiny_env.hdr",
        yr::Color3f{1.0f, 1.0f, 1.0f},
        1.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->environment.active);
    YR_EXPECT_TRUE(result.scene->environment.texture_index >= 0);
    YR_EXPECT_TRUE(result.scene->environment.distribution_index >= 0);
    YR_EXPECT_TRUE(!result.scene->environment_distributions.empty());
}

YR_TEST(scene_compiler_lightsource_infinite_scales_radiance) {
    const yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/tiny_env.hdr",
        yr::Color3f{2.0f, 3.0f, 4.0f},
        0.5f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // radiance = L * scale = (1.0, 1.5, 2.0).
    YR_EXPECT_NEAR(result.scene->environment.radiance.x, 1.0f, 1.0e-5f);
    YR_EXPECT_NEAR(result.scene->environment.radiance.y, 1.5f, 1.0e-5f);
    YR_EXPECT_NEAR(result.scene->environment.radiance.z, 2.0f, 1.0e-5f);
}

YR_TEST(scene_compiler_lightsource_infinite_missing_file_degrades_to_warning) {
    // A filename param is present but the referenced .hdr does not exist on
    // disk. Per the M2 degrade policy (Patch 2c), this is asset-missing and
    // must degrade to a Warning + 1x1 white sky, not a fatal Error.
    yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/no_such_environment.hdr",
        yr::Color3f{1.0f, 1.0f, 1.0f},
        1.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_lightsource_infinite_missing_filename_param_emits_error) {
    // A LightSource "infinite" with NO filename/mapname param at all is a
    // real scene-file bug (not an asset-missing case), so it still emits a
    // fatal Error and compilation fails.
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = TestDataDir();
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtLightRecord lr;
    lr.light.type = "infinite";
    // No filename, no mapname.
    lr.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(lr);

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}
