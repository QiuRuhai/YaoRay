# YaoRay AssetResource Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat `ImportedMesh` asset API with `AssetResource`, and make OBJ/glTF loaders plus the render compiler use that resource model directly.

**Architecture:** `yaoray_assets` becomes responsible for format-specific import into a shared static asset resource model. `yaoray_render` caches `AssetResource`, traverses default asset scenes and nodes, maps asset materials/textures to render materials/textures, and expands mesh primitives into the existing `RenderSceneIR`. This is an aggressive replacement: the old `ImportedMesh`, `ImportedTriangle`, and `ImportedMaterial` types are removed rather than bridged.

**Tech Stack:** C++20, CMake, tinyobjloader, tinygltf, custom `YR_TEST` harness, CTest.

---

## Scope Check

This plan implements Phase 2 from:

`docs/superpowers/specs/2026-05-22-yaoray-asset-resource-aggressive-replacement-design.md`

It changes the asset import contract and render compiler asset traversal. It does not add new glTF features such as animation, skinning, morph targets, cameras, lights, alpha modes, normal maps, or full PBR shading.

## File Structure

- Create `include/yaoray/assets/asset_resource.hpp`: shared asset resource structs and `AssetLoadResult`.
- Delete `include/yaoray/assets/imported_asset.hpp`: old flat imported asset API.
- Modify `include/yaoray/assets/obj_loader.hpp`: include `asset_resource.hpp`, expose `LoadObjResource()`.
- Modify `include/yaoray/assets/gltf_loader.hpp`: include `asset_resource.hpp`, expose `LoadGltfResource()`.
- Modify `src/assets/obj_loader.cpp`: build one-scene `AssetResource`, group faces into primitives by material and attribute availability.
- Modify `src/assets/gltf_loader.cpp`: build glTF scenes, nodes, meshes, primitives, materials, textures, images, and samplers without baking node transforms.
- Modify `src/render/scene_compiler.cpp`: cache `AssetResource`, traverse default scene nodes, compose transforms, compile asset materials/textures, and expand primitives to `RenderSceneIR`.
- Modify `tests/assets_tests.cpp`: replace imported mesh assertions with resource shape assertions.
- Modify `tests/render_scene_tests.cpp`: add resource traversal and material override compiler tests.
- Modify `README.md` and `docs/architecture/overview.md`: document implemented AssetResource layer.
- Modify `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-aggressive-replacement-design.md`: append implementation status when complete.

## Pre-Flight

- [ ] **Step 1: Create an isolated execution worktree**

Use `superpowers:using-git-worktrees` at execution time. Preferred worktree:

```bash
git worktree add .worktrees/asset-resource-replacement -b codex/asset-resource-replacement
```

Expected: new worktree on branch `codex/asset-resource-replacement`.

- [ ] **Step 2: Verify baseline in the execution worktree**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and `17/17` tests pass on macOS.

- [ ] **Step 3: Confirm old asset API usages**

Run:

```bash
rg -n "ImportedMesh|ImportedTriangle|ImportedMaterial|LoadObjMesh|LoadGltfMesh|AssetLoadResult" include src tests
```

Expected: matches in current asset headers/loaders, `scene_compiler.cpp`, and `tests/assets_tests.cpp`. These matches are the replacement target.

---

### Task 1: Add Failing AssetResource Contract Tests

**Files:**
- Modify: `tests/assets_tests.cpp`

- [ ] **Step 1: Add AssetResource include and default model tests**

Add this include below the existing test includes:

```cpp
#include <yaoray/assets/asset_resource.hpp>
```

Replace `imported_triangle_defaults_do_not_claim_vertex_normals` with:

```cpp
YR_TEST(asset_transform_defaults_to_identity_matrix) {
    const yr::AssetTransform transform;

    YR_EXPECT_NEAR(transform.local_to_parent[0], 1.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[5], 1.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[10], 1.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[15], 1.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[1], 0.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[4], 0.0, 1e-6);
    YR_EXPECT_NEAR(transform.local_to_parent[12], 0.0, 1e-6);
}

YR_TEST(asset_resource_defaults_are_empty_static_scene_data) {
    const yr::AssetResource resource;
    const yr::AssetPrimitive primitive;

    YR_EXPECT_TRUE(resource.scenes.empty());
    YR_EXPECT_EQ(resource.default_scene, 0);
    YR_EXPECT_TRUE(resource.nodes.empty());
    YR_EXPECT_TRUE(resource.meshes.empty());
    YR_EXPECT_TRUE(resource.materials.empty());
    YR_EXPECT_TRUE(resource.textures.empty());
    YR_EXPECT_TRUE(resource.images.empty());
    YR_EXPECT_TRUE(resource.samplers.empty());
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_TRUE(primitive.positions.empty());
    YR_EXPECT_TRUE(primitive.normals.empty());
    YR_EXPECT_TRUE(primitive.texcoords0.empty());
    YR_EXPECT_TRUE(primitive.indices.empty());
    YR_EXPECT_EQ(primitive.material, -1);
}
```

- [ ] **Step 2: Rename loader calls in tests to the new API**

In `tests/assets_tests.cpp`, replace calls:

```cpp
yr::LoadObjMesh(...)
yr::LoadGltfMesh(...)
```

with:

```cpp
yr::LoadObjResource(...)
yr::LoadGltfResource(...)
```

Replace result access:

```cpp
result.mesh
```

with:

```cpp
result.resource
```

- [ ] **Step 3: Add test helpers for resource assertions**

Add these helpers inside the anonymous namespace:

```cpp
const yr::AssetResource& ResourceValue(const yr::AssetLoadResult& result) {
    if (!result.resource.has_value()) {
        throw std::runtime_error("expected AssetLoadResult::resource to contain a value");
    }
    return result.resource.value();
}

const yr::AssetPrimitive& FirstPrimitive(const yr::AssetResource& resource) {
    if (resource.meshes.empty() || resource.meshes[0].primitives.empty()) {
        throw std::runtime_error("expected first mesh primitive");
    }
    return resource.meshes[0].primitives[0];
}
```

Add required include:

```cpp
#include <stdexcept>
```

- [ ] **Step 4: Rewrite the first OBJ shape test to assert resource structure**

Replace `obj_loader_loads_triangle_obj` with:

```cpp
YR_TEST(obj_loader_loads_triangle_obj_as_asset_resource) {
    const yr::AssetLoadResult result = yr::LoadObjResource(FixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetResource& resource = ResourceValue(result);
    YR_EXPECT_EQ(resource.scenes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.default_scene, 0);
    YR_EXPECT_EQ(resource.scenes[0].root_nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes[0].mesh, 0);

    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
    YR_EXPECT_NEAR(primitive.positions[0].x, -0.5, 1e-6);
    YR_EXPECT_NEAR(primitive.positions[1].x, 0.5, 1e-6);
    YR_EXPECT_NEAR(primitive.positions[2].y, 1.0, 1e-6);
}
```

- [ ] **Step 5: Rewrite OBJ UV, normal, material, and error expectations**

Update the existing OBJ tests with these exact expectations:

```cpp
YR_EXPECT_EQ(FirstPrimitive(ResourceValue(result)).positions.size(), std::size_t{6});
YR_EXPECT_EQ(FirstPrimitive(ResourceValue(result)).indices.size(), std::size_t{6});
YR_EXPECT_TRUE(!result.resource.has_value());
YR_EXPECT_TRUE(FirstPrimitive(ResourceValue(result)).texcoords0.size() == FirstPrimitive(ResourceValue(result)).positions.size());
YR_EXPECT_TRUE(FirstPrimitive(ResourceValue(result)).normals.size() == FirstPrimitive(ResourceValue(result)).positions.size());
YR_EXPECT_EQ(ResourceValue(result).materials.size(), std::size_t{1});
YR_EXPECT_EQ(ResourceValue(result).materials[0].name, "checker");
YR_EXPECT_NEAR(ResourceValue(result).materials[0].base_color.x, 0.25, 1e-6);
YR_EXPECT_TRUE(ResourceValue(result).materials[0].base_color_texture >= 0);
YR_EXPECT_EQ(ResourceValue(result).textures.size(), std::size_t{1});
YR_EXPECT_EQ(ResourceValue(result).images.size(), std::size_t{1});
YR_EXPECT_TRUE(ResourceValue(result).images[0].path.generic_string().find("checker_2x2.png") != std::string::npos);
YR_EXPECT_EQ(FirstPrimitive(ResourceValue(result)).material, 0);
```

These snippets replace the old `mesh->triangles` and `mesh->materials` assertions in the matching tests. Keep the existing test names where possible, but update names that mention `ImportedTriangle`.

- [ ] **Step 6: Rewrite glTF loader expectations**

Update the glTF tests to assert:

```cpp
const yr::AssetResource& resource = ResourceValue(result);
YR_EXPECT_TRUE(result.errors.empty());
YR_EXPECT_TRUE(!resource.scenes.empty());
YR_EXPECT_TRUE(!resource.nodes.empty());
YR_EXPECT_TRUE(!resource.meshes.empty());
YR_EXPECT_EQ(FirstPrimitive(resource).topology, yr::AssetPrimitiveTopology::Triangles);
YR_EXPECT_EQ(FirstPrimitive(resource).indices.size(), std::size_t{3});
```

For the texture fixture, assert:

```cpp
YR_EXPECT_TRUE(!resource.materials.empty());
YR_EXPECT_TRUE(resource.materials[0].base_color_texture >= 0);
const yr::AssetTexture& texture = resource.textures[static_cast<std::size_t>(resource.materials[0].base_color_texture)];
YR_EXPECT_TRUE(texture.image >= 0);
YR_EXPECT_TRUE(resource.images[static_cast<std::size_t>(texture.image)].path.generic_string().find("testTexture.png") != std::string::npos);
YR_EXPECT_TRUE(texture.sampler >= 0);
YR_EXPECT_EQ(resource.samplers[static_cast<std::size_t>(texture.sampler)].wrap_s, yr::TextureWrap::MirroredRepeat);
YR_EXPECT_EQ(resource.samplers[static_cast<std::size_t>(texture.sampler)].wrap_t, yr::TextureWrap::MirroredRepeat);
```

For the bad-wrap fixture, assert:

```cpp
YR_EXPECT_TRUE(!result.warnings.empty());
YR_EXPECT_TRUE(result.warnings[0].find("unsupported glTF texture wrap") != std::string::npos);
const yr::AssetTexture& texture = resource.textures[static_cast<std::size_t>(resource.materials[0].base_color_texture)];
YR_EXPECT_TRUE(texture.sampler >= 0);
YR_EXPECT_EQ(resource.samplers[static_cast<std::size_t>(texture.sampler)].wrap_s, yr::TextureWrap::Repeat);
YR_EXPECT_EQ(resource.samplers[static_cast<std::size_t>(texture.sampler)].wrap_t, yr::TextureWrap::Repeat);
```

- [ ] **Step 7: Run tests to verify RED**

Run:

```bash
cmake --build build --config Debug
```

Expected: compile fails because `asset_resource.hpp`, `AssetResource`, `LoadObjResource()`, `LoadGltfResource()`, and `AssetLoadResult::resource` do not exist.

- [ ] **Step 8: Commit failing tests**

Do not commit this task alone if the project does not compile. Keep the changes staged or uncommitted until Task 2 makes them compile.

---

### Task 2: Introduce AssetResource API And Make Tests Compile

**Files:**
- Create: `include/yaoray/assets/asset_resource.hpp`
- Delete: `include/yaoray/assets/imported_asset.hpp`
- Modify: `include/yaoray/assets/obj_loader.hpp`
- Modify: `include/yaoray/assets/gltf_loader.hpp`

- [ ] **Step 1: Create `asset_resource.hpp`**

Create `include/yaoray/assets/asset_resource.hpp`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

enum class AssetPrimitiveTopology {
    Triangles,
};

struct AssetScene {
    std::string name;
    std::vector<int> root_nodes;
};

struct AssetTransform {
    std::array<float, 16> local_to_parent{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

struct AssetNode {
    std::string name;
    AssetTransform transform;
    int mesh = -1;
    std::vector<int> children;
};

struct AssetPrimitive {
    AssetPrimitiveTopology topology = AssetPrimitiveTopology::Triangles;
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> texcoords0;
    std::vector<std::uint32_t> indices;
    int material = -1;
};

struct AssetMesh {
    std::string name;
    std::vector<AssetPrimitive> primitives;
};

struct AssetMaterial {
    std::string name;
    MaterialKind approximate_type = MaterialKind::Diffuse;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int base_color_texture = -1;
};

struct AssetTexture {
    int image = -1;
    int sampler = -1;
};

struct AssetImage {
    std::filesystem::path path;
};

struct AssetSampler {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};

struct AssetResource {
    std::vector<AssetScene> scenes;
    int default_scene = 0;
    std::vector<AssetNode> nodes;
    std::vector<AssetMesh> meshes;
    std::vector<AssetMaterial> materials;
    std::vector<AssetTexture> textures;
    std::vector<AssetImage> images;
    std::vector<AssetSampler> samplers;
};

struct AssetLoadResult {
    std::optional<AssetResource> resource;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

} // namespace yr
```

- [ ] **Step 2: Update loader headers**

Replace `include/yaoray/assets/obj_loader.hpp` with:

```cpp
#pragma once

#include <filesystem>

#include <yaoray/assets/asset_resource.hpp>

namespace yr {

AssetLoadResult LoadObjResource(const std::filesystem::path& path);

} // namespace yr
```

Replace `include/yaoray/assets/gltf_loader.hpp` with:

```cpp
#pragma once

#include <filesystem>

#include <yaoray/assets/asset_resource.hpp>

namespace yr {

AssetLoadResult LoadGltfResource(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 3: Delete old imported asset header**

Delete:

```text
include/yaoray/assets/imported_asset.hpp
```

- [ ] **Step 4: Add temporary compile stubs**

In `src/assets/obj_loader.cpp`, rename the public function signature only:

```cpp
AssetLoadResult LoadObjResource(const std::filesystem::path& path) {
```

In `src/assets/gltf_loader.cpp`, rename the public function signature only:

```cpp
AssetLoadResult LoadGltfResource(const std::filesystem::path& path) {
```

The bodies still refer to old imported types and will not compile yet. This is expected until Tasks 3 and 4.

- [ ] **Step 5: Verify expected compile failure moved forward**

Run:

```bash
cmake --build build --config Debug
```

Expected: compile now finds `AssetResource` and new loader names, but fails in `src/assets/obj_loader.cpp`, `src/assets/gltf_loader.cpp`, and `src/render/scene_compiler.cpp` because old imported types no longer exist.

---

### Task 3: Rewrite OBJ Loader To Produce AssetResource

**Files:**
- Modify: `src/assets/obj_loader.cpp`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Replace old imported material/triangle construction helpers**

Add this include to `src/assets/obj_loader.cpp`:

```cpp
#include <cstdint>
```

In `src/assets/obj_loader.cpp`, add these local structs and helpers inside the anonymous namespace:

```cpp
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
    imported.roughness = 0.0f;
    imported.specular = 0.04f;
    if (!material.diffuse_texname.empty()) {
        imported.base_color_texture = AddObjTexture(resource, asset_dir / material.diffuse_texname);
    }
    return imported;
}
```

- [ ] **Step 2: Initialize the OBJ resource shape**

At the start of successful parsing in `LoadObjResource()`, replace `ImportedMesh mesh;` with:

```cpp
AssetResource resource;
resource.scenes.push_back(AssetScene{"default", {0}});
AssetNode root;
root.name = path.stem().string();
root.mesh = 0;
resource.nodes.push_back(std::move(root));
resource.meshes.push_back(AssetMesh{path.stem().string(), {}});
AssetMesh& mesh = resource.meshes[0];
```

- [ ] **Step 3: Convert OBJ materials into resource materials**

Replace the old `ImportedMaterial` loop body with:

```cpp
AssetMaterial imported = ConvertObjMaterial(material, path.parent_path(), resource);
material_names.emplace(imported.name, static_cast<int>(resource.materials.size()));
resource.materials.push_back(std::move(imported));
```

- [ ] **Step 4: Append triangles into material-grouped primitives**

Before the shape loop, add:

```cpp
std::unordered_map<PrimitiveKey, std::size_t, PrimitiveKeyHash> primitive_indices;
```

Replace old `ImportedTriangle imported; ... mesh.triangles.push_back(imported);` code with:

```cpp
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
```

- [ ] **Step 5: Update empty mesh check and result assignment**

Replace:

```cpp
if (mesh.triangles.empty()) {
    result.errors.push_back("OBJ mesh contains no triangles: " + path.generic_string());
    return result;
}

result.mesh = std::move(mesh);
```

with:

```cpp
if (mesh.primitives.empty()) {
    result.errors.push_back("OBJ mesh contains no triangles: " + path.generic_string());
    return result;
}

result.resource = std::move(resource);
```

- [ ] **Step 6: Verify OBJ loader tests**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: project may still fail to build because glTF loader and compiler still use old imported types. If it builds far enough, OBJ resource tests should pass and remaining failures should reference glTF/compiler migration.

- [ ] **Step 7: Commit only if the project compiles**

If the project compiles and `yaoray_tests` passes, commit:

```bash
git add include/yaoray/assets/asset_resource.hpp include/yaoray/assets/obj_loader.hpp src/assets/obj_loader.cpp tests/assets_tests.cpp
git commit -m "refactor: load obj assets as resources"
```

If it does not compile because glTF/compiler still need migration, defer the commit until Task 5.

---

### Task 4: Rewrite glTF Loader To Preserve AssetResource Structure

**Files:**
- Modify: `src/assets/gltf_loader.cpp`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Keep `Mat4` as the loader-local transform representation**

Keep the existing `Mat4`, `Multiply()`, `TranslationMatrix()`, `ScaleMatrix()`, `RotationMatrix()`, and `NodeLocalTransform()` helpers. Remove loader-local `TransformPoint()` and `TransformVector()` after primitive baking is removed.

Add:

```cpp
AssetTransform ToAssetTransform(Mat4 value) {
    AssetTransform transform;
    transform.local_to_parent = value.m;
    return transform;
}
```

- [ ] **Step 2: Convert glTF samplers, images, and textures**

Add these helpers:

```cpp
int AddGltfSampler(const tinygltf::Sampler& sampler, AssetLoadResult& result, AssetResource& resource) {
    AssetSampler imported;
    imported.wrap_s = ConvertTextureWrap(sampler.wrapS, "wrapS", result);
    imported.wrap_t = ConvertTextureWrap(sampler.wrapT, "wrapT", result);
    const int index = static_cast<int>(resource.samplers.size());
    resource.samplers.push_back(imported);
    return index;
}

void CopyGltfImagesAndSamplers(
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
        AssetTexture imported;
        imported.image = texture.source;
        imported.sampler = texture.sampler;
        resource.textures.push_back(imported);
    }
}
```

- [ ] **Step 3: Replace `ImportedMaterial ConvertMaterial()`**

Replace it with:

```cpp
AssetMaterial ConvertMaterial(const tinygltf::Model& model, const tinygltf::Material& material) {
    AssetMaterial imported;
    imported.name = material.name;
    const tinygltf::PbrMetallicRoughness& pbr = material.pbrMetallicRoughness;
    imported.base_color = Color3f{
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
    imported.metallic = static_cast<float>(pbr.metallicFactor);
    if (imported.metallic >= 0.5f) {
        imported.approximate_type = MaterialKind::Metal;
    } else if (imported.roughness < 0.35f) {
        imported.approximate_type = MaterialKind::Plastic;
        imported.specular = 0.04f;
    } else {
        imported.approximate_type = MaterialKind::Diffuse;
    }

    if (pbr.baseColorTexture.index >= 0 &&
        static_cast<std::size_t>(pbr.baseColorTexture.index) < model.textures.size()) {
        const tinygltf::Texture& texture = model.textures[static_cast<std::size_t>(pbr.baseColorTexture.index)];
        if (texture.source >= 0 && static_cast<std::size_t>(texture.source) < model.images.size()) {
            const tinygltf::Image& image = model.images[static_cast<std::size_t>(texture.source)];
            if (!image.uri.empty()) {
                imported.base_color_texture = pbr.baseColorTexture.index;
            }
        }
    }
    return imported;
}
```

- [ ] **Step 4: Replace triangle append with primitive import**

Replace `AppendPrimitiveTriangles()` with:

```cpp
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
        if (normal_accessor.has_value()) {
            for (std::size_t vertex = 0; vertex < (**normal_accessor).count; ++vertex) {
                const std::optional<Point3f> normal = ReadVec3AccessorValue(model, **normal_accessor, vertex);
                if (!normal.has_value()) {
                    result.errors.push_back("glTF primitive has invalid NORMAL data");
                    return false;
                }
                imported.normals.push_back(Normalize(Vec3f{normal->x, normal->y, normal->z}));
            }
        }
    }

    if (const auto found = primitive.attributes.find("TEXCOORD_0"); found != primitive.attributes.end()) {
        const std::optional<const tinygltf::Accessor*> uv_accessor = GetAccessor(model, found->second);
        if (uv_accessor.has_value()) {
            for (std::size_t vertex = 0; vertex < (**uv_accessor).count; ++vertex) {
                const std::optional<Vec2f> uv = ReadVec2AccessorValue(model, **uv_accessor, vertex);
                if (!uv.has_value()) {
                    result.errors.push_back("glTF primitive has invalid TEXCOORD_0 data");
                    return false;
                }
                imported.texcoords0.push_back(*uv);
            }
        }
    }

    std::size_t index_count = imported.positions.size();
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
            imported.indices.push_back(*value);
        }
    } else {
        for (std::uint32_t index = 0; index < static_cast<std::uint32_t>(index_count); ++index) {
            imported.indices.push_back(index);
        }
    }

    if (imported.indices.size() % 3 != 0) {
        result.errors.push_back("glTF triangle primitive vertex count is not divisible by three");
        return false;
    }

    mesh.primitives.push_back(std::move(imported));
    return true;
}
```

- [ ] **Step 5: Build scenes, nodes, and meshes in `LoadGltfResource()`**

Replace the old world-space traversal with resource construction:

```cpp
AssetResource resource;
const std::filesystem::path asset_dir = path.parent_path();
CopyGltfImagesAndSamplers(model, asset_dir, result, resource);

resource.materials.reserve(model.materials.size());
for (const tinygltf::Material& material : model.materials) {
    resource.materials.push_back(ConvertMaterial(model, material));
}

for (const tinygltf::Mesh& gltf_mesh : model.meshes) {
    AssetMesh mesh;
    mesh.name = gltf_mesh.name;
    for (const tinygltf::Primitive& primitive : gltf_mesh.primitives) {
        if (!AppendPrimitiveResource(model, primitive, mesh, result)) {
            return result;
        }
    }
    resource.meshes.push_back(std::move(mesh));
}

for (const tinygltf::Node& gltf_node : model.nodes) {
    const std::optional<Mat4> local_transform = NodeLocalTransform(gltf_node);
    if (!local_transform.has_value()) {
        result.errors.push_back("glTF node matrix must contain 16 values");
        return result;
    }
    AssetNode node;
    node.name = gltf_node.name;
    node.transform = ToAssetTransform(*local_transform);
    node.mesh = gltf_node.mesh;
    node.children = gltf_node.children;
    resource.nodes.push_back(std::move(node));
}

for (const tinygltf::Scene& gltf_scene : model.scenes) {
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
if (resource.meshes.empty()) {
    result.errors.push_back("glTF file contains no supported triangle meshes: " + path.generic_string());
    return result;
}

result.resource = std::move(resource);
return result;
```

- [ ] **Step 6: Verify asset tests**

Before running tests, add these assertions to `gltf_loader_loads_indexed_triangle` after `const yr::AssetResource& resource = ResourceValue(result);`:

```cpp
YR_EXPECT_EQ(resource.default_scene, 0);
YR_EXPECT_EQ(resource.scenes.size(), std::size_t{1});
YR_EXPECT_EQ(resource.scenes[0].root_nodes.size(), std::size_t{1});
YR_EXPECT_EQ(resource.nodes.size(), std::size_t{1});
YR_EXPECT_EQ(resource.nodes[0].mesh, 0);
YR_EXPECT_NEAR(resource.nodes[0].transform.local_to_parent[0], 1.0, 1e-6);
YR_EXPECT_NEAR(resource.nodes[0].transform.local_to_parent[5], 1.0, 1e-6);
YR_EXPECT_NEAR(resource.nodes[0].transform.local_to_parent[10], 1.0, 1e-6);
YR_EXPECT_NEAR(resource.nodes[0].transform.local_to_parent[15], 1.0, 1e-6);
```

These assertions lock the loader contract that glTF scene and node structure survives asset loading instead of being flattened into triangles.

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build may still fail in `scene_compiler.cpp` because compiler still uses old imported types. Asset loader compile errors should be resolved before moving on.

---

### Task 5: Compile AssetResource Into RenderSceneIR

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Replace imported asset includes and cache type**

In `src/render/scene_compiler.cpp`, keep:

```cpp
#include <yaoray/assets/gltf_loader.hpp>
#include <yaoray/assets/obj_loader.hpp>
```

Replace:

```cpp
std::unordered_map<std::string, ImportedMesh> mesh_cache;
```

with:

```cpp
std::unordered_map<std::string, AssetResource> asset_cache;
```

- [ ] **Step 2: Add matrix helpers to `scene_compiler.cpp`**

Add these includes to `src/render/scene_compiler.cpp`:

```cpp
#include <array>
#include <cstdint>
```

Add these helpers inside the anonymous namespace:

```cpp
struct Mat4 {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

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

Mat4 FromAssetTransform(const AssetTransform& transform) {
    Mat4 result;
    result.m = transform.local_to_parent;
    return result;
}
```

- [ ] **Step 3: Convert `TransformDescription` to matrix**

Add:

```cpp
Mat4 TranslationMatrix(Vec3f translation) {
    Mat4 result;
    result.m[12] = translation.x;
    result.m[13] = translation.y;
    result.m[14] = translation.z;
    return result;
}

Mat4 ScaleMatrix(Vec3f scale) {
    Mat4 result;
    result.m[0] = scale.x;
    result.m[5] = scale.y;
    result.m[10] = scale.z;
    return result;
}

Mat4 RotationXMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    return result;
}

Mat4 RotationYMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 RotationZMatrix(float radians) {
    Mat4 result;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    result.m[0] = c;
    result.m[1] = s;
    result.m[4] = -s;
    result.m[5] = c;
    return result;
}

Mat4 InstanceTransformMatrix(const TransformDescription& transform) {
    return Multiply(
        TranslationMatrix(transform.translate),
        Multiply(
            RotationZMatrix(DegreesToRadians(transform.rotate_degrees.z)),
            Multiply(
                RotationYMatrix(DegreesToRadians(transform.rotate_degrees.y)),
                Multiply(
                    RotationXMatrix(DegreesToRadians(transform.rotate_degrees.x)),
                    ScaleMatrix(transform.scale)
                )
            )
        )
    );
}
```

- [ ] **Step 4: Compile asset materials**

Replace `CompileImportedMaterials()` with:

```cpp
std::vector<int> CompileAssetMaterials(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetResource& resource,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::vector<int> material_indices;
    material_indices.reserve(resource.materials.size());

    for (const AssetMaterial& material : resource.materials) {
        RenderMaterial render_material;
        render_material.type = material.approximate_type;
        render_material.albedo = material.base_color;
        render_material.emission = material.emission;
        render_material.roughness = material.roughness;
        render_material.specular = material.specular;
        if (material.base_color_texture >= 0) {
            if (static_cast<std::size_t>(material.base_color_texture) >= resource.textures.size()) {
                diagnostics.push_back(Error(scene, "assets.path", "asset material references an invalid texture"));
                material_indices.push_back(-1);
                continue;
            }
            const AssetTexture& texture = resource.textures[static_cast<std::size_t>(material.base_color_texture)];
            if (texture.image < 0 || static_cast<std::size_t>(texture.image) >= resource.images.size()) {
                diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid image"));
                material_indices.push_back(-1);
                continue;
            }
            TextureWrap wrap_s = TextureWrap::Repeat;
            TextureWrap wrap_t = TextureWrap::Repeat;
            if (texture.sampler >= 0) {
                if (static_cast<std::size_t>(texture.sampler) >= resource.samplers.size()) {
                    diagnostics.push_back(Error(scene, "assets.path", "asset texture references an invalid sampler"));
                    material_indices.push_back(-1);
                    continue;
                }
                const AssetSampler& sampler = resource.samplers[static_cast<std::size_t>(texture.sampler)];
                wrap_s = sampler.wrap_s;
                wrap_t = sampler.wrap_t;
            }
            const std::optional<int> texture_index = LoadTextureIndex(
                scene,
                compiled,
                resource.images[static_cast<std::size_t>(texture.image)].path,
                wrap_s,
                wrap_t,
                texture_cache,
                diagnostics
            );
            if (!texture_index.has_value()) {
                material_indices.push_back(-1);
                continue;
            }
            render_material.albedo_texture = *texture_index;
        }
        const int render_material_index = static_cast<int>(compiled.materials.size());
        compiled.materials.push_back(render_material);
        material_indices.push_back(render_material_index);
    }
    return material_indices;
}
```

- [ ] **Step 5: Expand asset primitives**

Replace `AppendImportedMesh()` with:

```cpp
void AppendAssetPrimitive(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetPrimitive& primitive,
    Mat4 transform,
    std::optional<int> override_material_index,
    const std::vector<int>& asset_material_indices,
    int& fallback_material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (primitive.topology != AssetPrimitiveTopology::Triangles) {
        diagnostics.push_back(Error(scene, "assets.path", "unsupported asset primitive topology"));
        return;
    }
    if (primitive.indices.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "assets.path", "asset primitive index count is not divisible by three"));
        return;
    }

    for (std::size_t i = 0; i < primitive.indices.size(); i += 3) {
        const std::uint32_t i0 = primitive.indices[i + 0];
        const std::uint32_t i1 = primitive.indices[i + 1];
        const std::uint32_t i2 = primitive.indices[i + 2];
        if (i0 >= primitive.positions.size() || i1 >= primitive.positions.size() || i2 >= primitive.positions.size()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset primitive index references an invalid position"));
            return;
        }

        int material_index = override_material_index.value_or(-1);
        if (!override_material_index.has_value() &&
            primitive.material >= 0 &&
            static_cast<std::size_t>(primitive.material) < asset_material_indices.size()) {
            material_index = asset_material_indices[static_cast<std::size_t>(primitive.material)];
        }
        if (material_index < 0) {
            if (fallback_material_index < 0) {
                fallback_material_index = AddDefaultMaterial(compiled);
            }
            material_index = fallback_material_index;
        }

        const Point3f p0 = TransformPoint(transform, primitive.positions[i0]);
        const Point3f p1 = TransformPoint(transform, primitive.positions[i1]);
        const Point3f p2 = TransformPoint(transform, primitive.positions[i2]);
        RenderTriangle render_triangle;
        render_triangle.p0 = p0;
        render_triangle.p1 = p1;
        render_triangle.p2 = p2;
        render_triangle.normal = Normalize(Cross(p1 - p0, p2 - p0));
        render_triangle.material_index = material_index;

        if (primitive.texcoords0.size() == primitive.positions.size()) {
            render_triangle.uv0 = primitive.texcoords0[i0];
            render_triangle.uv1 = primitive.texcoords0[i1];
            render_triangle.uv2 = primitive.texcoords0[i2];
            render_triangle.has_uv = true;
        }
        if (primitive.normals.size() == primitive.positions.size()) {
            render_triangle.n0 = Normalize(TransformVector(transform, primitive.normals[i0]));
            render_triangle.n1 = Normalize(TransformVector(transform, primitive.normals[i1]));
            render_triangle.n2 = Normalize(TransformVector(transform, primitive.normals[i2]));
            render_triangle.has_vertex_normals =
                LengthSquared(render_triangle.n0) > 0.0f &&
                LengthSquared(render_triangle.n1) > 0.0f &&
                LengthSquared(render_triangle.n2) > 0.0f;
        }
        compiled.triangles.push_back(render_triangle);
    }
}
```

- [ ] **Step 6: Traverse asset nodes**

Add:

```cpp
void AppendAssetNode(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const AssetResource& resource,
    int node_index,
    Mat4 parent_transform,
    std::optional<int> override_material_index,
    const std::vector<int>& asset_material_indices,
    int& fallback_material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (node_index < 0 || static_cast<std::size_t>(node_index) >= resource.nodes.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset scene references an invalid node"));
        return;
    }
    const AssetNode& node = resource.nodes[static_cast<std::size_t>(node_index)];
    const Mat4 node_transform = Multiply(parent_transform, FromAssetTransform(node.transform));
    if (node.mesh >= 0) {
        if (static_cast<std::size_t>(node.mesh) >= resource.meshes.size()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset node references an invalid mesh"));
            return;
        }
        const AssetMesh& mesh = resource.meshes[static_cast<std::size_t>(node.mesh)];
        for (const AssetPrimitive& primitive : mesh.primitives) {
            AppendAssetPrimitive(
                scene,
                compiled,
                primitive,
                node_transform,
                override_material_index,
                asset_material_indices,
                fallback_material_index,
                diagnostics
            );
            if (HasSceneErrors(diagnostics)) {
                return;
            }
        }
    }
    for (int child : node.children) {
        AppendAssetNode(
            scene,
            compiled,
            resource,
            child,
            node_transform,
            override_material_index,
            asset_material_indices,
            fallback_material_index,
            diagnostics
        );
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }
}
```

- [ ] **Step 7: Replace OBJ/glTF append paths with one resource path**

Replace `AppendObjAsset()` and `AppendGltfAsset()` with:

```cpp
enum class AssetFileKind {
    Obj,
    Gltf,
};

void AppendImportedAssetResource(
    const SceneDescription& scene,
    RenderSceneIR& compiled,
    const std::filesystem::path& asset_path,
    AssetFileKind kind,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    std::unordered_map<std::string, AssetResource>& asset_cache,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string cache_key = asset_path.generic_string();
    auto cached = asset_cache.find(cache_key);
    if (cached == asset_cache.end()) {
        AssetLoadResult load_result = kind == AssetFileKind::Obj
            ? LoadObjResource(asset_path)
            : LoadGltfResource(asset_path);
        for (const std::string& warning : load_result.warnings) {
            diagnostics.push_back(Warning(scene, "assets.path", warning));
        }
        for (const std::string& error : load_result.errors) {
            diagnostics.push_back(Error(scene, "assets.path", error));
        }
        if (!load_result.errors.empty()) {
            return;
        }
        if (!load_result.resource.has_value()) {
            diagnostics.push_back(Error(scene, "assets.path", "asset loader returned no resource: " + cache_key));
            return;
        }
        cached = asset_cache.emplace(cache_key, std::move(load_result.resource.value())).first;
    }

    const AssetResource& resource = cached->second;
    if (resource.default_scene < 0 || static_cast<std::size_t>(resource.default_scene) >= resource.scenes.size()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset resource default scene is invalid"));
        return;
    }

    std::vector<int> asset_material_indices;
    if (!override_material_index.has_value()) {
        asset_material_indices = CompileAssetMaterials(scene, compiled, resource, texture_cache, diagnostics);
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }

    int fallback_material_index = -1;
    const Mat4 instance_transform = InstanceTransformMatrix(transform);
    const AssetScene& asset_scene = resource.scenes[static_cast<std::size_t>(resource.default_scene)];
    for (int root_node : asset_scene.root_nodes) {
        AppendAssetNode(
            scene,
            compiled,
            resource,
            root_node,
            instance_transform,
            override_material_index,
            asset_material_indices,
            fallback_material_index,
            diagnostics
        );
        if (HasSceneErrors(diagnostics)) {
            return;
        }
    }
}
```

- [ ] **Step 8: Update main compile loop dispatch**

Replace calls to `AppendObjAsset()` and `AppendGltfAsset()` with:

```cpp
AppendImportedAssetResource(
    scene,
    compiled,
    asset_path,
    AssetFileKind::Obj,
    instance.transform,
    material_index,
    asset_cache,
    texture_cache,
    result.diagnostics
);
```

and:

```cpp
AppendImportedAssetResource(
    scene,
    compiled,
    asset_path,
    AssetFileKind::Gltf,
    instance.transform,
    material_index,
    asset_cache,
    texture_cache,
    result.diagnostics
);
```

- [ ] **Step 9: Add compiler tests for node and instance transform composition**

In `tests/render_scene_tests.cpp`, add:

```cpp
YR_TEST(scene_compiler_composes_asset_resource_instance_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"tri", FixturePath("assets/gltf/Triangle/glTF/Triangle.gltf")});
    yr::InstanceDescription instance{"tri", {}};
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->triangles.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->triangles[0].p0.x > 0.0f);
    YR_EXPECT_TRUE(result.scene->triangles[0].p0.y > 1.0f);
}
```

This test verifies the compiler uses the new matrix-based asset resource traversal path for instance transforms. Task 4 verifies that glTF scene and node records survive asset loading.

- [ ] **Step 10: Verify compiler and render tests**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 11: Commit asset resource compiler migration**

Run:

```bash
git add include/yaoray/assets src/assets src/render/scene_compiler.cpp tests/assets_tests.cpp tests/render_scene_tests.cpp
git commit -m "refactor: compile asset resources into render ir"
```

---

### Task 6: Remove Old Imported Asset API References

**Files:**
- Delete: `include/yaoray/assets/imported_asset.hpp`
- Modify: any remaining includes or references found by search

- [ ] **Step 1: Search for old API names**

Run:

```bash
rg -n "ImportedMesh|ImportedTriangle|ImportedMaterial|LoadObjMesh|LoadGltfMesh|imported_asset" include src tests
```

Expected: no matches. If any match remains, stop this task and remove that remaining old API dependency before continuing.

- [ ] **Step 2: Verify public asset headers**

Run:

```bash
sed -n '1,80p' include/yaoray/assets/obj_loader.hpp
sed -n '1,80p' include/yaoray/assets/gltf_loader.hpp
sed -n '1,220p' include/yaoray/assets/asset_resource.hpp
```

Expected: loader headers include `asset_resource.hpp`, and only resource-named loader APIs are exposed.

- [ ] **Step 3: Run full verification**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and all CTest tests pass.

- [ ] **Step 4: Commit cleanup if any files changed**

Run:

```bash
git status --short
git add include src tests
git commit -m "refactor: remove imported mesh asset api"
```

If `git status --short` is empty, skip the commit.

---

### Task 7: Update Documentation And Phase Status

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-aggressive-replacement-design.md`

- [ ] **Step 1: Update README implemented slices**

In `README.md`, replace any wording that says OBJ or glTF import produces flat imported meshes with:

```markdown
- shared `AssetResource` import for OBJ and static glTF/GLB assets through the `yaoray_assets` module
```

Keep existing notes about unsupported full glTF PBR and advanced maps.

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, replace the OBJ/glTF paragraphs with:

```markdown
The asset layer imports OBJ and static glTF/GLB files into `AssetResource`. `AssetResource` preserves static asset scenes, nodes, local transforms, meshes, triangle primitives, material slots, images, textures, and sampler wrap state. It does not expose tinyobjloader or tinygltf types to the render layer.

The render compiler consumes `AssetResource` from the default asset scene, traverses root nodes recursively, composes TOML instance transforms with asset node transforms, maps asset materials and base-color textures to current render materials and render textures, and expands supported triangle primitives into `RenderSceneIR`. Scene-authored `instance.material` remains a whole-instance override.
```

Then keep the existing limitations paragraph, but update it to say glTF animation, skinning, morph targets, cameras, lights, alpha, normal maps, and exact PBR remain future work.

- [ ] **Step 3: Append implementation status to the Phase 2 spec**

At the end of `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-aggressive-replacement-design.md`, add:

```markdown
## Implementation Status

Implemented in Phase 2. OBJ and glTF loaders now return `AssetResource`, the render compiler traverses asset resources directly, and the old `ImportedMesh` asset API has been removed.
```

- [ ] **Step 4: Verify docs and tests**

Run:

```bash
git diff --check
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: no whitespace errors, build succeeds, and all CTest tests pass.

- [ ] **Step 5: Commit docs**

Run:

```bash
git add README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-22-yaoray-asset-resource-aggressive-replacement-design.md
git commit -m "docs: document asset resource layer"
```

---

## Final Verification

- [ ] **Step 1: Run full test suite**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and all CTest tests pass.

- [ ] **Step 2: Run CLI smoke manually**

Run:

```bash
./build/yaoray render tests/fixtures/scene/builtin_triangle.toml --backend cpu
./build/yaoray render scenes/examples/gltf_textured_asset.toml --backend cpu
```

Expected: both commands exit 0, print `Scene compiled successfully.`, print `Rendered image:`, and write PNG outputs.

- [ ] **Step 3: Verify old API is gone**

Run:

```bash
rg -n "ImportedMesh|ImportedTriangle|ImportedMaterial|LoadObjMesh|LoadGltfMesh|imported_asset" include src tests
```

Expected: no matches.

- [ ] **Step 4: Verify working tree**

Run:

```bash
git status --short
```

Expected: clean working tree.

If all checks pass, use `superpowers:finishing-a-development-branch` to decide whether to merge, push, or keep the branch.
