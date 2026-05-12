#include "yr_test.hpp"

#include <string>
#include <string_view>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

YR_TEST(scene_defaults_match_schema) {
    const yr::SceneDescription scene;

    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.width, 0);
    YR_EXPECT_EQ(scene.render.height, 0);
    YR_EXPECT_EQ(scene.render.spp, 1);
    YR_EXPECT_EQ(scene.render.max_depth, 5);
    YR_EXPECT_EQ(scene.film.tone_mapper, yr::ToneMapperKind::Aces);
    YR_EXPECT_NEAR(scene.film.exposure, 0.0, 1e-6);
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::None);
}

YR_TEST(scene_enum_names_are_stable) {
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cpu), std::string_view{"cpu"});
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cuda), std::string_view{"cuda"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Aces), std::string_view{"aces"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Constant), std::string_view{"constant"});
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cpu").value(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cuda").value(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_TRUE(!yr::ParseRenderBackendName("metal").has_value());
}

YR_TEST(scene_diagnostics_format_field_messages) {
    const yr::SceneDiagnostic diagnostic{
        yr::DiagnosticSeverity::Error,
        "scenes/examples/minimal.toml",
        "camera.position",
        "missing required field"
    };

    const std::string text = yr::FormatSceneDiagnostic(diagnostic);

    YR_EXPECT_TRUE(text.find("Scene error in scenes/examples/minimal.toml:") != std::string::npos);
    YR_EXPECT_TRUE(text.find("[camera.position] missing required field") != std::string::npos);
}
