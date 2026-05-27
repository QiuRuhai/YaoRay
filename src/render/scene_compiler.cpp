#include <yaoray/render/scene_compiler.hpp>

#include <yaoray/assets/ply_loader.hpp>
#include <yaoray/render/texture.hpp>

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

    // Extract camera basis from camera_transform
    const Mat4f& ct = scene.camera_transform;
    ir.camera.origin = Point3f{ct.m[12], ct.m[13], ct.m[14]};
    ir.camera.right = Normalize(Vec3f{ct.m[0], ct.m[1], ct.m[2]});
    ir.camera.up = Normalize(Vec3f{ct.m[4], ct.m[5], ct.m[6]});
    ir.camera.forward = Normalize(Vec3f{ct.m[8], ct.m[9], ct.m[10]});
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
// Material compilation
// ---------------------------------------------------------------------------

int CompileMaterial(
    const PbrtEntity& entity,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    RenderMaterial material;
    const auto& params = entity.params;
    const std::string& type = entity.type;

    if (type == "matte" || type == "diffuse") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = RgbParam(FindParam(params, "reflectance"), Color3f{0.5f, 0.5f, 0.5f});
        material.reflectance.value = RgbParam(FindParam(params, "Kd"), material.reflectance.value);
    } else if (type == "conductor" || type == "metal") {
        material.kind = RenderMaterialKind::Conductor;
        material.eta.value = RgbParam(FindParam(params, "eta"), Color3f{0.2f, 0.2f, 0.2f});
        material.k.value = RgbParam(FindParam(params, "k"), Color3f{1.0f, 1.0f, 1.0f});
        material.uroughness.value = FloatParam(FindParam(params, "roughness"), 0.0f);
        material.uroughness.value = FloatParam(FindParam(params, "uroughness"), material.uroughness.value);
        material.vroughness.value = FloatParam(FindParam(params, "vroughness"), material.uroughness.value);
    } else if (type == "dielectric" || type == "glass") {
        material.kind = RenderMaterialKind::Dielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.ior = FloatParam(FindParam(params, "index"), material.ior);
        material.uroughness.value = FloatParam(FindParam(params, "uroughness"), 0.0f);
        material.vroughness.value = FloatParam(FindParam(params, "vroughness"), material.uroughness.value);
    } else if (type == "thindielectric") {
        material.kind = RenderMaterialKind::ThinDielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
    } else if (type == "coateddiffuse") {
        material.kind = RenderMaterialKind::CoatedDiffuse;
        material.reflectance.value = RgbParam(FindParam(params, "reflectance"), Color3f{0.5f, 0.5f, 0.5f});
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness.value = FloatParam(FindParam(params, "roughness"), 0.0f);
    } else if (type == "coatedconductor") {
        material.kind = RenderMaterialKind::CoatedConductor;
        material.eta.value = RgbParam(FindParam(params, "conductor.eta"), Color3f{0.2f, 0.2f, 0.2f});
        material.k.value = RgbParam(FindParam(params, "conductor.k"), Color3f{1.0f, 1.0f, 1.0f});
        material.uroughness.value = FloatParam(FindParam(params, "conductor.roughness"), 0.0f);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
    } else if (type == "diffusetransmission") {
        material.kind = RenderMaterialKind::DiffuseTransmission;
        material.reflectance.value = RgbParam(FindParam(params, "reflectance"), Color3f{0.25f, 0.25f, 0.25f});
    } else if (type == "plastic" || type == "uber" || type == "substrate") {
        // Map legacy types to diffuse with some roughness
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = RgbParam(FindParam(params, "Kd"), Color3f{0.5f, 0.5f, 0.5f});
        material.reflectance.value = RgbParam(FindParam(params, "reflectance"), material.reflectance.value);
    } else {
        diagnostics.push_back(Warning(scene, "Material", "unknown material type '" + type + "', defaulting to diffuse"));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = RgbParam(FindParam(params, "reflectance"), Color3f{0.5f, 0.5f, 0.5f});
        material.reflectance.value = RgbParam(FindParam(params, "Kd"), material.reflectance.value);
        material.reflectance.value = RgbParam(FindParam(params, "color"), material.reflectance.value);
    }

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
    sphere.area_light_index = -1;  // Area lights on spheres are handled in a later slice.

    // If an area light is attached, surface this as a warning for Slice 1 (we don't
    // yet sample analytic-shape emitters as area lights).
    if (record.area_light.has_value()) {
        diagnostics.push_back(Warning(scene, "Shape.sphere.AreaLightSource",
            "area light on a sphere is not yet supported in M1 Slice 1; the emission will be ignored"));
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
                mat_idx = CompileMaterial(*composed.inline_material, ir, scene, diagnostics);
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

    // 2. Compile named materials -> build name->index map
    std::unordered_map<std::string, int> material_name_to_index;
    for (const auto& [name, entity] : scene.named_materials) {
        int idx = CompileMaterial(entity, ir, scene, diagnostics);
        material_name_to_index[name] = idx;
    }

    // 3. Compile shapes
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
            mat_idx = CompileMaterial(*record.inline_material, ir, scene, diagnostics);
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

    // 4. Compile object instances
    CompileInstances(scene, material_name_to_index, ir, diagnostics);

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
