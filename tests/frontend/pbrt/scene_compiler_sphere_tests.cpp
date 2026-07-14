#include "scene_compiler_basic_test_support.hpp"
#include "yr_test.hpp"

#include <yaoray/frontend/pbrt/scene_compiler.hpp>

YR_TEST(scene_compiler_compiles_shape_sphere) {
    yr::PbrtScene scene = yr::test_support::MakeBasicPbrtScene();
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.object_to_world.m[12] = 1.0f;
    shape.object_to_world.m[13] = 2.0f;
    shape.object_to_world.m[14] = 3.0f;
    scene.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(scene);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene->spheres[0].center.x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.y, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.z, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].radius, 0.5f, 1.0e-6);
}
