#include "yr_test.hpp"

#include <cmath>
#include <cstddef>

#include <yaoray/lighting/environment.hpp>
#include <yaoray/scene/render_scene.hpp>

namespace {

yr::RenderTexture MakeEnvironmentTexture() {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.wrap_s = yr::TextureWrap::Repeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color4f{8.0f, 8.0f, 8.0f, 1.0f},
        yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f},
        yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f},
        yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f}
    };
    return texture;
}

yr::RenderSceneIR MakeHdriScene() {
    yr::RenderSceneIR scene;
    scene.environment.active = true;
    // Default radiance to neutral white so existing tests focus on the
    // texture * strength behaviour; tests that exercise L*scale weighting
    // override .radiance explicitly.
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.environment.strength = 2.0f;
    scene.environment.texture_index = 0;
    scene.environment.distribution_index = 0;
    scene.textures.push_back(MakeEnvironmentTexture());
    scene.environment_distributions.push_back(yr::BuildEnvironmentDistribution(scene.textures[0]));
    return scene;
}

} // namespace

YR_TEST(environment_direction_uv_round_trips_cardinal_directions) {
    const yr::Vec3f directions[] = {
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f}
    };

    for (const yr::Vec3f direction : directions) {
        const yr::Vec2f uv = yr::DirectionToEnvironmentUv(direction, 0.0f);
        const yr::Vec3f round_trip = yr::EnvironmentUvToDirection(uv, 0.0f);
        YR_EXPECT_NEAR(yr::Dot(yr::Normalize(direction), round_trip), 1.0, 1e-5);
    }
}

YR_TEST(environment_rotation_changes_horizontal_lookup) {
    const yr::Vec2f unrotated = yr::DirectionToEnvironmentUv(yr::Vec3f{0.0f, 0.0f, 1.0f}, 0.0f);
    const yr::Vec2f rotated = yr::DirectionToEnvironmentUv(
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        3.14159265358979323846f * 0.5f
    );

    YR_EXPECT_NEAR(unrotated.x, 0.5, 1e-5);
    YR_EXPECT_NEAR(rotated.x, 0.75, 1e-5);
    YR_EXPECT_NEAR(unrotated.y, rotated.y, 1e-5);
}

YR_TEST(environment_evaluates_hdri_with_strength) {
    const yr::RenderSceneIR scene = MakeHdriScene();

    const yr::Color3f color = yr::EvaluateEnvironment(
        yr::MakeLightSceneView(scene),
        yr::EnvironmentUvToDirection(yr::Vec2f{0.25f, 0.25f}, 0.0f)
    );

    YR_EXPECT_TRUE(color.x > 8.0f);
    YR_EXPECT_NEAR(color.x, color.y, 1e-5);
    YR_EXPECT_NEAR(color.y, color.z, 1e-5);
}

// Regression: the active-texture branch of EvaluateEnvironment must apply
// scene.environment.radiance (which the scene compiler populates from PBRT's
// LightSource "infinite" `L * scale` parameters). Prior to the fix the
// active branch returned `SampleTexture(texture, uv) * strength`, dropping
// `radiance` and rendering HDRI environments at raw texture brightness
// regardless of the scene's L and scale overrides. The inactive-texture
// branch always used `radiance * strength` correctly, so the two paths are
// now consistent.
YR_TEST(environment_active_branch_applies_radiance_modulation) {
    yr::RenderSceneIR scene;

    // Single-texel white texture so SampleTexture deterministically returns
    // Color3f{1, 1, 1} no matter which uv we feed it.
    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.wrap_s = yr::TextureWrap::Repeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.color_space = yr::TextureColorSpace::Linear;
    texture.texels.push_back(yr::Color4f{1.0f, 1.0f, 1.0f, 1.0f});
    scene.textures.push_back(std::move(texture));
    scene.environment_distributions.push_back(yr::BuildEnvironmentDistribution(scene.textures[0]));

    scene.environment.active = true;
    // Asymmetric per-channel radiance so a missing multiply is detected on
    // any channel (luminance-only checks would miss this).
    scene.environment.radiance = yr::Color3f{2.0f, 3.0f, 4.0f};
    scene.environment.strength = 1.0f;
    scene.environment.texture_index = 0;
    scene.environment.distribution_index = 0;

    const yr::Color3f color = yr::EvaluateEnvironment(
        yr::MakeLightSceneView(scene),
        yr::Vec3f{0.0f, 1.0f, 0.0f}
    );

    // Expected: texture(1,1,1) * radiance(2,3,4) * strength(1) = (2, 3, 4).
    YR_EXPECT_NEAR(color.x, 2.0, 1e-5);
    YR_EXPECT_NEAR(color.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(color.z, 4.0, 1e-5);
}

YR_TEST(environment_distribution_prefers_bright_texel) {
    const yr::RenderTexture texture = MakeEnvironmentTexture();
    const yr::RenderEnvironmentDistribution distribution = yr::BuildEnvironmentDistribution(texture);

    const std::size_t bright = 0;
    const std::size_t dim = 1;
    YR_EXPECT_TRUE(distribution.texel_weights[bright] > distribution.texel_weights[dim]);
    YR_EXPECT_TRUE(distribution.total_weight > 0.0f);
}

YR_TEST(environment_sampling_returns_positive_pdf) {
    const yr::RenderSceneIR scene = MakeHdriScene();

    const yr::EnvironmentSample sample = yr::SampleEnvironment(
        yr::MakeLightSceneView(scene), yr::Vec2f{0.1f, 0.1f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.pdf_solid_angle > 0.0f);
    YR_EXPECT_TRUE(yr::PdfEnvironment(
        yr::MakeLightSceneView(scene), sample.direction) > 0.0f);
    YR_EXPECT_NEAR(yr::Length(sample.direction), 1.0, 1e-5);
}

YR_TEST(environment_black_distribution_stays_finite) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color4f{},
        yr::Color4f{},
        yr::Color4f{},
        yr::Color4f{}
    };

    const yr::RenderEnvironmentDistribution distribution = yr::BuildEnvironmentDistribution(texture);

    YR_EXPECT_TRUE(distribution.uniform);
    YR_EXPECT_TRUE(distribution.total_weight > 0.0f);
    for (float weight : distribution.texel_weights) {
        YR_EXPECT_TRUE(std::isfinite(weight));
        YR_EXPECT_TRUE(weight > 0.0f);
    }
}
