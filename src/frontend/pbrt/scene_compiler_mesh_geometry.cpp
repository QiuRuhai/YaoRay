#include "scene_compiler_internal.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace yr::pbrt_compile {
namespace {

std::vector<float> ComputeSmoothVertexNormals(
    const std::vector<float>& positions,
    const std::vector<int>& indices
) {
    const std::size_t vertex_count = positions.size() / 3;
    const std::size_t triangle_count = indices.size() / 3;
    std::vector<Vec3f> accumulated(vertex_count, Vec3f{});

    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const int i0 = indices[triangle * 3];
        const int i1 = indices[triangle * 3 + 1];
        const int i2 = indices[triangle * 3 + 2];
        const Vec3f p0{positions[i0 * 3], positions[i0 * 3 + 1], positions[i0 * 3 + 2]};
        const Vec3f p1{positions[i1 * 3], positions[i1 * 3 + 1], positions[i1 * 3 + 2]};
        const Vec3f p2{positions[i2 * 3], positions[i2 * 3 + 1], positions[i2 * 3 + 2]};
        const Vec3f face_normal = Cross(p1 - p0, p2 - p0);
        accumulated[i0] = accumulated[i0] + face_normal;
        accumulated[i1] = accumulated[i1] + face_normal;
        accumulated[i2] = accumulated[i2] + face_normal;
    }

    std::vector<float> normals(vertex_count * 3);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        Vec3f normal = accumulated[vertex];
        normal = LengthSquared(normal) < 1.0e-12f
            ? Vec3f{0.0f, 0.0f, 1.0f}
            : Normalize(normal);
        normals[vertex * 3] = normal.x;
        normals[vertex * 3 + 1] = normal.y;
        normals[vertex * 3 + 2] = normal.z;
    }
    return normals;
}

} // namespace

bool CompileTriangleMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const auto& params = record.shape.params;
    const PbrtParam* positions = FindParam(params, "P");
    const PbrtParam* indices = FindParam(params, "indices");
    if (positions == nullptr || indices == nullptr) {
        diagnostics.push_back(Error(scene, "Shape", "trianglemesh requires P and indices"));
        return false;
    }
    if (positions->floats.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "Shape.P", "P count must be divisible by 3"));
        return false;
    }
    if (indices->ints.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "Shape.indices", "index count must be divisible by 3"));
        return false;
    }

    const std::uint32_t base_vertex = static_cast<std::uint32_t>(ir.vertices.size());
    const std::uint32_t base_index = static_cast<std::uint32_t>(ir.indices.size());
    const std::size_t vertex_count = positions->floats.size() / 3;
    const PbrtParam* normals = FindParam(params, "N");
    const bool has_normals = normals != nullptr && normals->floats.size() == vertex_count * 3;
    const PbrtParam* uvs = FindParam(params, "uv");
    if (uvs == nullptr) uvs = FindParam(params, "st");
    const bool has_uvs = uvs != nullptr && uvs->floats.size() == vertex_count * 2;
    const PbrtParam* tangents = FindParam(params, "S");
    const bool has_tangents = tangents != nullptr && tangents->floats.size() == vertex_count * 3;
    const std::optional<Color3f> emission = record.area_light.has_value()
        ? std::optional<Color3f>{RgbParam(FindParam(record.area_light->params, "L"), Color3f{1.0f, 1.0f, 1.0f})}
        : std::nullopt;
    const int primitive_material = emission.has_value()
        ? CloneMaterialWithEmission(ir, material_index, *emission)
        : material_index;

    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        RenderVertex render_vertex;
        const Point3f position{
            positions->floats[vertex * 3],
            positions->floats[vertex * 3 + 1],
            positions->floats[vertex * 3 + 2]};
        render_vertex.position = TransformPoint(record.object_to_world, position);
        if (has_normals) {
            const Vec3f normal{
                normals->floats[vertex * 3],
                normals->floats[vertex * 3 + 1],
                normals->floats[vertex * 3 + 2]};
            render_vertex.normal = TransformNormal(record.object_to_world, normal);
        }
        if (has_uvs) {
            render_vertex.uv = Vec2f{uvs->floats[vertex * 2], uvs->floats[vertex * 2 + 1]};
        }
        if (has_tangents) {
            const Vec3f tangent{
                tangents->floats[vertex * 3],
                tangents->floats[vertex * 3 + 1],
                tangents->floats[vertex * 3 + 2]};
            render_vertex.tangent = TransformVector(record.object_to_world, tangent);
        }
        ir.vertices.push_back(render_vertex);
    }

    for (int index : indices->ints) {
        ir.indices.push_back(base_vertex + static_cast<std::uint32_t>(index));
    }

    ir.primitives.push_back(RenderPrimitive{
        base_index,
        static_cast<std::uint32_t>(indices->ints.size()),
        primitive_material,
        has_normals,
        has_uvs,
        has_tangents});

    if (emission.has_value()) {
        float total_area = 0.0f;
        for (std::size_t triangle = 0; triangle < indices->ints.size() / 3; ++triangle) {
            const std::uint32_t i0 = base_vertex + static_cast<std::uint32_t>(indices->ints[triangle * 3]);
            const std::uint32_t i1 = base_vertex + static_cast<std::uint32_t>(indices->ints[triangle * 3 + 1]);
            const std::uint32_t i2 = base_vertex + static_cast<std::uint32_t>(indices->ints[triangle * 3 + 2]);
            total_area += Length(Cross(
                ir.vertices[i1].position - ir.vertices[i0].position,
                ir.vertices[i2].position - ir.vertices[i0].position)) * 0.5f;
        }
        ir.emissive_primitives.push_back(EmissivePrimitive{
            static_cast<int>(ir.primitives.size()) - 1, *emission, total_area});
    }

    return true;
}

bool CompileLoopSubdivShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* positions = FindParam(record.shape.params, "P");
    const PbrtParam* indices = FindParam(record.shape.params, "indices");
    if (positions == nullptr || indices == nullptr || positions->floats.empty() || indices->ints.empty()) {
        diagnostics.push_back(Warning(scene, "Shape.loopsubdiv",
            "loopsubdiv shape requires P and indices; skipping shape"));
        return false;
    }

    (void)IntParam(FindParam(record.shape.params, "levels"), 3);
    diagnostics.push_back(Warning(scene, "Shape.loopsubdiv",
        "loopsubdiv shape: subdivision levels not applied (out of scope); "
        "rendering the base control mesh with generated smooth (area-weighted) shading normals"));

    PbrtShapeRecord triangle_record = record;
    triangle_record.shape.type = "trianglemesh";
    const PbrtParam* normals = FindParam(triangle_record.shape.params, "N");
    const std::size_t vertex_count = positions->floats.size() / 3;
    if (normals == nullptr || normals->floats.size() != vertex_count * 3) {
        triangle_record.shape.params.push_back(PbrtParam{
            "normal", "N", ComputeSmoothVertexNormals(positions->floats, indices->ints), {}, {}, {}});
    }
    return CompileTriangleMeshShape(triangle_record, material_index, ir, scene, diagnostics);
}

} // namespace yr::pbrt_compile
