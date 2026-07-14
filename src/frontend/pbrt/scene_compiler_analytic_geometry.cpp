#include "scene_compiler_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace yr::pbrt_compile {
namespace {

constexpr float Pi = 3.14159265358979323846f;

} // namespace

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
    const float effective_radius = radius * scale;
    if (!IsFinite(radius) || !IsFinite(scale) || !IsFinite(effective_radius) ||
        radius <= 0.0f || scale <= 0.0f || effective_radius <= 0.0f) {
        diagnostics.push_back(Warning(scene, "Shape.sphere.radius",
            "effective sphere radius is not positive and finite; skipping sphere"));
        return false;
    }

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
    sphere.radius = effective_radius;
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

// Compute area-weighted smooth per-vertex normals in object space.
// P is a flat array of [x,y,z,...] with P.size()/3 vertices.
// indices is a flat array of triangle vertex indices (size must be divisible by 3).
// Returns a flat array [nx0,ny0,nz0, nx1,ny1,nz1, ...] of the same vertex count,
// each normalized.  Degenerate accumulators (zero-area neighbourhood) fall back
// to Vec3f{0, 0, 1}.

bool CompileDiskShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const float radius = FloatParam(FindParam(record.shape.params, "radius"), 1.0f);
    const float inner_radius = FloatParam(FindParam(record.shape.params, "innerradius"), 0.0f);
    const float height = FloatParam(FindParam(record.shape.params, "height"), 0.0f);

    if (radius <= 0.0f) {
        diagnostics.push_back(Warning(scene, "Shape.disk", "disk radius <= 0; skipping shape"));
        return false;
    }

    const bool has_phimax = (FindParam(record.shape.params, "phimax") != nullptr);
    if (has_phimax) {
        diagnostics.push_back(Warning(scene, "Shape.disk",
            "partial disk (phimax) is not supported; full disk will be used"));
    }

    // Tessellation quality: 32 sectors.
    constexpr int kSectors = 32;

    const std::uint32_t base_vertex = static_cast<std::uint32_t>(ir.vertices.size());
    const std::uint32_t base_index = static_cast<std::uint32_t>(ir.indices.size());
    const std::optional<Color3f> area_light_radiance = record.area_light.has_value()
        ? std::optional<Color3f>{RgbParam(FindParam(record.area_light->params, "L"), Color3f{1.0f, 1.0f, 1.0f})}
        : std::nullopt;
    const int primitive_material_index = area_light_radiance.has_value()
        ? CloneMaterialWithEmission(ir, material_index, *area_light_radiance)
        : material_index;

    // The disk normal in object space is (0, 0, 1); after transform it becomes
    // the world-space normal (sign depends on orientation but is consistent per-vertex).
    const Vec3f obj_normal{0.0f, 0.0f, 1.0f};
    const Vec3f world_normal = Normalize(TransformNormal(record.object_to_world, obj_normal));

    if (inner_radius <= 0.0f) {
        // Solid disk: 1 center vertex + kSectors ring vertices.
        // Total: kSectors + 1 vertices, kSectors triangles.

        // Center vertex at (0, 0, height) in object space.
        {
            RenderVertex v;
            v.position = TransformPoint(record.object_to_world, Point3f{0.0f, 0.0f, height});
            v.normal = world_normal;
            v.uv = Vec2f{0.5f, 0.5f};
            ir.vertices.push_back(v);
        }

        // Ring vertices
        for (int s = 0; s < kSectors; ++s) {
            const float angle = 2.0f * Pi * static_cast<float>(s) / static_cast<float>(kSectors);
            const float cos_a = std::cos(angle);
            const float sin_a = std::sin(angle);
            RenderVertex v;
            v.position = TransformPoint(record.object_to_world,
                Point3f{radius * cos_a, radius * sin_a, height});
            v.normal = world_normal;
            v.uv = Vec2f{0.5f + 0.5f * cos_a, 0.5f + 0.5f * sin_a};
            ir.vertices.push_back(v);
        }

        // Indices: triangle fan from center (base_vertex) to ring pairs.
        for (int s = 0; s < kSectors; ++s) {
            const std::uint32_t v_center = base_vertex;
            const std::uint32_t v_a = base_vertex + 1 + static_cast<std::uint32_t>(s);
            const std::uint32_t v_b = base_vertex + 1 + static_cast<std::uint32_t>((s + 1) % kSectors);
            ir.indices.push_back(v_center);
            ir.indices.push_back(v_a);
            ir.indices.push_back(v_b);
        }
    } else {
        // Annulus: 2 rings, kSectors * 2 vertices, kSectors * 2 triangles.
        // Inner ring at inner_radius, outer ring at radius.
        for (int s = 0; s < kSectors; ++s) {
            const float angle = 2.0f * Pi * static_cast<float>(s) / static_cast<float>(kSectors);
            const float cos_a = std::cos(angle);
            const float sin_a = std::sin(angle);

            // Inner ring vertex
            {
                RenderVertex v;
                v.position = TransformPoint(record.object_to_world,
                    Point3f{inner_radius * cos_a, inner_radius * sin_a, height});
                v.normal = world_normal;
                v.uv = Vec2f{0.5f + 0.5f * cos_a * (inner_radius / radius),
                              0.5f + 0.5f * sin_a * (inner_radius / radius)};
                ir.vertices.push_back(v);
            }
            // Outer ring vertex
            {
                RenderVertex v;
                v.position = TransformPoint(record.object_to_world,
                    Point3f{radius * cos_a, radius * sin_a, height});
                v.normal = world_normal;
                v.uv = Vec2f{0.5f + 0.5f * cos_a, 0.5f + 0.5f * sin_a};
                ir.vertices.push_back(v);
            }
        }

        // Indices: quad strip (2 triangles per sector).
        for (int s = 0; s < kSectors; ++s) {
            const int next_s = (s + 1) % kSectors;
            const std::uint32_t inner_a  = base_vertex + static_cast<std::uint32_t>(s * 2);
            const std::uint32_t outer_a  = base_vertex + static_cast<std::uint32_t>(s * 2 + 1);
            const std::uint32_t inner_b  = base_vertex + static_cast<std::uint32_t>(next_s * 2);
            const std::uint32_t outer_b  = base_vertex + static_cast<std::uint32_t>(next_s * 2 + 1);

            // Triangle 1: inner_a, outer_a, outer_b
            ir.indices.push_back(inner_a);
            ir.indices.push_back(outer_a);
            ir.indices.push_back(outer_b);

            // Triangle 2: inner_a, outer_b, inner_b
            ir.indices.push_back(inner_a);
            ir.indices.push_back(outer_b);
            ir.indices.push_back(inner_b);
        }
    }

    const std::uint32_t index_count = static_cast<std::uint32_t>(ir.indices.size()) - base_index;

    RenderPrimitive prim;
    prim.first_index = base_index;
    prim.index_count = index_count;
    prim.material_index = primitive_material_index;
    prim.has_normals = true;
    prim.has_uvs = true;
    prim.has_tangents = false;
    ir.primitives.push_back(prim);

    // Handle area light emission (same pattern as trianglemesh)
    if (area_light_radiance.has_value()) {
        float total_area = 0.0f;
        const std::size_t triangle_count = index_count / 3;
        for (std::size_t ti = 0; ti < triangle_count; ++ti) {
            const std::uint32_t i0 = ir.indices[base_index + ti * 3];
            const std::uint32_t i1 = ir.indices[base_index + ti * 3 + 1];
            const std::uint32_t i2 = ir.indices[base_index + ti * 3 + 2];
            const Vec3f e1 = ir.vertices[i1].position - ir.vertices[i0].position;
            const Vec3f e2 = ir.vertices[i2].position - ir.vertices[i0].position;
            total_area += Length(Cross(e1, e2)) * 0.5f;
        }

        EmissivePrimitive ep;
        ep.primitive_index = static_cast<int>(ir.primitives.size()) - 1;
        ep.radiance = *area_light_radiance;
        ep.area = total_area;
        ir.emissive_primitives.push_back(ep);
    }

    return true;
}


} // namespace yr::pbrt_compile
