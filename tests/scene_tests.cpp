#include "yr_test.hpp"

#include <string>
#include <string_view>
#include <vector>

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
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::None), std::string_view{"none"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Reinhard), std::string_view{"reinhard"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Aces), std::string_view{"aces"});
    YR_EXPECT_EQ(yr::CameraKindName(yr::CameraKind::Perspective), std::string_view{"perspective"});
    YR_EXPECT_EQ(yr::LightKindName(yr::LightKind::Area), std::string_view{"area"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::None), std::string_view{"none"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Constant), std::string_view{"constant"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Hdri), std::string_view{"hdri"});
}

YR_TEST(scene_enum_parsers_accept_stable_names) {
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cpu").value(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cuda").value(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(yr::ParseToneMapperName("none").value(), yr::ToneMapperKind::None);
    YR_EXPECT_EQ(yr::ParseToneMapperName("reinhard").value(), yr::ToneMapperKind::Reinhard);
    YR_EXPECT_EQ(yr::ParseToneMapperName("aces").value(), yr::ToneMapperKind::Aces);
    YR_EXPECT_EQ(yr::ParseCameraKindName("perspective").value(), yr::CameraKind::Perspective);
    YR_EXPECT_EQ(yr::ParseLightKindName("area").value(), yr::LightKind::Area);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("none").value(), yr::EnvironmentKind::None);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("constant").value(), yr::EnvironmentKind::Constant);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("hdri").value(), yr::EnvironmentKind::Hdri);
}

YR_TEST(scene_enum_parsers_reject_unknown_names) {
    YR_EXPECT_TRUE(!yr::ParseRenderBackendName("metal").has_value());
    YR_EXPECT_TRUE(!yr::ParseToneMapperName("filmic").has_value());
    YR_EXPECT_TRUE(!yr::ParseCameraKindName("orthographic").has_value());
    YR_EXPECT_TRUE(!yr::ParseLightKindName("point").has_value());
    YR_EXPECT_TRUE(!yr::ParseEnvironmentKindName("sky").has_value());
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

YR_TEST(scene_diagnostics_format_warning_messages) {
    const yr::SceneDiagnostic diagnostic{
        yr::DiagnosticSeverity::Warning,
        "scenes/examples/minimal.toml",
        "render.spp",
        "using default sample count"
    };

    const std::string text = yr::FormatSceneDiagnostic(diagnostic);

    YR_EXPECT_EQ(text, std::string{"Scene warning in scenes/examples/minimal.toml:\n  [render.spp] using default sample count"});
}

YR_TEST(scene_diagnostics_format_empty_field_without_brackets) {
    const yr::SceneDiagnostic diagnostic{
        yr::DiagnosticSeverity::Error,
        "scenes/examples/minimal.toml",
        "",
        "invalid scene"
    };

    const std::string text = yr::FormatSceneDiagnostic(diagnostic);

    YR_EXPECT_EQ(text, std::string{"Scene error in scenes/examples/minimal.toml:\n  invalid scene"});
    YR_EXPECT_TRUE(text.find("[]") == std::string::npos);
}

YR_TEST(scene_diagnostics_join_multiple_entries) {
    const std::vector<yr::SceneDiagnostic> diagnostics{
        {yr::DiagnosticSeverity::Error, "a.toml", "camera", "missing camera"},
        {yr::DiagnosticSeverity::Warning, "b.toml", "", "using default film"}
    };

    const std::string text = yr::FormatSceneDiagnostics(diagnostics);

    YR_EXPECT_EQ(text, std::string{"Scene error in a.toml:\n  [camera] missing camera\nScene warning in b.toml:\n  using default film"});
}

YR_TEST(scene_diagnostics_detect_errors) {
    const std::vector<yr::SceneDiagnostic> warnings{
        {yr::DiagnosticSeverity::Warning, "a.toml", "film", "using default output"}
    };
    const std::vector<yr::SceneDiagnostic> errors{
        {yr::DiagnosticSeverity::Warning, "a.toml", "film", "using default output"},
        {yr::DiagnosticSeverity::Error, "a.toml", "camera", "missing camera"}
    };

    YR_EXPECT_TRUE(!yr::HasSceneErrors(warnings));
    YR_EXPECT_TRUE(yr::HasSceneErrors(errors));
}
