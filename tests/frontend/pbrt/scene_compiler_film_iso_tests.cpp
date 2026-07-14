#include "yr_test.hpp"

#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/render_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>

#include <algorithm>
#include <cmath>

namespace {

// PBRT v4 Film "float iso" — sensor ISO sensitivity. yaoray translates this
// into the existing FilmSettings::exposure (in stops) via log2(iso / 100),
// where ISO 100 is the reference (zero stops). Many real-world PBRT scenes
// rely on `iso` being honored (e.g., Pavilion's pavilion-day.pbrt uses
// ISO 500), so without this the rendered images come out ~5x too dark.

yr::PbrtScene MakeSceneWithIso(float iso) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "iso_test.pbrt";
    pbrt.source_root = ".";

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"float", "iso", {iso}, {}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere so the empty-geometry guard passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_film_iso_100_is_zero_stops) {
    // ISO 100 is the reference -> exposure stays at the default (0.0 stops).
    const yr::PbrtScene pbrt = MakeSceneWithIso(100.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_NEAR(result.settings.film.exposure, 0.0f, 1e-6f);
}

YR_TEST(scene_compiler_film_iso_200_is_one_stop) {
    // Doubling ISO = +1 stop.
    const yr::PbrtScene pbrt = MakeSceneWithIso(200.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_NEAR(result.settings.film.exposure, 1.0f, 1e-6f);
}

YR_TEST(scene_compiler_film_iso_500_matches_pavilion_setting) {
    // Pavilion's pavilion-day.pbrt uses ISO 500 -> log2(5) ~= 2.32 stops
    // (i.e., a 5x linear brightness multiplier after exp2 in the tone-map).
    const yr::PbrtScene pbrt = MakeSceneWithIso(500.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.scene.has_value());
    const float expected = std::log2(5.0f);
    YR_EXPECT_NEAR(result.settings.film.exposure, expected, 1e-6f);
}

YR_TEST(scene_compiler_film_iso_missing_defaults_to_100) {
    // No "iso" param -> default ISO 100 -> 0 stops.
    yr::PbrtScene pbrt = MakeSceneWithIso(100.0f);
    // Strip the iso param we just added.
    pbrt.film.params.erase(
        std::remove_if(pbrt.film.params.begin(), pbrt.film.params.end(),
            [](const yr::PbrtParam& p) { return p.name == "iso"; }),
        pbrt.film.params.end());

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_NEAR(result.settings.film.exposure, 0.0f, 1e-6f);
}
