# YaoRay glTF Large Asset Compatibility v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make YaoRay load and compile a practical glTF PBR asset subset for CPU offline rendering, with FlightHelmet committed as the in-repo validation asset and Bistro documented as a local large-scene benchmark.

**Architecture:** Preserve the existing four-layer split. The asset layer stores source glTF semantics without renderer policy. The render compiler converts asset semantics into backend-neutral render IR, including texture color spaces, tangents, and alpha policy. The CPU backend resolves material texture samples, normal maps, and alpha mask visibility. CUDA and OptiX remain unimplemented consumers of the same render IR.

**Tech Stack:** C++17, CMake/CTest, tinygltf, stb_image, CPU path tracer/debug renderer, existing asset/render/backend test suites.

---

## Scope

- [ ] Support external-file glTF image assets used by Khronos FlightHelmet.
- [ ] Preserve core glTF PBR material fields: base color RGBA, metallic/roughness factors and texture, normal texture scale, occlusion texture strength, emissive factor and texture, alpha mode/cutoff, and double-sided flag.
- [ ] Preserve and compile glTF tangents when present.
- [ ] Generate tangents from positions, normals, and UVs when tangents are missing.
- [ ] Add RGBA texture storage and per-texture color-space loading.
- [ ] Implement CPU material sample resolution for albedo alpha, metallic-roughness, emissive texture, and normal maps.
- [ ] Implement alpha mask visibility for primary, indirect, and shadow rays.
- [ ] Add FlightHelmet to `scenes/examples/assets/gltf/FlightHelmet/`.
- [ ] Document Bistro as a local-only benchmark asset; do not commit Bistro model files.

## Out Of Scope

- [ ] Embedded image buffers and `data:` image URIs.
- [ ] Alpha blending/transmission sorting for `BLEND`; the loader records a warning and render compiler treats it as opaque for this slice.
- [ ] Occlusion texture shading effect.
- [ ] CUDA/OptiX implementation.
- [ ] Default test suite rendering of Bistro or full-sized production frames.

---

## Task 1: Add RGBA Textures And Color-Space Loading

### Files

- `include/yaoray/core/vec.hpp`
- `include/yaoray/render/texture.hpp`
- `src/render/texture.cpp`
- `tests/texture_tests.cpp`

### Tests First

- [ ] Update existing programmatic texture tests in `tests/texture_tests.cpp` so existing `Color3f` initializer usage still compiles through a `Color4f` conversion constructor.
- [ ] Add `texture_sample_texture4_preserves_alpha`:

```cpp
TEST(TextureTests, texture_sample_texture4_preserves_alpha) {
    RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = TextureFilter::Nearest;
    texture.texels = {
        Color4f{1.0f, 0.0f, 0.0f, 0.25f},
        Color4f{0.0f, 1.0f, 0.0f, 0.75f},
    };

    const Color4f left = SampleTexture4(texture, Vec2f{0.25f, 0.5f});
    const Color4f right = SampleTexture4(texture, Vec2f{0.75f, 0.5f});

    EXPECT_FLOAT_EQ(left.x, 1.0f);
    EXPECT_FLOAT_EQ(left.w, 0.25f);
    EXPECT_FLOAT_EQ(right.y, 1.0f);
    EXPECT_FLOAT_EQ(right.w, 0.75f);
    EXPECT_FLOAT_EQ(SampleTextureAlpha(texture, Vec2f{0.75f, 0.5f}), 0.75f);
}
```

- [ ] Add `texture_loader_preserves_png_alpha_channel` using `tests/fixtures/assets/gltf/SimpleTexture/glTF/testTexture.png`. Assert that loaded texels all have alpha in `[0, 1]` and at least one texel was loaded.
- [ ] Add `texture_loader_uses_requested_color_space`. Load the same fixture PNG twice:

```cpp
const auto srgb = LoadPngTexture(path, TextureColorSpace::Srgb);
const auto linear = LoadPngTexture(path, TextureColorSpace::Linear);
```

Assert dimensions match, alpha values match, and at least one RGB channel differs between the two loads.

- [ ] Run the red test command:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R TextureTests
```

### Implementation

- [ ] Add `Color4f` to `include/yaoray/core/vec.hpp`:

```cpp
struct Color4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Color4f() = default;
    constexpr Color4f(float x_value, float y_value, float z_value, float w_value = 1.0f)
        : x(x_value), y(y_value), z(z_value), w(w_value) {}
    constexpr Color4f(Color3f rgb, float alpha = 1.0f)
        : x(rgb.x), y(rgb.y), z(rgb.z), w(alpha) {}

    constexpr Color3f rgb() const { return Color3f{x, y, z}; }
};
```

- [ ] Add `TextureColorSpace` and change `RenderTexture::texels`:

```cpp
enum class TextureColorSpace {
    Srgb,
    Linear,
};

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureColorSpace color_space = TextureColorSpace::Linear;
};
```

- [ ] Keep the existing public API behavior by making `LoadPngTexture(path)` default to sRGB:

```cpp
TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
```

- [ ] Implement new sampling helpers:

```cpp
Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv);
Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv);
```

- [ ] `SampleTexture` returns `SampleTexture4(texture, uv).rgb()`.
- [ ] `SampleTextureAlpha` returns `SampleTexture4(texture, uv).w`.
- [ ] Nearest and bilinear filtering interpolate alpha exactly like RGB.
- [ ] `LoadPngTexture` always requests 4 channels from `stbi_load`, stores alpha as `a / 255.0f`, applies sRGB transfer only to RGB when `color_space == TextureColorSpace::Srgb`, and stores `texture.color_space = color_space`.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R TextureTests
```

### Commit

- [ ] Commit:

```bash
git add include/yaoray/core/vec.hpp include/yaoray/render/texture.hpp src/render/texture.cpp tests/texture_tests.cpp
git commit -m "refactor: add rgba texture color spaces"
```

---

## Task 2: Preserve glTF Material Fields In The Asset Layer

### Files

- `include/yaoray/assets/asset_resource.hpp`
- `src/assets/gltf_loader.cpp`
- `tests/assets_tests.cpp`
- `tests/fixtures/assets/gltf/PbrMaterialCore/glTF/PbrMaterialCore.gltf`

### Tests First

- [ ] Add an asset default test:

```cpp
TEST(AssetResourceTests, asset_material_defaults_include_gltf_pbr_fields) {
    AssetMaterial material;
    EXPECT_FLOAT_EQ(material.base_color_alpha, 1.0f);
    EXPECT_EQ(material.metallic_roughness_texture, -1);
    EXPECT_EQ(material.normal_texture, -1);
    EXPECT_EQ(material.occlusion_texture, -1);
    EXPECT_EQ(material.emissive_texture, -1);
    EXPECT_FLOAT_EQ(material.normal_scale, 1.0f);
    EXPECT_FLOAT_EQ(material.occlusion_strength, 1.0f);
    EXPECT_EQ(material.alpha_mode, AssetAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(material.alpha_cutoff, 0.5f);
    EXPECT_FALSE(material.double_sided);
}
```

- [ ] Add fixture `tests/fixtures/assets/gltf/PbrMaterialCore/glTF/PbrMaterialCore.gltf`:

```json
{
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [{ "nodes": [0] }],
  "nodes": [{ "mesh": 0 }],
  "meshes": [{
    "primitives": [{
      "attributes": {
        "POSITION": 0,
        "NORMAL": 1,
        "TEXCOORD_0": 2,
        "TANGENT": 3
      },
      "indices": 4,
      "material": 0
    }]
  }],
  "materials": [{
    "name": "CorePbrMaterial",
    "pbrMetallicRoughness": {
      "baseColorFactor": [0.2, 0.4, 0.6, 0.7],
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.75,
      "roughnessFactor": 0.25,
      "metallicRoughnessTexture": { "index": 1 }
    },
    "normalTexture": { "index": 2, "scale": 0.5 },
    "occlusionTexture": { "index": 1, "strength": 0.25 },
    "emissiveFactor": [0.1, 0.2, 0.3],
    "emissiveTexture": { "index": 0 },
    "alphaMode": "MASK",
    "alphaCutoff": 0.33,
    "doubleSided": true
  }],
  "textures": [
    { "sampler": 0, "source": 0 },
    { "sampler": 0, "source": 0 },
    { "sampler": 0, "source": 0 }
  ],
  "images": [{ "uri": "../../SimpleTexture/glTF/testTexture.png" }],
  "samplers": [{ "wrapS": 10497, "wrapT": 10497 }],
  "buffers": [{ "uri": "../../SimpleTexture/glTF/SimpleTexture.bin", "byteLength": 296 }],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 48, "target": 34962 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 48, "target": 34962 },
    { "buffer": 0, "byteOffset": 96, "byteLength": 32, "target": 34962 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 64, "target": 34962 },
    { "buffer": 0, "byteOffset": 192, "byteLength": 6, "target": 34963 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1.0, -1.0, 0.0], "max": [1.0, 1.0, 0.0] },
    { "bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
}
```

- [ ] The fixture reuses the existing SimpleTexture binary layout. Add a note in the test that the `TANGENT` accessor references the bytes after UVs only for loader shape validation; the actual tangent numeric values are not asserted in this test.
- [ ] Add `gltf_loader_loads_core_pbr_material_slots`:

```cpp
const AssetLoadResult result = LoadGltfAssetResource(fixture_path);
ASSERT_TRUE(result.resource.has_value()) << JoinDiagnostics(result.diagnostics);
const AssetMaterial& material = result.resource->materials.at(0);
EXPECT_EQ(material.name, "CorePbrMaterial");
EXPECT_NEAR(material.base_color.x, 0.2f, 1.0e-6f);
EXPECT_NEAR(material.base_color_alpha, 0.7f, 1.0e-6f);
EXPECT_EQ(material.base_color_texture, 0);
EXPECT_EQ(material.metallic_roughness_texture, 1);
EXPECT_EQ(material.normal_texture, 2);
EXPECT_EQ(material.occlusion_texture, 1);
EXPECT_EQ(material.emissive_texture, 0);
EXPECT_NEAR(material.metallic, 0.75f, 1.0e-6f);
EXPECT_NEAR(material.roughness, 0.25f, 1.0e-6f);
EXPECT_NEAR(material.normal_scale, 0.5f, 1.0e-6f);
EXPECT_NEAR(material.occlusion_strength, 0.25f, 1.0e-6f);
EXPECT_EQ(material.alpha_mode, AssetAlphaMode::Mask);
EXPECT_NEAR(material.alpha_cutoff, 0.33f, 1.0e-6f);
EXPECT_TRUE(material.double_sided);
EXPECT_EQ(result.resource->meshes.at(0).primitives.at(0).tangents.size(), 4U);
```

- [ ] Add a small `BLEND` alpha fixture test by copying the material portion above and changing `"alphaMode": "BLEND"`. Assert `AssetAlphaMode::Blend` is preserved and diagnostics contain `"alphaMode BLEND"`.
- [ ] Run the red test command:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R Asset
```

### Implementation

- [ ] Extend `include/yaoray/assets/asset_resource.hpp`:

```cpp
enum class AssetAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct AssetTangent {
    Vec3f direction;
    float handedness = 1.0f;
};

struct AssetMaterial {
    std::string name;
    MaterialKind approximate_type = MaterialKind::Diffuse;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    float base_color_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int occlusion_texture = -1;
    int emissive_texture = -1;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    AssetAlphaMode alpha_mode = AssetAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};
```

- [ ] Add `std::vector<AssetTangent> tangents;` to `AssetPrimitive`.
- [ ] In `src/assets/gltf_loader.cpp`, add helpers:

```cpp
int ReadTextureInfoIndex(const tinygltf::Value& texture_info);
int ValidateMaterialTextureIndex(
    const tinygltf::Model& model,
    int texture_index,
    std::string_view slot_name,
    std::vector<AssetDiagnostic>* diagnostics);
AssetAlphaMode ConvertAlphaMode(const std::string& alpha_mode);
```

- [ ] In `ConvertMaterial`, read:
  - `baseColorFactor[0..2]` into `base_color`
  - `baseColorFactor[3]` into `base_color_alpha`
  - `pbrMetallicRoughness.baseColorTexture.index`
  - `pbrMetallicRoughness.metallicRoughnessTexture.index`
  - `metallicFactor`
  - `roughnessFactor`
  - `normalTexture.index` and `normalTexture.scale`
  - `occlusionTexture.index` and `occlusionTexture.strength`
  - `emissiveFactor`
  - `emissiveTexture.index`
  - `alphaMode`, `alphaCutoff`, and `doubleSided`
- [ ] Validate image-backed texture slots with the same file-URI constraints currently used for base color textures. Keep `data:` and embedded images unsupported.
- [ ] Add a warning diagnostic for `alphaMode == BLEND`:

```cpp
diagnostics->push_back(AssetDiagnostic{
    AssetDiagnosticLevel::Warning,
    "glTF alphaMode BLEND is preserved but rendered as opaque in this compatibility slice"
});
```

- [ ] Add tangent accessor support in `AppendPrimitiveResource`:
  - Read the `"TANGENT"` attribute when present.
  - Accept only FLOAT/VEC4.
  - Require `count == positions.size()`.
  - Store xyz in `AssetTangent::direction` and w in `AssetTangent::handedness`.
  - Emit an error diagnostic on malformed tangent attributes.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R Asset
```

### Commit

- [ ] Commit:

```bash
git add include/yaoray/assets/asset_resource.hpp src/assets/gltf_loader.cpp tests/assets_tests.cpp tests/fixtures/assets/gltf/PbrMaterialCore
git commit -m "feat: preserve gltf pbr material fields"
```

---

## Task 3: Compile PBR Material Fields And Tangents Into Render IR

### Files

- `include/yaoray/render/render_scene.hpp`
- `src/render/scene_compiler.cpp`
- `tests/render_scene_tests.cpp`

### Tests First

- [ ] Add render material default coverage:

```cpp
TEST(RenderSceneTests, render_material_defaults_include_gltf_pbr_fields) {
    RenderMaterial material;
    EXPECT_FLOAT_EQ(material.albedo_alpha, 1.0f);
    EXPECT_FLOAT_EQ(material.metallic, 0.0f);
    EXPECT_FLOAT_EQ(material.roughness, 1.0f);
    EXPECT_EQ(material.metallic_roughness_texture, -1);
    EXPECT_EQ(material.normal_texture, -1);
    EXPECT_EQ(material.emissive_texture, -1);
    EXPECT_EQ(material.occlusion_texture, -1);
    EXPECT_EQ(material.alpha_mode, RenderAlphaMode::Opaque);
    EXPECT_FLOAT_EQ(material.alpha_cutoff, 0.5f);
    EXPECT_FALSE(material.double_sided);
}
```

- [ ] Add `scene_compiler_preserves_gltf_pbr_material_fields` using the `PbrMaterialCore` fixture from Task 2. Assert compiled material fields match asset fields and texture slots are non-negative.
- [ ] Add `scene_compiler_texture_cache_keeps_distinct_color_spaces` using the same source image for base color and normal/metallic-roughness slots. Assert:
  - `compiled.textures.size() == 2`
  - color texture has `TextureColorSpace::Srgb`
  - data texture has `TextureColorSpace::Linear`
- [ ] Add `scene_compiler_generates_tangents_for_uv_normal_assets` using a small programmatic asset resource with positions, normals, UVs, indices, and no tangents. Assert all emitted `RenderTriangle::has_tangents` values are true and tangent directions are finite.
- [ ] Add `scene_compiler_imports_asset_tangents` using a programmatic primitive with explicit `AssetTangent` values. Assert the render triangle tangent directions and handedness values match the input.
- [ ] Run the red test command:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R RenderScene
```

### Implementation

- [ ] Extend `include/yaoray/render/render_scene.hpp`:

```cpp
enum class RenderAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    float albedo_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int emissive_texture = -1;
    int occlusion_texture = -1;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    RenderAlphaMode alpha_mode = RenderAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

- [ ] Add tangent fields to `RenderTriangle`:

```cpp
Vec3f t0;
Vec3f t1;
Vec3f t2;
float tangent_handedness0 = 1.0f;
float tangent_handedness1 = 1.0f;
float tangent_handedness2 = 1.0f;
bool has_tangents = false;
```

- [ ] In `src/render/scene_compiler.cpp`, introduce texture usage:

```cpp
enum class TextureUsage {
    Color,
    Data,
};

TextureColorSpace TextureColorSpaceForUsage(TextureUsage usage) {
    return usage == TextureUsage::Color ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
}
```

- [ ] Include usage in the texture cache key:

```cpp
std::string MakeTextureCacheKey(
    const std::filesystem::path& path,
    TextureWrap wrap_s,
    TextureWrap wrap_t,
    TextureUsage usage);
```

- [ ] Update texture loading:

```cpp
const TextureLoadResult texture = LoadPngTexture(path, TextureColorSpaceForUsage(usage));
```

- [ ] Compile material slots:
  - `base_color_texture` uses `TextureUsage::Color`
  - `emissive_texture` uses `TextureUsage::Color`
  - `metallic_roughness_texture`, `normal_texture`, and `occlusion_texture` use `TextureUsage::Data`
- [ ] Map `AssetAlphaMode` to `RenderAlphaMode`.
- [ ] Preserve `Blend` in the IR, but add compiler diagnostic text that this CPU slice renders `BLEND` as opaque.
- [ ] Implement tangent import:
  - When `primitive.tangents.size() == primitive.positions.size()`, copy per-vertex tangents into emitted triangles.
  - Set `has_tangents = true`.
- [ ] Implement tangent generation for primitives without tangents:
  - Accumulate triangle tangents per vertex from position and UV deltas.
  - Orthogonalize against normals using Gram-Schmidt.
  - Normalize with fallback tangent `(1, 0, 0)` projected against the normal.
  - Set handedness to `1.0f`.
  - Emit `has_tangents = true` only when normals and UVs are available and valid.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R RenderScene
```

### Commit

- [ ] Commit:

```bash
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile gltf pbr material data"
```

---

## Task 4: Add CPU Material Sample Resolver And Normal Maps

### Files

- `include/yaoray/backends/cpu/cpu_material.hpp`
- `src/backends/cpu/cpu_material.cpp`
- `src/backends/cpu/cpu_path_tracer.cpp`
- `CMakeLists.txt`
- `tests/cpu_material_tests.cpp`

### Tests First

- [ ] Add `tests/cpu_material_tests.cpp` and register it in the test target.
- [ ] Add `cpu_material_combines_base_color_texture_alpha`. Build a small render scene with one material and an albedo texture. Assert final color multiplies factor RGB and final alpha is `albedo_alpha * texture_alpha`.
- [ ] Add `cpu_material_samples_metallic_roughness_texture`. Use a one-pixel texture with channels `(unused, roughness, metallic, 1)`. Assert resolved material roughness and metallic use G and B channels, and `type` becomes `MaterialKind::Metal` when metallic is at least `0.5f`.
- [ ] Add `cpu_material_samples_emissive_texture`. Assert emissive factor multiplies emissive texture RGB.
- [ ] Add `cpu_material_resolves_normal_map_from_tangent_space`. Use a triangle with normal `(0, 0, 1)`, tangent `(1, 0, 0)`, handedness `1`, and a normal texture encoding tangent-space +X. Assert the resolved shading normal points toward world +X after normalization.
- [ ] Run the red test command:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R CpuMaterial
```

### Implementation

- [ ] Add `include/yaoray/backends/cpu/cpu_material.hpp`:

```cpp
#pragma once

#include "yaoray/core/math.hpp"
#include "yaoray/render/render_scene.hpp"

namespace yaoray {

struct ResolvedMaterialSample {
    RenderMaterial material;
    Vec3f shading_normal;
    Vec2f uv;
    float alpha = 1.0f;
};

ResolvedMaterialSample ResolveCpuMaterialSample(
    const RenderSceneIR& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& base_material,
    Vec3f barycentric,
    Vec3f geometric_normal,
    Vec3f wo);

bool IsAlphaVisible(const ResolvedMaterialSample& sample);

} // namespace yaoray
```

- [ ] Implement `ResolveCpuMaterialSample` in `src/backends/cpu/cpu_material.cpp`:
  - Interpolate UV from barycentric values.
  - Start from the compiled `RenderMaterial`.
  - Sample `albedo_texture` with `SampleTexture4`, multiply RGB into `albedo`, and compute `alpha = albedo_alpha * sampled.w`.
  - Sample `metallic_roughness_texture` with `SampleTexture4`, set `roughness = clamp(G, 0, 1)` and `metallic = clamp(B, 0, 1)`.
  - Set `type = MaterialKind::Metal` when metallic is at least `0.5f`; keep the existing type otherwise.
  - Sample `emissive_texture` with `SampleTexture`, multiplying into `emission`.
  - Resolve normal map only when `normal_texture >= 0` and `triangle.has_tangents` is true.
  - Decode normal texture from `[0, 1]` to `[-1, 1]`, apply `normal_scale` to tangent-space X/Y, normalize, and transform through TBN.
  - Face-forward resolved normal against `wo` using the existing path tracer convention.
- [ ] `IsAlphaVisible` returns:

```cpp
if (sample.material.alpha_mode != RenderAlphaMode::Mask) {
    return true;
}
return sample.alpha >= sample.material.alpha_cutoff;
```

- [ ] Replace the local `ResolveHitMaterial()` helper in `src/backends/cpu/cpu_path_tracer.cpp` with `ResolveCpuMaterialSample`.
- [ ] Keep raw BVH intersection in the path tracer for this task. Alpha-skip traversal is implemented in Task 5.
- [ ] Add `src/backends/cpu/cpu_material.cpp` to `CMakeLists.txt`.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R CpuMaterial
ctest --test-dir build --output-on-failure -R Cpu
```

### Commit

- [ ] Commit:

```bash
git add include/yaoray/backends/cpu/cpu_material.hpp src/backends/cpu/cpu_material.cpp src/backends/cpu/cpu_path_tracer.cpp CMakeLists.txt tests/cpu_material_tests.cpp
git commit -m "feat: resolve cpu gltf material samples"
```

---

## Task 5: Add Alpha-Masked Visible Surface Tracing

### Files

- `include/yaoray/render/bvh.hpp`
- `src/render/bvh.cpp`
- `include/yaoray/backends/cpu/cpu_surface.hpp`
- `src/backends/cpu/cpu_surface.cpp`
- `src/backends/cpu/cpu_debug_renderer.cpp`
- `src/backends/cpu/cpu_path_tracer.cpp`
- `CMakeLists.txt`
- `tests/cpu_surface_tests.cpp`
- `tests/cpu_debug_renderer_tests.cpp`

### Tests First

- [ ] Add `tests/cpu_surface_tests.cpp` and register it in the test target.
- [ ] Add `cpu_surface_skips_masked_front_triangle_and_returns_back_triangle`:
  - Build a render scene with two triangles along the same ray.
  - Front triangle uses `RenderAlphaMode::Mask`, `alpha_cutoff = 0.5f`, and an albedo texture alpha of `0.0f`.
  - Back triangle uses opaque material.
  - Build BVH and prepared scene.
  - Call `TraceVisibleSurface`.
  - Assert the hit triangle index is the back triangle.
- [ ] Add `cpu_surface_returns_masked_triangle_when_alpha_passes_cutoff` with front alpha `1.0f`.
- [ ] Add a debug renderer regression test in `tests/cpu_debug_renderer_tests.cpp`:
  - Render a tiny scene with a masked plane between camera/light and an opaque object.
  - Assert the object is not shadowed by the masked-out plane.
- [ ] Run the red test command:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R "CpuSurface|CpuDebug"
```

### Implementation

- [ ] Extend `IntersectBvh` signature while preserving defaults:

```cpp
BvhHit IntersectBvh(
    const RenderSceneIR& scene,
    const Bvh& bvh,
    const Ray3f& ray,
    BvhTraceStats* stats = nullptr,
    float t_min = 1.0e-5f,
    float t_max = std::numeric_limits<float>::infinity());
```

- [ ] In `src/render/bvh.cpp`, use `t_min` and `t_max` for both bounds and triangle intersection tests.
- [ ] Add `include/yaoray/backends/cpu/cpu_surface.hpp`:

```cpp
#pragma once

#include "yaoray/backends/cpu/cpu_material.hpp"
#include "yaoray/backends/cpu/cpu_scene.hpp"
#include "yaoray/render/bvh.hpp"

namespace yaoray {

struct CpuSurfaceHit {
    bool hit = false;
    bool exhausted = false;
    BvhHit geometry_hit;
    Vec3f barycentric;
    Vec2f uv;
    ResolvedMaterialSample sample;
};

CpuSurfaceHit TraceVisibleSurface(
    const CpuPreparedScene& prepared_scene,
    const Ray3f& ray,
    float t_min,
    float t_max,
    BvhTraceStats* stats = nullptr);

} // namespace yaoray
```

- [ ] Implement `TraceVisibleSurface`:
  - Start at `current_t_min = t_min`.
  - Call `IntersectBvh(prepared_scene.scene, prepared_scene.bvh, ray, stats, current_t_min, t_max)`.
  - Return miss when no geometry hit exists.
  - Compute hit point and barycentric values using `BarycentricCoordinates`.
  - Resolve material through `ResolveCpuMaterialSample`.
  - Return the hit when `IsAlphaVisible(sample)` is true.
  - Advance `current_t_min = hit.t + 1.0e-4f` and continue when alpha mask rejects the hit.
  - Stop after `64` rejected masked hits and return `exhausted = true`.
- [ ] Update `src/backends/cpu/cpu_path_tracer.cpp`:
  - Main path tracing uses `TraceVisibleSurface` instead of raw `IntersectBvh`.
  - Shadow rays use `TraceVisibleSurface` so masked-out surfaces are skipped before existing dielectric transparency logic.
  - Treat `exhausted` as blocked for shadow rays and miss for camera/path rays.
- [ ] Update `src/backends/cpu/cpu_debug_renderer.cpp`:
  - Primary rays use `TraceVisibleSurface`.
  - Shadow rays use `TraceVisibleSurface`.
  - Debug renderer consumes `CpuSurfaceHit::sample.material`; normal-map shading remains CPU path tracer only.
- [ ] Add `src/backends/cpu/cpu_surface.cpp` to `CMakeLists.txt`.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R "CpuSurface|CpuDebug|Cpu"
```

### Commit

- [ ] Commit:

```bash
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp include/yaoray/backends/cpu/cpu_surface.hpp src/backends/cpu/cpu_surface.cpp src/backends/cpu/cpu_debug_renderer.cpp src/backends/cpu/cpu_path_tracer.cpp CMakeLists.txt tests/cpu_surface_tests.cpp tests/cpu_debug_renderer_tests.cpp
git commit -m "feat: support alpha mask visibility on cpu"
```

---

## Task 6: Add FlightHelmet Asset, Scene, And Asset Documentation

### Files

- `scenes/examples/assets/gltf/FlightHelmet/`
- `scenes/examples/gltf_flight_helmet.toml`
- `tests/assets_tests.cpp`
- `tests/render_scene_tests.cpp`
- `docs/assets/khronos-sample-assets.md`
- `docs/assets/bistro-local-benchmark.md`
- `README.md`

### Download Source

- [ ] Download FlightHelmet through a sparse clone into `/private/tmp`:

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git /private/tmp/gltf-sample-assets
git -C /private/tmp/gltf-sample-assets sparse-checkout set Models/FlightHelmet/glTF Models/FlightHelmet/LICENSE.md Models/FlightHelmet/README.md Models/FlightHelmet/metadata.json
```

- [ ] Copy only FlightHelmet files into the repository:

```bash
mkdir -p scenes/examples/assets/gltf/FlightHelmet
cp -R /private/tmp/gltf-sample-assets/Models/FlightHelmet/glTF scenes/examples/assets/gltf/FlightHelmet/
cp /private/tmp/gltf-sample-assets/Models/FlightHelmet/LICENSE.md scenes/examples/assets/gltf/FlightHelmet/
cp /private/tmp/gltf-sample-assets/Models/FlightHelmet/README.md scenes/examples/assets/gltf/FlightHelmet/
cp /private/tmp/gltf-sample-assets/Models/FlightHelmet/metadata.json scenes/examples/assets/gltf/FlightHelmet/
```

### Tests First

- [ ] Add `gltf_loader_loads_flight_helmet_asset`:

```cpp
const AssetLoadResult result = LoadGltfAssetResource(
    "scenes/examples/assets/gltf/FlightHelmet/glTF/FlightHelmet.gltf");
ASSERT_TRUE(result.resource.has_value()) << JoinDiagnostics(result.diagnostics);
EXPECT_GT(result.resource->meshes.size(), 0U);
EXPECT_GT(result.resource->materials.size(), 0U);
EXPECT_GT(result.resource->textures.size(), 0U);
EXPECT_TRUE(std::any_of(result.resource->meshes.begin(), result.resource->meshes.end(), [](const AssetMesh& mesh) {
    return std::any_of(mesh.primitives.begin(), mesh.primitives.end(), [](const AssetPrimitive& primitive) {
        return !primitive.tangents.empty();
    });
}));
```

- [ ] Add `scene_compiler_compiles_flight_helmet_pbr_fields`:
  - Load FlightHelmet asset.
  - Compile to render IR.
  - Assert no error diagnostics.
  - Assert triangle count is greater than zero.
  - Assert texture count is greater than zero.
  - Assert at least one material has `normal_texture >= 0`.
  - Assert at least one material has `metallic_roughness_texture >= 0`.
  - Assert at least one triangle has `has_tangents == true`.
- [ ] Run the red test command after copying assets and before final fixes:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R "FlightHelmet|RenderScene"
```

### Implementation

- [ ] Add `scenes/examples/gltf_flight_helmet.toml`:

```toml
[scene]
name = "glTF FlightHelmet"

[camera]
origin = [0.0, 0.7, 3.0]
target = [0.0, 0.35, 0.0]
up = [0.0, 1.0, 0.0]
vfov = 35.0

[[assets]]
path = "assets/gltf/FlightHelmet/glTF/FlightHelmet.gltf"

[[lights]]
type = "area"
position = [0.0, 3.0, 2.0]
u = [1.5, 0.0, 0.0]
v = [0.0, 0.0, 1.5]
emission = [10.0, 10.0, 10.0]

[render]
backend = "cpu"
width = 640
height = 480
samples_per_pixel = 16
max_depth = 6
output = "out/gltf_flight_helmet.png"
```

- [ ] Adjust field names to match the current TOML schema exactly before committing.
- [ ] Add `docs/assets/khronos-sample-assets.md`:
  - Source repository URL: `https://github.com/KhronosGroup/glTF-Sample-Assets`
  - Committed model: `Models/FlightHelmet/glTF`
  - Purpose: in-repo glTF compatibility validation
  - License files copied under `scenes/examples/assets/gltf/FlightHelmet/`
- [ ] Add `docs/assets/bistro-local-benchmark.md`:
  - Source: NVIDIA Amazon Lumberyard Bistro scene.
  - Expected local root: `external/assets/bistro/`.
  - The model is intentionally not committed because the archive is approximately 853 MiB.
  - Use Bistro after FlightHelmet passes loader/compiler/render smoke tests.
- [ ] Update `README.md` with:
  - FlightHelmet sample render command.
  - A short note that Bistro is local-only.

### Verify

- [ ] Run:

```bash
cmake --build build --target yaoray_tests
ctest --test-dir build --output-on-failure -R "FlightHelmet|Asset|RenderScene"
```

- [ ] Run a CPU smoke render:

```bash
./build/yaoray render scenes/examples/gltf_flight_helmet.toml --backend cpu
```

- [ ] Confirm `out/gltf_flight_helmet.png` is created and non-empty:

```bash
test -s out/gltf_flight_helmet.png
```

### Commit

- [ ] Commit:

```bash
git add scenes/examples/assets/gltf/FlightHelmet scenes/examples/gltf_flight_helmet.toml tests/assets_tests.cpp tests/render_scene_tests.cpp docs/assets/khronos-sample-assets.md docs/assets/bistro-local-benchmark.md README.md
git commit -m "feat: add flight helmet gltf compatibility target"
```

---

## Task 7: Final Cleanup And Full Verification

### Stale API Checks

- [ ] Search for old texture storage:

```bash
rg -n "std::vector<Color3f> texels|Color3f> texels" include src tests
```

- [ ] Search for old loader signature declarations:

```bash
rg -n "LoadPngTexture\\(const std::filesystem::path& path\\)" include src
```

- [ ] Search for raw CPU backend BVH calls:

```bash
rg -n "IntersectBvh\\(" src/backends/cpu include/yaoray/backends/cpu
```

Expected remaining matches are inside `cpu_surface.cpp` only.

### Full Verification

- [ ] Run formatting checks available in this repository. Use the existing project command from `README.md` or `CMakeLists.txt` when present.
- [ ] Run:

```bash
git diff --check
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
./build/yaoray render scenes/examples/gltf_flight_helmet.toml --backend cpu
test -s out/gltf_flight_helmet.png
```

### Final Commit

- [ ] Commit final cleanup only when verification changes files:

```bash
git add <changed-files>
git commit -m "chore: verify gltf compatibility v2"
```

---

## Implementation Order

1. RGBA texture and color-space foundation.
2. Asset-layer glTF PBR material and tangent preservation.
3. Render compiler IR conversion, texture usage cache, and tangent generation.
4. CPU material sample resolver and normal maps.
5. Alpha mask visible-surface tracing for CPU renderers.
6. FlightHelmet asset, example scene, and docs.
7. Full cleanup and verification.

This order keeps each layer independently testable and prevents CPU alpha/normal-map work from depending on ad hoc loader behavior.

## Plan Self-Review Checklist

- [ ] The plan modifies the asset layer before render compiler/backend consumers.
- [ ] Every new behavior has at least one named test.
- [ ] FlightHelmet is committed; Bistro is documented as local-only.
- [ ] Texture color space is a compiler policy, not an asset-layer global field.
- [ ] Alpha mask is implemented through CPU visible-surface tracing, not BVH mutation.
- [ ] CUDA/OptiX remain future consumers of the backend-neutral render IR.
