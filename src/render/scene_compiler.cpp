#include <yaoray/render/scene_compiler.hpp>

#include <yaoray/assets/ply_loader.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const PbrtScene& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic Warning(const PbrtScene& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic MaterialFallbackWarning(const PbrtScene& scene, const std::string& declared_kind) {
    return Warning(scene,
        "Material",
        "material kind '" + declared_kind + "' is not directly supported; applying degradation policy substitution.");
}

const PbrtParam* FindParam(const std::vector<PbrtParam>& params, const std::string& name) {
    for (const PbrtParam& param : params) {
        if (param.name == name) return &param;
    }
    return nullptr;
}

float FloatParam(const PbrtParam* param, float fallback) {
    if (param == nullptr || param->floats.empty()) return fallback;
    return param->floats[0];
}

Color3f RgbParam(const PbrtParam* param, Color3f fallback) {
    if (param == nullptr || param->floats.size() < 3) return fallback;
    return Color3f{param->floats[0], param->floats[1], param->floats[2]};
}

std::string StringParam(const PbrtParam* param, const std::string& fallback) {
    if (param == nullptr || param->strings.empty()) return fallback;
    return param->strings[0];
}

int IntParam(const PbrtParam* param, int fallback) {
    if (param == nullptr || param->ints.empty()) return fallback;
    return param->ints[0];
}

Point3f Point3FromParam(const PbrtParam* param, Point3f fallback) {
    if (param == nullptr || param->floats.size() < 3) return fallback;
    return Point3f{param->floats[0], param->floats[1], param->floats[2]};
}

// ---------------------------------------------------------------------------
// Film / Camera / Integrator / Sampler
// ---------------------------------------------------------------------------

void CompileFilm(const PbrtScene& scene, RenderSceneIR& ir) {
    const auto& params = scene.film.params;
    ir.width = IntParam(FindParam(params, "xresolution"), 1280);
    ir.height = IntParam(FindParam(params, "yresolution"), 720);

    const PbrtParam* filename = FindParam(params, "filename");
    if (filename != nullptr && !filename->strings.empty()) {
        ir.film.output = scene.source_root / filename->strings[0];
    } else {
        ir.film.output = scene.source_root / "out" / (scene.source_path.stem().string() + ".png");
    }
}

void CompileCamera(const PbrtScene& scene, RenderSceneIR& ir) {
    float fov = FloatParam(FindParam(scene.camera.params, "fov"), 45.0f);
    ir.camera.fov_y_radians = DegreesToRadians(fov);

    // PBRT v4 stores the CTM at the camera point as world-to-camera (matching
    // pbrt-v4's LookAt() helper, which returns Transform(Inverse(worldFromCamera),
    // worldFromCamera) and thus puts W2C in the forward matrix). To recover
    // the camera basis in world space we invert to get worldFromCamera, then
    // read columns 0/1/2 as right/up/forward and column 3 as the camera origin.
    const Mat4f world_from_camera = Inverse(scene.camera_transform);
    ir.camera.origin = Point3f{
        world_from_camera.m[12], world_from_camera.m[13], world_from_camera.m[14]
    };
    ir.camera.right = Normalize(Vec3f{
        world_from_camera.m[0], world_from_camera.m[1], world_from_camera.m[2]
    });
    ir.camera.up = Normalize(Vec3f{
        world_from_camera.m[4], world_from_camera.m[5], world_from_camera.m[6]
    });
    ir.camera.forward = Normalize(Vec3f{
        world_from_camera.m[8], world_from_camera.m[9], world_from_camera.m[10]
    });
}

void CompileIntegrator(const PbrtScene& scene, RenderSceneIR& ir) {
    if (scene.integrator.type == "path") {
        ir.integrator = RenderIntegratorKind::Path;
    } else {
        ir.integrator = RenderIntegratorKind::Path; // default to path
    }
    ir.max_depth = static_cast<int>(FloatParam(FindParam(scene.integrator.params, "maxdepth"), 5.0f));
}

void CompileSampler(const PbrtScene& scene, RenderSceneIR& ir) {
    if (scene.sampler.type == "stratified") {
        ir.sampler = RenderSamplerKind::Stratified;
    } else {
        ir.sampler = RenderSamplerKind::Independent;
    }

    const auto& params = scene.sampler.params;
    const PbrtParam* pixelsamples = FindParam(params, "pixelsamples");
    if (pixelsamples != nullptr && !pixelsamples->floats.empty()) {
        ir.spp = static_cast<int>(pixelsamples->floats[0]);
    } else if (pixelsamples != nullptr && !pixelsamples->ints.empty()) {
        ir.spp = pixelsamples->ints[0];
    } else {
        const PbrtParam* xsamples = FindParam(params, "xsamples");
        const PbrtParam* ysamples = FindParam(params, "ysamples");
        if (xsamples != nullptr) {
            int x = static_cast<int>(FloatParam(xsamples, 1.0f));
            int y = static_cast<int>(FloatParam(ysamples, 1.0f));
            ir.spp = x * y;
        }
    }
}

// ---------------------------------------------------------------------------
// Texture compilation
// ---------------------------------------------------------------------------

struct TextureBindings {
    // Texture name -> ir.textures index. -1 means "folded constant"; look in constant_values.
    std::unordered_map<std::string, int> name_to_index;
    // Texture name -> constant Color3f for folded constants.
    std::unordered_map<std::string, Color3f> constant_values;
};

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

    const std::filesystem::path resolved = scene.source_root / filename->strings[0];

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
    if (ext == ".hdr" || ext == ".pfm") {
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
        diagnostics.push_back(Error(scene, "Texture." + name, load.error));
        return false;
    }
    load.texture.color_space = color_space;

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

TextureBindings CompileTextures(
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TextureBindings bindings;
    for (const auto& [name, entity] : scene.named_textures) {
        if (entity.type == "imagemap") {
            CompileImagemapTexture(name, entity, scene, ir, bindings, diagnostics);
        } else if (entity.type == "constant") {
            CompileConstantTexture(name, entity, bindings);
        } else {
            diagnostics.push_back(Warning(scene, "Texture." + name,
                "unsupported texture class '" + entity.type + "' is ignored in M1; "
                "callers will see the parameter fall back to its inline constant"));
        }
    }
    return bindings;
}

// ---------------------------------------------------------------------------
// Texture-param helpers
// ---------------------------------------------------------------------------

// Returns the texture name referenced by a "texture"-typed param, or empty if not a texture ref.
std::string TextureNameInParam(const PbrtParam* param) {
    if (param == nullptr) return {};
    if (param->type != "texture") return {};
    if (param->strings.empty()) return {};
    return param->strings[0];
}

TexParam3f TexParam3fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    Color3f fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam3f result;
    result.value = fallback_value;
    result.texture = -1;

    const PbrtParam* p = FindParam(params, param_name);
    if (p == nullptr) {
        return result;
    }

    const std::string texture_name = TextureNameInParam(p);
    if (!texture_name.empty()) {
        auto it = bindings.name_to_index.find(texture_name);
        if (it == bindings.name_to_index.end()) {
            diagnostics.push_back(Warning(scene, "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"));
            return result;
        }
        if (it->second >= 0) {
            // Real RenderTexture index.
            result.texture = it->second;
        } else {
            // Folded constant -- look up the value.
            auto cv = bindings.constant_values.find(texture_name);
            if (cv != bindings.constant_values.end()) {
                result.value = cv->second;
            }
        }
        return result;
    }

    // Fall back to the inline RGB / spectrum value.
    if (!p->floats.empty()) {
        if (p->floats.size() >= 3) {
            result.value = Color3f{p->floats[0], p->floats[1], p->floats[2]};
        } else {
            const float scalar = p->floats[0];
            result.value = Color3f{scalar, scalar, scalar};
        }
    }
    return result;
}

TexParam1f TexParam1fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    float fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam1f result;
    result.value = fallback_value;
    result.texture = -1;

    const PbrtParam* p = FindParam(params, param_name);
    if (p == nullptr) {
        return result;
    }

    const std::string texture_name = TextureNameInParam(p);
    if (!texture_name.empty()) {
        auto it = bindings.name_to_index.find(texture_name);
        if (it == bindings.name_to_index.end()) {
            diagnostics.push_back(Warning(scene, "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"));
            return result;
        }
        if (it->second >= 0) {
            result.texture = it->second;
        } else {
            auto cv = bindings.constant_values.find(texture_name);
            if (cv != bindings.constant_values.end()) {
                // Take the X channel of an RGB constant as the scalar.
                result.value = cv->second.x;
            }
        }
        return result;
    }

    if (!p->floats.empty()) {
        result.value = p->floats[0];
    }
    return result;
}

// ---------------------------------------------------------------------------
// Normal-map compilation helper
// ---------------------------------------------------------------------------

// Returns true if a normal map was loaded and assigned. Mutates material.normal_map
// and material.normal_scale. On load failure, pushes an Error and returns false.
bool CompileNormalMap(
    const std::vector<PbrtParam>& params,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    RenderMaterial& material,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* normalmap = FindParam(params, "normalmap");
    if (normalmap == nullptr || normalmap->strings.empty()) {
        return false;
    }

    const std::filesystem::path resolved = scene.source_root / normalmap->strings[0];

    std::string ext = resolved.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
        diagnostics.push_back(Error(scene, "Material.normalmap",
            "normal map must be a PNG or JPEG: " + resolved.generic_string()));
        return false;
    }

    // Normal maps are always linear data (not radiometric).
    TextureLoadResult load = LoadLdrTexture(resolved, TextureColorSpace::Linear);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "Material.normalmap", load.error));
        return false;
    }

    material.normal_map = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    material.normal_scale = FloatParam(FindParam(params, "normalscale"), 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Material compilation
// ---------------------------------------------------------------------------

int CompileMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    RenderMaterial material;
    const auto& params = entity.params;
    const std::string& type = entity.type;

    if (type == "matte" || type == "diffuse") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        // PBRT v3 "Kd" alias only matters when explicit; if present, override.
        if (FindParam(params, "Kd") != nullptr) {
            material.reflectance = TexParam3fFromParams(params, "Kd",
                material.reflectance.value, bindings, scene, diagnostics);
        }
    } else if (type == "conductor" || type == "metal") {
        material.kind = RenderMaterialKind::Conductor;
        material.eta = TexParam3fFromParams(params, "eta",
            Color3f{0.2f, 0.2f, 0.2f}, bindings, scene, diagnostics);
        material.k = TexParam3fFromParams(params, "k",
            Color3f{1.0f, 1.0f, 1.0f}, bindings, scene, diagnostics);
        const float fallback_u = FloatParam(FindParam(params, "roughness"), 0.0f);
        material.uroughness = TexParam1fFromParams(params, "uroughness",
            fallback_u, bindings, scene, diagnostics);
        material.vroughness = TexParam1fFromParams(params, "vroughness",
            material.uroughness.value, bindings, scene, diagnostics);
    } else if (type == "dielectric" || type == "glass") {
        material.kind = RenderMaterialKind::Dielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.ior = FloatParam(FindParam(params, "index"), material.ior);
        material.uroughness = TexParam1fFromParams(params, "uroughness",
            0.0f, bindings, scene, diagnostics);
        material.vroughness = TexParam1fFromParams(params, "vroughness",
            material.uroughness.value, bindings, scene, diagnostics);
    } else if (type == "thindielectric") {
        material.kind = RenderMaterialKind::ThinDielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
    } else if (type == "coateddiffuse") {
        material.kind = RenderMaterialKind::CoatedDiffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
    } else if (type == "coatedconductor") {
        material.kind = RenderMaterialKind::CoatedConductor;
        material.eta = TexParam3fFromParams(params, "conductor.eta",
            Color3f{0.2f, 0.2f, 0.2f}, bindings, scene, diagnostics);
        material.k = TexParam3fFromParams(params, "conductor.k",
            Color3f{1.0f, 1.0f, 1.0f}, bindings, scene, diagnostics);
        material.uroughness = TexParam1fFromParams(params, "conductor.roughness",
            0.0f, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
    } else if (type == "diffusetransmission") {
        material.kind = RenderMaterialKind::DiffuseTransmission;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.25f, 0.25f, 0.25f}, bindings, scene, diagnostics);
    } else if (type == "plastic" || type == "uber" || type == "substrate") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "Kd",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        if (FindParam(params, "reflectance") != nullptr) {
            material.reflectance = TexParam3fFromParams(params, "reflectance",
                material.reflectance.value, bindings, scene, diagnostics);
        }
    } else if (type == "subsurface") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    } else if (type == "measured") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Conductor;
        material.eta.value = Color3f{0.2f, 0.2f, 0.2f};
        material.k.value = Color3f{1.0f, 1.0f, 1.0f};
        material.uroughness.value = 0.0f;
        material.vroughness.value = 0.0f;
    } else if (type == "hair") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = Color3f{0.5f, 0.5f, 0.5f};
    } else if (type == "mix") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        // PBRT v4 mix material references two named materials, which we don't
        // resolve recursively in M1. Use the inline-reflectance param if the
        // mix declares one; otherwise default grey.
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    } else {
        // Catch-all: any other unrecognized kind.
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = Color3f{0.5f, 0.5f, 0.5f};
    }

    CompileNormalMap(params, scene, ir, material, diagnostics);

    int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
}

// ---------------------------------------------------------------------------
// Shape compilation
// ---------------------------------------------------------------------------

bool CompileTriangleMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const auto& params = record.shape.params;
    const PbrtParam* p_param = FindParam(params, "P");
    const PbrtParam* indices_param = FindParam(params, "indices");
    if (p_param == nullptr || indices_param == nullptr) {
        diagnostics.push_back(Error(scene, "Shape", "trianglemesh requires P and indices"));
        return false;
    }
    if (p_param->floats.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "Shape.P", "P count must be divisible by 3"));
        return false;
    }
    if (indices_param->ints.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "Shape.indices", "index count must be divisible by 3"));
        return false;
    }

    std::uint32_t base_vertex = static_cast<std::uint32_t>(ir.vertices.size());
    std::uint32_t base_index = static_cast<std::uint32_t>(ir.indices.size());
    std::size_t vertex_count = p_param->floats.size() / 3;

    // Normals
    const PbrtParam* n_param = FindParam(params, "N");
    bool has_normals = (n_param != nullptr && n_param->floats.size() == vertex_count * 3);

    // UVs
    const PbrtParam* uv_param = FindParam(params, "uv");
    if (uv_param == nullptr) uv_param = FindParam(params, "st");
    bool has_uvs = (uv_param != nullptr && uv_param->floats.size() == vertex_count * 2);

    // Tangents (PBRT v4 "normal S"). Per-vertex handedness defaults to +1.
    const PbrtParam* s_param = FindParam(params, "S");
    bool has_tangents = (s_param != nullptr && s_param->floats.size() == vertex_count * 3);

    // Build vertices
    for (std::size_t vi = 0; vi < vertex_count; ++vi) {
        RenderVertex v;
        Point3f pos{p_param->floats[vi * 3], p_param->floats[vi * 3 + 1], p_param->floats[vi * 3 + 2]};
        v.position = TransformPoint(record.object_to_world, pos);
        if (has_normals) {
            Vec3f normal{n_param->floats[vi * 3], n_param->floats[vi * 3 + 1], n_param->floats[vi * 3 + 2]};
            v.normal = TransformNormal(record.object_to_world, normal);
        }
        if (has_uvs) {
            v.uv = Vec2f{uv_param->floats[vi * 2], uv_param->floats[vi * 2 + 1]};
        }
        if (has_tangents) {
            Vec3f tangent{s_param->floats[vi * 3], s_param->floats[vi * 3 + 1], s_param->floats[vi * 3 + 2]};
            v.tangent = TransformVector(record.object_to_world, tangent);
            v.tangent_handedness = 1.0f;
        }
        ir.vertices.push_back(v);
    }

    // Build indices
    for (int idx : indices_param->ints) {
        ir.indices.push_back(base_vertex + static_cast<std::uint32_t>(idx));
    }

    // Build primitive
    RenderPrimitive prim;
    prim.first_index = base_index;
    prim.index_count = static_cast<std::uint32_t>(indices_param->ints.size());
    prim.material_index = material_index;
    prim.has_normals = has_normals;
    prim.has_uvs = has_uvs;
    prim.has_tangents = has_tangents;
    ir.primitives.push_back(prim);

    // Handle area light emission
    if (record.area_light.has_value()) {
        Color3f radiance = RgbParam(FindParam(record.area_light->params, "L"), Color3f{1.0f, 1.0f, 1.0f});

        // Compute total area of all triangles in this primitive
        float total_area = 0.0f;
        for (std::size_t ti = 0; ti < indices_param->ints.size() / 3; ++ti) {
            std::uint32_t i0 = base_vertex + static_cast<std::uint32_t>(indices_param->ints[ti * 3]);
            std::uint32_t i1 = base_vertex + static_cast<std::uint32_t>(indices_param->ints[ti * 3 + 1]);
            std::uint32_t i2 = base_vertex + static_cast<std::uint32_t>(indices_param->ints[ti * 3 + 2]);
            Vec3f e1 = ir.vertices[i1].position - ir.vertices[i0].position;
            Vec3f e2 = ir.vertices[i2].position - ir.vertices[i0].position;
            total_area += Length(Cross(e1, e2)) * 0.5f;
        }

        EmissivePrimitive ep;
        ep.primitive_index = static_cast<int>(ir.primitives.size()) - 1;
        ep.radiance = radiance;
        ep.area = total_area;
        ir.emissive_primitives.push_back(ep);

        // Also set emission on the material
        ir.materials[material_index].emission = radiance;
    }

    return true;
}

bool CompileSphereShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const float radius = FloatParam(FindParam(record.shape.params, "radius"), 1.0f);

    // Warn on unsupported partial-sphere params.
    if (FindParam(record.shape.params, "zmin") != nullptr ||
        FindParam(record.shape.params, "zmax") != nullptr ||
        FindParam(record.shape.params, "phimax") != nullptr) {
        diagnostics.push_back(Warning(scene, "Shape.sphere",
            "partial sphere parameters (zmin/zmax/phimax) are not supported in M1; full sphere will be used"));
    }

    // Sphere center = object_to_world * (0,0,0). Radius is scaled by the uniform component
    // of the linear part; warn if non-uniform.
    const Mat4f& m = record.object_to_world;
    const Point3f center = TransformPoint(m, Point3f{0.0f, 0.0f, 0.0f});

    const Vec3f sx = TransformVector(m, Vec3f{1.0f, 0.0f, 0.0f});
    const Vec3f sy = TransformVector(m, Vec3f{0.0f, 1.0f, 0.0f});
    const Vec3f sz = TransformVector(m, Vec3f{0.0f, 0.0f, 1.0f});
    const float lx = std::sqrt(Dot(sx, sx));
    const float ly = std::sqrt(Dot(sy, sy));
    const float lz = std::sqrt(Dot(sz, sz));
    const float scale = (lx + ly + lz) / 3.0f;
    const float max_dev = std::max({
        std::fabs(lx - scale),
        std::fabs(ly - scale),
        std::fabs(lz - scale)
    });
    if (max_dev > 1.0e-3f * scale) {
        diagnostics.push_back(Warning(scene, "Shape.sphere",
            "non-uniform scale on sphere transform; using average scale"));
    }

    RenderSphere sphere;
    sphere.center = center;
    sphere.radius = radius * scale;
    sphere.material_index = material_index;
    sphere.area_light_index = -1;  // Sphere area lights are not yet supported.

    // Analytic spheres can't yet be sampled as emitters; warn if the source
    // scene attaches an AreaLightSource to a Shape "sphere".
    if (record.area_light.has_value()) {
        diagnostics.push_back(Warning(scene, "Shape.sphere.AreaLightSource",
            "area light on a sphere is not yet supported; the emission will be ignored"));
    }

    ir.spheres.push_back(sphere);
    return true;
}

bool CompilePlyMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* filename_param = FindParam(record.shape.params, "filename");
    if (filename_param == nullptr || filename_param->strings.empty()) {
        diagnostics.push_back(Error(scene, "Shape.filename", "plymesh requires filename"));
        return false;
    }

    std::filesystem::path ply_path = scene.source_root / filename_param->strings[0];
    AssetLoadResult load = LoadPlyResource(ply_path);
    for (const std::string& w : load.warnings) {
        diagnostics.push_back(Warning(scene, "Shape.filename", w));
    }
    for (const std::string& e : load.errors) {
        diagnostics.push_back(Error(scene, "Shape.filename", e));
    }
    if (!load.resource.has_value()) {
        diagnostics.push_back(Error(scene, "Shape.filename", "PLY loader returned no resource"));
        return false;
    }

    for (const AssetMesh& mesh : load.resource->meshes) {
        for (const AssetPrimitive& ap : mesh.primitives) {
            std::uint32_t base_vertex = static_cast<std::uint32_t>(ir.vertices.size());
            std::uint32_t base_index = static_cast<std::uint32_t>(ir.indices.size());

            bool has_normals = (ap.normals.size() == ap.positions.size());
            bool has_uvs = (ap.texcoords0.size() == ap.positions.size());
            bool has_tangents = (ap.tangents.size() == ap.positions.size());

            for (std::size_t vi = 0; vi < ap.positions.size(); ++vi) {
                RenderVertex v;
                v.position = TransformPoint(record.object_to_world, ap.positions[vi]);
                if (has_normals) {
                    v.normal = TransformNormal(record.object_to_world, ap.normals[vi]);
                }
                if (has_uvs) {
                    v.uv = ap.texcoords0[vi];
                }
                if (has_tangents) {
                    v.tangent = TransformVector(record.object_to_world, ap.tangents[vi].direction);
                    v.tangent_handedness = ap.tangents[vi].handedness;
                }
                ir.vertices.push_back(v);
            }

            for (std::uint32_t idx : ap.indices) {
                ir.indices.push_back(base_vertex + idx);
            }

            RenderPrimitive prim;
            prim.first_index = base_index;
            prim.index_count = static_cast<std::uint32_t>(ap.indices.size());
            prim.material_index = material_index;
            prim.has_normals = has_normals;
            prim.has_uvs = has_uvs;
            prim.has_tangents = has_tangents;
            ir.primitives.push_back(prim);

            // Handle area light emission (same as trianglemesh)
            if (record.area_light.has_value()) {
                Color3f radiance = RgbParam(FindParam(record.area_light->params, "L"), Color3f{1.0f, 1.0f, 1.0f});
                float total_area = 0.0f;
                for (std::size_t ti = 0; ti < ap.indices.size() / 3; ++ti) {
                    std::uint32_t i0 = base_vertex + ap.indices[ti * 3];
                    std::uint32_t i1 = base_vertex + ap.indices[ti * 3 + 1];
                    std::uint32_t i2 = base_vertex + ap.indices[ti * 3 + 2];
                    Vec3f e1 = ir.vertices[i1].position - ir.vertices[i0].position;
                    Vec3f e2 = ir.vertices[i2].position - ir.vertices[i0].position;
                    total_area += Length(Cross(e1, e2)) * 0.5f;
                }
                EmissivePrimitive ep;
                ep.primitive_index = static_cast<int>(ir.primitives.size()) - 1;
                ep.radiance = radiance;
                ep.area = total_area;
                ir.emissive_primitives.push_back(ep);
                ir.materials[material_index].emission = radiance;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Object instancing
// ---------------------------------------------------------------------------

bool CompileInstances(
    const PbrtScene& scene,
    const std::unordered_map<std::string, int>& material_name_to_index,
    const TextureBindings& texture_bindings,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    for (const PbrtObjectInstance& instance : scene.instances) {
        auto it = scene.object_definitions.find(instance.name);
        if (it == scene.object_definitions.end()) {
            diagnostics.push_back(Warning(scene, "ObjectInstance", "undefined object: " + instance.name));
            continue;
        }
        for (const PbrtShapeRecord& shape : it->second) {
            // Compose instance transform with shape's local transform
            PbrtShapeRecord composed = shape;
            composed.object_to_world = Multiply(instance.instance_to_world, shape.object_to_world);

            // Resolve material
            int mat_idx = 0;
            if (!composed.material_name.empty()) {
                auto mit = material_name_to_index.find(composed.material_name);
                if (mit != material_name_to_index.end()) mat_idx = mit->second;
            } else if (composed.inline_material.has_value()) {
                mat_idx = CompileMaterial(*composed.inline_material, texture_bindings, ir, scene, diagnostics);
            }

            if (composed.shape.type == "trianglemesh") {
                CompileTriangleMeshShape(composed, mat_idx, ir, scene, diagnostics);
            } else if (composed.shape.type == "plymesh") {
                CompilePlyMeshShape(composed, mat_idx, ir, scene, diagnostics);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Analytic light compilation
// ---------------------------------------------------------------------------

bool CompileEnvironmentLight(
    const PbrtLightRecord& record,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    // Filename: PBRT v4 accepts "filename"; PBRT v3 used "mapname". Honor both.
    const PbrtParam* fname = FindParam(record.light.params, "filename");
    if (fname == nullptr || fname->strings.empty()) {
        fname = FindParam(record.light.params, "mapname");
    }
    if (fname == nullptr || fname->strings.empty()) {
        diagnostics.push_back(Error(scene, "LightSource.infinite",
            "infinite light requires a filename (or mapname)"));
        return false;
    }

    const std::filesystem::path resolved = scene.source_root / fname->strings[0];
    TextureLoadResult load = LoadHdrTexture(resolved);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "LightSource.infinite", load.error));
        return false;
    }

    const int texture_index = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));

    RenderEnvironmentDistribution dist = BuildEnvironmentDistribution(ir.textures[texture_index]);
    const int dist_index = static_cast<int>(ir.environment_distributions.size());
    ir.environment_distributions.push_back(std::move(dist));

    const Color3f L = RgbParam(FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
    Color3f scale{1.0f, 1.0f, 1.0f};
    const PbrtParam* scale_param = FindParam(record.light.params, "scale");
    if (scale_param != nullptr) {
        if (scale_param->floats.size() >= 3) {
            scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
        } else if (!scale_param->floats.empty()) {
            const float s = scale_param->floats[0];
            scale = Color3f{s, s, s};
        }
    }

    ir.environment.active = true;
    ir.environment.radiance = Color3f{L.x * scale.x, L.y * scale.y, L.z * scale.z};
    ir.environment.strength = 1.0f;
    ir.environment.rotation_radians = 0.0f;
    ir.environment.texture_index = texture_index;
    ir.environment.distribution_index = dist_index;

    if (FindParam(record.light.params, "portals") != nullptr) {
        diagnostics.push_back(Warning(scene, "LightSource.infinite",
            "portal sampling is not supported in M1; portals parameter ignored"));
    }
    return true;
}

void CompileAnalyticLights(const PbrtScene& scene, RenderSceneIR& ir, std::vector<SceneDiagnostic>& diagnostics) {
    for (const PbrtLightRecord& record : scene.lights) {
        if (record.light.type == "point") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Point;

            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            al.position = TransformPoint(record.light_to_world, from_local);

            const Color3f intensity = RgbParam(FindParam(record.light.params, "I"),
                                               Color3f{1.0f, 1.0f, 1.0f});
            const Color3f scale = RgbParam(FindParam(record.light.params, "scale"),
                                           Color3f{1.0f, 1.0f, 1.0f});
            al.intensity = Color3f{
                intensity.x * scale.x,
                intensity.y * scale.y,
                intensity.z * scale.z
            };
            al.direction = Vec3f{};
            al.cone_angle = 0.0f;

            ir.analytic_lights.push_back(al);
        } else if (record.light.type == "infinite") {
            CompileEnvironmentLight(record, scene, ir, diagnostics);
        } else if (record.light.type == "distant") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Distant;
            // PBRT v4: direction = to - from, then transformed by light_to_world's
            // linear part (translation is irrelevant for a directional light).
            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            const Point3f to_local = Point3FromParam(FindParam(record.light.params, "to"),
                                                     Point3f{0.0f, 0.0f, 1.0f});
            Vec3f dir_local{to_local.x - from_local.x, to_local.y - from_local.y, to_local.z - from_local.z};
            if (LengthSquared(dir_local) == 0.0f) {
                diagnostics.push_back(Warning(scene, "LightSource.distant",
                    "distant light has zero direction; defaulting to (0,-1,0)"));
                dir_local = Vec3f{0.0f, -1.0f, 0.0f};
            }
            const Vec3f dir_world = TransformVector(record.light_to_world, dir_local);
            al.direction = Normalize(dir_world);

            const Color3f L = RgbParam(FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
            Color3f scale{1.0f, 1.0f, 1.0f};
            const PbrtParam* scale_param = FindParam(record.light.params, "scale");
            if (scale_param != nullptr) {
                if (scale_param->floats.size() >= 3) {
                    scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
                } else if (!scale_param->floats.empty()) {
                    const float s = scale_param->floats[0];
                    scale = Color3f{s, s, s};
                }
            }
            al.intensity = Color3f{L.x * scale.x, L.y * scale.y, L.z * scale.z};
            al.position = Point3f{};
            al.cone_angle = 0.0f;
            ir.analytic_lights.push_back(al);
        } else if (record.light.type == "spot") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Spot;

            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            const Point3f to_local = Point3FromParam(FindParam(record.light.params, "to"),
                                                     Point3f{0.0f, 0.0f, 1.0f});
            al.position = TransformPoint(record.light_to_world, from_local);

            Vec3f dir_local{to_local.x - from_local.x, to_local.y - from_local.y, to_local.z - from_local.z};
            if (LengthSquared(dir_local) == 0.0f) {
                diagnostics.push_back(Warning(scene, "LightSource.spot",
                    "spot light has zero direction; defaulting to (0,0,1)"));
                dir_local = Vec3f{0.0f, 0.0f, 1.0f};
            }
            const Vec3f dir_world = TransformVector(record.light_to_world, dir_local);
            al.direction = Normalize(dir_world);

            const Color3f I = RgbParam(FindParam(record.light.params, "I"), Color3f{1.0f, 1.0f, 1.0f});
            Color3f scale{1.0f, 1.0f, 1.0f};
            const PbrtParam* scale_param = FindParam(record.light.params, "scale");
            if (scale_param != nullptr) {
                if (scale_param->floats.size() >= 3) {
                    scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
                } else if (!scale_param->floats.empty()) {
                    const float s = scale_param->floats[0];
                    scale = Color3f{s, s, s};
                }
            }
            al.intensity = Color3f{I.x * scale.x, I.y * scale.y, I.z * scale.z};

            const float cone_deg = FloatParam(FindParam(record.light.params, "coneangle"), 30.0f);
            const float delta_deg = FloatParam(FindParam(record.light.params, "conedeltaangle"), 5.0f);
            const float outer_rad = cone_deg * Pi / 180.0f;
            const float inner_rad = std::max(0.0f, (cone_deg - delta_deg)) * Pi / 180.0f;
            al.cone_angle = std::cos(outer_rad);
            al.cone_cos_inner = std::cos(inner_rad);

            ir.analytic_lights.push_back(al);
        } else {
            diagnostics.push_back(Warning(scene, "LightSource",
                "unsupported LightSource type: " + record.light.type));
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SceneCompileResult CompilePbrtScene(const PbrtScene& scene) {
    SceneCompileResult result;
    RenderSceneIR ir;
    auto& diagnostics = result.diagnostics;

    // 1. Film, camera, integrator, sampler
    CompileFilm(scene, ir);
    CompileCamera(scene, ir);
    CompileIntegrator(scene, ir);
    CompileSampler(scene, ir);

    // 2. Compile named textures -> name-to-index map (+ constant value side-map).
    TextureBindings texture_bindings = CompileTextures(scene, ir, diagnostics);

    // Early-exit if texture loading failed (e.g. missing imagemap files).
    if (HasSceneErrors(diagnostics)) {
        return result;
    }

    // 3. Compile named materials -> build name->index map
    std::unordered_map<std::string, int> material_name_to_index;
    for (const auto& [name, entity] : scene.named_materials) {
        int idx = CompileMaterial(entity, texture_bindings, ir, scene, diagnostics);
        material_name_to_index[name] = idx;
    }

    // 4. Compile shapes
    for (const PbrtShapeRecord& record : scene.shapes) {
        // Resolve material
        int mat_idx = 0;
        if (!record.material_name.empty()) {
            auto it = material_name_to_index.find(record.material_name);
            if (it != material_name_to_index.end()) {
                mat_idx = it->second;
            } else {
                diagnostics.push_back(Warning(scene, "Shape", "undefined material: " + record.material_name));
            }
        } else if (record.inline_material.has_value()) {
            mat_idx = CompileMaterial(*record.inline_material, texture_bindings, ir, scene, diagnostics);
        }
        // If no material at all, ensure at least one default exists
        if (ir.materials.empty()) {
            RenderMaterial default_mat;
            ir.materials.push_back(default_mat);
        }

        if (record.shape.type == "trianglemesh") {
            CompileTriangleMeshShape(record, mat_idx, ir, scene, diagnostics);
        } else if (record.shape.type == "plymesh") {
            CompilePlyMeshShape(record, mat_idx, ir, scene, diagnostics);
        } else if (record.shape.type == "sphere") {
            CompileSphereShape(record, mat_idx, ir, scene, diagnostics);
        } else {
            diagnostics.push_back(Warning(scene, "Shape", "unsupported shape type: " + record.shape.type));
        }
    }

    // 5. Compile object instances
    CompileInstances(scene, material_name_to_index, texture_bindings, ir, diagnostics);

    // 6. Compile analytic light sources
    CompileAnalyticLights(scene, ir, diagnostics);

    if (ir.primitives.empty() && ir.spheres.empty()) {
        diagnostics.push_back(Error(scene, "Shape", "scene contains no geometry"));
    }

    if (HasSceneErrors(diagnostics)) {
        return result;
    }

    result.scene = std::move(ir);
    return result;
}

} // namespace yr
