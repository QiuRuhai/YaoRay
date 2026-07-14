#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/scene/render_scene.hpp>

YR_TEST(render_scene_ir_and_settings_have_independent_defaults) {
    const yr::RenderSceneIR scene;
    const yr::RenderSettings settings;

    YR_EXPECT_EQ(settings.requested_backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(settings.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(settings.sampler, yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(settings.width, 0);
    YR_EXPECT_EQ(settings.height, 0);
    YR_EXPECT_EQ(settings.spp, 1);
    YR_EXPECT_EQ(settings.max_depth, 5);
    YR_EXPECT_EQ(settings.seed, std::uint64_t{0});
    YR_EXPECT_EQ(settings.threads, 0);
    YR_EXPECT_NEAR(settings.radiance_clamp, 0.0, 1e-6);
    YR_EXPECT_TRUE(scene.vertices.empty());
    YR_EXPECT_TRUE(scene.indices.empty());
    YR_EXPECT_TRUE(scene.primitives.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.emissive_primitives.empty());
}
