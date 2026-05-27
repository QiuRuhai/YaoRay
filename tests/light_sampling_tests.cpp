#include "yr_test.hpp"

#include <optional>

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

// TODO(Task 11): Rewrite light sampling tests for emissive primitive API.

namespace {

yr::RenderSceneIR MakeEmissiveScene() {
    yr::RenderSceneIR scene;
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f, -1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, 2.0f,  1.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});
    yr::RenderMaterial mat;
    mat.emission = yr::Color3f{3.0f, 2.0f, 1.0f};
    scene.materials.push_back(mat);
    scene.emissive_primitives.push_back(yr::EmissivePrimitive{0, yr::Color3f{3.0f, 2.0f, 1.0f}, 2.0f});
    return scene;
}

} // namespace

YR_TEST(light_sampling_emissive_returns_valid_sample) {
    const yr::RenderSceneIR scene = MakeEmissiveScene();

    const std::optional<yr::EmissiveSample> sample = yr::SampleEmissiveLights(
        scene, 0.5f, yr::Vec2f{0.3f, 0.3f});

    YR_EXPECT_TRUE(sample.has_value());
    if (sample.has_value()) {
        YR_EXPECT_TRUE(sample->pdf > 0.0f);
        YR_EXPECT_TRUE(sample->radiance.x > 0.0f);
    }
}
