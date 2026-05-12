#include "yr_test.hpp"

#include <cstdint>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>
#include <yaoray/scene/scene_parser.hpp>

namespace {

std::filesystem::path SceneFixture(std::string_view name) {
    const std::filesystem::path fixture = std::filesystem::path{"tests"} / "fixtures" / "scene" / std::string{name};
    if (std::filesystem::exists(fixture)) {
        return fixture;
    }
    return std::filesystem::path{".."} / fixture;
}

std::filesystem::path WriteTempScene(std::string_view name, std::string_view text) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "yaoray_scene_parser_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / std::string{name};
    std::ofstream out{path};
    out << text;
    return path;
}

std::string ValidScene(
    std::string_view render,
    std::string_view film,
    std::string_view camera,
    std::string_view extra = {}
) {
    std::string text;
    text += render;
    text += film;
    text += camera;
    text += R"toml(
[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
)toml";
    text += extra;
    return text;
}

std::string ValidSceneWith(std::string_view extra) {
    return ValidScene(
        R"toml(
[render]
width = 64
height = 32
)toml",
        R"toml(
[film]
output = "out/test.png"
)toml",
        R"toml(
[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
)toml",
        extra
    );
}

bool DiagnosticsMention(const std::vector<yr::SceneDiagnostic>& diagnostics, std::string_view text) {
    return yr::FormatSceneDiagnostics(diagnostics).find(std::string{text}) != std::string::npos;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

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

YR_TEST(scene_enum_names_return_unknown_for_invalid_values) {
    YR_EXPECT_EQ(yr::RenderBackendName(static_cast<yr::RenderBackendKind>(999)), std::string_view{"unknown"});
    YR_EXPECT_EQ(yr::ToneMapperName(static_cast<yr::ToneMapperKind>(999)), std::string_view{"unknown"});
    YR_EXPECT_EQ(yr::CameraKindName(static_cast<yr::CameraKind>(999)), std::string_view{"unknown"});
    YR_EXPECT_EQ(yr::LightKindName(static_cast<yr::LightKind>(999)), std::string_view{"unknown"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(static_cast<yr::EnvironmentKind>(999)), std::string_view{"unknown"});
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

YR_TEST(scene_parser_loads_minimal_scene_file) {
    const std::filesystem::path scene_path = SceneFixture("minimal.toml").lexically_normal();
    const yr::SceneLoadResult result = yr::LoadSceneFile(scene_path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.source_path.generic_string(), scene_path.generic_string());
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.width, 1280);
    YR_EXPECT_EQ(scene.render.height, 720);
    YR_EXPECT_EQ(scene.render.spp, 64);
    YR_EXPECT_EQ(scene.render.max_depth, 8);
    YR_EXPECT_EQ(scene.render.seed, std::uint64_t{42});
    const std::filesystem::path expected_output = (scene_path.parent_path() / "out" / "example.png").lexically_normal();
    YR_EXPECT_EQ(scene.film.output.generic_string(), expected_output.generic_string());
    YR_EXPECT_EQ(scene.film.tone_mapper, yr::ToneMapperKind::Aces);
    YR_EXPECT_NEAR(scene.film.exposure, 0.0, 1e-6);
    YR_EXPECT_EQ(scene.film.checkpoint_interval_s, 0);
    YR_EXPECT_EQ(scene.film.checkpoint_path.generic_string(), std::string{});
    YR_EXPECT_EQ(scene.camera.value().type, yr::CameraKind::Perspective);
    YR_EXPECT_NEAR(scene.camera.value().position.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().position.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().position.z, 4.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().target.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().target.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().target.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().fov_y, 45.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().aperture, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.camera.value().focus_distance, 4.0, 1e-6);
    YR_EXPECT_EQ(scene.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(scene.assets[0].name, std::string{"model"});
    YR_EXPECT_EQ(scene.assets[0].path.generic_string(), (scene_path.parent_path() / "assets" / "models" / "model.glb").lexically_normal().generic_string());
    YR_EXPECT_EQ(scene.instances.size(), std::size_t{1});
    YR_EXPECT_EQ(scene.instances[0].asset, std::string{"model"});
    YR_EXPECT_NEAR(scene.instances[0].transform.translate.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.translate.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.translate.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.rotate_degrees.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.rotate_degrees.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.rotate_degrees.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.scale.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.scale.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.instances[0].transform.scale.z, 1.0, 1e-6);
    YR_EXPECT_EQ(scene.lights.size(), std::size_t{1});
    YR_EXPECT_EQ(scene.lights[0].type, yr::LightKind::Area);
    YR_EXPECT_NEAR(scene.lights[0].area.position.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.position.y, 4.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.position.z, 2.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.size[0], 2.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.size[1], 2.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.radiance.x, 8.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.radiance.y, 7.0, 1e-6);
    YR_EXPECT_NEAR(scene.lights[0].area.radiance.z, 6.0, 1e-6);
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(scene.environment.radiance.x, 0.02, 1e-6);
    YR_EXPECT_NEAR(scene.environment.radiance.y, 0.025, 1e-6);
    YR_EXPECT_NEAR(scene.environment.radiance.z, 0.03, 1e-6);
    YR_EXPECT_NEAR(scene.environment.strength, 1.0, 1e-6);
}

YR_TEST(scene_parser_applies_defaults) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("defaults.toml"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.spp, 1);
    YR_EXPECT_EQ(scene.render.max_depth, 5);
    YR_EXPECT_EQ(scene.film.tone_mapper, yr::ToneMapperKind::Aces);
    YR_EXPECT_NEAR(scene.film.exposure, 0.0, 1e-6);
    YR_EXPECT_EQ(scene.film.checkpoint_interval_s, 0);
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(scene.environment.strength, 1.0, 1e-6);
}

YR_TEST(scene_parser_rejects_out_of_range_integer_fields) {
    const std::filesystem::path path = WriteTempScene(
        "out_of_range_integer.toml",
        ValidScene(
            "[render]\nwidth = 4294967297\nheight = 32\n",
            "[film]\noutput = \"out/test.png\"\n",
            "[camera]\ntype = \"perspective\"\nposition = [0, 1, 4]\ntarget = [0, 1, 0]\nfov_y = 45\n"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
}

YR_TEST(scene_parser_rejects_float_overflow_fields) {
    const std::filesystem::path path = WriteTempScene(
        "float_overflow.toml",
        ValidScene(
            "[render]\nwidth = 64\nheight = 32\n",
            "[film]\noutput = \"out/test.png\"\nexposure = 3.5e38\n",
            "[camera]\ntype = \"perspective\"\nposition = [0, 1, 4]\ntarget = [0, 1, 0]\nfov_y = 45\n"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
}

YR_TEST(scene_parser_rejects_float_overflow_vector_components) {
    const std::filesystem::path path = WriteTempScene(
        "float_vector_overflow.toml",
        ValidScene(
            "[render]\nwidth = 64\nheight = 32\n",
            "[film]\noutput = \"out/test.png\"\n",
            "[camera]\ntype = \"perspective\"\nposition = [3.5e38, 1, 4]\ntarget = [0, 1, 0]\nfov_y = 45\n"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
}

YR_TEST(scene_parser_rejects_short_required_camera_position) {
    const std::filesystem::path path = WriteTempScene(
        "short_camera_position.toml",
        ValidScene(
            "[render]\nwidth = 64\nheight = 32\n",
            "[film]\noutput = \"out/test.png\"\n",
            "[camera]\ntype = \"perspective\"\nposition = [0, 1]\ntarget = [0, 1, 0]\nfov_y = 45\n"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsMention(result.diagnostics, "camera.position"));
    YR_EXPECT_TRUE(DiagnosticsMention(result.diagnostics, "expected three numeric values") ||
                   DiagnosticsMention(result.diagnostics, "invalid"));
}

YR_TEST(scene_parser_rejects_non_array_required_camera_target) {
    const std::filesystem::path path = WriteTempScene(
        "bad_camera_target.toml",
        ValidScene(
            "[render]\nwidth = 64\nheight = 32\n",
            "[film]\noutput = \"out/test.png\"\n",
            "[camera]\ntype = \"perspective\"\nposition = [0, 1, 4]\ntarget = \"bad\"\nfov_y = 45\n"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsMention(result.diagnostics, "camera.target"));
    YR_EXPECT_TRUE(DiagnosticsMention(result.diagnostics, "expected three numeric values") ||
                   DiagnosticsMention(result.diagnostics, "invalid"));
}

YR_TEST(scene_parser_rejects_empty_asset_names_paths_and_instance_references) {
    const std::filesystem::path path = WriteTempScene(
        "empty_references.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = ""
path = ""

[[instances]]
asset = ""
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
}

YR_TEST(scene_parser_rejects_missing_render) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_render.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render", "missing required table"));
}

YR_TEST(scene_parser_rejects_missing_camera) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_camera.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "camera", "missing required table"));
}

YR_TEST(scene_parser_rejects_invalid_width) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("invalid_width.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.width", "must be positive"));
}

YR_TEST(scene_parser_rejects_short_vectors) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("short_position.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "camera.position", "expected three numeric values"));
}

YR_TEST(scene_parser_rejects_unknown_fields) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("unknown_field.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.widht", "unknown field"));
}

YR_TEST(scene_parser_rejects_unknown_fields_inside_table_arrays) {
    const std::filesystem::path path = WriteTempScene(
        "unknown_asset_field.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "model"
path = "assets/model.glb"
colour = "red"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.colour", "unknown field"));
}

YR_TEST(scene_parser_rejects_misdeclared_table_array_sections) {
    const std::filesystem::path assets_path = WriteTempScene(
        "misdeclared_assets.toml",
        ValidSceneWith(R"toml(
[assets]
name = "model"
path = "assets/model.glb"
)toml")
    );
    const std::filesystem::path instances_path = WriteTempScene(
        "misdeclared_instances.toml",
        ValidSceneWith(R"toml(
[instances]
asset = "model"
)toml")
    );
    const std::filesystem::path lights_path = WriteTempScene(
        "misdeclared_lights.toml",
        ValidSceneWith(R"toml(
[lights]
type = "area"
)toml")
    );

    const yr::SceneLoadResult assets_result = yr::LoadSceneFile(assets_path);
    const yr::SceneLoadResult instances_result = yr::LoadSceneFile(instances_path);
    const yr::SceneLoadResult lights_result = yr::LoadSceneFile(lights_path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(assets_result.diagnostics));
    YR_EXPECT_TRUE(!assets_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(assets_result.diagnostics, "assets", "must be an array of tables"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(instances_result.diagnostics));
    YR_EXPECT_TRUE(!instances_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(instances_result.diagnostics, "instances", "must be an array of tables"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(lights_result.diagnostics));
    YR_EXPECT_TRUE(!lights_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(lights_result.diagnostics, "lights", "must be an array of tables"));
}

YR_TEST(scene_parser_rejects_duplicate_assets) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("duplicate_asset.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.name", "duplicate asset name"));
}

YR_TEST(scene_parser_rejects_missing_asset_references) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_asset_reference.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "instances.asset", "references unknown asset"));
}

YR_TEST(scene_parser_rejects_empty_scene) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("empty_scene.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(
        result.diagnostics,
        "scene",
        "must contain at least one instance, light, or non-none environment"
    ));
}

YR_TEST(scene_parser_reports_missing_files) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("does_not_exist.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "scene file not found"));
}

YR_TEST(scene_parser_reports_bad_toml) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("bad_syntax.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "invalid TOML"));
}
