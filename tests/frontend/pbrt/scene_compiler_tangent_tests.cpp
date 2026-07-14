#include "yr_test.hpp"

#include <algorithm>
#include <yaoray/frontend/pbrt/pbrt_scene.hpp>
#include <yaoray/frontend/pbrt/scene_compiler.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace {

yr::PbrtScene MinimalSceneWithTangentTriangle() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
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
    record.shape.type = "trianglemesh";
    record.shape.params.push_back(yr::PbrtParam{
        "point3", "P",
        {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "normal", "N",
        {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "point2", "uv",
        {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "normal", "S",
        {1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{"integer", "indices", {}, {0, 1, 2}, {}, {}});
    record.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(record);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_trianglemesh_S_param_populates_vertex_tangents) {
    const yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->primitives.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->primitives[0].has_tangents);
    YR_EXPECT_EQ(result.scene->vertices.size(), std::size_t{3});
    for (std::size_t vi = 0; vi < 3; ++vi) {
        const yr::Vec3f t = result.scene->vertices[vi].tangent;
        YR_EXPECT_NEAR(t.x, 1.0f, 1.0e-6);
        YR_EXPECT_NEAR(t.y, 0.0f, 1.0e-6);
        YR_EXPECT_NEAR(t.z, 0.0f, 1.0e-6);
        YR_EXPECT_NEAR(result.scene->vertices[vi].tangent_handedness, 1.0f, 1.0e-6);
    }
}

YR_TEST(scene_compiler_trianglemesh_without_S_param_has_no_tangents) {
    yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    // Strip the S param.
    auto& params = pbrt.shapes[0].shape.params;
    params.erase(
        std::remove_if(params.begin(), params.end(), [](const yr::PbrtParam& p) {
            return p.name == "S";
        }),
        params.end()
    );
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!result.scene->primitives[0].has_tangents);
}

YR_TEST(scene_compiler_trianglemesh_S_transformed_by_object_to_world) {
    yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    // Set object_to_world to a 90-degree rotation about Y: (1,0,0) -> (0,0,-1).
    yr::Mat4f m{};
    m.m[0] = 0.0f; m.m[2] = -1.0f;
    m.m[8] = 1.0f; m.m[10] = 0.0f;
    pbrt.shapes[0].object_to_world = m;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->primitives[0].has_tangents);
    const yr::Vec3f t = result.scene->vertices[0].tangent;
    YR_EXPECT_NEAR(t.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(t.y, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(t.z, -1.0f, 1.0e-5);
}
