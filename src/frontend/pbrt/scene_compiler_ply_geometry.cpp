#include "scene_compiler_internal.hpp"

#include <yaoray/io/ply_loader.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

namespace yr::pbrt_compile {

bool CompilePlyMeshShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* filename = FindParam(record.shape.params, "filename");
    if (filename == nullptr || filename->strings.empty()) {
        diagnostics.push_back(Error(scene, "Shape.filename", "plymesh requires filename"));
        return false;
    }

    const std::filesystem::path path =
        ResolveSceneResourcePath(scene, filename->strings[0], record.shape.source_root);
    AssetLoadResult load = LoadPlyResource(path);
    for (const std::string& warning : load.warnings) {
        diagnostics.push_back(Warning(scene, "Shape.filename", warning));
    }
    for (const std::string& error : load.errors) {
        diagnostics.push_back(Warning(scene, "Shape.filename",
            "PLY load failed (" + error + "); skipping shape"));
    }
    if (!load.resource.has_value()) {
        diagnostics.push_back(Warning(scene, "Shape.filename",
            "PLY loader returned no resource; skipping shape"));
        return false;
    }

    const std::optional<Color3f> emission = record.area_light.has_value()
        ? std::optional<Color3f>{RgbParam(FindParam(record.area_light->params, "L"), Color3f{1.0f, 1.0f, 1.0f})}
        : std::nullopt;
    std::optional<int> emissive_material;

    for (const AssetMesh& mesh : load.resource->meshes) {
        for (const AssetPrimitive& asset_primitive : mesh.primitives) {
            const std::uint32_t base_vertex = static_cast<std::uint32_t>(ir.vertices.size());
            const std::uint32_t base_index = static_cast<std::uint32_t>(ir.indices.size());
            if (emission.has_value() && !emissive_material.has_value()) {
                emissive_material = CloneMaterialWithEmission(ir, material_index, *emission);
            }
            const bool has_normals = asset_primitive.normals.size() == asset_primitive.positions.size();
            const bool has_uvs = asset_primitive.texcoords0.size() == asset_primitive.positions.size();
            const bool has_tangents = asset_primitive.tangents.size() == asset_primitive.positions.size();

            for (std::size_t vertex = 0; vertex < asset_primitive.positions.size(); ++vertex) {
                RenderVertex render_vertex;
                render_vertex.position = TransformPoint(record.object_to_world, asset_primitive.positions[vertex]);
                if (has_normals) {
                    render_vertex.normal = TransformNormal(record.object_to_world, asset_primitive.normals[vertex]);
                }
                if (has_uvs) render_vertex.uv = asset_primitive.texcoords0[vertex];
                if (has_tangents) {
                    render_vertex.tangent = TransformVector(
                        record.object_to_world, asset_primitive.tangents[vertex].direction);
                    render_vertex.tangent_handedness = asset_primitive.tangents[vertex].handedness;
                }
                ir.vertices.push_back(render_vertex);
            }
            for (std::uint32_t index : asset_primitive.indices) {
                ir.indices.push_back(base_vertex + index);
            }

            ir.primitives.push_back(RenderPrimitive{
                base_index,
                static_cast<std::uint32_t>(asset_primitive.indices.size()),
                emissive_material.value_or(material_index),
                has_normals,
                has_uvs,
                has_tangents});

            if (emission.has_value()) {
                float total_area = 0.0f;
                for (std::size_t triangle = 0; triangle < asset_primitive.indices.size() / 3; ++triangle) {
                    const std::uint32_t i0 = base_vertex + asset_primitive.indices[triangle * 3];
                    const std::uint32_t i1 = base_vertex + asset_primitive.indices[triangle * 3 + 1];
                    const std::uint32_t i2 = base_vertex + asset_primitive.indices[triangle * 3 + 2];
                    total_area += Length(Cross(
                        ir.vertices[i1].position - ir.vertices[i0].position,
                        ir.vertices[i2].position - ir.vertices[i0].position)) * 0.5f;
                }
                ir.emissive_primitives.push_back(EmissivePrimitive{
                    static_cast<int>(ir.primitives.size()) - 1, *emission, total_area});
            }
        }
    }
    return true;
}

} // namespace yr::pbrt_compile
