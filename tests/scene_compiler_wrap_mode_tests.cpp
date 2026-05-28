#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <filesystem>
#include <string>

namespace {

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = YAORAY_TEST_DATA_DIR;
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

yr::PbrtEntity ImagemapTextureWithWrap(const std::string& wrap_value) {
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    tex.params.push_back(yr::PbrtParam{"string", "wrap", {}, {}, {wrap_value}, {}});
    return tex;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& substring
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_imagemap_wrap_repeat_sets_repeat_on_both_axes) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("repeat");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::Repeat);
}

YR_TEST(scene_compiler_imagemap_wrap_clamp_sets_clamp_on_both_axes) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("clamp");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::ClampToEdge);
}

YR_TEST(scene_compiler_imagemap_wrap_black_degrades_to_clamp_with_warning) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("black");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "black"));
}

YR_TEST(scene_compiler_imagemap_wrap_unknown_value_warns_and_falls_back_to_repeat) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("totally-bogus-wrap");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "totally-bogus-wrap"));
}

YR_TEST(scene_compiler_imagemap_no_wrap_param_defaults_to_repeat) {
    yr::PbrtScene pbrt = MinimalScene();
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["t"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& loaded = result.scene->textures[0];
    YR_EXPECT_TRUE(loaded.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(loaded.wrap_t == yr::TextureWrap::Repeat);
}
