#include <yaoray/assets/gltf_loader.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace yr {
namespace {

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

AssetTransform ToAssetTransform(Mat4 value) {
    AssetTransform transform;
    transform.local_to_parent = value.m;
    return transform;
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

std::optional<std::size_t> ComponentByteSize(int component_type) {
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return 2;
    case TINYGLTF_COMPONENT_TYPE_INT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return 4;
    default:
        return std::nullopt;
    }
}

std::optional<std::size_t> ComponentCount(int type) {
    switch (type) {
    case TINYGLTF_TYPE_SCALAR:
        return 1;
    case TINYGLTF_TYPE_VEC2:
        return 2;
    case TINYGLTF_TYPE_VEC3:
        return 3;
    case TINYGLTF_TYPE_VEC4:
    case TINYGLTF_TYPE_MAT2:
        return 4;
    case TINYGLTF_TYPE_MAT3:
        return 9;
    case TINYGLTF_TYPE_MAT4:
        return 16;
    default:
        return std::nullopt;
    }
}

std::optional<std::size_t> ElementByteSize(const tinygltf::Accessor& accessor) {
    const std::optional<std::size_t> component_byte_size = ComponentByteSize(accessor.componentType);
    const std::optional<std::size_t> component_count = ComponentCount(accessor.type);
    if (!component_byte_size.has_value() || !component_count.has_value()) {
        return std::nullopt;
    }
    if (*component_count > std::numeric_limits<std::size_t>::max() / *component_byte_size) {
        return std::nullopt;
    }
    return *component_byte_size * *component_count;
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
    if (view.byteOffset > buffer.data.size() ||
        view.byteLength > buffer.data.size() - view.byteOffset ||
        accessor.byteOffset > view.byteLength) {
        return nullptr;
    }

    const std::optional<std::size_t> element_byte_size = ElementByteSize(accessor);
    if (!element_byte_size.has_value()) {
        return nullptr;
    }

    const int byte_stride = accessor.ByteStride(view);
    if (byte_stride <= 0 || static_cast<std::size_t>(byte_stride) < *element_byte_size) {
        return nullptr;
    }
    const std::size_t stride = static_cast<std::size_t>(byte_stride);

    if (index > std::numeric_limits<std::size_t>::max() / stride) {
        return nullptr;
    }

    const std::size_t relative_offset = index * stride;
    const std::size_t view_remaining = view.byteLength - accessor.byteOffset;
    if (relative_offset > view_remaining || *element_byte_size > view_remaining - relative_offset) {
        return nullptr;
    }

    const std::size_t offset = view.byteOffset + accessor.byteOffset + relative_offset;
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

std::optional<AssetTangent> ReadTangentAccessorValue(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::size_t index
) {
    if (!AccessorHasBufferView(accessor) ||
        accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.type != TINYGLTF_TYPE_VEC4 ||
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
    return AssetTangent{Vec3f{values[0], values[1], values[2]}, values[3]};
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

int AddGltfSampler(const tinygltf::Sampler& sampler, AssetLoadResult& result, AssetResource& resource) {
    AssetSampler imported;
    imported.wrap_s = ConvertTextureWrap(sampler.wrapS, "wrapS", result);
    imported.wrap_t = ConvertTextureWrap(sampler.wrapT, "wrapT", result);
    const int index = static_cast<int>(resource.samplers.size());
    resource.samplers.push_back(imported);
    return index;
}

bool CopyGltfImagesAndSamplers(
    const tinygltf::Model& model,
    const std::filesystem::path& asset_dir,
    AssetLoadResult& result,
    AssetResource& resource
) {
    for (const tinygltf::Sampler& sampler : model.samplers) {
        AddGltfSampler(sampler, result, resource);
    }
    for (const tinygltf::Image& image : model.images) {
        AssetImage imported;
        if (!image.uri.empty()) {
            imported.path = (asset_dir / image.uri).lexically_normal();
        }
        resource.images.push_back(std::move(imported));
    }
    for (const tinygltf::Texture& texture : model.textures) {
        if (texture.source < -1 ||
            (texture.source >= 0 && static_cast<std::size_t>(texture.source) >= model.images.size())) {
            result.errors.push_back("glTF texture references an invalid image");
            return false;
        }
        if (texture.sampler < -1 ||
            (texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) >= model.samplers.size())) {
            result.errors.push_back("glTF texture references an invalid sampler");
            return false;
        }
        AssetTexture imported;
        imported.image = texture.source;
        imported.sampler = texture.sampler;
        resource.textures.push_back(imported);
    }
    return true;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

AssetAlphaMode ConvertAlphaMode(const std::string& alpha_mode, AssetLoadResult& result) {
    if (alpha_mode == "MASK") {
        return AssetAlphaMode::Mask;
    }
    if (alpha_mode == "BLEND") {
        result.warnings.push_back(
            "glTF alphaMode BLEND is preserved but rendered as opaque in this compatibility slice"
        );
        return AssetAlphaMode::Blend;
    }
    if (alpha_mode != "OPAQUE") {
        result.warnings.push_back("unsupported glTF alphaMode: " + alpha_mode + "; using opaque");
    }
    return AssetAlphaMode::Opaque;
}

std::optional<int> ValidateMaterialTextureIndex(
    const tinygltf::Model& model,
    int texture_index,
    std::string_view slot_name,
    AssetLoadResult& result
) {
    if (texture_index == -1) {
        return -1;
    }
    if (texture_index < -1 || static_cast<std::size_t>(texture_index) >= model.textures.size()) {
        result.errors.push_back("invalid " + std::string{slot_name} + " texture index");
        return std::nullopt;
    }

    const tinygltf::Texture& texture = model.textures[static_cast<std::size_t>(texture_index)];
    if (texture.source < 0 || static_cast<std::size_t>(texture.source) >= model.images.size()) {
        result.errors.push_back(std::string{slot_name} + " texture image source is invalid");
        return std::nullopt;
    }

    const tinygltf::Image& image = model.images[static_cast<std::size_t>(texture.source)];
    if (StartsWith(image.uri, "data:")) {
        result.errors.push_back(std::string{slot_name} + " texture image data URI is unsupported");
        return std::nullopt;
    }
    if (image.uri.empty()) {
        result.errors.push_back(
            std::string{slot_name} + " texture image requires an external image URI; embedded images and data URI textures are unsupported"
        );
        return std::nullopt;
    }

    return texture_index;
}

std::optional<AssetMaterial> ConvertMaterial(
    const tinygltf::Model& model,
    const tinygltf::Material& material,
    AssetLoadResult& result
) {
    AssetMaterial imported;
    imported.name = material.name;
    const tinygltf::PbrMetallicRoughness& pbr = material.pbrMetallicRoughness;
    imported.base_color = Color3f{
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2])
    };
    imported.base_color_alpha = static_cast<float>(pbr.baseColorFactor[3]);
    imported.emission = Color3f{
        static_cast<float>(material.emissiveFactor[0]),
        static_cast<float>(material.emissiveFactor[1]),
        static_cast<float>(material.emissiveFactor[2])
    };
    imported.roughness = static_cast<float>(pbr.roughnessFactor);
    imported.metallic = static_cast<float>(pbr.metallicFactor);

    const std::optional<int> base_color_texture =
        ValidateMaterialTextureIndex(model, pbr.baseColorTexture.index, "base color", result);
    if (!base_color_texture.has_value()) {
        return std::nullopt;
    }
    imported.base_color_texture = *base_color_texture;

    const std::optional<int> metallic_roughness_texture =
        ValidateMaterialTextureIndex(model, pbr.metallicRoughnessTexture.index, "metallic-roughness", result);
    if (!metallic_roughness_texture.has_value()) {
        return std::nullopt;
    }
    imported.metallic_roughness_texture = *metallic_roughness_texture;

    const std::optional<int> normal_texture =
        ValidateMaterialTextureIndex(model, material.normalTexture.index, "normal", result);
    if (!normal_texture.has_value()) {
        return std::nullopt;
    }
    imported.normal_texture = *normal_texture;
    imported.normal_scale = static_cast<float>(material.normalTexture.scale);

    const std::optional<int> occlusion_texture =
        ValidateMaterialTextureIndex(model, material.occlusionTexture.index, "occlusion", result);
    if (!occlusion_texture.has_value()) {
        return std::nullopt;
    }
    imported.occlusion_texture = *occlusion_texture;
    imported.occlusion_strength = static_cast<float>(material.occlusionTexture.strength);

    const std::optional<int> emissive_texture =
        ValidateMaterialTextureIndex(model, material.emissiveTexture.index, "emissive", result);
    if (!emissive_texture.has_value()) {
        return std::nullopt;
    }
    imported.emissive_texture = *emissive_texture;
    imported.alpha_mode = ConvertAlphaMode(material.alphaMode, result);
    imported.alpha_cutoff = static_cast<float>(material.alphaCutoff);
    imported.double_sided = material.doubleSided;
    return imported;
}

bool AppendPrimitiveResource(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    AssetMesh& mesh,
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

    if (primitive.material < -1 ||
        (primitive.material >= 0 && static_cast<std::size_t>(primitive.material) >= model.materials.size())) {
        result.errors.push_back("glTF primitive references an invalid material");
        return false;
    }

    AssetPrimitive imported;
    imported.topology = AssetPrimitiveTopology::Triangles;
    imported.material = primitive.material;

    for (std::size_t vertex = 0; vertex < (**position_accessor).count; ++vertex) {
        const std::optional<Point3f> position = ReadVec3AccessorValue(model, **position_accessor, vertex);
        if (!position.has_value()) {
            result.errors.push_back("glTF primitive has invalid POSITION data");
            return false;
        }
        imported.positions.push_back(*position);
    }

    if (const auto found = primitive.attributes.find("NORMAL"); found != primitive.attributes.end()) {
        const std::optional<const tinygltf::Accessor*> normal_accessor = GetAccessor(model, found->second);
        if (!normal_accessor.has_value()) {
            result.errors.push_back("glTF primitive references an invalid NORMAL accessor");
            return false;
        }
        if ((**normal_accessor).count != (**position_accessor).count) {
            result.errors.push_back("glTF NORMAL accessor count must match POSITION accessor count");
            return false;
        }
        for (std::size_t vertex = 0; vertex < (**normal_accessor).count; ++vertex) {
            const std::optional<Point3f> normal = ReadVec3AccessorValue(model, **normal_accessor, vertex);
            if (!normal.has_value()) {
                result.errors.push_back("glTF primitive has invalid NORMAL data");
                return false;
            }
            imported.normals.push_back(Normalize(Vec3f{normal->x, normal->y, normal->z}));
        }
    }

    if (const auto found = primitive.attributes.find("TEXCOORD_0"); found != primitive.attributes.end()) {
        const std::optional<const tinygltf::Accessor*> uv_accessor = GetAccessor(model, found->second);
        if (!uv_accessor.has_value()) {
            result.errors.push_back("glTF primitive references an invalid TEXCOORD_0 accessor");
            return false;
        }
        if ((**uv_accessor).count != (**position_accessor).count) {
            result.errors.push_back("glTF TEXCOORD_0 accessor count must match POSITION accessor count");
            return false;
        }
        for (std::size_t vertex = 0; vertex < (**uv_accessor).count; ++vertex) {
            const std::optional<Vec2f> uv = ReadVec2AccessorValue(model, **uv_accessor, vertex);
            if (!uv.has_value()) {
                result.errors.push_back("glTF primitive has invalid TEXCOORD_0 data");
                return false;
            }
            imported.texcoords0.push_back(*uv);
        }
    }

    if (const auto found = primitive.attributes.find("TANGENT"); found != primitive.attributes.end()) {
        const std::optional<const tinygltf::Accessor*> tangent_accessor = GetAccessor(model, found->second);
        if (!tangent_accessor.has_value()) {
            result.errors.push_back("glTF primitive references an invalid TANGENT accessor");
            return false;
        }
        if ((**tangent_accessor).count != (**position_accessor).count) {
            result.errors.push_back("glTF TANGENT accessor count must match POSITION accessor count");
            return false;
        }
        for (std::size_t vertex = 0; vertex < (**tangent_accessor).count; ++vertex) {
            const std::optional<AssetTangent> tangent = ReadTangentAccessorValue(model, **tangent_accessor, vertex);
            if (!tangent.has_value()) {
                result.errors.push_back("glTF primitive has invalid TANGENT data");
                return false;
            }
            imported.tangents.push_back(*tangent);
        }
    }

    std::size_t index_count = imported.positions.size();
    if (primitive.indices < -1) {
        result.errors.push_back("glTF primitive references an invalid index accessor");
        return false;
    }
    if (primitive.indices >= 0) {
        const std::optional<const tinygltf::Accessor*> index_accessor = GetAccessor(model, primitive.indices);
        if (!index_accessor.has_value() || !AccessorHasBufferView(**index_accessor)) {
            result.errors.push_back("glTF index accessor must have a buffer view");
            return false;
        }
        index_count = (**index_accessor).count;
        for (std::size_t index = 0; index < index_count; ++index) {
            const std::optional<std::uint32_t> value = ReadIndexAccessorValue(model, **index_accessor, index);
            if (!value.has_value()) {
                result.errors.push_back("glTF primitive has invalid indices");
                return false;
            }
            if (*value >= imported.positions.size()) {
                result.errors.push_back("glTF primitive index references an invalid vertex");
                return false;
            }
            imported.indices.push_back(*value);
        }
    } else {
        if (index_count > std::numeric_limits<std::uint32_t>::max()) {
            result.errors.push_back("glTF primitive has too many vertices for uint32 indices");
            return false;
        }
        for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(index_count); ++index) {
            imported.indices.push_back(index);
        }
    }

    if (imported.indices.empty()) {
        result.errors.push_back("glTF triangle primitive contains no vertices");
        return false;
    }
    if (imported.indices.size() % 3 != 0) {
        result.errors.push_back("glTF triangle primitive vertex count is not divisible by three");
        return false;
    }

    mesh.primitives.push_back(std::move(imported));
    return true;
}

} // namespace

AssetLoadResult LoadGltfResource(const std::filesystem::path& path) {
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

    AssetResource resource;
    const std::filesystem::path asset_dir = path.parent_path();
    if (!CopyGltfImagesAndSamplers(model, asset_dir, result, resource)) {
        return result;
    }

    resource.materials.reserve(model.materials.size());
    for (const tinygltf::Material& material : model.materials) {
        std::optional<AssetMaterial> imported = ConvertMaterial(model, material, result);
        if (!imported.has_value()) {
            return result;
        }
        resource.materials.push_back(std::move(*imported));
    }

    bool has_supported_primitives = false;
    for (const tinygltf::Mesh& gltf_mesh : model.meshes) {
        AssetMesh mesh;
        mesh.name = gltf_mesh.name;
        for (const tinygltf::Primitive& primitive : gltf_mesh.primitives) {
            if (!AppendPrimitiveResource(model, primitive, mesh, result)) {
                return result;
            }
        }
        if (!mesh.primitives.empty()) {
            has_supported_primitives = true;
        }
        resource.meshes.push_back(std::move(mesh));
    }

    for (const tinygltf::Node& gltf_node : model.nodes) {
        const std::optional<Mat4> local_transform = NodeLocalTransform(gltf_node);
        if (!local_transform.has_value()) {
            result.errors.push_back("glTF node matrix must contain 16 values");
            return result;
        }
        if (gltf_node.mesh < -1 ||
            (gltf_node.mesh >= 0 && static_cast<std::size_t>(gltf_node.mesh) >= model.meshes.size())) {
            result.errors.push_back("glTF node references an invalid mesh");
            return result;
        }
        for (int child : gltf_node.children) {
            if (child < 0 || static_cast<std::size_t>(child) >= model.nodes.size()) {
                result.errors.push_back("glTF node references an invalid child node");
                return result;
            }
        }
        AssetNode node;
        node.name = gltf_node.name;
        node.transform = ToAssetTransform(*local_transform);
        node.mesh = gltf_node.mesh;
        node.children = gltf_node.children;
        resource.nodes.push_back(std::move(node));
    }

    for (const tinygltf::Scene& gltf_scene : model.scenes) {
        for (int node_index : gltf_scene.nodes) {
            if (node_index < 0 || static_cast<std::size_t>(node_index) >= model.nodes.size()) {
                result.errors.push_back("glTF scene references an invalid root node");
                return result;
            }
        }
        AssetScene scene;
        scene.name = gltf_scene.name;
        scene.root_nodes = gltf_scene.nodes;
        resource.scenes.push_back(std::move(scene));
    }

    resource.default_scene = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (resource.default_scene < 0 || static_cast<std::size_t>(resource.default_scene) >= resource.scenes.size()) {
        result.errors.push_back("glTF default scene index is invalid: " + path.generic_string());
        return result;
    }

    if (!has_supported_primitives) {
        result.errors.push_back("glTF file contains no supported triangle meshes: " + path.generic_string());
        return result;
    }

    result.resource = std::move(resource);
    return result;
}

} // namespace yr
