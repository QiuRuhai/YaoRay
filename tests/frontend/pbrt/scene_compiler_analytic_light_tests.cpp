#include "scene_compiler_basic_test_support.hpp"
#include "yr_test.hpp"

#include <cmath>

#include <yaoray/frontend/pbrt/scene_compiler.hpp>

namespace {

yr::SceneCompileResult CompileWithLight(yr::PbrtLightRecord light) {
    yr::PbrtScene scene = yr::test_support::MakeBasicPbrtScene();
    yr::test_support::AddBasicSphere(scene);
    scene.lights.push_back(std::move(light));
    return yr::CompilePbrtScene(scene);
}

} // namespace

YR_TEST(scene_compiler_compiles_lightsource_point) {
    yr::PbrtLightRecord light;
    light.light.type = "point";
    light.light.params.push_back(yr::PbrtParam{"point3", "from", {1.0f, 2.0f, 3.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"rgb", "I", {10.0f, 20.0f, 30.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"rgb", "scale", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    light.light_to_world = yr::Mat4f{};

    const yr::SceneCompileResult result = CompileWithLight(std::move(light));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->analytic_lights.size(), std::size_t{1});
    const yr::AnalyticLight& compiled = result.scene->analytic_lights[0];
    YR_EXPECT_EQ(compiled.kind, yr::AnalyticLightKind::Point);
    YR_EXPECT_NEAR(compiled.position.x, 1.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.position.y, 2.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.position.z, 3.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.x, 5.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.y, 10.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.z, 15.0f, 1.0e-5);
}

YR_TEST(scene_compiler_compiles_lightsource_distant) {
    yr::PbrtLightRecord light;
    light.light.type = "distant";
    light.light.params.push_back(yr::PbrtParam{"point3", "from", {0.0f, 1.0f, 0.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"point3", "to", {0.0f, 0.0f, 0.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"rgb", "L", {5.0f, 5.0f, 5.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"float", "scale", {0.5f}, {}, {}, {}});
    light.light_to_world = yr::Mat4f{};

    const yr::SceneCompileResult result = CompileWithLight(std::move(light));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::AnalyticLight& compiled = result.scene->analytic_lights[0];
    YR_EXPECT_EQ(compiled.kind, yr::AnalyticLightKind::Distant);
    YR_EXPECT_NEAR(compiled.direction.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.direction.y, -1.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.direction.z, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.x, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.y, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.intensity.z, 2.5f, 1.0e-5);
}

YR_TEST(scene_compiler_compiles_lightsource_spot) {
    yr::PbrtLightRecord light;
    light.light.type = "spot";
    light.light.params.push_back(yr::PbrtParam{"point3", "from", {0.0f, 5.0f, 0.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"point3", "to", {0.0f, 0.0f, 0.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"rgb", "I", {10.0f, 10.0f, 10.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"float", "coneangle", {30.0f}, {}, {}, {}});
    light.light.params.push_back(yr::PbrtParam{"float", "conedeltaangle", {5.0f}, {}, {}, {}});
    light.light_to_world = yr::Mat4f{};

    const yr::SceneCompileResult result = CompileWithLight(std::move(light));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::AnalyticLight& compiled = result.scene->analytic_lights[0];
    YR_EXPECT_EQ(compiled.kind, yr::AnalyticLightKind::Spot);
    YR_EXPECT_NEAR(compiled.position.y, 5.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.direction.y, -1.0f, 1.0e-5);
    YR_EXPECT_NEAR(compiled.cone_angle, std::cos(0.5236f), 1.0e-4);
    YR_EXPECT_NEAR(compiled.cone_cos_inner, std::cos(0.4363f), 1.0e-4);
}
