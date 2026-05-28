#include "yr_test.hpp"

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/film/film.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Create a small temp EXR file and return its path. tinyexr/EXR is the right
// HDR format for environments and material textures from modern PBRT v4 / DCC
// pipelines, so the imagemap dispatch must accept `.exr` alongside .hdr/.pfm.
std::filesystem::path WriteTempExrFixture(std::string_view name) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "yaoray_imagemap_exr_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / std::string{name};
    std::filesystem::remove(path);

    // 2x2 film with a known HDR pattern that includes values > 1.0 so we can
    // be sure the loader actually parsed EXR (not a clamped LDR fallback).
    yr::Film film{2, 2};
    film.AddSample(0, 0, yr::Color3f{0.25f, 0.5f, 1.0f});
    film.AddSample(1, 0, yr::Color3f{2.0f, 3.0f, 4.0f});
    film.AddSample(0, 1, yr::Color3f{0.1f, 0.2f, 0.3f});
    film.AddSample(1, 1, yr::Color3f{5.0f, 6.0f, 7.0f});

    const yr::ToneMapSettings tone_map{yr::ToneMapper::None, 0.0f};
    const yr::ImageWriteResult write_result = yr::WriteExr(film, tone_map, path);
    (void)write_result;
    return path;
}

yr::PbrtScene MakeSceneWithExrImagemap(const std::filesystem::path& exr_path) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "imagemap_exr.pbrt";
    pbrt.source_root = exr_path.parent_path();

    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});

    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};

    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere so the empty-geometry guard passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    // Declare one imagemap texture pointing at the .exr fixture (relative to
    // source_root so the dispatcher resolves it through the same path that
    // production scenes use).
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{
        "string", "filename", {}, {}, {exr_path.filename().string()}, {}
    });
    pbrt.named_textures["env_like"] = tex;

    return pbrt;
}

bool HasDiagnosticContaining(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasFieldDiagnostic(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& field_needle
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.field.find(field_needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

// Regression: PR #8 extended LoadHdrTexture to accept .exr, but missed the
// separate extension whitelist inside CompileImagemapTexture, so material
// `Texture "x" "spectrum" "imagemap" "string filename" ["foo.exr"]` fell into
// the "unsupported texture extension" Error branch. The envmap path
// (LightSource "infinite") was unaffected because it calls LoadHdrTexture
// directly. After the fix, .exr joins .hdr / .pfm in the imagemap dispatch.
YR_TEST(scene_compiler_imagemap_accepts_exr_extension) {
    const std::filesystem::path exr_path = WriteTempExrFixture("dispatch_smoke.exr");
    const yr::PbrtScene pbrt = MakeSceneWithExrImagemap(exr_path);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // No "unsupported texture extension" Error.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics,
        yr::DiagnosticSeverity::Error,
        "unsupported texture extension"));

    // No Error tagged with the texture name (i.e., no degraded-load path).
    YR_EXPECT_TRUE(!HasFieldDiagnostic(
        result.diagnostics,
        yr::DiagnosticSeverity::Error,
        "env_like"));

    // No "imagemap load failed" Warning either: the EXR really loaded, it
    // was not silently degraded to the Patch 2a neutral-grey constant.
    YR_EXPECT_TRUE(!HasDiagnosticContaining(
        result.diagnostics,
        yr::DiagnosticSeverity::Warning,
        "imagemap load failed"));

    // Compilation succeeds and produces a scene with the loaded texture.
    YR_EXPECT_TRUE(result.scene.has_value());
    if (!result.scene.has_value()) {
        return;
    }
    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));

    // The texture was registered as a real RenderTexture (not folded to a
    // constant). With only one declared texture, ir.textures must contain
    // exactly one entry whose dimensions match the EXR fixture (2x2).
    YR_EXPECT_EQ(result.scene->textures.size(), std::size_t{1});
    const yr::RenderTexture& loaded = result.scene->textures[0];
    YR_EXPECT_EQ(loaded.width, 2);
    YR_EXPECT_EQ(loaded.height, 2);
    YR_EXPECT_TRUE(!loaded.texels.empty());
    // EXR is HDR -> Linear color space (matches the .hdr / .pfm convention).
    YR_EXPECT_TRUE(loaded.color_space == yr::TextureColorSpace::Linear);
}
