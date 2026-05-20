#include <yaoray/assets/gltf_loader.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>
#include <string>

namespace yr {
namespace {

bool HasGltfExtension(const std::filesystem::path& path) {
    const std::filesystem::path extension = path.extension();
    return extension == ".gltf" || extension == ".glb";
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
    if (!warning.empty()) {
        result.warnings.push_back(warning);
    }
    if (!ok) {
        result.errors.push_back(error.empty() ? "failed to parse glTF file: " + path.generic_string() : error);
        return result;
    }

    result.errors.push_back("glTF file contains no supported triangle meshes: " + path.generic_string());
    return result;
}

} // namespace yr
