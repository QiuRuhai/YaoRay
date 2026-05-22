# YaoRay Asset Render Backend IR Slice 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first vertical slice of the approved Asset IR -> GPU-ready Render IR -> Backend architecture while preserving current CLI behavior and renderer output.

**Architecture:** Slice 1 introduces semantic `AssetDocument` records, backend-facing `RenderWorld` records, a render-world compiler, and a transitional `RenderWorld -> RenderScene` adapter. Public backend dispatch moves to `RenderWorld`; CPU internals may use the adapter during this slice, with full backend dependency cleanup reserved for the next architecture slices.

**Tech Stack:** C++20, CMake, existing YaoRay modules (`yaoray_scene`, `yaoray_assets`, `yaoray_render`, `yaoray_backends`), CTest, custom `YR_TEST` harness.

---

## Scope

This plan implements Slice 1 from
`docs/superpowers/specs/2026-05-22-yaoray-asset-render-backend-ir-architecture-design.md`.

The slice must:

- Add Asset IR type definitions and tests.
- Add Render IR type definitions and tests.
- Compile existing scene settings, cameras, constant/HDRI environments, rectangle area lights, current material fields, builtin triangles, inline quads, and the current OBJ/glTF imported subset into `RenderWorld`.
- Keep the current OBJ/glTF loaders behavior through a bridge from `ImportedMesh` to Asset IR. This is not the full importer migration from Slice 2; it exists so all current CLI tests keep passing.
- Change backend dispatch to accept `RenderWorld`.
- Keep CPU rendering behavior by adapting `RenderWorld` to the existing `RenderScene` until CPU internals are migrated in Slice 3.
- Keep all existing CTest tests passing.

The slice must not:

- Rewrite the CPU path tracer internals to native `RenderWorld` traversal.
- Change scene TOML schema.
- Change rendered output intentionally.
- Add CUDA rendering.
- Add new asset feature support beyond preserving the existing imported subset through Asset IR.

## File Structure

Create:

- `include/yaoray/assets/asset_ir.hpp`
  - Semantic asset documents, nodes, meshes, primitives, materials, images, samplers, textures, and texture slots.
- `include/yaoray/render/render_world.hpp`
  - GPU-ready render records: settings, camera, vertices, primitives, materials, images, samplers, textures, lights, environment, and BVH.
- `include/yaoray/render/render_world_compiler.hpp`
  - Public `CompileSceneToRenderWorld()` entry point.
- `src/render/render_world_compiler.cpp`
  - Scene + Asset IR -> RenderWorld implementation.
- `include/yaoray/render/render_world_adapter.hpp`
  - Transitional `RenderScene ToLegacyRenderScene(const RenderWorld& world)` adapter.
- `src/render/render_world_adapter.cpp`
  - RenderWorld -> old RenderScene conversion used by CPU backends during Slice 1.
- `tests/asset_ir_tests.cpp`
  - Asset IR default and semantic preservation tests.
- `tests/render_world_tests.cpp`
  - RenderWorld default, table, and record tests.
- `tests/render_world_compiler_tests.cpp`
  - Vertical slice compile tests for builtin, inline quad, environment, lights, material lowering, and imported bridge behavior.

Modify:

- `CMakeLists.txt`
  - Add new render source files and new tests.
- `include/yaoray/backends/backend.hpp`
  - Change backend API to render `RenderWorld`.
- `src/backends/backend.cpp`
  - Update CUDA stub signature.
- `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
  - Update CPU backend signature.
- `src/backends/cpu/cpu_debug_backend.cpp`
  - Adapt `RenderWorld` to `RenderScene` before calling current CPU renderers.
- `src/app/main.cpp`
  - Use `CompileSceneToRenderWorld()` for CLI rendering and print equivalent scene compile stats.
- `tests/backend_tests.cpp`
  - Build backend tests with `RenderWorld`.
- `tests/render_scene_tests.cpp`
  - Keep existing tests for transitional `CompileScene()`, and add only compatibility assertions where needed.

## Task 1: Add Asset IR Types

**Files:**
- Create: `include/yaoray/assets/asset_ir.hpp`
- Create: `tests/asset_ir_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing Asset IR default tests**

Add `tests/asset_ir_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>

#include <yaoray/assets/asset_ir.hpp>

YR_TEST(asset_document_defaults_are_empty) {
    const yr::AssetDocument document;

    YR_EXPECT_TRUE(document.nodes.empty());
    YR_EXPECT_TRUE(document.meshes.empty());
    YR_EXPECT_TRUE(document.materials.empty());
    YR_EXPECT_TRUE(document.images.empty());
    YR_EXPECT_TRUE(document.samplers.empty());
    YR_EXPECT_TRUE(document.textures.empty());
    YR_EXPECT_TRUE(document.diagnostics.empty());
}

YR_TEST(asset_material_defaults_preserve_pbr_semantics) {
    const yr::AssetMaterial material;

    YR_EXPECT_EQ(material.model, yr::AssetMaterialModel::PbrMetallicRoughness);
    YR_EXPECT_NEAR(material.base_color_factor.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.base_color_factor.y, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.base_color_factor.z, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.base_color_factor.w, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.metallic_factor, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.roughness_factor, 1.0, 1e-6);
    YR_EXPECT_EQ(material.base_color.texture, -1);
    YR_EXPECT_EQ(material.metallic_roughness.texture, -1);
    YR_EXPECT_EQ(material.normal.texture, -1);
    YR_EXPECT_EQ(material.emissive.texture, -1);
    YR_EXPECT_EQ(material.alpha_mode, yr::AssetAlphaMode::Opaque);
    YR_EXPECT_TRUE(!material.double_sided);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
    YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
}

YR_TEST(asset_primitive_defaults_to_triangle_topology) {
    const yr::AssetPrimitive primitive;

    YR_EXPECT_EQ(primitive.topology, yr::AssetPrimitiveTopology::Triangles);
    YR_EXPECT_EQ(primitive.material, -1);
    YR_EXPECT_TRUE(primitive.attributes.positions.empty());
    YR_EXPECT_TRUE(primitive.indices.empty());
}
```

- [ ] **Step 2: Add the test file to CMake and verify the test fails**

Modify the `add_executable(yaoray_tests ...)` block in `CMakeLists.txt`:

```cmake
    tests/asset_ir_tests.cpp
```

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails with an include error for `yaoray/assets/asset_ir.hpp`.

- [ ] **Step 3: Implement Asset IR types**

Create `include/yaoray/assets/asset_ir.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/diagnostic.hpp>

namespace yr {

struct Color4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

enum class AssetPrimitiveTopology {
    Triangles
};

enum class AssetMaterialModel {
    PbrMetallicRoughness,
    DiffuseLegacy,
    EmissiveLegacy,
    MirrorLegacy,
    MetalLegacy,
    PlasticLegacy,
    DielectricLegacy
};

enum class AssetAlphaMode {
    Opaque,
    Mask,
    Blend
};

enum class AssetTextureColorSpace {
    Linear,
    Srgb
};

struct AssetTextureSlot {
    int texture = -1;
    int uv_set = 0;
    float scale = 1.0f;
};

struct AssetAttributeSet {
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> uv0;
};

using AssetIndexBuffer = std::vector<std::uint32_t>;

struct AssetPrimitive {
    AssetAttributeSet attributes;
    AssetIndexBuffer indices;
    int material = -1;
    AssetPrimitiveTopology topology = AssetPrimitiveTopology::Triangles;
};

struct AssetMesh {
    std::string name;
    std::vector<AssetPrimitive> primitives;
};

struct AssetTransform {
    Vec3f translate{0.0f, 0.0f, 0.0f};
    Vec3f rotate_degrees{0.0f, 0.0f, 0.0f};
    Vec3f scale{1.0f, 1.0f, 1.0f};
};

struct AssetNode {
    std::string name;
    int mesh = -1;
    int parent = -1;
    std::vector<int> children;
    AssetTransform local_transform;
};

struct AssetImage {
    std::filesystem::path path;
};

struct AssetSampler {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureFilter min_filter = TextureFilter::Bilinear;
    TextureFilter mag_filter = TextureFilter::Bilinear;
};

struct AssetTexture {
    int image = -1;
    int sampler = -1;
    AssetTextureColorSpace color_space = AssetTextureColorSpace::Srgb;
};

struct AssetMaterial {
    std::string name;
    AssetMaterialModel model = AssetMaterialModel::PbrMetallicRoughness;
    Color4f base_color_factor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;
    Color3f emissive_factor{};
    AssetTextureSlot base_color;
    AssetTextureSlot metallic_roughness;
    AssetTextureSlot normal;
    AssetTextureSlot emissive;
    AssetAlphaMode alpha_mode = AssetAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    float specular = 0.04f;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

struct AssetDocument {
    std::vector<AssetNode> nodes;
    std::vector<AssetMesh> meshes;
    std::vector<AssetMaterial> materials;
    std::vector<AssetImage> images;
    std::vector<AssetSampler> samplers;
    std::vector<AssetTexture> textures;
    std::vector<SceneDiagnostic> diagnostics;
};

} // namespace yr
```

- [ ] **Step 4: Run the targeted tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: `asset_document_defaults_are_empty`, `asset_material_defaults_preserve_pbr_semantics`, and `asset_primitive_defaults_to_triangle_topology` pass. Other tests should continue to pass because no existing pipeline has changed.

- [ ] **Step 5: Commit Asset IR scaffolding**

Run:

```powershell
git add CMakeLists.txt include/yaoray/assets/asset_ir.hpp tests/asset_ir_tests.cpp
git commit -m "feat: add asset ir scaffolding"
```

Expected: commit succeeds and includes only the Asset IR header, test file, and CMake test entry.

## Task 2: Add RenderWorld Types

**Files:**
- Create: `include/yaoray/render/render_world.hpp`
- Create: `tests/render_world_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing RenderWorld tests**

Add `tests/render_world_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <cstdint>

#include <yaoray/render/render_world.hpp>

YR_TEST(render_world_defaults_are_backend_friendly) {
    const yr::RenderWorld world;

    YR_EXPECT_EQ(world.settings.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(world.settings.integrator, yr::RenderIntegratorKind::DebugDirect);
    YR_EXPECT_EQ(world.settings.sampler, yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(world.settings.width, 0);
    YR_EXPECT_EQ(world.settings.height, 0);
    YR_EXPECT_EQ(world.settings.spp, 1);
    YR_EXPECT_EQ(world.settings.max_depth, 5);
    YR_EXPECT_EQ(world.settings.seed, std::uint64_t{0});
    YR_EXPECT_EQ(world.settings.threads, 0);
    YR_EXPECT_EQ(world.settings.light_samples, 1);
    YR_EXPECT_NEAR(world.settings.radiance_clamp, 0.0, 1e-6);
    YR_EXPECT_TRUE(world.vertices.empty());
    YR_EXPECT_TRUE(world.indices.empty());
    YR_EXPECT_TRUE(world.primitives.empty());
    YR_EXPECT_TRUE(world.materials.empty());
    YR_EXPECT_TRUE(world.lights.empty());
}

YR_TEST(render_material_record_defaults_are_handle_based) {
    const yr::RenderMaterialRecord material;

    YR_EXPECT_EQ(material.model, yr::RenderMaterialModel::Diffuse);
    YR_EXPECT_NEAR(material.base_color_factor.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.base_color_factor.w, 1.0, 1e-6);
    YR_EXPECT_EQ(material.base_color_texture, -1);
    YR_EXPECT_EQ(material.metallic_roughness_texture, -1);
    YR_EXPECT_EQ(material.normal_texture, -1);
    YR_EXPECT_EQ(material.emissive_texture, -1);
    YR_EXPECT_EQ(material.alpha_mode, yr::RenderAlphaMode::Opaque);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
}

YR_TEST(render_texture_records_split_image_sampler_and_texture) {
    const yr::RenderImageRecord image;
    const yr::RenderSamplerRecord sampler;
    const yr::RenderTextureRecord texture;

    YR_EXPECT_EQ(image.width, 0);
    YR_EXPECT_EQ(image.height, 0);
    YR_EXPECT_EQ(image.texel_offset, std::uint32_t{0});
    YR_EXPECT_EQ(image.texel_count, std::uint32_t{0});
    YR_EXPECT_EQ(sampler.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(sampler.wrap_t, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(texture.image, -1);
    YR_EXPECT_EQ(texture.sampler, -1);
}
```

- [ ] **Step 2: Add the test file to CMake and verify the test fails**

Modify the `add_executable(yaoray_tests ...)` block in `CMakeLists.txt`:

```cmake
    tests/render_world_tests.cpp
```

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails with an include error for `yaoray/render/render_world.hpp`.

- [ ] **Step 3: Implement RenderWorld types**

Create `include/yaoray/render/render_world.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include <yaoray/assets/asset_ir.hpp>
#include <yaoray/core/bounds.hpp>
#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/environment.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    float radiance_clamp = 0.0f;
};

struct RenderWorldCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
    float aperture = 0.0f;
    float focus_distance = 1.0f;
};

enum class RenderMaterialModel {
    Diffuse,
    Mirror,
    Metal,
    Plastic,
    Dielectric
};

enum class RenderAlphaMode {
    Opaque,
    Mask,
    Blend
};

enum class RenderImageFormat {
    LinearRgb32f
};

enum class RenderTextureColorSpace {
    Linear,
    Srgb
};

enum class RenderLightType {
    RectArea
};

struct RenderVertex {
    Point3f position;
    Vec3f geometric_normal{0.0f, 0.0f, 1.0f};
    Vec3f shading_normal{0.0f, 0.0f, 1.0f};
    Vec2f uv0;
    std::uint32_t flags = 0;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material = -1;
    Bounds3f bounds;
    std::uint32_t flags = 0;
};

struct RenderMaterialRecord {
    RenderMaterialModel model = RenderMaterialModel::Diffuse;
    Color4f base_color_factor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 0.0f;
    float specular = 0.04f;
    Color3f emissive_factor{};
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int emissive_texture = -1;
    float ior = 1.5f;
    float alpha_cutoff = 0.5f;
    RenderAlphaMode alpha_mode = RenderAlphaMode::Opaque;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
    std::uint32_t flags = 0;
};

struct RenderImageRecord {
    int width = 0;
    int height = 0;
    RenderImageFormat format = RenderImageFormat::LinearRgb32f;
    std::uint32_t texel_offset = 0;
    std::uint32_t texel_count = 0;
};

struct RenderSamplerRecord {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureFilter min_filter = TextureFilter::Bilinear;
    TextureFilter mag_filter = TextureFilter::Bilinear;
};

struct RenderTextureRecord {
    int image = -1;
    int sampler = -1;
    RenderTextureColorSpace color_space = RenderTextureColorSpace::Linear;
};

struct RenderLightRecord {
    RenderLightType type = RenderLightType::RectArea;
    Color3f radiance{1.0f, 1.0f, 1.0f};
    Point3f position;
    Vec3f u{1.0f, 0.0f, 0.0f};
    Vec3f v{0.0f, 1.0f, 0.0f};
    int primitive = -1;
    std::uint32_t flags = 0;
};

struct RenderEnvironmentRecord {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture = -1;
    int distribution = -1;
};

struct RenderWorld {
    RenderSettings settings;
    RenderWorldCamera camera;
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;
    std::vector<RenderMaterialRecord> materials;
    std::vector<RenderImageRecord> images;
    std::vector<RenderSamplerRecord> samplers;
    std::vector<RenderTextureRecord> textures;
    std::vector<Color4f> texels;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<RenderLightRecord> lights;
    RenderEnvironmentRecord environment;
    RenderBvh bvh;
};

} // namespace yr
```

- [ ] **Step 4: Run the targeted tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: the new RenderWorld tests pass and no existing tests regress.

- [ ] **Step 5: Commit RenderWorld scaffolding**

Run:

```powershell
git add CMakeLists.txt include/yaoray/render/render_world.hpp tests/render_world_tests.cpp
git commit -m "feat: add render world scaffolding"
```

Expected: commit succeeds and includes only RenderWorld types, tests, and CMake test entry.

## Task 3: Add RenderWorld Compiler Public API And Basic Settings

**Files:**
- Create: `include/yaoray/render/render_world_compiler.hpp`
- Create: `src/render/render_world_compiler.cpp`
- Create: `tests/render_world_compiler_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing settings/camera compiler tests**

Add `tests/render_world_compiler_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

#include <yaoray/render/render_world_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace {

yr::SceneDescription MakeBaseWorldScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated_render_world.toml";
    scene.render.backend = yr::RenderBackendKind::Cuda;
    scene.render.integrator = yr::RenderIntegratorKind::Path;
    scene.render.sampler = yr::RenderSamplerKind::Stratified;
    scene.render.width = 320;
    scene.render.height = 180;
    scene.render.spp = 4;
    scene.render.max_depth = 6;
    scene.render.seed = std::uint64_t{123};
    scene.render.threads = 4;
    scene.render.light_samples = 3;
    scene.render.radiance_clamp = 18.0f;
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera->fov_y = 60.0f;
    return scene;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(render_world_compiler_copies_render_settings) {
    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(MakeBaseWorldScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderWorld& world = result.world.value();
    YR_EXPECT_EQ(world.settings.backend, yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(world.settings.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(world.settings.sampler, yr::RenderSamplerKind::Stratified);
    YR_EXPECT_EQ(world.settings.width, 320);
    YR_EXPECT_EQ(world.settings.height, 180);
    YR_EXPECT_EQ(world.settings.spp, 4);
    YR_EXPECT_EQ(world.settings.max_depth, 6);
    YR_EXPECT_EQ(world.settings.seed, std::uint64_t{123});
    YR_EXPECT_EQ(world.settings.threads, 4);
    YR_EXPECT_EQ(world.settings.light_samples, 3);
    YR_EXPECT_NEAR(world.settings.radiance_clamp, 18.0, 1e-6);
}

YR_TEST(render_world_compiler_builds_camera_basis) {
    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(MakeBaseWorldScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderWorldCamera& camera = result.world.value().camera;
    YR_EXPECT_NEAR(camera.origin.z, 4.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.fov_y_radians, 1.04719758, 1e-6);
}

YR_TEST(render_world_compiler_rejects_missing_camera) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.camera.reset();

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.world.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "camera", "missing camera"));
}
```

- [ ] **Step 2: Add source/test files to CMake and verify the test fails**

Modify `yaoray_render` sources:

```cmake
    src/render/render_world_compiler.cpp
```

Modify `yaoray_tests` sources:

```cmake
    tests/render_world_compiler_tests.cpp
```

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `yaoray/render/render_world_compiler.hpp` does not exist.

- [ ] **Step 3: Implement compiler API and settings/camera compilation**

Create `include/yaoray/render/render_world_compiler.hpp`:

```cpp
#pragma once

#include <optional>
#include <vector>

#include <yaoray/render/render_world.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct RenderWorldCompileResult {
    std::optional<RenderWorld> world;
    std::vector<SceneDiagnostic> diagnostics;
};

RenderWorldCompileResult CompileSceneToRenderWorld(const SceneDescription& scene);

} // namespace yr
```

Create `src/render/render_world_compiler.cpp` with this initial implementation:

```cpp
#include <yaoray/render/render_world_compiler.hpp>

#include <cmath>
#include <utility>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

RenderWorldCamera CompileCamera(const CameraDescription& camera) {
    RenderWorldCamera compiled;
    compiled.origin = camera.position;
    compiled.forward = Normalize(camera.target - camera.position);
    if (LengthSquared(compiled.forward) == 0.0f) {
        compiled.forward = Vec3f{0.0f, 0.0f, -1.0f};
    }
    const Vec3f world_up{0.0f, 1.0f, 0.0f};
    compiled.right = Normalize(Cross(compiled.forward, world_up));
    if (LengthSquared(compiled.right) == 0.0f) {
        compiled.right = Vec3f{1.0f, 0.0f, 0.0f};
    }
    compiled.up = Normalize(Cross(compiled.right, compiled.forward));
    compiled.fov_y_radians = DegreesToRadians(camera.fov_y);
    compiled.aperture = camera.aperture;
    compiled.focus_distance = camera.focus_distance;
    return compiled;
}

void CopyRenderSettings(const SceneDescription& scene, RenderWorld& world) {
    world.settings.backend = scene.render.backend;
    world.settings.integrator = scene.render.integrator;
    world.settings.sampler = scene.render.sampler;
    world.settings.width = scene.render.width;
    world.settings.height = scene.render.height;
    world.settings.spp = scene.render.spp;
    world.settings.max_depth = scene.render.max_depth;
    world.settings.seed = scene.render.seed;
    world.settings.threads = scene.render.threads;
    world.settings.light_samples = scene.render.light_samples;
    world.settings.radiance_clamp = scene.render.radiance_clamp;
}

} // namespace

RenderWorldCompileResult CompileSceneToRenderWorld(const SceneDescription& scene) {
    RenderWorldCompileResult result;
    RenderWorld world;
    CopyRenderSettings(scene, world);

    if (!scene.camera.has_value()) {
        result.diagnostics.push_back(Error(scene, "camera", "missing camera"));
        return result;
    }
    world.camera = CompileCamera(scene.camera.value());

    result.world = std::move(world);
    return result;
}

} // namespace yr
```

- [ ] **Step 4: Run targeted tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: the new compiler settings and camera tests pass.

- [ ] **Step 5: Commit compiler API baseline**

Run:

```powershell
git add CMakeLists.txt include/yaoray/render/render_world_compiler.hpp src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: add render world compiler baseline"
```

Expected: commit succeeds and includes only compiler API, implementation, tests, and CMake entries.

## Task 4: Compile Scene Materials And Analytic Scene State

**Files:**
- Modify: `src/render/render_world_compiler.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add failing tests for environment, lights, and material lowering**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
YR_TEST(render_world_compiler_copies_constant_environment) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_EQ(result.world->environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(result.world->environment.radiance.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(result.world->environment.radiance.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(result.world->environment.radiance.z, 0.4, 1e-6);
    YR_EXPECT_NEAR(result.world->environment.strength, 2.0, 1e-6);
}

YR_TEST(render_world_compiler_copies_area_lights) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.area.size = {4.0f, 5.0f};
    light.area.radiance = yr::Color3f{6.0f, 7.0f, 8.0f};
    scene.lights.push_back(light);

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_EQ(result.world->lights.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->lights[0].type, yr::RenderLightType::RectArea);
    YR_EXPECT_NEAR(result.world->lights[0].position.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.world->lights[0].u.x, 4.0, 1e-6);
    YR_EXPECT_NEAR(result.world->lights[0].v.y, 5.0, 1e-6);
    YR_EXPECT_NEAR(result.world->lights[0].radiance.z, 8.0, 1e-6);
}

YR_TEST(render_world_compiler_lowers_scene_materials) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.materials.push_back(yr::MaterialDescription{
        "blue_glass",
        yr::MaterialKind::Dielectric,
        yr::Color3f{0.5f, 0.7f, 1.0f},
        yr::Color3f{0.0f, 0.0f, 0.0f},
        0.2f,
        0.04f,
        1.45f,
        true,
        yr::Color3f{0.4f, 0.6f, 1.0f},
        2.0f
    });

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_EQ(result.world->materials.size(), std::size_t{1});
    const yr::RenderMaterialRecord& material = result.world->materials[0];
    YR_EXPECT_EQ(material.model, yr::RenderMaterialModel::Dielectric);
    YR_EXPECT_NEAR(material.base_color_factor.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(material.roughness_factor, 0.2, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
    YR_EXPECT_NEAR(material.ior, 1.45, 1e-6);
    YR_EXPECT_TRUE(material.thin);
    YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_distance, 2.0, 1e-6);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: the three new `render_world_compiler_*` tests fail because environment, lights, and materials are not compiled yet.

- [ ] **Step 3: Implement environment, lights, and material lowering**

In `src/render/render_world_compiler.cpp`, add helpers:

```cpp
AssetMaterial ToAssetMaterial(const MaterialDescription& material) {
    AssetMaterial asset_material;
    asset_material.name = material.name;
    switch (material.type) {
        case MaterialKind::Diffuse:
            asset_material.model = AssetMaterialModel::DiffuseLegacy;
            break;
        case MaterialKind::Mirror:
            asset_material.model = AssetMaterialModel::MirrorLegacy;
            break;
        case MaterialKind::Metal:
            asset_material.model = AssetMaterialModel::MetalLegacy;
            break;
        case MaterialKind::Plastic:
            asset_material.model = AssetMaterialModel::PlasticLegacy;
            break;
        case MaterialKind::Dielectric:
            asset_material.model = AssetMaterialModel::DielectricLegacy;
            break;
    }
    asset_material.base_color_factor = Color4f{material.albedo.x, material.albedo.y, material.albedo.z, 1.0f};
    asset_material.emissive_factor = material.emission;
    asset_material.roughness_factor = material.roughness;
    asset_material.specular = material.specular;
    asset_material.ior = material.ior;
    asset_material.thin = material.thin;
    asset_material.absorption_color = material.absorption_color;
    asset_material.absorption_distance = material.absorption_distance;
    return asset_material;
}

RenderMaterialModel ToRenderMaterialModel(AssetMaterialModel model) {
    switch (model) {
        case AssetMaterialModel::PbrMetallicRoughness:
        case AssetMaterialModel::DiffuseLegacy:
        case AssetMaterialModel::EmissiveLegacy:
            return RenderMaterialModel::Diffuse;
        case AssetMaterialModel::MirrorLegacy:
            return RenderMaterialModel::Mirror;
        case AssetMaterialModel::MetalLegacy:
            return RenderMaterialModel::Metal;
        case AssetMaterialModel::PlasticLegacy:
            return RenderMaterialModel::Plastic;
        case AssetMaterialModel::DielectricLegacy:
            return RenderMaterialModel::Dielectric;
    }
    return RenderMaterialModel::Diffuse;
}

RenderMaterialRecord LowerAssetMaterial(const AssetMaterial& material) {
    RenderMaterialRecord record;
    record.model = ToRenderMaterialModel(material.model);
    record.base_color_factor = material.base_color_factor;
    record.emissive_factor = material.emissive_factor;
    record.metallic_factor = material.metallic_factor;
    record.roughness_factor = material.roughness_factor;
    record.specular = material.specular;
    record.ior = material.ior;
    record.thin = material.thin;
    record.absorption_color = material.absorption_color;
    record.absorption_distance = material.absorption_distance;
    return record;
}

void CompileMaterials(const SceneDescription& scene, RenderWorld& world) {
    for (const MaterialDescription& material : scene.materials) {
        const AssetMaterial asset_material = ToAssetMaterial(material);
        const RenderMaterialRecord record = LowerAssetMaterial(asset_material);
        world.materials.push_back(record);
    }
}

void CompileEnvironment(const SceneDescription& scene, RenderWorld& world) {
    world.environment.type = scene.environment.type;
    world.environment.radiance = scene.environment.radiance;
    world.environment.strength = scene.environment.strength;
    world.environment.rotation_radians = DegreesToRadians(scene.environment.rotation_degrees);
}

void CompileAreaLights(const SceneDescription& scene, RenderWorld& world) {
    for (const LightDescription& light : scene.lights) {
        if (light.type != LightKind::Area) {
            continue;
        }
        RenderLightRecord record;
        record.type = RenderLightType::RectArea;
        record.position = light.area.position;
        record.u = Vec3f{light.area.size[0], 0.0f, 0.0f};
        record.v = Vec3f{0.0f, light.area.size[1], 0.0f};
        record.radiance = light.area.radiance;
        world.lights.push_back(record);
    }
}
```

Call these helpers in `CompileSceneToRenderWorld()` after camera compilation:

```cpp
CompileMaterials(scene, world);
CompileEnvironment(scene, world);
CompileAreaLights(scene, world);
```

- [ ] **Step 4: Run tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: environment, light, and material lowering tests pass.

- [ ] **Step 5: Commit analytic scene state compilation**

Run:

```powershell
git add src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: compile render world scene state"
```

Expected: commit succeeds with compiler and test changes.

## Task 5: Compile Geometry Through Asset IR

**Files:**
- Modify: `src/render/render_world_compiler.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add failing geometry tests**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
YR_TEST(render_world_compiler_expands_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderWorld& world = result.world.value();
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.vertices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.indices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.primitives.size(), std::size_t{1});
    YR_EXPECT_EQ(world.primitives[0].material, 0);
    YR_EXPECT_NEAR(world.vertices[0].position.x, -0.5, 1e-6);
    YR_EXPECT_NEAR(world.vertices[1].position.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(world.vertices[2].position.y, 1.0, 1e-6);
}

YR_TEST(render_world_compiler_expands_inline_quad_asset) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{
        "panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            }
        }
    });
    scene.instances.push_back(yr::InstanceDescription{"panel", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderWorld& world = result.world.value();
    YR_EXPECT_EQ(world.vertices.size(), std::size_t{6});
    YR_EXPECT_EQ(world.indices.size(), std::size_t{6});
    YR_EXPECT_EQ(world.primitives.size(), std::size_t{2});
    YR_EXPECT_EQ(world.primitives[0].material, 0);
    YR_EXPECT_EQ(world.primitives[1].material, 0);
    YR_EXPECT_NEAR(world.vertices[2].position.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(world.vertices[5].position.y, 1.0, 1e-6);
}

YR_TEST(render_world_compiler_applies_instance_transform) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderVertex& v0 = result.world->vertices[0];
    const yr::RenderVertex& v1 = result.world->vertices[1];
    const yr::RenderVertex& v2 = result.world->vertices[2];
    YR_EXPECT_NEAR(v0.position.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(v0.position.y, 1.0, 1e-5);
    YR_EXPECT_NEAR(v0.position.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(v1.position.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(v1.position.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(v2.position.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(v2.position.y, 2.0, 1e-5);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: the new geometry tests fail because `CompileSceneToRenderWorld()` does not compile assets or instances.

- [ ] **Step 3: Implement transforms and default material handling**

In `src/render/render_world_compiler.cpp`, add:

```cpp
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
```

Add helpers:

```cpp
constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

Point3f ApplyTransform(Point3f point, const TransformDescription& transform) {
    Vec3f value{
        point.x * transform.scale.x,
        point.y * transform.scale.y,
        point.z * transform.scale.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Point3f{
        value.x + transform.translate.x,
        value.y + transform.translate.y,
        value.z + transform.translate.z
    };
}

RenderMaterialRecord MakeDefaultMaterial() {
    return RenderMaterialRecord{};
}

int AddDefaultMaterial(RenderWorld& world) {
    const int material_index = static_cast<int>(world.materials.size());
    world.materials.push_back(MakeDefaultMaterial());
    return material_index;
}

std::unordered_map<std::string, const AssetDescription*> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, const AssetDescription*> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, &asset);
    }
    return assets;
}

std::unordered_map<std::string, int> BuildMaterialMap(const SceneDescription& scene) {
    std::unordered_map<std::string, int> materials;
    for (std::size_t index = 0; index < scene.materials.size(); ++index) {
        materials.emplace(scene.materials[index].name, static_cast<int>(index));
    }
    return materials;
}

int ResolveInstanceMaterial(
    const SceneDescription& scene,
    const InstanceDescription& instance,
    const std::unordered_map<std::string, int>& materials,
    RenderWorld& world,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (instance.material.empty()) {
        return AddDefaultMaterial(world);
    }
    const auto found = materials.find(instance.material);
    if (found == materials.end()) {
        diagnostics.push_back(Error(scene, "instances.material", "references unknown material"));
        return -1;
    }
    return found->second;
}
```

Reuse the `RotateX`, `RotateY`, and `RotateZ` helper bodies from `src/render/scene_compiler.cpp`.

- [ ] **Step 4: Implement triangle appending from semantic assets**

Add:

```cpp
void AppendTriangle(
    const SceneDescription& scene,
    RenderWorld& world,
    Point3f p0,
    Point3f p1,
    Point3f p2,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const Vec3f normal = Cross(p1 - p0, p2 - p0);
    if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
        diagnostics.push_back(Error(scene, "assets.quads", "quad produces degenerate triangle"));
        return;
    }

    const std::uint32_t first_vertex = static_cast<std::uint32_t>(world.vertices.size());
    const Vec3f normalized = Normalize(normal);
    world.vertices.push_back(RenderVertex{p0, normalized, normalized});
    world.vertices.push_back(RenderVertex{p1, normalized, normalized});
    world.vertices.push_back(RenderVertex{p2, normalized, normalized});

    const std::uint32_t first_index = static_cast<std::uint32_t>(world.indices.size());
    world.indices.push_back(first_vertex + 0);
    world.indices.push_back(first_vertex + 1);
    world.indices.push_back(first_vertex + 2);

    RenderPrimitive primitive;
    primitive.first_index = first_index;
    primitive.index_count = 3;
    primitive.material = material_index;
    primitive.bounds = Union(Union(Union(Bounds3f{}, p0), p1), p2);
    world.primitives.push_back(primitive);
}

void AppendBuiltinTriangle(RenderWorld& world, const SceneDescription& scene, const TransformDescription& transform, int material_index, std::vector<SceneDiagnostic>& diagnostics) {
    const Point3f p0 = ApplyTransform(Point3f{-0.5f, 0.0f, 0.0f}, transform);
    const Point3f p1 = ApplyTransform(Point3f{0.5f, 0.0f, 0.0f}, transform);
    const Point3f p2 = ApplyTransform(Point3f{0.0f, 1.0f, 0.0f}, transform);
    AppendTriangle(scene, world, p0, p1, p2, material_index, diagnostics);
}

void AppendInlineQuadAsset(
    const SceneDescription& scene,
    RenderWorld& world,
    const AssetDescription& asset,
    const TransformDescription& transform,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    for (const QuadDescription& quad : asset.quads) {
        const Point3f p0 = ApplyTransform(quad.p0, transform);
        const Point3f p1 = ApplyTransform(quad.p1, transform);
        const Point3f p2 = ApplyTransform(quad.p2, transform);
        const Point3f p3 = ApplyTransform(quad.p3, transform);
        AppendTriangle(scene, world, p0, p1, p2, material_index, diagnostics);
        AppendTriangle(scene, world, p0, p2, p3, material_index, diagnostics);
    }
}
```

- [ ] **Step 5: Compile instances**

Add:

```cpp
void CompileInstances(
    const SceneDescription& scene,
    RenderWorld& world,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::unordered_map<std::string, const AssetDescription*> assets = BuildAssetMap(scene);
    const std::unordered_map<std::string, int> materials = BuildMaterialMap(scene);

    for (const InstanceDescription& instance : scene.instances) {
        const auto asset = assets.find(instance.asset);
        if (asset == assets.end()) {
            diagnostics.push_back(Error(scene, "instances.asset", "references unknown asset"));
            continue;
        }

        std::optional<int> material_index;
        if (!instance.material.empty()) {
            const int resolved_material = ResolveInstanceMaterial(scene, instance, materials, world, diagnostics);
            if (resolved_material < 0) {
                continue;
            }
            material_index = resolved_material;
        }

        const AssetDescription& asset_description = *asset->second;
        const std::string asset_path_string = asset_description.path.generic_string();
        if (!asset_description.quads.empty()) {
            const int bound_material = material_index.value_or(AddDefaultMaterial(world));
            AppendInlineQuadAsset(scene, world, asset_description, instance.transform, bound_material, diagnostics);
        } else if (asset_path_string == "builtin:triangle") {
            const int bound_material = material_index.value_or(AddDefaultMaterial(world));
            AppendBuiltinTriangle(world, scene, instance.transform, bound_material, diagnostics);
        } else {
            diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented in RenderWorld compiler yet: " + asset_path_string));
        }
    }
}
```

Call `CompileInstances(scene, world, result.diagnostics);` before assigning `result.world`.

- [ ] **Step 6: Run geometry tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: builtin and inline geometry tests pass. Existing external asset compile tests may still pass through old `CompileScene()` because the CLI and legacy compiler have not changed.

- [ ] **Step 7: Commit geometry compilation**

Run:

```powershell
git add src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: compile render world geometry"
```

Expected: commit succeeds with compiler and test changes.

## Task 6: Add RenderWorld To RenderScene Adapter

**Files:**
- Create: `include/yaoray/render/render_world_adapter.hpp`
- Create: `src/render/render_world_adapter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Write failing adapter tests**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
#include <yaoray/render/render_world_adapter.hpp>

YR_TEST(render_world_adapter_produces_legacy_render_scene_for_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);
    YR_EXPECT_TRUE(result.world.has_value());

    const yr::RenderScene legacy = yr::ToLegacyRenderScene(result.world.value());

    YR_EXPECT_EQ(legacy.backend, scene.render.backend);
    YR_EXPECT_EQ(legacy.integrator, scene.render.integrator);
    YR_EXPECT_EQ(legacy.width, scene.render.width);
    YR_EXPECT_EQ(legacy.height, scene.render.height);
    YR_EXPECT_EQ(legacy.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(legacy.triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(legacy.triangles[0].material_index, 0);
    YR_EXPECT_EQ(legacy.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(legacy.bvh.triangle_indices.size(), std::size_t{1});
}
```

- [ ] **Step 2: Add adapter source to CMake and verify failure**

Modify `yaoray_render` sources:

```cmake
    src/render/render_world_adapter.cpp
```

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `render_world_adapter.hpp` does not exist.

- [ ] **Step 3: Implement adapter header**

Create `include/yaoray/render/render_world_adapter.hpp`:

```cpp
#pragma once

#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/render_world.hpp>

namespace yr {

RenderScene ToLegacyRenderScene(const RenderWorld& world);

} // namespace yr
```

- [ ] **Step 4: Implement adapter body**

Create `src/render/render_world_adapter.cpp`:

```cpp
#include <yaoray/render/render_world_adapter.hpp>

#include <yaoray/render/bvh.hpp>

namespace yr {
namespace {

MaterialKind ToLegacyMaterialKind(RenderMaterialModel model) {
    switch (model) {
        case RenderMaterialModel::Diffuse:
            return MaterialKind::Diffuse;
        case RenderMaterialModel::Mirror:
            return MaterialKind::Mirror;
        case RenderMaterialModel::Metal:
            return MaterialKind::Metal;
        case RenderMaterialModel::Plastic:
            return MaterialKind::Plastic;
        case RenderMaterialModel::Dielectric:
            return MaterialKind::Dielectric;
    }
    return MaterialKind::Diffuse;
}

RenderMaterial ToLegacyMaterial(const RenderMaterialRecord& material) {
    RenderMaterial result;
    result.type = ToLegacyMaterialKind(material.model);
    result.albedo = Color3f{
        material.base_color_factor.x,
        material.base_color_factor.y,
        material.base_color_factor.z
    };
    result.emission = material.emissive_factor;
    result.roughness = material.roughness_factor;
    result.specular = material.specular;
    result.albedo_texture = material.base_color_texture;
    result.ior = material.ior;
    result.thin = material.thin;
    result.absorption_color = material.absorption_color;
    result.absorption_distance = material.absorption_distance;
    return result;
}

RenderTriangle ToLegacyTriangle(const RenderWorld& world, const RenderPrimitive& primitive) {
    const std::uint32_t i0 = world.indices[primitive.first_index + 0];
    const std::uint32_t i1 = world.indices[primitive.first_index + 1];
    const std::uint32_t i2 = world.indices[primitive.first_index + 2];
    const RenderVertex& v0 = world.vertices[i0];
    const RenderVertex& v1 = world.vertices[i1];
    const RenderVertex& v2 = world.vertices[i2];

    RenderTriangle triangle;
    triangle.p0 = v0.position;
    triangle.p1 = v1.position;
    triangle.p2 = v2.position;
    triangle.normal = Normalize(Cross(triangle.p1 - triangle.p0, triangle.p2 - triangle.p0));
    triangle.material_index = primitive.material;
    triangle.uv0 = v0.uv0;
    triangle.uv1 = v1.uv0;
    triangle.uv2 = v2.uv0;
    triangle.has_uv = (v0.flags & 1u) != 0u && (v1.flags & 1u) != 0u && (v2.flags & 1u) != 0u;
    triangle.n0 = v0.shading_normal;
    triangle.n1 = v1.shading_normal;
    triangle.n2 = v2.shading_normal;
    triangle.has_vertex_normals = (v0.flags & 2u) != 0u && (v1.flags & 2u) != 0u && (v2.flags & 2u) != 0u;
    return triangle;
}

} // namespace

RenderScene ToLegacyRenderScene(const RenderWorld& world) {
    RenderScene scene;
    scene.backend = world.settings.backend;
    scene.integrator = world.settings.integrator;
    scene.sampler = world.settings.sampler;
    scene.width = world.settings.width;
    scene.height = world.settings.height;
    scene.spp = world.settings.spp;
    scene.max_depth = world.settings.max_depth;
    scene.seed = world.settings.seed;
    scene.threads = world.settings.threads;
    scene.light_samples = world.settings.light_samples;
    scene.radiance_clamp = world.settings.radiance_clamp;

    scene.camera.origin = world.camera.origin;
    scene.camera.forward = world.camera.forward;
    scene.camera.right = world.camera.right;
    scene.camera.up = world.camera.up;
    scene.camera.fov_y_radians = world.camera.fov_y_radians;
    scene.camera.aperture = world.camera.aperture;
    scene.camera.focus_distance = world.camera.focus_distance;

    scene.environment.type = world.environment.type;
    scene.environment.radiance = world.environment.radiance;
    scene.environment.strength = world.environment.strength;
    scene.environment.rotation_radians = world.environment.rotation_radians;
    scene.environment.texture_index = world.environment.texture;
    scene.environment.distribution_index = world.environment.distribution;
    scene.environment_distributions = world.environment_distributions;

    for (const RenderMaterialRecord& material : world.materials) {
        scene.materials.push_back(ToLegacyMaterial(material));
    }

    for (const RenderLightRecord& light : world.lights) {
        if (light.type != RenderLightType::RectArea) {
            continue;
        }
        scene.area_lights.push_back(RenderAreaLight{
            light.position,
            Length(light.u),
            Length(light.v),
            light.radiance
        });
    }

    for (const RenderPrimitive& primitive : world.primitives) {
        if (primitive.index_count != 3) {
            continue;
        }
        scene.triangles.push_back(ToLegacyTriangle(world, primitive));
    }

    const BvhBuildResult bvh_result = BuildBvh(scene.triangles);
    scene.bvh = bvh_result.bvh;
    return scene;
}

} // namespace yr
```

If `Length()` for `Vec3f` is not available, use the existing vector helper that returns magnitude. Do not store area-light width/height in `flags`.

- [ ] **Step 5: Run adapter tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: adapter tests pass. If the `Bounds3f` or vector helper names differ, make the smallest local adjustment to use existing APIs.

- [ ] **Step 6: Commit adapter**

Run:

```powershell
git add CMakeLists.txt include/yaoray/render/render_world_adapter.hpp src/render/render_world_adapter.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: adapt render world to legacy render scene"
```

Expected: commit succeeds with adapter files and tests.

## Task 7: Bridge Current Imported Assets Through RenderWorld

**Files:**
- Modify: `src/render/render_world_compiler.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add failing imported bridge tests**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

} // namespace

YR_TEST(render_world_compiler_bridges_obj_asset) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_EQ(result.world->primitives.size(), std::size_t{2});
    YR_EXPECT_EQ(result.world->indices.size(), std::size_t{6});
    YR_EXPECT_EQ(result.world->materials.size(), std::size_t{1});
}

YR_TEST(render_world_compiler_bridges_gltf_asset_with_texture) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"textured", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_TRUE(!result.world->primitives.empty());
    YR_EXPECT_EQ(result.world->materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->images.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->samplers.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->textures.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->materials[0].base_color_texture, 0);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: imported bridge tests fail with `asset import not implemented in RenderWorld compiler yet`.

- [ ] **Step 3: Add current importer includes and texture loading**

In `src/render/render_world_compiler.cpp`, include:

```cpp
#include <yaoray/assets/gltf_loader.hpp>
#include <yaoray/assets/imported_asset.hpp>
#include <yaoray/assets/obj_loader.hpp>
#include <yaoray/render/texture.hpp>
```

Add extension helpers:

```cpp
bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
}

bool HasGltfExtension(const std::filesystem::path& path) {
    return path.extension() == ".gltf" || path.extension() == ".glb";
}
```

Add texture table helper:

```cpp
int AddRenderTexture(RenderWorld& world, RenderTexture texture) {
    RenderImageRecord image;
    image.width = texture.width;
    image.height = texture.height;
    image.texel_offset = static_cast<std::uint32_t>(world.texels.size());
    image.texel_count = static_cast<std::uint32_t>(texture.texels.size());
    for (Color3f texel : texture.texels) {
        world.texels.push_back(Color4f{texel.x, texel.y, texel.z, 1.0f});
    }
    const int image_index = static_cast<int>(world.images.size());
    world.images.push_back(image);

    RenderSamplerRecord sampler;
    sampler.wrap_s = texture.wrap_s;
    sampler.wrap_t = texture.wrap_t;
    sampler.min_filter = texture.filter;
    sampler.mag_filter = texture.filter;
    const int sampler_index = static_cast<int>(world.samplers.size());
    world.samplers.push_back(sampler);

    RenderTextureRecord texture_record;
    texture_record.image = image_index;
    texture_record.sampler = sampler_index;
    texture_record.color_space = RenderTextureColorSpace::Linear;
    const int texture_index = static_cast<int>(world.textures.size());
    world.textures.push_back(texture_record);
    return texture_index;
}
```

- [ ] **Step 4: Add imported material lowering bridge**

Add:

```cpp
RenderMaterialRecord CompileImportedMaterial(
    const SceneDescription& scene,
    RenderWorld& world,
    const ImportedMaterial& material,
    std::vector<SceneDiagnostic>& diagnostics
) {
    RenderMaterialRecord record;
    record.model = ToRenderMaterialModel(material.type);
    record.base_color_factor = Color4f{material.diffuse.x, material.diffuse.y, material.diffuse.z, 1.0f};
    record.emissive_factor = material.emission;
    record.roughness_factor = material.roughness;
    record.specular = material.specular;

    if (material.has_diffuse_texture) {
        TextureLoadResult load = LoadPngTexture(material.diffuse_texture_path);
        if (!load.ok) {
            diagnostics.push_back(Error(scene, "assets.path", load.error));
            return record;
        }
        load.texture.wrap_s = material.diffuse_texture_wrap_s;
        load.texture.wrap_t = material.diffuse_texture_wrap_t;
        load.texture.filter = TextureFilter::Bilinear;
        record.base_color_texture = AddRenderTexture(world, std::move(load.texture));
    }

    return record;
}
```

- [ ] **Step 5: Add imported triangle bridge**

Add:

```cpp
Vec3f ApplyNormalTransform(Vec3f normal, const TransformDescription& transform) {
    Vec3f value{
        transform.scale.x != 0.0f ? normal.x / transform.scale.x : normal.x,
        transform.scale.y != 0.0f ? normal.y / transform.scale.y : normal.y,
        transform.scale.z != 0.0f ? normal.z / transform.scale.z : normal.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Normalize(value);
}

void AppendImportedTriangle(
    RenderWorld& world,
    const ImportedTriangle& source,
    const TransformDescription& transform,
    int material_index
) {
    const Point3f p0 = ApplyTransform(source.p0, transform);
    const Point3f p1 = ApplyTransform(source.p1, transform);
    const Point3f p2 = ApplyTransform(source.p2, transform);
    const Vec3f geometric_normal = Normalize(Cross(p1 - p0, p2 - p0));
    const Vec3f n0 = ApplyNormalTransform(source.n0, transform);
    const Vec3f n1 = ApplyNormalTransform(source.n1, transform);
    const Vec3f n2 = ApplyNormalTransform(source.n2, transform);

    const std::uint32_t first_vertex = static_cast<std::uint32_t>(world.vertices.size());
    std::uint32_t flags = 0;
    if (source.has_uv) {
        flags |= 1u;
    }
    if (source.has_vertex_normals) {
        flags |= 2u;
    }
    world.vertices.push_back(RenderVertex{p0, geometric_normal, source.has_vertex_normals ? n0 : geometric_normal, source.uv0, flags});
    world.vertices.push_back(RenderVertex{p1, geometric_normal, source.has_vertex_normals ? n1 : geometric_normal, source.uv1, flags});
    world.vertices.push_back(RenderVertex{p2, geometric_normal, source.has_vertex_normals ? n2 : geometric_normal, source.uv2, flags});

    const std::uint32_t first_index = static_cast<std::uint32_t>(world.indices.size());
    world.indices.push_back(first_vertex + 0);
    world.indices.push_back(first_vertex + 1);
    world.indices.push_back(first_vertex + 2);

    RenderPrimitive primitive;
    primitive.first_index = first_index;
    primitive.index_count = 3;
    primitive.material = material_index;
    world.primitives.push_back(primitive);
}
```

- [ ] **Step 6: Load OBJ/glTF with current importers**

Add:

```cpp
void AppendImportedAsset(
    const SceneDescription& scene,
    RenderWorld& world,
    const std::filesystem::path& asset_path,
    const TransformDescription& transform,
    int override_material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    AssetLoadResult load_result = HasObjExtension(asset_path)
        ? LoadObjMesh(asset_path)
        : LoadGltfMesh(asset_path);

    for (const std::string& warning : load_result.warnings) {
        diagnostics.push_back(SceneDiagnostic{DiagnosticSeverity::Warning, scene.source_path, "assets.path", warning});
    }
    for (const std::string& error_message : load_result.errors) {
        diagnostics.push_back(Error(scene, "assets.path", error_message));
    }
    if (!load_result.mesh.has_value()) {
        diagnostics.push_back(Error(scene, "assets.path", "asset loader returned no mesh: " + asset_path.generic_string()));
        return;
    }

    std::vector<int> material_indices;
    if (override_material_index >= 0) {
        material_indices.push_back(override_material_index);
    } else {
        for (const ImportedMaterial& material : load_result.mesh->materials) {
            const int material_index = static_cast<int>(world.materials.size());
            world.materials.push_back(CompileImportedMaterial(scene, world, material, diagnostics));
            material_indices.push_back(material_index);
        }
        if (material_indices.empty()) {
            material_indices.push_back(AddDefaultMaterial(world));
        }
    }

    for (const ImportedTriangle& triangle : load_result.mesh->triangles) {
        int material_index = override_material_index;
        if (material_index < 0 && triangle.material_index >= 0 &&
            static_cast<std::size_t>(triangle.material_index) < material_indices.size()) {
            material_index = material_indices[static_cast<std::size_t>(triangle.material_index)];
        }
        if (material_index < 0) {
            material_index = material_indices[0];
        }
        AppendImportedTriangle(world, triangle, transform, material_index);
    }
}
```

Update the external asset branch in `CompileInstances()`:

```cpp
        } else if (HasObjExtension(asset_description.path) || HasGltfExtension(asset_description.path)) {
            AppendImportedAsset(scene, world, asset_description.path, instance.transform, material_index.value_or(-1), diagnostics);
        } else {
            diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path_string));
        }
```

- [ ] **Step 7: Run imported bridge tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: imported bridge tests pass. Existing scene compiler tests should still pass because `CompileScene()` has not been changed.

- [ ] **Step 8: Commit imported bridge**

Run:

```powershell
git add src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: bridge imported assets into render world"
```

Expected: commit succeeds with imported bridge changes.

## Task 8: Compile HDRI Environment Into RenderWorld

**Files:**
- Modify: `src/render/render_world_compiler.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add failing HDRI compile test**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
YR_TEST(render_world_compiler_compiles_hdri_environment) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.path = FixturePath("assets/tiny_env.hdr");
    scene.environment.strength = 1.5f;
    scene.environment.rotation_degrees = 90.0f;

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    const yr::RenderWorld& world = result.world.value();
    YR_EXPECT_EQ(world.environment.type, yr::EnvironmentKind::Hdri);
    YR_EXPECT_NEAR(world.environment.strength, 1.5, 1e-6);
    YR_EXPECT_NEAR(world.environment.rotation_radians, 1.57079637, 1e-5);
    YR_EXPECT_EQ(world.environment.texture, 0);
    YR_EXPECT_EQ(world.environment.distribution, 0);
    YR_EXPECT_EQ(world.images.size(), std::size_t{1});
    YR_EXPECT_EQ(world.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(world.environment_distributions.size(), std::size_t{1});
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: HDRI test fails because the compiler only copies environment fields.

- [ ] **Step 3: Implement HDRI texture/distribution compilation**

Change `CompileEnvironment()` to:

```cpp
void CompileEnvironment(
    const SceneDescription& scene,
    RenderWorld& world,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        world.environment.type = scene.environment.type;
        world.environment.radiance = scene.environment.radiance;
        world.environment.strength = scene.environment.strength;
        return;
    }

    if (scene.environment.type != EnvironmentKind::Hdri) {
        return;
    }
    if (scene.environment.path.empty()) {
        diagnostics.push_back(Error(scene, "environment.path", "must not be empty for hdri environment"));
        return;
    }

    TextureLoadResult load = LoadHdrTexture(scene.environment.path);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "environment.path", load.error));
        return;
    }

    RenderTexture source_texture = std::move(load.texture);
    const RenderEnvironmentDistribution distribution = BuildEnvironmentDistribution(source_texture);
    const int texture_index = AddRenderTexture(world, std::move(source_texture));
    const int distribution_index = static_cast<int>(world.environment_distributions.size());
    world.environment_distributions.push_back(distribution);

    world.environment.type = EnvironmentKind::Hdri;
    world.environment.strength = scene.environment.strength;
    world.environment.rotation_radians = DegreesToRadians(scene.environment.rotation_degrees);
    world.environment.texture = texture_index;
    world.environment.distribution = distribution_index;
}
```

Update the call in `CompileSceneToRenderWorld()`:

```cpp
CompileEnvironment(scene, world, result.diagnostics);
```

- [ ] **Step 4: Run HDRI tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: HDRI compile test passes.

- [ ] **Step 5: Commit HDRI RenderWorld compilation**

Run:

```powershell
git add src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: compile hdri environment into render world"
```

Expected: commit succeeds with HDRI compiler changes.

## Task 9: Move Backend API To RenderWorld

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `src/backends/backend.cpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Update backend tests to build RenderWorld**

In `tests/backend_tests.cpp`, replace the `MakeBackendTriangleScene()` helper with:

```cpp
#include <yaoray/render/render_world.hpp>

yr::RenderWorld MakeBackendTriangleWorld(int width = 4, int height = 3) {
    yr::RenderWorld world;
    world.settings.backend = yr::RenderBackendKind::Cpu;
    world.settings.width = width;
    world.settings.height = height;
    world.settings.spp = 1;
    world.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    world.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    world.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    world.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    world.camera.fov_y_radians = 1.04719758f;
    world.environment.type = yr::EnvironmentKind::Constant;
    world.environment.radiance = yr::Color3f{0.05f, 0.10f, 0.15f};
    world.environment.strength = 1.0f;

    yr::RenderMaterialRecord material;
    material.model = yr::RenderMaterialModel::Diffuse;
    material.base_color_factor = yr::Color4f{1.0f, 0.2f, 0.1f, 1.0f};
    world.materials.push_back(material);

    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    world.vertices.push_back(yr::RenderVertex{yr::Point3f{-0.5f, -0.5f, 0.0f}, normal, normal});
    world.vertices.push_back(yr::RenderVertex{yr::Point3f{0.5f, -0.5f, 0.0f}, normal, normal});
    world.vertices.push_back(yr::RenderVertex{yr::Point3f{0.0f, 0.5f, 0.0f}, normal, normal});
    world.indices = {0, 1, 2};
    yr::RenderPrimitive primitive;
    primitive.first_index = 0;
    primitive.index_count = 3;
    primitive.material = 0;
    world.primitives.push_back(primitive);
    return world;
}
```

Change each backend render call from:

```cpp
const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});
```

to:

```cpp
const yr::RenderResult result = backend->Render(world, yr::RenderRequest{});
```

For the CUDA test, set `world.settings.backend = yr::RenderBackendKind::Cuda;`.

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `RenderBackend::Render()` still accepts `RenderScene`.

- [ ] **Step 3: Change public backend API**

In `include/yaoray/backends/backend.hpp`, replace the `render_scene.hpp` include with:

```cpp
#include <yaoray/render/render_world.hpp>
```

Change:

```cpp
virtual RenderResult Render(const RenderScene& scene, const RenderRequest& request) = 0;
```

to:

```cpp
virtual RenderResult Render(const RenderWorld& world, const RenderRequest& request) = 0;
```

- [ ] **Step 4: Update CUDA stub**

In `src/backends/backend.cpp`, change the stub signature:

```cpp
RenderResult Render(const RenderWorld& world, const RenderRequest& request) override {
    (void)world;
    (void)request;

    RenderResult result;
    result.ok = false;
    result.error = "CUDA backend not implemented yet.";
    return result;
}
```

- [ ] **Step 5: Update CPU backend with adapter**

In `include/yaoray/backends/cpu/cpu_debug_backend.hpp`, change:

```cpp
RenderResult Render(const RenderWorld& world, const RenderRequest& request) override;
```

In `src/backends/cpu/cpu_debug_backend.cpp`, include the adapter:

```cpp
#include <yaoray/render/render_world_adapter.hpp>
```

Change the render function:

```cpp
RenderResult CpuDebugBackend::Render(const RenderWorld& world, const RenderRequest& request) {
    (void)request;

    const RenderScene scene = ToLegacyRenderScene(world);

    RenderResult result;
    result.ok = true;

    if (scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(scene);
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}
```

- [ ] **Step 6: Run backend tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: backend tests pass through the new `RenderWorld` API.

- [ ] **Step 7: Commit backend API migration**

Run:

```powershell
git add include/yaoray/backends/backend.hpp src/backends/backend.cpp include/yaoray/backends/cpu/cpu_debug_backend.hpp src/backends/cpu/cpu_debug_backend.cpp tests/backend_tests.cpp
git commit -m "feat: render backends consume render world"
```

Expected: commit succeeds with backend API and tests.

## Task 10: Move CLI Render Path To RenderWorld Compiler

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add legacy adapter equivalence test for imported textured asset**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
YR_TEST(render_world_adapter_preserves_textured_gltf_legacy_shape) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"textured", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);
    YR_EXPECT_TRUE(result.world.has_value());

    const yr::RenderScene legacy = yr::ToLegacyRenderScene(result.world.value());

    YR_EXPECT_TRUE(!legacy.triangles.empty());
    YR_EXPECT_TRUE(legacy.triangles[0].has_uv);
    YR_EXPECT_EQ(legacy.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(legacy.materials[0].albedo_texture, 0);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: this test fails until `ToLegacyRenderScene()` reconstructs `RenderTexture` entries from RenderWorld image/sampler/texel tables.

- [ ] **Step 3: Preserve textures in adapter**

In `src/render/render_world_adapter.cpp`, add texture conversion:

```cpp
RenderTexture ToLegacyTexture(const RenderWorld& world, const RenderTextureRecord& texture_record) {
    RenderTexture texture;
    if (texture_record.image < 0 || static_cast<std::size_t>(texture_record.image) >= world.images.size()) {
        return texture;
    }
    const RenderImageRecord& image = world.images[static_cast<std::size_t>(texture_record.image)];
    texture.width = image.width;
    texture.height = image.height;

    if (texture_record.sampler >= 0 && static_cast<std::size_t>(texture_record.sampler) < world.samplers.size()) {
        const RenderSamplerRecord& sampler = world.samplers[static_cast<std::size_t>(texture_record.sampler)];
        texture.wrap_s = sampler.wrap_s;
        texture.wrap_t = sampler.wrap_t;
        texture.filter = sampler.mag_filter;
    }

    texture.texels.reserve(image.texel_count);
    for (std::uint32_t i = 0; i < image.texel_count; ++i) {
        const std::uint32_t texel_index = image.texel_offset + i;
        if (texel_index >= world.texels.size()) {
            break;
        }
        const Color4f texel = world.texels[texel_index];
        texture.texels.push_back(Color3f{texel.x, texel.y, texel.z});
    }
    return texture;
}
```

Call it inside `ToLegacyRenderScene()` before material conversion:

```cpp
for (const RenderTextureRecord& texture : world.textures) {
    scene.textures.push_back(ToLegacyTexture(world, texture));
}
```

- [ ] **Step 4: Update CLI to compile RenderWorld**

In `src/app/main.cpp`, replace:

```cpp
#include <yaoray/render/scene_compiler.hpp>
```

with:

```cpp
#include <yaoray/render/render_world_compiler.hpp>
```

Replace compile block:

```cpp
const yr::SceneCompileResult compile_result = yr::CompileScene(scene);
if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.scene.has_value()) {
    std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
    return 1;
}

const yr::RenderScene& render_scene = compile_result.scene.value();
```

with:

```cpp
const yr::RenderWorldCompileResult compile_result = yr::CompileSceneToRenderWorld(scene);
if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.world.has_value()) {
    std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
    return 1;
}

const yr::RenderWorld& render_world = compile_result.world.value();
```

Replace CLI stats:

```cpp
std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
std::cout << "Integrator: " << yr::RenderIntegratorName(render_scene.integrator) << '\n';
std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
std::cout << "BVH nodes: " << render_scene.bvh.nodes.size() << '\n';
std::cout << "BVH max depth: " << render_scene.bvh.max_depth << '\n';
```

with:

```cpp
std::cout << "Requested backend: " << yr::RenderBackendName(render_world.settings.backend) << '\n';
std::cout << "Integrator: " << yr::RenderIntegratorName(render_world.settings.integrator) << '\n';
std::cout << "Compiled triangles: " << render_world.primitives.size() << '\n';
std::cout << "BVH nodes: " << render_world.bvh.nodes.size() << '\n';
std::cout << "BVH max depth: " << render_world.bvh.max_depth << '\n';
```

Replace backend creation and render call:

```cpp
const auto backend = yr::CreateRenderBackend(render_world.settings.backend);
...
const yr::RenderResult render_result = backend->Render(render_world, yr::RenderRequest{});
```

- [ ] **Step 5: Build and run CLI smoke tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all CLI tests still pass. If BVH nodes print as zero, add BVH construction to `CompileSceneToRenderWorld()` by converting `RenderWorld` primitives to temporary `RenderTriangle` values and calling `BuildBvh()` before assigning `result.world`.

- [ ] **Step 6: Commit CLI migration**

Run:

```powershell
git add src/app/main.cpp src/render/render_world_adapter.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: route cli renders through render world"
```

Expected: commit succeeds with CLI and adapter changes.

## Task 11: Build BVH In RenderWorld Compiler

**Files:**
- Modify: `src/render/render_world_compiler.cpp`
- Modify: `tests/render_world_compiler_tests.cpp`

- [ ] **Step 1: Add failing BVH tests**

Append to `tests/render_world_compiler_tests.cpp`:

```cpp
YR_TEST(render_world_compiler_builds_empty_bvh_for_empty_scene) {
    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(MakeBaseWorldScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_TRUE(result.world->bvh.nodes.empty());
    YR_EXPECT_TRUE(result.world->bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.world->bvh.max_depth, 0);
}

YR_TEST(render_world_compiler_builds_bvh_for_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseWorldScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::RenderWorldCompileResult result = yr::CompileSceneToRenderWorld(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.world.has_value());
    YR_EXPECT_EQ(result.world->primitives.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.world->bvh.triangle_indices[0], 0);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: builtin BVH test fails if BVH is not compiled into `RenderWorld`.

- [ ] **Step 3: Add RenderWorld BVH construction**

In `src/render/render_world_compiler.cpp`, include:

```cpp
#include <yaoray/render/render_world_adapter.hpp>
```

At the end of `CompileSceneToRenderWorld()`, before assigning `result.world`, add:

```cpp
if (HasSceneErrors(result.diagnostics)) {
    return result;
}

const RenderScene legacy_for_bvh = ToLegacyRenderScene(world);
world.bvh = legacy_for_bvh.bvh;
```

This uses the transitional adapter in Slice 1. It is acceptable because Slice 3 removes backend dependence on old `RenderScene`; the Slice 1 goal is a working vertical render path.

- [ ] **Step 4: Run BVH tests**

Run:

```powershell
cmake --build build --config Debug
build\Debug\yaoray_tests.exe
```

Expected: BVH tests pass.

- [ ] **Step 5: Commit RenderWorld BVH compilation**

Run:

```powershell
git add src/render/render_world_compiler.cpp tests/render_world_compiler_tests.cpp
git commit -m "feat: build bvh for render world"
```

Expected: commit succeeds with BVH compiler changes.

## Task 12: Final Verification And Documentation Touch

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README status wording**

In `README.md`, add one bullet to the Current Status list after the render scene compilation or backend dispatch bullets:

```markdown
- first Asset IR -> GPU-ready Render IR -> backend dispatch slice, with CPU rendering routed through RenderWorld and a transitional legacy adapter
```

In the limitations paragraph, add:

```markdown
The first Render IR slice still uses a transitional adapter for CPU internals; native CPU RenderWorld traversal, full importer-to-Asset-IR migration, and CUDA packing remain follow-up architecture slices.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, add a short paragraph after the two-layer renderer architecture introduction:

```markdown
The next architecture generation introduces an explicit `Asset IR -> Render IR -> Backend` path. Asset IR preserves importer and authoring semantics, while Render IR is the flat, handle-based contract that CPU and future GPU backends consume. The first slice routes CLI rendering through RenderWorld while the CPU path tracer still uses a transitional RenderWorld-to-RenderScene adapter internally.
```

- [ ] **Step 3: Run full verification**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed, 0 tests failed out of 17
```

The count may increase only if CTest entries are added. Unit-test additions inside `yaoray_tests` do not change the CTest count.

- [ ] **Step 4: Run focused source-boundary checks**

Run:

```powershell
rg -n "#include <yaoray/(scene|assets)/" include\yaoray\backends src\backends
```

Expected: no matches. During Slice 1, backends may include `render_world.hpp` and `render_world_adapter.hpp`; they must not include scene parser or asset importer headers directly.

Run:

```powershell
rg -n "RenderBackend::Render|backend->Render|CompileSceneToRenderWorld|ToLegacyRenderScene" include src tests
```

Expected: render calls use `RenderWorld`; `ToLegacyRenderScene` appears only in the adapter, CPU backend, compiler BVH bridge, and tests.

- [ ] **Step 5: Commit documentation and final verification state**

Run:

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: document render world architecture slice"
```

Expected: commit succeeds.

- [ ] **Step 6: Report final state**

Run:

```powershell
git status --short
git log --oneline -n 6
```

Expected: implementation commits are present. Existing unrelated local changes may remain; do not stage or remove them unless they are part of this plan.

## Self-Review Checklist

- Spec coverage:
  - Asset IR scaffolding: Task 1.
  - GPU-ready Render IR scaffolding: Task 2.
  - Scene -> RenderWorld compiler: Tasks 3-8 and 11.
  - Backend runtime receives RenderWorld: Task 9.
  - CLI route uses RenderWorld: Task 10.
  - Transitional adapter documented: Tasks 6 and 12.
  - Existing test suite preserved: Task 12.
- Type consistency:
  - `RenderWorldCompileResult`, `CompileSceneToRenderWorld()`, `RenderWorld`, and `ToLegacyRenderScene()` are named consistently across tests, headers, and implementation.
  - `RenderMaterialRecord::base_color_texture` maps to legacy `RenderMaterial::albedo_texture`.
  - `RenderLightRecord::u` and `v` encode rectangle width and height vectors for the adapter.
- Boundary check:
  - Backend API takes `RenderWorld`.
  - CPU backend may call `ToLegacyRenderScene()` during Slice 1.
  - Scene/importer headers stay outside backend code.
