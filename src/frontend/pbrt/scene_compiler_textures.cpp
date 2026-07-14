#include "scene_compiler_internal.hpp"

#include <yaoray/io/image_loader.hpp>
#include <yaoray/shading/texture.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace yr::pbrt_compile {

// ---------------------------------------------------------------------------
// Texture compilation
// ---------------------------------------------------------------------------

TextureWrap ParseWrapMode(
    const std::string& wrap_value,
    const std::string& texture_name,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (wrap_value == "repeat" || wrap_value.empty()) {
        return TextureWrap::Repeat;
    }
    if (wrap_value == "clamp") {
        return TextureWrap::ClampToEdge;
    }
    if (wrap_value == "black") {
        diagnostics.push_back(Warning(scene, "Texture." + texture_name,
            "wrap mode 'black' is not fully supported in M1; degraded to 'clamp'. "
            "True black-border sampling lands in M2."));
        return TextureWrap::ClampToEdge;
    }
    diagnostics.push_back(Warning(scene, "Texture." + texture_name,
        "unknown wrap mode '" + wrap_value + "'; falling back to 'repeat'"));
    return TextureWrap::Repeat;
}

TextureColorSpace InferTextureColorSpace(
    const std::filesystem::path& path,
    const std::string& explicit_encoding
) {
    if (explicit_encoding == "linear") return TextureColorSpace::Linear;
    if (explicit_encoding == "sRGB" || explicit_encoding == "srgb") return TextureColorSpace::Srgb;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    // HDR float formats default to linear; LDR formats default to sRGB.
    if (ext == ".hdr" || ext == ".exr") return TextureColorSpace::Linear;
    return TextureColorSpace::Srgb;
}

bool CompileImagemapTexture(
    const std::string& name,
    const PbrtEntity& entity,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    TextureBindings& bindings,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* filename = FindParam(entity.params, "filename");
    if (filename == nullptr || filename->strings.empty()) {
        diagnostics.push_back(Error(scene, "Texture." + name,
            "imagemap texture requires a filename"));
        return false;
    }

    const std::filesystem::path resolved = ResolveSceneResourcePath(scene, filename->strings[0], entity.source_root);

    std::string explicit_encoding;
    const PbrtParam* encoding = FindParam(entity.params, "encoding");
    if (encoding != nullptr && !encoding->strings.empty()) {
        explicit_encoding = encoding->strings[0];
    }
    const TextureColorSpace color_space = InferTextureColorSpace(resolved, explicit_encoding);

    std::string ext = resolved.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    TextureLoadResult load;
    if (ext == ".hdr" || ext == ".pfm" || ext == ".exr") {
        load = LoadHdrTexture(resolved);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".tga" || ext == ".bmp") {
        load = LoadLdrTexture(resolved, color_space);
    } else {
        diagnostics.push_back(Error(scene, "Texture." + name,
            "unsupported texture extension: " + ext));
        return false;
    }

    if (!load.ok) {
        // Degradation policy: a missing or unreadable imagemap is a non-fatal
        // compatibility issue, not a scene-file bug. Demote the diagnostic from
        // Error to Warning and substitute a folded neutral-grey constant so the
        // rest of the compile (materials, samplers, geometry, BVH) can proceed
        // and surface any downstream issues. Matches the existing M1 material
        // fallback philosophy (see MaterialFallbackWarning above).
        diagnostics.push_back(Warning(scene, "Texture." + name,
            "imagemap load failed (" + load.error + "); degrading to neutral constant (0.5, 0.5, 0.5)"));
        bindings.name_to_index[name] = -1;            // -1 == folded constant.
        bindings.constant_values[name] = Color3f{0.5f, 0.5f, 0.5f};
        return true;
    }
    load.texture.color_space = color_space;
    load.texture.filter = TextureFilter::Ewa;
    const PbrtParam* filter_param = FindParam(entity.params, "filter");
    if (filter_param != nullptr && !filter_param->strings.empty()) {
        const std::string& filter = filter_param->strings[0];
        if (filter == "nearest" || filter == "point") {
            load.texture.filter = TextureFilter::Nearest;
        } else if (filter == "bilinear") {
            load.texture.filter = TextureFilter::Bilinear;
        } else if (filter == "trilinear") {
            load.texture.filter = TextureFilter::Trilinear;
        } else if (filter != "ewa") {
            diagnostics.push_back(Warning(scene, "Texture." + name,
                "unknown texture filter '" + filter + "'; falling back to EWA"));
        }
    }

    // PBRT applies the same wrap to both axes. Defaults to repeat.
    std::string wrap_value;
    const PbrtParam* wrap_param = FindParam(entity.params, "wrap");
    if (wrap_param != nullptr && !wrap_param->strings.empty()) {
        wrap_value = wrap_param->strings[0];
    }
    const TextureWrap wrap = ParseWrapMode(wrap_value, name, scene, diagnostics);
    load.texture.wrap_s = wrap;
    load.texture.wrap_t = wrap;

    bindings.name_to_index[name] = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    return true;
}

void CompileConstantTexture(
    const std::string& name,
    const PbrtEntity& entity,
    TextureBindings& bindings
) {
    const PbrtParam* value_param = FindParam(entity.params, "value");
    Color3f value{1.0f, 1.0f, 1.0f};
    if (value_param != nullptr && !value_param->floats.empty()) {
        if (value_param->floats.size() >= 3) {
            value = Color3f{value_param->floats[0], value_param->floats[1], value_param->floats[2]};
        } else {
            const float scalar = value_param->floats[0];
            value = Color3f{scalar, scalar, scalar};
        }
    }
    bindings.name_to_index[name] = -1;          // -1 == folded constant.
    bindings.constant_values[name] = value;
}

// Compile-time fold for PBRT v4 "scale" textures. The scale wraps an inner
// texture (folded constant OR real RenderTexture) and multiplies every value
// by a single scalar. We resolve the inner binding, multiply each channel,
// and register the result as an ordinary binding -- no backend changes
// required because the output is indistinguishable from a regular constant
// or imagemap from the consumer's POV. PBRT v4 also permits an "rgb scale"
// 3-component scaling vector, but every scale wrapper used by Pavilion (and
// most modern scenes) uses scalar scaling, so we take only floats[0].
void CompileScaleTexture(
    const std::string& name,
    const PbrtEntity& entity,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    TextureBindings& bindings,
    std::vector<SceneDiagnostic>& diagnostics
) {
    // Scale factor: default to 1.0 (passes inner through unchanged).
    float scale = 1.0f;
    const PbrtParam* scale_param = FindParam(entity.params, "scale");
    if (scale_param != nullptr && !scale_param->floats.empty()) {
        scale = scale_param->floats[0];
    }

    // Inner texture reference is required.
    const PbrtParam* tex_param = FindParam(entity.params, "tex");
    if (tex_param == nullptr || tex_param->strings.empty()) {
        diagnostics.push_back(Warning(scene, "Texture." + name,
            "scale texture missing inner 'tex' reference; degrading to neutral constant"));
        bindings.name_to_index[name] = -1;
        bindings.constant_values[name] = Color3f{1.0f, 1.0f, 1.0f};
        return;
    }
    const std::string inner_name = tex_param->strings[0];

    // Resolve the inner binding produced by an earlier pass.
    auto it = bindings.name_to_index.find(inner_name);
    if (it == bindings.name_to_index.end()) {
        diagnostics.push_back(Warning(scene, "Texture." + name,
            "scale texture references unknown inner texture '" + inner_name +
            "'; degrading to neutral constant"));
        bindings.name_to_index[name] = -1;
        bindings.constant_values[name] = Color3f{1.0f, 1.0f, 1.0f};
        return;
    }

    if (it->second < 0) {
        // Inner is a folded constant: produce a new folded constant.
        auto cv = bindings.constant_values.find(inner_name);
        const Color3f inner_value = (cv != bindings.constant_values.end())
            ? cv->second
            : Color3f{1.0f, 1.0f, 1.0f};
        bindings.name_to_index[name] = -1;
        bindings.constant_values[name] = inner_value * scale;
        return;
    }

    // Inner is a real RenderTexture: clone the texels and scale the RGB
    // channels. Alpha is preserved so masked textures (if any) keep their
    // coverage. The clone's color_space / filter / wrap modes are copied
    // verbatim; we are scaling in the inner's color space (this matches
    // PBRT's behaviour of applying scale post-decode).
    const RenderTexture& inner_tex = ir.textures[static_cast<std::size_t>(it->second)];
    RenderTexture scaled_tex = inner_tex;
    for (Color4f& texel : scaled_tex.texels) {
        texel.x *= scale;
        texel.y *= scale;
        texel.z *= scale;
    }
    bindings.name_to_index[name] = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(scaled_tex));
}

TextureBindings CompileTextures(
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TextureBindings bindings;

    // Pass 1: leaf textures with no inter-texture dependencies.
    for (const auto& [name, entity] : scene.named_textures) {
        if (entity.type == "imagemap") {
            CompileImagemapTexture(name, entity, scene, ir, bindings, diagnostics);
        } else if (entity.type == "constant") {
            CompileConstantTexture(name, entity, bindings);
        }
    }

    // Pass 2: scale wrappers, which need Pass 1's bindings to look up the
    // inner texture they reference.
    for (const auto& [name, entity] : scene.named_textures) {
        if (entity.type == "scale") {
            CompileScaleTexture(name, entity, scene, ir, bindings, diagnostics);
        }
    }

    // Pass 3: anything else still gets the catch-all unsupported warning,
    // so unimplemented classes (checkerboard, mix, marble, ...) remain
    // grep-friendly in the diagnostics output.
    for (const auto& [name, entity] : scene.named_textures) {
        if (entity.type != "imagemap" &&
            entity.type != "constant" &&
            entity.type != "scale") {
            diagnostics.push_back(Warning(scene, "Texture." + name,
                "unsupported texture class '" + entity.type + "' is ignored in M1; "
                "callers will see the parameter fall back to its inline constant"));
        }
    }

    return bindings;
}

} // namespace yr::pbrt_compile
