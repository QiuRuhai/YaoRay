#include "scene_compiler_internal.hpp"

#include <cstddef>
#include <unordered_map>
#include <utility>

namespace {

struct FlattenedObjectShape {
    yr::PbrtShapeRecord shape;
    int material_index = -1;
};

struct CompiledObjectDefinition {
    std::vector<yr::MeshPrimitiveHandle> instanced_primitives;
    std::vector<FlattenedObjectShape> flattened_shapes;
};

} // namespace

namespace yr::pbrt_compile {

bool CompileShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics,
    const std::string& diagnostic_field
) {
    if (record.shape.type == "trianglemesh") {
        return CompileTriangleMeshShape(record, material_index, ir, scene, diagnostics);
    }
    if (record.shape.type == "plymesh") {
        return CompilePlyMeshShape(record, material_index, ir, scene, diagnostics);
    }
    if (record.shape.type == "sphere") {
        return CompileSphereShape(record, material_index, ir, scene, diagnostics);
    }
    if (record.shape.type == "disk") {
        return CompileDiskShape(record, material_index, ir, scene, diagnostics);
    }
    if (record.shape.type == "loopsubdiv") {
        return CompileLoopSubdivShape(record, material_index, ir, scene, diagnostics);
    }
    diagnostics.push_back(Warning(
        scene, diagnostic_field, "unsupported shape type: " + record.shape.type));
    return false;
}

bool CompileInstances(
    const PbrtScene& scene,
    const std::unordered_map<std::string, int>& material_name_to_index,
    const TextureBindings& texture_bindings,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics,
    int& default_material_index
) {
    std::unordered_map<std::string, CompiledObjectDefinition> compiled_definitions;
    std::vector<bool> primitive_is_instanced_base(ir.primitives.size(), false);

    for (const PbrtObjectInstance& instance : scene.instances) {
        auto it = scene.object_definitions.find(instance.name);
        if (it == scene.object_definitions.end()) {
            diagnostics.push_back(Warning(scene, "ObjectInstance", "undefined object: " + instance.name));
            continue;
        }

        auto [compiled_it, inserted] = compiled_definitions.try_emplace(instance.name);
        CompiledObjectDefinition& compiled = compiled_it->second;
        if (inserted) {
            for (const PbrtShapeRecord& shape : it->second) {
                const int mat_idx = ResolveMaterialIndexForShape(
                    shape, material_name_to_index, texture_bindings, ir, scene, diagnostics,
                    default_material_index, "ObjectInstance");
                const bool material_supports_instancing =
                    mat_idx >= 0 && static_cast<std::size_t>(mat_idx) < ir.materials.size() &&
                    ir.materials[static_cast<std::size_t>(mat_idx)].kind !=
                        RenderMaterialKind::Subsurface;
                const bool mesh_shape = shape.shape.type != "sphere";
                const bool can_instance =
                    mesh_shape && !shape.area_light.has_value() && material_supports_instancing;
                if (!can_instance) {
                    compiled.flattened_shapes.push_back(FlattenedObjectShape{shape, mat_idx});
                    continue;
                }

                const std::size_t first_primitive = ir.primitives.size();
                CompileShape(shape, mat_idx, ir, scene, diagnostics, "ObjectInstance");
                primitive_is_instanced_base.resize(ir.primitives.size(), false);
                for (std::size_t primitive_index = first_primitive;
                     primitive_index < ir.primitives.size();
                     ++primitive_index) {
                    primitive_is_instanced_base[primitive_index] = true;
                    compiled.instanced_primitives.push_back(
                        MeshPrimitiveHandle{static_cast<int>(primitive_index)});
                }
            }
        }

        for (MeshPrimitiveHandle primitive : compiled.instanced_primitives) {
            ir.instances.push_back(RenderInstance{primitive, instance.instance_to_world});
        }
        for (const FlattenedObjectShape& flattened : compiled.flattened_shapes) {
            PbrtShapeRecord composed = flattened.shape;
            composed.object_to_world = Multiply(
                instance.instance_to_world, flattened.shape.object_to_world);
            CompileShape(
                composed,
                flattened.material_index,
                ir,
                scene,
                diagnostics,
                "ObjectInstance");
            primitive_is_instanced_base.resize(ir.primitives.size(), false);
        }
    }

    if (!ir.instances.empty()) {
        for (int primitive_index = 0;
             primitive_index < static_cast<int>(ir.primitives.size());
             ++primitive_index) {
            if (!primitive_is_instanced_base[static_cast<std::size_t>(primitive_index)]) {
                ir.instances.push_back(RenderInstance{
                    MeshPrimitiveHandle{primitive_index}, Mat4f{}});
            }
        }
    }
    return true;
}

} // namespace yr::pbrt_compile
