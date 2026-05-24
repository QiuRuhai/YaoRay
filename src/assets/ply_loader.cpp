#include <yaoray/assets/ply_loader.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace yr {
namespace {

enum class PlyFormat {
    Ascii,
    BinaryLittleEndian,
};

struct PlyProperty {
    std::string name;
};

struct PlyHeader {
    PlyFormat format = PlyFormat::Ascii;
    int vertex_count = 0;
    int face_count = 0;
    std::vector<PlyProperty> vertex_properties;
};

void StripCarriageReturn(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

void PushError(AssetLoadResult& result, std::string message) {
    result.errors.push_back(std::move(message));
}

bool HasPlyExtension(const std::filesystem::path& path) {
    return path.extension() == ".ply";
}

std::optional<PlyHeader> ReadHeader(std::istream& in, AssetLoadResult& result) {
    std::string line;
    if (!std::getline(in, line)) {
        PushError(result, "PLY file must start with ply header");
        return std::nullopt;
    }
    StripCarriageReturn(line);
    if (line != "ply") {
        PushError(result, "PLY file must start with ply header");
        return std::nullopt;
    }

    PlyHeader header;
    std::string active_element;
    while (std::getline(in, line)) {
        StripCarriageReturn(line);
        if (line == "end_header") {
            if (header.vertex_count < 0 || header.face_count < 0) {
                PushError(result, "PLY element counts must be non-negative");
                return std::nullopt;
            }
            return header;
        }
        std::istringstream row{line};
        std::string keyword;
        row >> keyword;
        if (keyword.empty() || keyword == "comment") {
            continue;
        }
        if (keyword == "format") {
            std::string format;
            row >> format;
            if (format == "ascii") {
                header.format = PlyFormat::Ascii;
            } else if (format == "binary_little_endian") {
                header.format = PlyFormat::BinaryLittleEndian;
            } else {
                PushError(result, "unsupported PLY format: " + format);
                return std::nullopt;
            }
        } else if (keyword == "element") {
            row >> active_element;
            int count = 0;
            row >> count;
            if (!row) {
                PushError(result, "invalid PLY element declaration");
                return std::nullopt;
            }
            if (active_element == "vertex") {
                header.vertex_count = count;
            } else if (active_element == "face") {
                header.face_count = count;
            }
        } else if (keyword == "property" && active_element == "vertex") {
            std::string type;
            std::string name;
            row >> type >> name;
            if (!row) {
                PushError(result, "invalid PLY vertex property declaration");
                return std::nullopt;
            }
            header.vertex_properties.push_back(PlyProperty{name});
        }
    }

    PushError(result, "PLY header missing end_header");
    return std::nullopt;
}

AssetResource MakeSingleMeshResource(AssetPrimitive primitive) {
    AssetResource resource;
    AssetMesh mesh;
    mesh.primitives.push_back(std::move(primitive));
    resource.meshes.push_back(std::move(mesh));
    AssetNode node;
    node.mesh = 0;
    resource.nodes.push_back(node);
    AssetScene scene;
    scene.root_nodes.push_back(0);
    resource.scenes.push_back(scene);
    resource.default_scene = 0;
    return resource;
}

bool HasProperty(const PlyHeader& header, std::string_view name) {
    for (const PlyProperty& property : header.vertex_properties) {
        if (property.name == name) {
            return true;
        }
    }
    return false;
}

bool AppendFace(AssetLoadResult& result, AssetPrimitive& primitive, const std::vector<std::uint32_t>& face) {
    if (face.size() != 3 && face.size() != 4) {
        PushError(result, "only triangle and quad faces are supported");
        return false;
    }
    for (std::uint32_t index : face) {
        if (index >= primitive.positions.size()) {
            PushError(result, "PLY face index references an invalid vertex");
            return false;
        }
    }

    primitive.indices.push_back(face[0]);
    primitive.indices.push_back(face[1]);
    primitive.indices.push_back(face[2]);
    if (face.size() == 4) {
        primitive.indices.push_back(face[0]);
        primitive.indices.push_back(face[2]);
        primitive.indices.push_back(face[3]);
    }
    return true;
}

bool ReadAsciiVertices(std::istream& in, const PlyHeader& header, AssetLoadResult& result, AssetPrimitive& primitive) {
    const bool has_normals = HasProperty(header, "nx") && HasProperty(header, "ny") && HasProperty(header, "nz");
    const bool has_uv = (HasProperty(header, "s") && HasProperty(header, "t")) ||
                        (HasProperty(header, "u") && HasProperty(header, "v"));
    std::string line;
    for (int vertex = 0; vertex < header.vertex_count; ++vertex) {
        if (!std::getline(in, line)) {
            PushError(result, "unexpected end of PLY vertex data");
            return false;
        }
        StripCarriageReturn(line);
        std::istringstream row{line};
        Point3f position;
        Vec3f normal;
        Vec2f uv;
        for (const PlyProperty& property : header.vertex_properties) {
            float value = 0.0f;
            row >> value;
            if (!row) {
                PushError(result, "invalid PLY vertex row");
                return false;
            }
            if (property.name == "x") {
                position.x = value;
            } else if (property.name == "y") {
                position.y = value;
            } else if (property.name == "z") {
                position.z = value;
            } else if (property.name == "nx") {
                normal.x = value;
            } else if (property.name == "ny") {
                normal.y = value;
            } else if (property.name == "nz") {
                normal.z = value;
            } else if (property.name == "s" || property.name == "u") {
                uv.x = value;
            } else if (property.name == "t" || property.name == "v") {
                uv.y = value;
            }
        }
        primitive.positions.push_back(position);
        if (has_normals) {
            primitive.normals.push_back(normal);
        }
        if (has_uv) {
            primitive.texcoords0.push_back(uv);
        }
    }
    return true;
}

bool ReadAsciiFaces(std::istream& in, const PlyHeader& header, AssetLoadResult& result, AssetPrimitive& primitive) {
    std::string line;
    for (int face_index = 0; face_index < header.face_count; ++face_index) {
        if (!std::getline(in, line)) {
            PushError(result, "unexpected end of PLY face data");
            return false;
        }
        StripCarriageReturn(line);
        std::istringstream row{line};
        int count = 0;
        row >> count;
        if (!row || count < 0) {
            PushError(result, "invalid PLY face row");
            return false;
        }
        std::vector<std::uint32_t> face;
        face.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            std::int64_t index = 0;
            row >> index;
            if (!row || index < 0) {
                PushError(result, "invalid PLY face row");
                return false;
            }
            face.push_back(static_cast<std::uint32_t>(index));
        }
        if (!AppendFace(result, primitive, face)) {
            return false;
        }
    }
    return true;
}

bool ReadBinaryVertices(std::istream& in, const PlyHeader& header, AssetLoadResult& result, AssetPrimitive& primitive) {
    const bool has_normals = HasProperty(header, "nx") && HasProperty(header, "ny") && HasProperty(header, "nz");
    const bool has_uv = (HasProperty(header, "s") && HasProperty(header, "t")) ||
                        (HasProperty(header, "u") && HasProperty(header, "v"));
    for (int vertex = 0; vertex < header.vertex_count; ++vertex) {
        Point3f position;
        Vec3f normal;
        Vec2f uv;
        for (const PlyProperty& property : header.vertex_properties) {
            float value = 0.0f;
            in.read(reinterpret_cast<char*>(&value), sizeof(value));
            if (!in) {
                PushError(result, "unexpected end of binary PLY vertex data");
                return false;
            }
            if (property.name == "x") {
                position.x = value;
            } else if (property.name == "y") {
                position.y = value;
            } else if (property.name == "z") {
                position.z = value;
            } else if (property.name == "nx") {
                normal.x = value;
            } else if (property.name == "ny") {
                normal.y = value;
            } else if (property.name == "nz") {
                normal.z = value;
            } else if (property.name == "s" || property.name == "u") {
                uv.x = value;
            } else if (property.name == "t" || property.name == "v") {
                uv.y = value;
            }
        }
        primitive.positions.push_back(position);
        if (has_normals) {
            primitive.normals.push_back(normal);
        }
        if (has_uv) {
            primitive.texcoords0.push_back(uv);
        }
    }
    return true;
}

bool ReadBinaryFaces(std::istream& in, const PlyHeader& header, AssetLoadResult& result, AssetPrimitive& primitive) {
    for (int face_index = 0; face_index < header.face_count; ++face_index) {
        unsigned char count = 0;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!in) {
            PushError(result, "unexpected end of binary PLY face data");
            return false;
        }
        std::vector<std::uint32_t> face;
        face.reserve(count);
        for (unsigned char i = 0; i < count; ++i) {
            std::int32_t index = 0;
            in.read(reinterpret_cast<char*>(&index), sizeof(index));
            if (!in || index < 0) {
                PushError(result, "PLY face index references an invalid vertex");
                return false;
            }
            face.push_back(static_cast<std::uint32_t>(index));
        }
        if (!AppendFace(result, primitive, face)) {
            return false;
        }
    }
    return true;
}

} // namespace

AssetLoadResult LoadPlyResource(const std::filesystem::path& path) {
    AssetLoadResult result;
    if (!HasPlyExtension(path)) {
        PushError(result, "expected .ply file");
        return result;
    }
    if (!std::filesystem::exists(path)) {
        PushError(result, "PLY file not found: " + path.generic_string());
        return result;
    }

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        PushError(result, "failed to open PLY file: " + path.generic_string());
        return result;
    }

    std::optional<PlyHeader> header = ReadHeader(in, result);
    if (!header.has_value()) {
        return result;
    }
    if (!HasProperty(*header, "x") || !HasProperty(*header, "y") || !HasProperty(*header, "z")) {
        PushError(result, "PLY vertex positions require x, y, and z properties");
        return result;
    }

    AssetPrimitive primitive;
    primitive.topology = AssetPrimitiveTopology::Triangles;
    primitive.positions.reserve(static_cast<std::size_t>(header->vertex_count));
    if (header->format == PlyFormat::Ascii) {
        if (!ReadAsciiVertices(in, *header, result, primitive) ||
            !ReadAsciiFaces(in, *header, result, primitive)) {
            return result;
        }
    } else {
        if (!ReadBinaryVertices(in, *header, result, primitive) ||
            !ReadBinaryFaces(in, *header, result, primitive)) {
            return result;
        }
    }

    if (primitive.positions.empty()) {
        PushError(result, "PLY file has no vertices");
        return result;
    }
    if (primitive.indices.empty()) {
        PushError(result, "PLY file has no triangles");
        return result;
    }

    result.resource = MakeSingleMeshResource(std::move(primitive));
    return result;
}

} // namespace yr
