#include <yaoray/assets/obj_loader.hpp>

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace yr {
namespace {

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

struct PrimitiveKey {
    int material = -1;
    bool has_uv = false;
    bool has_normals = false;

    bool operator==(const PrimitiveKey& other) const {
        return material == other.material &&
            has_uv == other.has_uv &&
            has_normals == other.has_normals;
    }
};

struct PrimitiveKeyHash {
    std::size_t operator()(const PrimitiveKey& key) const {
        std::size_t value = static_cast<std::size_t>(key.material + 2048);
        value = value * 31u + (key.has_uv ? 1u : 0u);
        value = value * 31u + (key.has_normals ? 1u : 0u);
        return value;
    }
};

bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
}

int AddObjTexture(AssetResource& resource, const std::filesystem::path& path) {
    const int image_index = static_cast<int>(resource.images.size());
    resource.images.push_back(AssetImage{path});
    const int sampler_index = static_cast<int>(resource.samplers.size());
    resource.samplers.push_back(AssetSampler{TextureWrap::Repeat, TextureWrap::Repeat});
    const int texture_index = static_cast<int>(resource.textures.size());
    resource.textures.push_back(AssetTexture{image_index, sampler_index});
    return texture_index;
}

AssetMaterial ConvertObjMaterial(
    const tinyobj::material_t& material,
    const std::filesystem::path& asset_dir,
    AssetResource& resource
) {
    AssetMaterial imported;
    imported.name = material.name;
    imported.approximate_type = MaterialKind::Diffuse;
    imported.base_color = Color3f{material.diffuse[0], material.diffuse[1], material.diffuse[2]};
    imported.specular = 0.04f;
    if (!material.diffuse_texname.empty()) {
        imported.base_color_texture = AddObjTexture(resource, asset_dir / material.diffuse_texname);
    }
    return imported;
}

Point3f ReadPosition(const tinyobj::attrib_t& attrib, int vertex_index, bool& ok) {
    if (vertex_index < 0) {
        ok = false;
        return {};
    }

    const std::size_t base = static_cast<std::size_t>(vertex_index) * 3;
    if (base + 2 >= attrib.vertices.size()) {
        ok = false;
        return {};
    }

    return Point3f{
        attrib.vertices[base + 0],
        attrib.vertices[base + 1],
        attrib.vertices[base + 2]
    };
}

Vec2f ReadTexCoord(const tinyobj::attrib_t& attrib, int texcoord_index, bool& ok) {
    if (texcoord_index < 0) {
        ok = false;
        return {};
    }

    const std::size_t base = static_cast<std::size_t>(texcoord_index) * 2;
    if (base + 1 >= attrib.texcoords.size()) {
        ok = false;
        return {};
    }

    return Vec2f{attrib.texcoords[base + 0], attrib.texcoords[base + 1]};
}

Vec3f ReadNormal(const tinyobj::attrib_t& attrib, int normal_index, bool& ok) {
    if (normal_index < 0) {
        ok = false;
        return {};
    }

    const std::size_t base = static_cast<std::size_t>(normal_index) * 3;
    if (base + 2 >= attrib.normals.size()) {
        ok = false;
        return {};
    }

    return Normalize(Vec3f{
        attrib.normals[base + 0],
        attrib.normals[base + 1],
        attrib.normals[base + 2]
    });
}

void AddIfNotEmpty(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) {
        values.push_back(value);
    }
}

} // namespace

AssetLoadResult LoadObjResource(const std::filesystem::path& path) {
    AssetLoadResult result;

    if (!HasObjExtension(path)) {
        result.errors.push_back("OBJ asset path must use a .obj extension: " + path.generic_string());
        return result;
    }

    if (!std::filesystem::exists(path)) {
        result.errors.push_back("OBJ file not found: " + path.generic_string());
        return result;
    }

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;
    config.mtl_search_path = path.parent_path().string();

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string(), config)) {
        AddIfNotEmpty(result.errors, reader.Error());
        if (result.errors.empty()) {
            result.errors.push_back("failed to parse OBJ file: " + path.generic_string());
        }
        AddIfNotEmpty(result.warnings, reader.Warning());
        return result;
    }

    AddIfNotEmpty(result.warnings, reader.Warning());

    AssetResource resource;
    resource.scenes.push_back(AssetScene{"default", {0}});
    AssetNode root;
    root.name = path.stem().string();
    root.mesh = 0;
    resource.nodes.push_back(std::move(root));
    resource.meshes.push_back(AssetMesh{path.stem().string(), {}});
    AssetMesh& mesh = resource.meshes[0];

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();
    std::vector<int> material_index_map(materials.size(), -1);
    std::unordered_map<std::string, int> material_names;

    for (std::size_t source_index = 0; source_index < materials.size(); ++source_index) {
        const tinyobj::material_t& material = materials[source_index];
        if (material.name.empty()) {
            continue;
        }
        if (material_names.find(material.name) != material_names.end()) {
            result.errors.push_back("duplicate OBJ material: " + material.name);
            return result;
        }

        AssetMaterial imported = ConvertObjMaterial(material, path.parent_path(), resource);
        material_names.emplace(imported.name, static_cast<int>(resource.materials.size()));
        material_index_map[source_index] = static_cast<int>(resource.materials.size());
        resource.materials.push_back(std::move(imported));
    }

    std::unordered_map<PrimitiveKey, std::size_t, PrimitiveKeyHash> primitive_indices;

    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t index_offset = 0;
        for (std::size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); ++face_index) {
            const int face_vertices = shape.mesh.num_face_vertices[face_index];
            if (face_vertices != 3) {
                result.warnings.push_back("skipping non-triangle OBJ face after triangulation: " + path.generic_string());
                index_offset += static_cast<std::size_t>(face_vertices);
                continue;
            }

            if (index_offset + 2 >= shape.mesh.indices.size()) {
                result.errors.push_back("OBJ face index data is incomplete: " + path.generic_string());
                return result;
            }

            bool positions_ok = true;
            const Point3f p0 = ReadPosition(attrib, shape.mesh.indices[index_offset + 0].vertex_index, positions_ok);
            const Point3f p1 = ReadPosition(attrib, shape.mesh.indices[index_offset + 1].vertex_index, positions_ok);
            const Point3f p2 = ReadPosition(attrib, shape.mesh.indices[index_offset + 2].vertex_index, positions_ok);
            bool uvs_ok = true;
            const Vec2f uv0 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 0].texcoord_index, uvs_ok);
            const Vec2f uv1 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 1].texcoord_index, uvs_ok);
            const Vec2f uv2 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 2].texcoord_index, uvs_ok);
            bool normals_ok = true;
            const Vec3f n0 = ReadNormal(attrib, shape.mesh.indices[index_offset + 0].normal_index, normals_ok);
            const Vec3f n1 = ReadNormal(attrib, shape.mesh.indices[index_offset + 1].normal_index, normals_ok);
            const Vec3f n2 = ReadNormal(attrib, shape.mesh.indices[index_offset + 2].normal_index, normals_ok);
            const int source_material_index =
                face_index < shape.mesh.material_ids.size() ? shape.mesh.material_ids[face_index] : -1;
            index_offset += 3;

            if (!positions_ok) {
                result.errors.push_back("OBJ face references an invalid vertex index: " + path.generic_string());
                return result;
            }

            int material_index = -1;
            if (source_material_index >= 0) {
                const std::size_t material_map_index = static_cast<std::size_t>(source_material_index);
                if (material_map_index >= material_index_map.size()) {
                    result.errors.push_back("OBJ face references an invalid material index: " + path.generic_string());
                    return result;
                }
                material_index = material_index_map[material_map_index];
            }

            const Vec3f face_normal = Cross(p1 - p0, p2 - p0);
            if (LengthSquared(face_normal) <= DegenerateTriangleEpsilon) {
                result.warnings.push_back("skipping degenerate OBJ triangle: " + path.generic_string());
                continue;
            }

            const bool has_uv = uvs_ok;
            const bool has_normals =
                normals_ok &&
                LengthSquared(n0) > 0.0f &&
                LengthSquared(n1) > 0.0f &&
                LengthSquared(n2) > 0.0f;
            const PrimitiveKey key{material_index, has_uv, has_normals};

            auto primitive_found = primitive_indices.find(key);
            if (primitive_found == primitive_indices.end()) {
                AssetPrimitive primitive;
                primitive.topology = AssetPrimitiveTopology::Triangles;
                primitive.material = material_index;
                mesh.primitives.push_back(std::move(primitive));
                primitive_found = primitive_indices.emplace(key, mesh.primitives.size() - 1).first;
            }

            AssetPrimitive& primitive = mesh.primitives[primitive_found->second];
            if (primitive.positions.size() > std::numeric_limits<std::uint32_t>::max() - 3) {
                result.errors.push_back("OBJ mesh has too many vertices for uint32 indices: " + path.generic_string());
                return result;
            }
            const std::uint32_t base_index = static_cast<std::uint32_t>(primitive.positions.size());
            primitive.positions.push_back(p0);
            primitive.positions.push_back(p1);
            primitive.positions.push_back(p2);
            primitive.indices.push_back(base_index + 0);
            primitive.indices.push_back(base_index + 1);
            primitive.indices.push_back(base_index + 2);
            if (has_uv) {
                primitive.texcoords0.push_back(uv0);
                primitive.texcoords0.push_back(uv1);
                primitive.texcoords0.push_back(uv2);
            }
            if (has_normals) {
                primitive.normals.push_back(n0);
                primitive.normals.push_back(n1);
                primitive.normals.push_back(n2);
            }
        }
    }

    if (mesh.primitives.empty()) {
        result.errors.push_back("OBJ mesh contains no triangles: " + path.generic_string());
        return result;
    }

    result.resource = std::move(resource);
    return result;
}

} // namespace yr
