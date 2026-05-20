#include <yaoray/assets/gltf_loader.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace yr {
namespace {

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

struct Mat4 {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

bool HasGltfExtension(const std::filesystem::path& path) {
    const std::filesystem::path extension = path.extension();
    return extension == ".gltf" || extension == ".glb";
}

void AddIfNotEmpty(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) {
        values.push_back(value);
    }
}

Mat4 Multiply(Mat4 a, Mat4 b) {
    Mat4 result;
    result.m.fill(0.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[static_cast<std::size_t>(column * 4 + row)] +=
                    a.m[static_cast<std::size_t>(k * 4 + row)] *
                    b.m[static_cast<std::size_t>(column * 4 + k)];
            }
        }
    }
    return result;
}

Point3f TransformPoint(Mat4 transform, Point3f point) {
    return Point3f{
        transform.m[0] * point.x + transform.m[4] * point.y + transform.m[8] * point.z + transform.m[12],
        transform.m[1] * point.x + transform.m[5] * point.y + transform.m[9] * point.z + transform.m[13],
        transform.m[2] * point.x + transform.m[6] * point.y + transform.m[10] * point.z + transform.m[14]
    };
}

Vec3f TransformVector(Mat4 transform, Vec3f value) {
    return Vec3f{
        transform.m[0] * value.x + transform.m[4] * value.y + transform.m[8] * value.z,
        transform.m[1] * value.x + transform.m[5] * value.y + transform.m[9] * value.z,
        transform.m[2] * value.x + transform.m[6] * value.y + transform.m[10] * value.z
    };
}

Mat4 TranslationMatrix(const std::vector<double>& translation) {
    Mat4 result;
    if (translation.size() == 3) {
        result.m[12] = static_cast<float>(translation[0]);
        result.m[13] = static_cast<float>(translation[1]);
        result.m[14] = static_cast<float>(translation[2]);
    }
    return result;
}

Mat4 ScaleMatrix(const std::vector<double>& scale) {
    Mat4 result;
    if (scale.size() == 3) {
        result.m[0] = static_cast<float>(scale[0]);
        result.m[5] = static_cast<float>(scale[1]);
        result.m[10] = static_cast<float>(scale[2]);
    }
    return result;
}

Mat4 RotationMatrix(const std::vector<double>& rotation) {
    Mat4 result;
    if (rotation.size() != 4) {
        return result;
    }

    const float x = static_cast<float>(rotation[0]);
    const float y = static_cast<float>(rotation[1]);
    const float z = static_cast<float>(rotation[2]);
    const float w = static_cast<float>(rotation[3]);
    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);
    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);
    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);
    return result;
}

std::optional<Mat4> NodeLocalTransform(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        if (node.matrix.size() != 16) {
            return std::nullopt;
        }

        Mat4 result;
        for (std::size_t i = 0; i < 16; ++i) {
            result.m[i] = static_cast<float>(node.matrix[i]);
        }
        return result;
    }

    return Multiply(Multiply(TranslationMatrix(node.translation), RotationMatrix(node.rotation)), ScaleMatrix(node.scale));
}

bool AccessorIndexValid(const tinygltf::Model& model, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < model.accessors.size();
}

bool BufferViewIndexValid(const tinygltf::Model& model, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < model.bufferViews.size();
}

bool BufferIndexValid(const tinygltf::Model& model, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < model.buffers.size();
}

std::optional<const tinygltf::Accessor*> GetAccessor(const tinygltf::Model& model, int index) {
    if (!AccessorIndexValid(model, index)) {
        return std::nullopt;
    }
    return &model.accessors[static_cast<std::size_t>(index)];
}

bool AccessorHasBufferView(const tinygltf::Accessor& accessor) {
    return accessor.bufferView >= 0 && !accessor.sparse.isSparse;
}

const std::byte* AccessorData(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    const tinygltf::BufferView& view,
    std::size_t index
) {
    if (!BufferIndexValid(model, view.buffer)) {
        return nullptr;
    }

    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const int byte_stride = accessor.ByteStride(view);
    if (byte_stride <= 0) {
        return nullptr;
    }

    const std::size_t offset = view.byteOffset + accessor.byteOffset + index * static_cast<std::size_t>(byte_stride);
    if (offset >= buffer.data.size()) {
        return nullptr;
    }
    return reinterpret_cast<const std::byte*>(buffer.data.data() + offset);
}

std::optional<Point3f> ReadVec3AccessorValue(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::size_t index
) {
    if (!AccessorHasBufferView(accessor) ||
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC3 ||
        index >= accessor.count ||
        !BufferViewIndexValid(model, accessor.bufferView)) {
        return std::nullopt;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::byte* data = AccessorData(model, accessor, view, index);
    if (data == nullptr) {
        return std::nullopt;
    }
    const float* values = reinterpret_cast<const float*>(data);
    return Point3f{values[0], values[1], values[2]};
}

std::optional<Vec2f> ReadVec2AccessorValue(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::size_t index
) {
    if (!AccessorHasBufferView(accessor) ||
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC2 ||
        index >= accessor.count ||
        !BufferViewIndexValid(model, accessor.bufferView)) {
        return std::nullopt;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::byte* data = AccessorData(model, accessor, view, index);
    if (data == nullptr) {
        return std::nullopt;
    }
    const float* values = reinterpret_cast<const float*>(data);
    return Vec2f{values[0], values[1]};
}

std::optional<std::uint32_t> ReadIndexAccessorValue(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::size_t index
) {
    if (!AccessorHasBufferView(accessor) ||
        accessor.type != TINYGLTF_TYPE_SCALAR ||
        index >= accessor.count ||
        !BufferViewIndexValid(model, accessor.bufferView)) {
        return std::nullopt;
    }

    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::byte* data = AccessorData(model, accessor, view, index);
    if (data == nullptr) {
        return std::nullopt;
    }

    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        return *reinterpret_cast<const std::uint16_t*>(data);
    }
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        return *reinterpret_cast<const std::uint32_t*>(data);
    }
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        return *reinterpret_cast<const std::uint8_t*>(data);
    }
    return std::nullopt;
}

TextureWrap ConvertTextureWrap(int value, std::string_view field, AssetLoadResult& result) {
    if (value < 0 || value == TINYGLTF_TEXTURE_WRAP_REPEAT) {
        return TextureWrap::Repeat;
    }
    if (value == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) {
        return TextureWrap::ClampToEdge;
    }
    if (value == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT) {
        return TextureWrap::MirroredRepeat;
    }
    result.warnings.push_back(
        "unsupported glTF texture wrap " + std::string{field} + ": " + std::to_string(value) + "; using repeat"
    );
    return TextureWrap::Repeat;
}

ImportedMaterial ConvertMaterial(
    const tinygltf::Model& model,
    const tinygltf::Material& material,
    const std::filesystem::path& asset_dir,
    AssetLoadResult& result
) {
    ImportedMaterial imported;
    imported.name = material.name;
    const tinygltf::PbrMetallicRoughness& pbr = material.pbrMetallicRoughness;
    imported.diffuse = Color3f{
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2])
    };
    imported.emission = Color3f{
        static_cast<float>(material.emissiveFactor[0]),
        static_cast<float>(material.emissiveFactor[1]),
        static_cast<float>(material.emissiveFactor[2])
    };
    imported.roughness = static_cast<float>(pbr.roughnessFactor);
    const float metallic = static_cast<float>(pbr.metallicFactor);
    if (metallic >= 0.5f) {
        imported.type = MaterialKind::Metal;
    } else if (imported.roughness < 0.35f) {
        imported.type = MaterialKind::Plastic;
        imported.specular = 0.04f;
    } else {
        imported.type = MaterialKind::Diffuse;
    }

    if (pbr.baseColorTexture.index >= 0 &&
        static_cast<std::size_t>(pbr.baseColorTexture.index) < model.textures.size()) {
        const tinygltf::Texture& texture = model.textures[static_cast<std::size_t>(pbr.baseColorTexture.index)];
        if (texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) < model.samplers.size()) {
            const tinygltf::Sampler& sampler = model.samplers[static_cast<std::size_t>(texture.sampler)];
            imported.diffuse_texture_wrap_s = ConvertTextureWrap(sampler.wrapS, "wrapS", result);
            imported.diffuse_texture_wrap_t = ConvertTextureWrap(sampler.wrapT, "wrapT", result);
        }
        if (texture.source >= 0 && static_cast<std::size_t>(texture.source) < model.images.size()) {
            const tinygltf::Image& image = model.images[static_cast<std::size_t>(texture.source)];
            if (!image.uri.empty()) {
                imported.diffuse_texture_path = (asset_dir / image.uri).lexically_normal();
                imported.has_diffuse_texture = true;
            }
        }
    }
    return imported;
}

std::optional<std::uint32_t> VertexIndex(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    std::size_t primitive_vertex
) {
    if (primitive.indices < 0) {
        return static_cast<std::uint32_t>(primitive_vertex);
    }

    const std::optional<const tinygltf::Accessor*> index_accessor = GetAccessor(model, primitive.indices);
    if (!index_accessor.has_value()) {
        return std::nullopt;
    }
    return ReadIndexAccessorValue(model, **index_accessor, primitive_vertex);
}

bool AppendPrimitiveTriangles(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    Mat4 transform,
    ImportedMesh& mesh,
    AssetLoadResult& result
) {
    const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
    if (mode != TINYGLTF_MODE_TRIANGLES) {
        result.errors.push_back("unsupported glTF primitive mode");
        return false;
    }

    const auto position_attribute = primitive.attributes.find("POSITION");
    if (position_attribute == primitive.attributes.end()) {
        result.errors.push_back("glTF primitive is missing POSITION");
        return false;
    }

    const std::optional<const tinygltf::Accessor*> position_accessor = GetAccessor(model, position_attribute->second);
    if (!position_accessor.has_value() ||
        (**position_accessor).componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        (**position_accessor).type != TINYGLTF_TYPE_VEC3 ||
        !AccessorHasBufferView(**position_accessor)) {
        result.errors.push_back("glTF POSITION accessor must be float VEC3 with a buffer view");
        return false;
    }

    std::optional<const tinygltf::Accessor*> normal_accessor;
    if (const auto found = primitive.attributes.find("NORMAL"); found != primitive.attributes.end()) {
        normal_accessor = GetAccessor(model, found->second);
    }

    std::optional<const tinygltf::Accessor*> uv_accessor;
    if (const auto found = primitive.attributes.find("TEXCOORD_0"); found != primitive.attributes.end()) {
        uv_accessor = GetAccessor(model, found->second);
    }

    std::size_t vertex_count = (**position_accessor).count;
    if (primitive.indices >= 0) {
        const std::optional<const tinygltf::Accessor*> index_accessor = GetAccessor(model, primitive.indices);
        if (!index_accessor.has_value() || !AccessorHasBufferView(**index_accessor)) {
            result.errors.push_back("glTF index accessor must have a buffer view");
            return false;
        }
        vertex_count = (**index_accessor).count;
    }

    if (vertex_count % 3 != 0) {
        result.errors.push_back("glTF triangle primitive vertex count is not divisible by three");
        return false;
    }

    for (std::size_t vertex = 0; vertex < vertex_count; vertex += 3) {
        const std::optional<std::uint32_t> i0 = VertexIndex(model, primitive, vertex + 0);
        const std::optional<std::uint32_t> i1 = VertexIndex(model, primitive, vertex + 1);
        const std::optional<std::uint32_t> i2 = VertexIndex(model, primitive, vertex + 2);
        if (!i0.has_value() || !i1.has_value() || !i2.has_value()) {
            result.errors.push_back("glTF primitive has invalid indices");
            return false;
        }

        const std::optional<Point3f> local_p0 = ReadVec3AccessorValue(model, **position_accessor, *i0);
        const std::optional<Point3f> local_p1 = ReadVec3AccessorValue(model, **position_accessor, *i1);
        const std::optional<Point3f> local_p2 = ReadVec3AccessorValue(model, **position_accessor, *i2);
        if (!local_p0.has_value() || !local_p1.has_value() || !local_p2.has_value()) {
            result.errors.push_back("glTF primitive has invalid POSITION data");
            return false;
        }

        const Point3f p0 = TransformPoint(transform, *local_p0);
        const Point3f p1 = TransformPoint(transform, *local_p1);
        const Point3f p2 = TransformPoint(transform, *local_p2);
        const Vec3f normal = Normalize(Cross(p1 - p0, p2 - p0));
        if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
            result.warnings.push_back("skipping degenerate glTF triangle");
            continue;
        }

        std::optional<Vec3f> n0;
        std::optional<Vec3f> n1;
        std::optional<Vec3f> n2;
        if (normal_accessor.has_value()) {
            n0 = ReadVec3AccessorValue(model, **normal_accessor, *i0);
            n1 = ReadVec3AccessorValue(model, **normal_accessor, *i1);
            n2 = ReadVec3AccessorValue(model, **normal_accessor, *i2);
        }

        std::optional<Vec2f> uv0;
        std::optional<Vec2f> uv1;
        std::optional<Vec2f> uv2;
        if (uv_accessor.has_value()) {
            uv0 = ReadVec2AccessorValue(model, **uv_accessor, *i0);
            uv1 = ReadVec2AccessorValue(model, **uv_accessor, *i1);
            uv2 = ReadVec2AccessorValue(model, **uv_accessor, *i2);
        }

        ImportedTriangle triangle;
        triangle.p0 = p0;
        triangle.p1 = p1;
        triangle.p2 = p2;
        triangle.normal = normal;
        triangle.uv0 = uv0.value_or(Vec2f{});
        triangle.uv1 = uv1.value_or(Vec2f{});
        triangle.uv2 = uv2.value_or(Vec2f{});
        triangle.has_uv = uv0.has_value() && uv1.has_value() && uv2.has_value();
        triangle.n0 = Normalize(TransformVector(transform, n0.value_or(Vec3f{})));
        triangle.n1 = Normalize(TransformVector(transform, n1.value_or(Vec3f{})));
        triangle.n2 = Normalize(TransformVector(transform, n2.value_or(Vec3f{})));
        triangle.has_vertex_normals =
            n0.has_value() && n1.has_value() && n2.has_value() &&
            LengthSquared(triangle.n0) > 0.0f &&
            LengthSquared(triangle.n1) > 0.0f &&
            LengthSquared(triangle.n2) > 0.0f;
        triangle.material_index = primitive.material;
        mesh.triangles.push_back(triangle);
    }

    return true;
}

} // namespace

AssetLoadResult LoadGltfMesh(const std::filesystem::path& path) {
    AssetLoadResult result;

    if (!HasGltfExtension(path)) {
        result.errors.push_back("glTF asset path must use a .gltf or .glb extension: " + path.generic_string());
        return result;
    }

    if (!std::filesystem::exists(path)) {
        result.errors.push_back("glTF file not found: " + path.generic_string());
        return result;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error;
    std::string warning;
    const bool ok = path.extension() == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
        : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    AddIfNotEmpty(result.warnings, warning);
    if (!ok) {
        result.errors.push_back(error.empty() ? "failed to parse glTF file: " + path.generic_string() : error);
        return result;
    }

    ImportedMesh mesh;
    const std::filesystem::path asset_dir = path.parent_path();
    mesh.materials.reserve(model.materials.size());
    for (const tinygltf::Material& material : model.materials) {
        mesh.materials.push_back(ConvertMaterial(model, material, asset_dir, result));
    }

    if (model.scenes.empty()) {
        result.errors.push_back("glTF file contains no scenes: " + path.generic_string());
        return result;
    }

    const int scene_index = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (scene_index < 0 || static_cast<std::size_t>(scene_index) >= model.scenes.size()) {
        result.errors.push_back("glTF default scene index is invalid: " + path.generic_string());
        return result;
    }

    std::function<bool(int, Mat4)> visit_node = [&](int node_index, Mat4 parent_transform) {
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= model.nodes.size()) {
            result.errors.push_back("glTF scene references an invalid node");
            return false;
        }

        const tinygltf::Node& node = model.nodes[static_cast<std::size_t>(node_index)];
        const std::optional<Mat4> local_transform = NodeLocalTransform(node);
        if (!local_transform.has_value()) {
            result.errors.push_back("glTF node matrix must contain 16 values");
            return false;
        }
        const Mat4 world_transform = Multiply(parent_transform, *local_transform);

        if (node.mesh >= 0) {
            if (static_cast<std::size_t>(node.mesh) >= model.meshes.size()) {
                result.errors.push_back("glTF node references an invalid mesh");
                return false;
            }
            const tinygltf::Mesh& gltf_mesh = model.meshes[static_cast<std::size_t>(node.mesh)];
            for (const tinygltf::Primitive& primitive : gltf_mesh.primitives) {
                if (!AppendPrimitiveTriangles(model, primitive, world_transform, mesh, result)) {
                    return false;
                }
            }
        }

        for (int child : node.children) {
            if (!visit_node(child, world_transform)) {
                return false;
            }
        }
        return true;
    };

    const tinygltf::Scene& scene = model.scenes[static_cast<std::size_t>(scene_index)];
    for (int node : scene.nodes) {
        if (!visit_node(node, Mat4{})) {
            return result;
        }
    }

    if (mesh.triangles.empty()) {
        result.errors.push_back("glTF file contains no supported triangle meshes: " + path.generic_string());
        return result;
    }

    result.mesh = std::move(mesh);
    return result;
}

} // namespace yr
