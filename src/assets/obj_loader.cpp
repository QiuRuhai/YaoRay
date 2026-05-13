#include <yaoray/assets/obj_loader.hpp>

#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace yr {
namespace {

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
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

void AddIfNotEmpty(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) {
        values.push_back(value);
    }
}

} // namespace

AssetLoadResult LoadObjMesh(const std::filesystem::path& path) {
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

    ImportedMesh mesh;
    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

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
            index_offset += 3;

            if (!positions_ok) {
                result.errors.push_back("OBJ face references an invalid vertex index: " + path.generic_string());
                return result;
            }

            const Vec3f normal = Normalize(Cross(p1 - p0, p2 - p0));
            if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
                result.warnings.push_back("skipping degenerate OBJ triangle: " + path.generic_string());
                continue;
            }

            mesh.triangles.push_back(ImportedTriangle{p0, p1, p2, normal});
        }
    }

    if (mesh.triangles.empty()) {
        result.errors.push_back("OBJ mesh contains no triangles: " + path.generic_string());
        return result;
    }

    result.mesh = std::move(mesh);
    return result;
}

} // namespace yr
