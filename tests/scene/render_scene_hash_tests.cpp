#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/scene/render_scene_hash.hpp>

YR_TEST(render_settings_hash_is_stable_for_identical_scene) {
    yr::RenderSceneIR scene;
    yr::RenderSettings settings;
    settings.width = 320;
    settings.height = 180;
    settings.spp = 4;

    yr::RenderSceneIR same_scene;
    const yr::RenderSettings same_settings = settings;
    YR_EXPECT_EQ(
        yr::ComputeRenderSettingsHash(scene, settings),
        yr::ComputeRenderSettingsHash(same_scene, same_settings));
}

YR_TEST(render_settings_hash_changes_for_different_settings) {
    const yr::RenderSceneIR scene;
    yr::RenderSettings base;
    base.width = 320;
    base.height = 180;
    base.spp = 4;
    yr::RenderSettings changed_spp = base;
    changed_spp.spp = 8;
    yr::RenderSettings changed_seed = base;
    changed_seed.seed = 42;

    const std::uint64_t base_hash = yr::ComputeRenderSettingsHash(scene, base);
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(scene, changed_spp));
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(scene, changed_seed));
}

YR_TEST(render_settings_hash_changes_for_geometry_table_counts) {
    const yr::RenderSceneIR base;
    const yr::RenderSettings settings;
    yr::RenderSceneIR changed;
    changed.vertices.push_back(yr::RenderVertex{});

    YR_EXPECT_TRUE(
        yr::ComputeRenderSettingsHash(base, settings) !=
        yr::ComputeRenderSettingsHash(changed, settings));
}

YR_TEST(render_settings_hash_changes_for_instance_transform) {
    yr::RenderSceneIR base;
    base.instances.push_back(yr::RenderInstance{yr::MeshPrimitiveHandle{0}, yr::Mat4f{}});
    yr::RenderSceneIR moved = base;
    moved.instances[0].object_to_world =
        yr::TranslationMatrix(yr::Vec3f{1.0f, 0.0f, 0.0f});
    const yr::RenderSettings settings;

    YR_EXPECT_TRUE(
        yr::ComputeRenderSettingsHash(base, settings) !=
        yr::ComputeRenderSettingsHash(moved, settings));
}
