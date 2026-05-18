# YaoRay Texture UV MTL v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a minimal textured OBJ asset pipeline so diffuse `map_Kd` PNG textures can flow from OBJ/MTL files into CPU path-traced shading.

**Architecture:** Keep the existing flat-triangle `RenderScene` pipeline and extend it with UVs, render-owned textures, and material texture indices. Reuse tinyobjloader's OBJ/MTL parsing for `vt`, `mtllib`, `usemtl`, `Kd`, and `map_Kd`, then load PNG texels through a small render texture module using vendored `stb_image.h`. Resolve textured diffuse albedo in the CPU path tracer by interpolating hit UVs and sampling the texture before BSDF/direct-light evaluation.

**Tech Stack:** C++20, CMake/CTest, existing `yr_test` harness, tinyobjloader, stb single-header image dependencies, existing CPU path tracer, existing scene compiler, existing PNG writer for verification.

---

## Scope

This plan implements:

- `docs/superpowers/specs/2026-05-18-yaoray-texture-uv-mtl-v1-design.md`

It does not implement glTF, full resource-system refactoring, bilinear filtering, mipmaps, alpha cutouts, normal maps, roughness/metallic/specular textures, CUDA texture sampling, or color-management changes.

## Current Context

- `src/assets/obj_loader.cpp` already uses tinyobjloader and sets `config.triangulate = true`.
- tinyobjloader can parse OBJ `vt`, `mtllib`, `usemtl`, MTL diffuse color, and MTL diffuse texture names. Use that instead of writing a second MTL parser.
- `RenderScene` currently stores flat `RenderTriangle` values with position, normal, and material index only.
- `RenderMaterial` currently stores material kind, albedo, emission, roughness, and specular.
- `src/backends/cpu/cpu_path_tracer.cpp` fetches `RenderMaterial` directly from `scene.materials`.
- `external/stb` currently contains `stb_image_write.h` but not `stb_image.h`.
- The local worktree currently has an unrelated dirty file, `scenes/examples/material_showcase.toml`. Do not stage or modify it unless the user explicitly asks.

## File Structure

- Create `include/yaoray/render/texture.hpp`
  - Own render texture data types.
  - Declare PNG loading and nearest sampling helpers.
- Create `src/render/texture.cpp`
  - Implement PNG loading through `stb_image.h`.
  - Implement repeat-wrapped nearest sampling.
- Create `tests/texture_tests.cpp`
  - Unit tests for nearest sampling, repeat wrapping, and PNG loading.
- Add `external/stb/stb_image.h`
  - Vendor the public-domain/MIT stb image loader header that matches the existing stb dependency style.
- Modify `CMakeLists.txt`
  - Add `src/render/texture.cpp` to `yaoray_render`.
  - Link `yaoray_render` privately to `stb`.
  - Add `tests/texture_tests.cpp` to `yaoray_tests`.
  - Add a CLI smoke test for `textured_quad.toml`.
- Modify `include/yaoray/assets/obj_loader.hpp`
  - Add imported material data.
  - Add optional UVs and imported material index on imported triangles.
- Modify `src/assets/obj_loader.cpp`
  - Read tinyobj texcoords, material ids, material diffuse color, and diffuse texture names.
- Modify `tests/assets_tests.cpp`
  - Add OBJ UV/material/MTL tests.
- Modify `include/yaoray/render/render_scene.hpp`
  - Add `RenderTexture` include/use.
  - Add `RenderScene::textures`.
  - Add `RenderMaterial::albedo_texture`.
  - Add `RenderTriangle` UV fields and `has_uv`.
- Modify `src/render/scene_compiler.cpp`
  - Import OBJ materials when an instance has no scene material override.
  - Load and cache PNG textures referenced by imported MTL `map_Kd`.
  - Preserve triangle UVs through transforms.
- Modify `tests/render_scene_tests.cpp`
  - Add scene compiler tests for imported materials, texture caching, UV propagation, override behavior, and missing texture diagnostics.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Resolve hit material albedo from texture and interpolated UV before shading.
- Modify `tests/cpu_path_tracer_tests.cpp`
  - Add a focused textured-triangle path tracer test and threaded determinism coverage.
- Add text fixtures under `tests/fixtures/assets/`
  - `uv_triangle.obj`
  - `uv_quad.obj`
  - `textured_quad.obj`
  - `textured_quad.mtl`
  - `duplicate_materials.obj`
  - `duplicate_a.mtl`
  - `duplicate_b.mtl`
  - `missing_texture.obj`
  - `missing_texture.mtl`
- Add PNG fixture under `tests/fixtures/assets/`
  - `checker_2x2.png`
- Add example assets under `scenes/examples/assets/`
  - `textured_quad.obj`
  - `textured_quad.mtl`
  - `checker_2x2.png`
- Add `scenes/examples/textured_quad.toml`
- Modify `README.md`
  - Document textured OBJ/MTL v1 support and limits.
- Modify `docs/architecture/overview.md`
  - Document the texture pipeline boundary and future resource refactor.

---

## Task 1: Add Render Texture Data and Nearest Sampling

**Files:**
- Create: `include/yaoray/render/texture.hpp`
- Create: `src/render/texture.cpp`
- Create: `tests/texture_tests.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing texture sampler tests**

Create `tests/texture_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>

#include <yaoray/render/texture.hpp>

YR_TEST(texture_nearest_samples_expected_texels) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f lower_left = yr::SampleTextureNearest(texture, yr::Vec2f{0.25f, 0.25f});
    const yr::Color3f lower_right = yr::SampleTextureNearest(texture, yr::Vec2f{0.75f, 0.25f});
    const yr::Color3f upper_left = yr::SampleTextureNearest(texture, yr::Vec2f{0.25f, 0.75f});
    const yr::Color3f upper_right = yr::SampleTextureNearest(texture, yr::Vec2f{0.75f, 0.75f});

    YR_EXPECT_NEAR(lower_left.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(lower_right.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_left.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(upper_right.z, 1.0, 1e-6);
}

YR_TEST(texture_nearest_repeats_wrapped_uvs) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f wrapped = yr::SampleTextureNearest(texture, yr::Vec2f{1.25f, -0.75f});

    YR_EXPECT_NEAR(wrapped.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(wrapped.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(wrapped.z, 0.0, 1e-6);
}

YR_TEST(texture_nearest_returns_black_for_empty_texture) {
    const yr::Color3f color = yr::SampleTextureNearest(yr::RenderTexture{}, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_NEAR(color.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.0, 1e-6);
}
```

- [ ] **Step 2: Register failing test file**

In `CMakeLists.txt`, add `src/render/texture.cpp` to `yaoray_render` and `tests/texture_tests.cpp` to `yaoray_tests`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
    src/render/texture.cpp
)
...
add_executable(yaoray_tests
    tests/test_main.cpp
    ...
    tests/texture_tests.cpp
    tests/backend_tests.cpp
    ...
)
```

- [ ] **Step 3: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `yaoray/render/texture.hpp` and `src/render/texture.cpp` do not exist yet.

- [ ] **Step 4: Add texture header**

Create `include/yaoray/render/texture.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
};

struct TextureLoadResult {
    RenderTexture texture;
    bool ok = false;
    std::string error;
};

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);

TextureLoadResult LoadPngTexture(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 5: Add sampler-only implementation**

Create `src/render/texture.cpp` with the sampler implementation and a deliberate red-state PNG loader that Task 2 replaces:

```cpp
#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

float WrapRepeat(float value) {
    const float wrapped = value - std::floor(value);
    return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

int NearestIndex(float value, int count) {
    const float wrapped = WrapRepeat(value);
    return std::clamp(static_cast<int>(std::floor(wrapped * static_cast<float>(count))), 0, count - 1);
}

} // namespace

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color3f{};
    }

    const int x = NearestIndex(uv.x, texture.width);
    const int y = NearestIndex(uv.y, texture.height);
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                              static_cast<std::size_t>(x);
    if (index >= texture.texels.size()) {
        return Color3f{};
    }
    return texture.texels[index];
}

TextureLoadResult LoadPngTexture(const std::filesystem::path& path) {
    return TextureLoadResult{RenderTexture{}, false, "PNG texture loading not implemented yet: " + path.generic_string()};
}

} // namespace yr
```

- [ ] **Step 6: Add render scene texture fields**

Modify `include/yaoray/render/render_scene.hpp`:

```cpp
#include <yaoray/render/texture.hpp>
```

Extend `RenderMaterial`, `RenderTriangle`, and `RenderScene`:

```cpp
struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
};

struct RenderTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    int material_index = 0;
    Vec2f uv0;
    Vec2f uv1;
    Vec2f uv2;
    bool has_uv = false;
};

struct RenderScene {
    ...
    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;
    std::vector<RenderTriangle> triangles;
    ...
};
```

- [ ] **Step 7: Update aggregate initializers**

Fix compile errors from the new `RenderMaterial` field by adding `-1` to aggregate initializers that already specify all material fields:

```cpp
compiled.materials.push_back(RenderMaterial{
    material.type,
    material.albedo,
    material.emission,
    material.roughness,
    material.specular,
    -1
});
```

Existing tests that construct `RenderMaterial` with only the first three fields remain valid because trailing fields have defaults.

- [ ] **Step 8: Run texture tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all tests pass, except no PNG loader behavior is tested yet.

- [ ] **Step 9: Commit**

```powershell
git add CMakeLists.txt include\yaoray\render\render_scene.hpp include\yaoray\render\texture.hpp src\render\texture.cpp tests\texture_tests.cpp
git commit -m "feat: add render texture sampler"
```

---

## Task 2: Add PNG Texture Loading

**Files:**
- Add: `external/stb/stb_image.h`
- Modify: `src/render/texture.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/texture_tests.cpp`
- Add: `tests/fixtures/assets/checker_2x2.png`

- [ ] **Step 1: Vendor `stb_image.h`**

Add the official `stb_image.h` single-header image loader to `external/stb/stb_image.h`, matching the existing `external/stb/stb_image_write.h` style. Use the upstream stb public-domain/MIT header.

After adding the file, confirm:

```powershell
Test-Path external\stb\stb_image.h
```

Expected: `True`.

- [ ] **Step 2: Link render library to stb**

Modify `CMakeLists.txt` so `yaoray_render` can include `stb_image.h`:

```cmake
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets stb)
```

- [ ] **Step 3: Add PNG load test fixture**

Create `tests/fixtures/assets/checker_2x2.png` as a 2x2 RGB/RGBA PNG with these texels in row-major top-to-bottom order:

```text
red, green
blue, white
```

Use the same file later for example assets so test and manual render behavior match.

- [ ] **Step 4: Write failing PNG load test**

Append to `tests/texture_tests.cpp`:

```cpp
#include <filesystem>
#include <string>

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

namespace {

std::filesystem::path TextureFixturePath(const std::string& relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / relative;
}

} // namespace

YR_TEST(texture_loader_reads_png_texels) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[0].y, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.texture.texels[3].z, 1.0, 1e-6);
}

YR_TEST(texture_loader_rejects_non_png_extension) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}

YR_TEST(texture_loader_reports_missing_file) {
    const yr::TextureLoadResult result = yr::LoadPngTexture(TextureFixturePath("assets/missing_texture.png"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("not found") != std::string::npos);
}
```

- [ ] **Step 5: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `texture_loader_reads_png_texels` fails because `LoadPngTexture()` still returns "not implemented".

- [ ] **Step 6: Implement PNG loader**

Replace `LoadPngTexture()` in `src/render/texture.cpp` and add includes:

```cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <filesystem>
#include <string>
```

Use this implementation:

```cpp
namespace {

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

} // namespace

TextureLoadResult LoadPngTexture(const std::filesystem::path& path) {
    if (LowerExtension(path) != ".png") {
        return TextureLoadResult{RenderTexture{}, false, "texture path must use a .png extension: " + path.generic_string()};
    }
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{RenderTexture{}, false, "texture file not found: " + path.generic_string()};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            RenderTexture{},
            false,
            "failed to load PNG texture: " + path.generic_string() + (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 4;
        texture.texels.push_back(Color3f{
            static_cast<float>(pixels[base + 0]) / 255.0f,
            static_cast<float>(pixels[base + 1]) / 255.0f,
            static_cast<float>(pixels[base + 2]) / 255.0f
        });
    }
    stbi_image_free(pixels);

    return TextureLoadResult{std::move(texture), true, {}};
}
```

Also include `<algorithm>`, `<cctype>`, `<utility>`, and keep existing includes needed by the sampler.

- [ ] **Step 7: Run texture tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```powershell
git add CMakeLists.txt external\stb\stb_image.h src\render\texture.cpp tests\texture_tests.cpp tests\fixtures\assets\checker_2x2.png
git commit -m "feat: load png textures"
```

---

## Task 3: Extend OBJ Import with UVs and Basic MTL Data

**Files:**
- Modify: `include/yaoray/assets/obj_loader.hpp`
- Modify: `src/assets/obj_loader.cpp`
- Modify: `tests/assets_tests.cpp`
- Add: `tests/fixtures/assets/uv_triangle.obj`
- Add: `tests/fixtures/assets/uv_quad.obj`
- Add: `tests/fixtures/assets/textured_quad.obj`
- Add: `tests/fixtures/assets/textured_quad.mtl`
- Add: `tests/fixtures/assets/duplicate_materials.obj`
- Add: `tests/fixtures/assets/duplicate_a.mtl`
- Add: `tests/fixtures/assets/duplicate_b.mtl`

- [ ] **Step 1: Add OBJ fixture files**

Create `tests/fixtures/assets/uv_triangle.obj`:

```text
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 0.5 1.0
f 1/1 2/2 3/3
```

Create `tests/fixtures/assets/uv_quad.obj`:

```text
v -0.5 -0.5 0.0
v 0.5 -0.5 0.0
v 0.5 0.5 0.0
v -0.5 0.5 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
f 1/1 2/2 3/3 4/4
```

Create `tests/fixtures/assets/textured_quad.mtl`:

```text
newmtl checker
Kd 0.25 0.5 0.75
map_Kd checker_2x2.png
```

Create `tests/fixtures/assets/textured_quad.obj`:

```text
mtllib textured_quad.mtl
v -0.5 -0.5 0.0
v 0.5 -0.5 0.0
v 0.5 0.5 0.0
v -0.5 0.5 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
usemtl checker
f 1/1 2/2 3/3 4/4
```

Create `tests/fixtures/assets/duplicate_a.mtl`:

```text
newmtl duplicate
Kd 1.0 0.0 0.0
```

Create `tests/fixtures/assets/duplicate_b.mtl`:

```text
newmtl duplicate
Kd 0.0 1.0 0.0
```

Create `tests/fixtures/assets/duplicate_materials.obj`:

```text
mtllib duplicate_a.mtl
mtllib duplicate_b.mtl
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
usemtl duplicate
f 1 2 3
```

- [ ] **Step 2: Write failing OBJ UV/material tests**

Append to `tests/assets_tests.cpp`:

```cpp
YR_TEST(obj_loader_preserves_triangle_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/uv_triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_TRUE(triangle.has_uv);
    YR_EXPECT_NEAR(triangle.uv0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.uv1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.uv2.y, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/uv_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});
    YR_EXPECT_TRUE(result.mesh->triangles[0].has_uv);
    YR_EXPECT_TRUE(result.mesh->triangles[1].has_uv);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[0].uv2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv1.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.mesh->triangles[1].uv2.y, 1.0, 1e-6);
}

YR_TEST(obj_loader_imports_basic_mtl_material) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/textured_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.mesh->materials[0].name, "checker");
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(result.mesh->materials[0].diffuse.z, 0.75, 1e-6);
    YR_EXPECT_TRUE(result.mesh->materials[0].has_diffuse_texture);
    YR_EXPECT_TRUE(result.mesh->materials[0].diffuse_texture_path.generic_string().find("checker_2x2.png") != std::string::npos);
    YR_EXPECT_EQ(result.mesh->triangles[0].material_index, 0);
}

YR_TEST(obj_loader_rejects_duplicate_mtl_material_names) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/duplicate_materials.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "duplicate OBJ material"));
}
```

- [ ] **Step 3: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: compile fails because imported UV/material fields do not exist.

- [ ] **Step 4: Extend imported data structs**

Modify `include/yaoray/assets/obj_loader.hpp`:

```cpp
struct ImportedMaterial {
    std::string name;
    Color3f diffuse{0.8f, 0.8f, 0.8f};
    std::filesystem::path diffuse_texture_path;
    bool has_diffuse_texture = false;
};

struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    Vec2f uv0;
    Vec2f uv1;
    Vec2f uv2;
    bool has_uv = false;
    int material_index = -1;
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
    std::vector<ImportedMaterial> materials;
};
```

- [ ] **Step 5: Implement UV and material extraction in OBJ loader**

In `src/assets/obj_loader.cpp`, add a helper:

```cpp
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
```

After `const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();`, convert materials:

```cpp
std::unordered_map<std::string, int> material_names;
for (const tinyobj::material_t& material : reader.GetMaterials()) {
    if (material.name.empty()) {
        continue;
    }
    if (material_names.find(material.name) != material_names.end()) {
        result.errors.push_back("duplicate OBJ material: " + material.name);
        return result;
    }
    ImportedMaterial imported;
    imported.name = material.name;
    imported.diffuse = Color3f{material.diffuse[0], material.diffuse[1], material.diffuse[2]};
    if (!material.diffuse_texname.empty()) {
        imported.diffuse_texture_path = path.parent_path() / material.diffuse_texname;
        imported.has_diffuse_texture = true;
    }
    material_names.emplace(imported.name, static_cast<int>(mesh.materials.size()));
    mesh.materials.push_back(std::move(imported));
}
```

When building each `ImportedTriangle`, read UVs when all three indices have valid `texcoord_index`:

```cpp
bool uvs_ok = true;
const Vec2f uv0 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 0].texcoord_index, uvs_ok);
const Vec2f uv1 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 1].texcoord_index, uvs_ok);
const Vec2f uv2 = ReadTexCoord(attrib, shape.mesh.indices[index_offset + 2].texcoord_index, uvs_ok);
const bool has_uv = uvs_ok;
const int material_index = face_index < shape.mesh.material_ids.size() ? shape.mesh.material_ids[face_index] : -1;
```

Push:

```cpp
mesh.triangles.push_back(ImportedTriangle{
    p0,
    p1,
    p2,
    normal,
    uv0,
    uv1,
    uv2,
    has_uv,
    material_index
});
```

Keep position-only OBJ valid by treating missing UVs as `has_uv = false`.

- [ ] **Step 6: Run tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```powershell
git add include\yaoray\assets\obj_loader.hpp src\assets\obj_loader.cpp tests\assets_tests.cpp tests\fixtures\assets\uv_triangle.obj tests\fixtures\assets\uv_quad.obj tests\fixtures\assets\textured_quad.obj tests\fixtures\assets\textured_quad.mtl tests\fixtures\assets\duplicate_materials.obj tests\fixtures\assets\duplicate_a.mtl tests\fixtures\assets\duplicate_b.mtl
git commit -m "feat: import obj uvs and mtl materials"
```

---

## Task 4: Compile Imported Textures into RenderScene

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Add: `tests/fixtures/assets/missing_texture.obj`
- Add: `tests/fixtures/assets/missing_texture.mtl`

- [ ] **Step 1: Add missing texture fixtures**

Create `tests/fixtures/assets/missing_texture.mtl`:

```text
newmtl missing
Kd 1.0 1.0 1.0
map_Kd missing_texture.png
```

Create `tests/fixtures/assets/missing_texture.obj`:

```text
mtllib missing_texture.mtl
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 0.5 1.0
usemtl missing
f 1/1 2/2 3/3
```

- [ ] **Step 2: Write failing compiler tests**

Append to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_imports_obj_material_texture_and_uvs) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/textured_quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.x, 0.25, 1e-6);
    YR_EXPECT_TRUE(compiled.triangles[0].has_uv);
    YR_EXPECT_NEAR(compiled.triangles[0].uv1.x, 1.0, 1e-6);
}

YR_TEST(scene_compiler_caches_duplicate_obj_textures) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"first", FixturePath("assets/textured_quad.obj")});
    scene.assets.push_back(yr::AssetDescription{"second", FixturePath("assets/textured_quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"first", {}});
    yr::InstanceDescription second;
    second.asset = "second";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
    YR_EXPECT_EQ(compiled.materials[1].albedo_texture, 0);
}

YR_TEST(scene_compiler_scene_material_overrides_imported_obj_material) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/textured_quad.obj")});
    scene.materials.push_back(yr::MaterialDescription{
        "override",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.9f, 0.1f, 0.2f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "quad";
    instance.material = "override";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_TRUE(compiled.textures.empty());
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, -1);
}

YR_TEST(scene_compiler_reports_missing_obj_texture) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"bad", FixturePath("assets/missing_texture.obj")});
    scene.instances.push_back(yr::InstanceDescription{"bad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.path", "texture file not found"));
}
```

- [ ] **Step 3: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: compiler texture tests fail because OBJ imported materials are ignored by `CompileScene()`.

- [ ] **Step 4: Add compiler helpers**

In `src/render/scene_compiler.cpp`, include texture support:

```cpp
#include <yaoray/render/texture.hpp>
```

Add helper types inside the anonymous namespace:

```cpp
struct TextureCache {
    std::unordered_map<std::string, int> indices;
};
```

Add helpers:

```cpp
std::string CanonicalTextureKey(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal().generic_string() : canonical.generic_string();
}

std::optional<int> LoadTextureIndex(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& path,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string key = CanonicalTextureKey(path);
    const auto found = texture_cache.indices.find(key);
    if (found != texture_cache.indices.end()) {
        return found->second;
    }

    TextureLoadResult load = LoadPngTexture(path);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "assets.path", load.error));
        return std::nullopt;
    }

    const int texture_index = static_cast<int>(compiled.textures.size());
    compiled.textures.push_back(std::move(load.texture));
    texture_cache.indices.emplace(key, texture_index);
    return texture_index;
}
```

Add imported material compilation:

```cpp
std::vector<int> CompileImportedMaterials(
    const SceneDescription& scene,
    RenderScene& compiled,
    const ImportedMesh& mesh,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::vector<int> material_indices;
    material_indices.reserve(mesh.materials.size());

    for (const ImportedMaterial& material : mesh.materials) {
        RenderMaterial render_material;
        render_material.type = MaterialKind::Diffuse;
        render_material.albedo = material.diffuse;
        if (material.has_diffuse_texture) {
            const std::optional<int> texture_index =
                LoadTextureIndex(scene, compiled, material.diffuse_texture_path, texture_cache, diagnostics);
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

- [ ] **Step 5: Preserve UVs in imported mesh append**

Change `AppendImportedMesh()` to accept an optional override material and imported material index map:

```cpp
void AppendImportedMesh(
    RenderScene& compiled,
    const ImportedMesh& mesh,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    const std::vector<int>& imported_material_indices
) {
    for (const ImportedTriangle& triangle : mesh.triangles) {
        const Point3f world_p0 = ApplyTransform(triangle.p0, transform);
        const Point3f world_p1 = ApplyTransform(triangle.p1, transform);
        const Point3f world_p2 = ApplyTransform(triangle.p2, transform);

        int material_index = override_material_index.value_or(-1);
        if (!override_material_index.has_value() &&
            triangle.material_index >= 0 &&
            static_cast<std::size_t>(triangle.material_index) < imported_material_indices.size()) {
            material_index = imported_material_indices[static_cast<std::size_t>(triangle.material_index)];
        }
        if (material_index < 0) {
            material_index = AddDefaultMaterial(compiled);
        }

        compiled.triangles.push_back(RenderTriangle{
            world_p0,
            world_p1,
            world_p2,
            Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
            material_index,
            triangle.uv0,
            triangle.uv1,
            triangle.uv2,
            triangle.has_uv
        });
    }
}
```

- [ ] **Step 6: Change OBJ append flow**

Change `AppendObjAsset()` signature:

```cpp
void AppendObjAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& asset_path,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
)
```

Inside it, after loading or retrieving the cached mesh:

```cpp
std::vector<int> imported_material_indices;
if (!override_material_index.has_value()) {
    imported_material_indices = CompileImportedMaterials(scene, compiled, cached->second, texture_cache, diagnostics);
    if (HasSceneErrors(diagnostics)) {
        return;
    }
}

AppendImportedMesh(compiled, cached->second, transform, override_material_index, imported_material_indices);
```

In `CompileScene()`, only resolve a material index before non-OBJ assets or when the instance has an explicit `material`. For OBJ:

```cpp
std::optional<int> material_index;
if (!instance.material.empty()) {
    material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
    if (!material_index.has_value()) {
        continue;
    }
}
```

For builtin and inline assets, preserve current default behavior:

```cpp
if (!material_index.has_value()) {
    material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
}
```

Create `TextureCache texture_cache;` near `mesh_cache`.

- [ ] **Step 7: Run tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```powershell
git add src\render\scene_compiler.cpp tests\render_scene_tests.cpp tests\fixtures\assets\missing_texture.obj tests\fixtures\assets\missing_texture.mtl
git commit -m "feat: compile obj textures into render scene"
```

---

## Task 5: Sample Textures in the CPU Path Tracer

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Write failing path tracer texture tests**

In `tests/cpu_path_tracer_tests.cpp`, add helper after `RebuildBvh()`:

```cpp
yr::RenderScene MakeTexturedTriangleScene(std::uint64_t seed = 7, int threads = 1) {
    yr::RenderScene scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = seed;
    scene.threads = threads;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 2.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.1f, 0.1f, 0.1f},
        yr::Color3f{1.0f, 1.0f, 1.0f},
        0.0f,
        0.04f,
        0
    });
    scene.textures.push_back(yr::RenderTexture{
        2,
        2,
        std::vector<yr::Color3f>{
            yr::Color3f{1.0f, 0.0f, 0.0f},
            yr::Color3f{0.0f, 1.0f, 0.0f},
            yr::Color3f{0.0f, 0.0f, 1.0f},
            yr::Color3f{1.0f, 1.0f, 1.0f}
        }
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 0.0f},
        yr::Point3f{10.0f, -10.0f, 0.0f},
        yr::Point3f{-10.0f, 10.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0,
        yr::Vec2f{0.0f, 0.0f},
        yr::Vec2f{1.0f, 0.0f},
        yr::Vec2f{0.0f, 1.0f},
        true
    });
    RebuildBvh(scene);
    return scene;
}
```

Add tests near other CPU path tracer material tests:

```cpp
YR_TEST(cpu_path_tracer_uses_diffuse_texture_albedo_on_hit) {
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(MakeTexturedTriangleScene());
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.x > 0.9f);
    YR_EXPECT_TRUE(pixel.y < 0.1f);
    YR_EXPECT_TRUE(pixel.z < 0.1f);
}

YR_TEST(cpu_path_tracer_textured_scene_is_deterministic_across_thread_counts) {
    const yr::CpuPathTraceResult single = yr::RenderCpuPathTrace(MakeTexturedTriangleScene(91, 1));
    const yr::CpuPathTraceResult threaded = yr::RenderCpuPathTrace(MakeTexturedTriangleScene(91, 8));

    YR_EXPECT_TRUE(FilmsEqual(single.film, threaded.film));
    YR_EXPECT_EQ(single.stats.rays_traced, threaded.stats.rays_traced);
}
```

- [ ] **Step 2: Run tests to verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `cpu_path_tracer_uses_diffuse_texture_albedo_on_hit` fails because the path tracer still uses material albedo and ignores textures.

- [ ] **Step 3: Add barycentric and material resolving helpers**

In `src/backends/cpu/cpu_path_tracer.cpp`, include texture support:

```cpp
#include <yaoray/render/texture.hpp>
```

Add helpers near `Multiply()`:

```cpp
Vec3f Barycentric(Point3f point, const RenderTriangle& triangle) {
    const Vec3f v0 = triangle.p1 - triangle.p0;
    const Vec3f v1 = triangle.p2 - triangle.p0;
    const Vec3f v2 = point - triangle.p0;
    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) <= 1.0e-12f) {
        return Vec3f{1.0f, 0.0f, 0.0f};
    }
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    const float u = 1.0f - v - w;
    return Vec3f{u, v, w};
}

Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric) {
    return Vec2f{
        triangle.uv0.x * barycentric.x + triangle.uv1.x * barycentric.y + triangle.uv2.x * barycentric.z,
        triangle.uv0.y * barycentric.x + triangle.uv1.y * barycentric.y + triangle.uv2.y * barycentric.z
    };
}

RenderMaterial ResolveHitMaterial(
    const RenderScene& scene,
    const RenderTriangle& triangle,
    const RenderMaterial& material,
    Point3f hit_point
) {
    RenderMaterial resolved = material;
    if (material.albedo_texture < 0 || !triangle.has_uv) {
        return resolved;
    }
    const std::size_t texture_index = static_cast<std::size_t>(material.albedo_texture);
    if (texture_index >= scene.textures.size()) {
        return resolved;
    }
    const Vec3f barycentric = Barycentric(hit_point, triangle);
    const Vec2f uv = InterpolateUv(triangle, barycentric);
    resolved.albedo = SampleTextureNearest(scene.textures[texture_index], uv);
    return resolved;
}
```

- [ ] **Step 4: Use resolved material in `TracePath()`**

Replace:

```cpp
const RenderTriangle& triangle = *hit.triangle;
const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
const Point3f hit_point = ray.At(hit.t);
const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);
const Vec3f wo = -ray.direction;
```

with:

```cpp
const RenderTriangle& triangle = *hit.triangle;
const RenderMaterial& base_material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
const Point3f hit_point = ray.At(hit.t);
const RenderMaterial material = ResolveHitMaterial(scene, triangle, base_material, hit_point);
const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);
const Vec3f wo = -ray.direction;
```

Leave the rest of path tracing generic through the existing BSDF API.

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: sample diffuse textures in cpu path tracer"
```

---

## Task 6: Add Textured OBJ Example, CLI Smoke Test, and Docs

**Files:**
- Add: `scenes/examples/assets/textured_quad.obj`
- Add: `scenes/examples/assets/textured_quad.mtl`
- Add: `scenes/examples/assets/checker_2x2.png`
- Add: `scenes/examples/textured_quad.toml`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add example asset files**

Copy the tested fixture asset contents into `scenes/examples/assets/textured_quad.obj`:

```text
mtllib textured_quad.mtl
v -1.0 -1.0 0.0
v 1.0 -1.0 0.0
v 1.0 1.0 0.0
v -1.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
usemtl checker
f 1/1 2/2 3/3 4/4
```

Create `scenes/examples/assets/textured_quad.mtl`:

```text
newmtl checker
Kd 1.0 1.0 1.0
map_Kd checker_2x2.png
```

Copy `tests/fixtures/assets/checker_2x2.png` to `scenes/examples/assets/checker_2x2.png`.

- [ ] **Step 2: Add example TOML scene**

Create `scenes/examples/textured_quad.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 256
height = 256
spp = 16
max_depth = 1
seed = 17
sampler = "stratified"
threads = 0
light_samples = 1

[film]
output = "scenes/examples/out/textured_quad.png"

[camera]
position = [0.0, 0.0, 3.0]
target = [0.0, 0.0, 0.0]
fov_y = 45.0

[environment]
type = "constant"
radiance = [0.0, 0.0, 0.0]
strength = 1.0

[[assets]]
name = "textured_quad"
path = "scenes/examples/assets/textured_quad.obj"

[[instances]]
asset = "textured_quad"
```

- [ ] **Step 3: Add CLI smoke test**

In `CMakeLists.txt`, add:

```cmake
add_test(NAME yaoray_cli_render_textured_obj
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/textured_quad.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/textured_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Integrator: path') { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
)
```

- [ ] **Step 4: Update README**

In `README.md`, update the feature list near existing OBJ/material bullets to mention:

```text
- basic textured OBJ import through `vt`, `mtllib`, `usemtl`, `Kd`, and PNG `map_Kd`
```

In the render command paragraph, add:

```text
OBJ assets can now carry UV coordinates and basic MTL diffuse textures through `map_Kd`; v1 texture sampling is nearest-filtered, repeat-wrapped, PNG-only, and diffuse-only.
```

Keep limitations explicit:

```text
The path integrator still does not implement user-configurable roulette parameters, environment MIS, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, glass refraction, normal maps, alpha masks, mipmaps, bilinear texture filtering, imported roughness/metallic textures, CUDA materials, or other advanced material models.
```

- [ ] **Step 5: Update architecture docs**

In `docs/architecture/overview.md`, update the OBJ importer paragraph:

```text
The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It now preserves OBJ UV coordinates and imports basic MTL diffuse data (`Kd` and PNG `map_Kd`) into render materials and render-owned textures. It still ignores imported normals, smoothing data, normal maps, alpha masks, mipmaps, roughness/metallic maps, and full material-library semantics.
```

Add to the material showcase paragraph:

```text
`textured_quad.toml` is the first texture-pipeline smoke scene and verifies that OBJ UVs, MTL `map_Kd`, PNG loading, and CPU diffuse texture sampling work end to end.
```

- [ ] **Step 6: Run tests and manual render**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
.\build\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
```

If the Debug executable path differs on the generator, use:

```powershell
.\build\Debug\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
```

Expected:

- CTest passes.
- CLI prints `Integrator: path`.
- CLI prints `Compiled triangles: 2`.
- `scenes/examples/out/textured_quad.png` exists and has PNG signature.

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt README.md docs\architecture\overview.md scenes\examples\assets\textured_quad.obj scenes\examples\assets\textured_quad.mtl scenes\examples\assets\checker_2x2.png scenes\examples\textured_quad.toml
git commit -m "docs: add textured obj example"
```

---

## Task 7: Full Verification

**Files:**
- No source edits expected.

- [ ] **Step 1: Configure Debug build**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
```

Expected: configure succeeds.

- [ ] **Step 2: Build Debug**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build succeeds.

- [ ] **Step 3: Run full Debug tests**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass, including `yaoray_cli_render_textured_obj`.

- [ ] **Step 4: Build Release**

Run:

```powershell
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build-release --config Release
```

If MSBuild cannot read the Windows SDK directory inside the sandbox, rerun the failing command with escalation.

- [ ] **Step 5: Manual renders**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\cornell_box_path.toml --backend cpu
```

Expected:

- `scenes/examples/out/textured_quad.png` is written.
- `scenes/examples/out/material_v2_showcase.png` is written.
- `scenes/examples/out/cornell_box_path.png` is written.
- CLI stats print normally for all three renders.

- [ ] **Step 6: Scope checks**

Run:

```powershell
rg -n "RenderTexture|LoadPngTexture|SampleTextureNearest|albedo_texture|has_uv|map_Kd|stb_image|textured_quad" include src tests scenes README.md docs\architecture\overview.md
git status --short --branch
git log --oneline --decorate -12
```

Expected:

- Search results show only planned texture/UV/MTL additions.
- `git status` may still show the user's pre-existing `scenes/examples/material_showcase.toml` modification, but no uncommitted files from this plan.
- Recent log includes the task commits from this plan.

---

## Plan Self-Review

Spec coverage:

- OBJ `vt`: Task 3.
- OBJ `v/vt` and `v/vt/vn`: Task 3 uses tinyobjloader indices; add a `v/vt/vn` test if tinyobj normal syntax regresses during implementation.
- Quad triangulation UV preservation: Task 3.
- Basic MTL `newmtl`, `Kd`, `map_Kd`: Task 3 via tinyobjloader, Task 4 through compiler.
- PNG loading: Task 2.
- Texture cache: Task 4.
- Render data UV/material texture fields: Task 1.
- CPU path tracer texture sampling: Task 5.
- Textured example: Task 6.
- Documentation: Task 6.
- Full verification: Task 7.

Important implementation notes:

- Do not stage `scenes/examples/material_showcase.toml` unless the user explicitly asks.
- Keep MTL scope small by consuming tinyobjloader's parsed material data.
- Keep the first texture sampling policy nearest + repeat + PNG-only.
- Keep color values as direct `0..1` RGB for this slice; sRGB-to-linear remains follow-up work.
