# YaoRay Texture Quality v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve imported texture quality with linearized PNG albedo textures, bilinear filtering, and glTF sampler wrap support.

**Architecture:** Keep texture sampling in `yaoray_render` and keep integrators unaware of filtering details. Put shared sampler enums in `yaoray_core` so `yaoray_assets`, `yaoray_render`, and future CUDA code can all reference the same wrap/filter semantics without creating an assets-to-render dependency. Continue using the existing flat `RenderTexture` vector in `RenderScene`.

**Tech Stack:** C++20, CMake, CTest, stb_image PNG loading, tinygltf, existing YaoRay CPU path tracer.

---

## File Structure

- Create: `include/yaoray/core/texture_sampler.hpp`
  - Defines `TextureFilter` and `TextureWrap`.
- Modify: `include/yaoray/render/texture.hpp`
  - Adds sampler state to `RenderTexture`.
  - Declares `SrgbToLinear()`, `SampleTexture()`, `SampleTextureNearest()`, and `SampleTextureBilinear()`.
- Modify: `src/render/texture.cpp`
  - Implements sRGB conversion, repeat/clamp/mirrored wrap, nearest sampling, bilinear sampling, and PNG linearization.
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
  - Uses the unified `SampleTexture()` entry point.
- Modify: `include/yaoray/assets/imported_asset.hpp`
  - Adds diffuse/base-color texture wrap fields to `ImportedMaterial`.
- Modify: `src/assets/gltf_loader.cpp`
  - Imports glTF `sampler.wrapS` and `sampler.wrapT`.
  - Warns on unsupported wrap constants.
- Modify: `src/render/scene_compiler.cpp`
  - Propagates imported wrap state into `RenderTexture`.
  - Includes wrap/filter in texture-cache keys.
- Create: `tests/fixtures/assets/gltf/SimpleTextureClamp/glTF/SimpleTextureClamp.gltf`
  - Compact glTF fixture that reuses the existing SimpleTexture `.bin` and `.png` through relative paths but uses clamp wrapping.
- Create: `tests/fixtures/assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf`
  - Compact glTF fixture that reuses the existing SimpleTexture `.bin` and `.png` through relative paths but uses unsupported wrap constants.
- Modify: `tests/texture_tests.cpp`
  - Adds filter, wrap, and sRGB tests.
- Modify: `tests/assets_tests.cpp`
  - Adds glTF sampler import tests.
- Modify: `tests/render_scene_tests.cpp`
  - Adds scene-compiler sampler propagation and cache-key tests.
- Modify: `tests/cpu_path_tracer_tests.cpp`
  - Adds a path-tracer regression that proves bilinear texture sampling affects resolved albedo.
- Modify: `README.md`
  - Documents linear PNG albedo/base-color storage and bilinear/wrap support.
- Modify: `docs/architecture/overview.md`
  - Documents Texture Quality v1 boundaries and remaining texture limitations.

Do not stage or commit `scenes/examples/assets/gltf/Duck/` or `scenes/examples/duck_gltf.toml` in this plan. Those are local manual verification files with separate license considerations.

---

## Task 1: Render Texture Sampling API

**Files:**
- Create: `include/yaoray/core/texture_sampler.hpp`
- Modify: `include/yaoray/render/texture.hpp`
- Modify: `src/render/texture.cpp`
- Test: `tests/texture_tests.cpp`

- [ ] **Step 1: Add failing texture sampler tests**

Append these tests to `tests/texture_tests.cpp` after `texture_nearest_repeats_wrapped_uvs`:

```cpp
YR_TEST(texture_bilinear_blends_center_of_2x2_texture) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.filter = yr::TextureFilter::Bilinear;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f},
        yr::Color3f{0.0f, 0.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };

    const yr::Color3f color = yr::SampleTexture(texture, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_NEAR(color.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.5, 1e-6);
}

YR_TEST(texture_clamp_to_edge_clamps_out_of_range_uvs) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Nearest;
    texture.wrap_s = yr::TextureWrap::ClampToEdge;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f}
    };

    const yr::Color3f left = yr::SampleTexture(texture, yr::Vec2f{-2.0f, 0.5f});
    const yr::Color3f right = yr::SampleTexture(texture, yr::Vec2f{3.0f, 0.5f});

    YR_EXPECT_NEAR(left.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(left.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(right.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(right.y, 1.0, 1e-6);
}

YR_TEST(texture_mirrored_repeat_mirrors_adjacent_intervals) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Nearest;
    texture.wrap_s = yr::TextureWrap::MirroredRepeat;
    texture.wrap_t = yr::TextureWrap::Repeat;
    texture.texels = {
        yr::Color3f{1.0f, 0.0f, 0.0f},
        yr::Color3f{0.0f, 1.0f, 0.0f}
    };

    const yr::Color3f mirrored = yr::SampleTexture(texture, yr::Vec2f{1.25f, 0.5f});
    const yr::Color3f repeated_again = yr::SampleTexture(texture, yr::Vec2f{2.25f, 0.5f});

    YR_EXPECT_NEAR(mirrored.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(mirrored.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(repeated_again.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(repeated_again.y, 0.0, 1e-6);
}

YR_TEST(texture_bilinear_single_pixel_returns_only_texel) {
    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.filter = yr::TextureFilter::Bilinear;
    texture.wrap_s = yr::TextureWrap::MirroredRepeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color3f{0.25f, 0.5f, 0.75f}
    };

    const yr::Color3f color = yr::SampleTexture(texture, yr::Vec2f{12.5f, -4.0f});

    YR_EXPECT_NEAR(color.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(color.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(color.z, 0.75, 1e-6);
}

YR_TEST(texture_srgb_to_linear_uses_standard_transfer_curve) {
    YR_EXPECT_NEAR(yr::SrgbToLinear(0.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::SrgbToLinear(1.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::SrgbToLinear(0.5f), 0.21404114, 1e-6);
}
```

Update `texture_loader_reads_png_texels` so it also verifies the default loaded sampler state:

```cpp
YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::Repeat);
```

- [ ] **Step 2: Run tests and verify the expected failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `TextureFilter`, `TextureWrap`, `SampleTexture()`, and `SrgbToLinear()` do not exist.

- [ ] **Step 3: Add sampler enums**

Create `include/yaoray/core/texture_sampler.hpp`:

```cpp
#pragma once

namespace yr {

enum class TextureFilter {
    Nearest,
    Bilinear
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

} // namespace yr
```

- [ ] **Step 4: Update the texture header**

Modify `include/yaoray/render/texture.hpp` to include sampler enums and expose the new API:

```cpp
#include <yaoray/core/texture_sampler.hpp>
#include <yaoray/core/vec.hpp>
```

Replace `RenderTexture` with:

```cpp
struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};
```

Add declarations:

```cpp
float SrgbToLinear(float value);

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);
Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv);
```

- [ ] **Step 5: Implement wrapping, filtering, and sRGB conversion**

Modify `src/render/texture.cpp`.

Add these helpers inside the anonymous namespace:

```cpp
float ApplyWrap(float value, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) {
        return std::clamp(value, 0.0f, 1.0f);
    }
    const float base = std::floor(value);
    const float fraction = value - base;
    if (wrap == TextureWrap::MirroredRepeat) {
        const int interval = static_cast<int>(base);
        return (interval & 1) == 0 ? fraction : 1.0f - fraction;
    }
    return fraction < 0.0f ? fraction + 1.0f : fraction;
}

int NearestIndex(float value, int count, TextureWrap wrap) {
    const float wrapped = ApplyWrap(value, wrap);
    return std::clamp(static_cast<int>(std::floor(wrapped * static_cast<float>(count))), 0, count - 1);
}

int WrappedTexelIndex(int value, int count, TextureWrap wrap) {
    if (wrap == TextureWrap::ClampToEdge) {
        return std::clamp(value, 0, count - 1);
    }
    if (wrap == TextureWrap::MirroredRepeat) {
        const int period = count * 2;
        int wrapped = value % period;
        if (wrapped < 0) {
            wrapped += period;
        }
        return wrapped < count ? wrapped : period - 1 - wrapped;
    }
    int wrapped = value % count;
    if (wrapped < 0) {
        wrapped += count;
    }
    return wrapped;
}

Color3f TexelAt(const RenderTexture& texture, int x, int y) {
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(texture.width) +
                              static_cast<std::size_t>(x);
    if (index >= texture.texels.size()) {
        return Color3f{};
    }
    return texture.texels[index];
}

Color3f Lerp(Color3f a, Color3f b, float t) {
    return a * (1.0f - t) + b * t;
}
```

Replace the existing `NearestIndex()` call sites to pass per-axis wrap state.

Add public implementations:

```cpp
float SrgbToLinear(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped <= 0.04045f) {
        return clamped / 12.92f;
    }
    return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
}

Color3f SampleTexture(const RenderTexture& texture, Vec2f uv) {
    if (texture.filter == TextureFilter::Nearest) {
        return SampleTextureNearest(texture, uv);
    }
    return SampleTextureBilinear(texture, uv);
}

Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color3f{};
    }
    const int x = NearestIndex(uv.x, texture.width, texture.wrap_s);
    const int y = NearestIndex(uv.y, texture.height, texture.wrap_t);
    return TexelAt(texture, x, y);
}

Color3f SampleTextureBilinear(const RenderTexture& texture, Vec2f uv) {
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        return Color3f{};
    }
    if (texture.width == 1 && texture.height == 1) {
        return texture.texels[0];
    }

    const float x = ApplyWrap(uv.x, texture.wrap_s) * static_cast<float>(texture.width) - 0.5f;
    const float y = ApplyWrap(uv.y, texture.wrap_t) * static_cast<float>(texture.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const int ix0 = WrappedTexelIndex(x0, texture.width, texture.wrap_s);
    const int ix1 = WrappedTexelIndex(x0 + 1, texture.width, texture.wrap_s);
    const int iy0 = WrappedTexelIndex(y0, texture.height, texture.wrap_t);
    const int iy1 = WrappedTexelIndex(y0 + 1, texture.height, texture.wrap_t);

    const Color3f c00 = TexelAt(texture, ix0, iy0);
    const Color3f c10 = TexelAt(texture, ix1, iy0);
    const Color3f c01 = TexelAt(texture, ix0, iy1);
    const Color3f c11 = TexelAt(texture, ix1, iy1);
    return Lerp(Lerp(c00, c10, tx), Lerp(c01, c11, tx), ty);
}
```

In `LoadPngTexture()`, replace direct byte normalization with sRGB conversion:

```cpp
texture.texels.push_back(Color3f{
    SrgbToLinear(static_cast<float>(pixels[base + 0]) / 255.0f),
    SrgbToLinear(static_cast<float>(pixels[base + 1]) / 255.0f),
    SrgbToLinear(static_cast<float>(pixels[base + 2]) / 255.0f)
});
```

- [ ] **Step 6: Run focused tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all texture tests pass and the full test executable exits with code 0.

- [ ] **Step 7: Commit**

```powershell
git add include/yaoray/core/texture_sampler.hpp include/yaoray/render/texture.hpp src/render/texture.cpp tests/texture_tests.cpp
git commit -m "feat: add bilinear texture sampling"
```

---

## Task 2: CPU Path Tracer Uses Unified Texture Sampling

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add a path-tracer bilinear texture regression test**

Add this helper near `MakeTexturedTriangleScene()` in `tests/cpu_path_tracer_tests.cpp`:

```cpp
yr::RenderScene MakeBilinearTexturedTriangleScene() {
    yr::RenderScene scene = MakeTexturedTriangleScene();
    scene.textures[0].filter = yr::TextureFilter::Bilinear;
    scene.triangles[0].uv0 = yr::Vec2f{0.5f, 0.5f};
    scene.triangles[0].uv1 = yr::Vec2f{0.5f, 0.5f};
    scene.triangles[0].uv2 = yr::Vec2f{0.5f, 0.5f};
    RebuildBvh(scene);
    return scene;
}
```

Add this test after `cpu_path_tracer_uses_diffuse_texture_albedo_on_hit`:

```cpp
YR_TEST(cpu_path_tracer_uses_bilinear_texture_sampling) {
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(MakeBilinearTexturedTriangleScene());
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.x > 0.0f);
    YR_EXPECT_NEAR(pixel.x, pixel.y, 1e-5);
    YR_EXPECT_NEAR(pixel.y, pixel.z, 1e-5);
}
```

- [ ] **Step 2: Run the new test and verify the expected failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: the new test fails because the CPU path tracer still calls `SampleTextureNearest()` and therefore does not respect `RenderTexture::filter`.

- [ ] **Step 3: Update CPU texture sampling call**

In `src/backends/cpu/cpu_path_tracer.cpp`, replace:

```cpp
resolved.albedo = SampleTextureNearest(scene.textures[texture_index], uv);
```

with:

```cpp
resolved.albedo = SampleTexture(scene.textures[texture_index], uv);
```

- [ ] **Step 4: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: the new path-tracer test and existing deterministic texture tests pass.

- [ ] **Step 5: Commit**

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: sample textures through render sampler"
```

---

## Task 3: Import glTF Texture Wrap State

**Files:**
- Modify: `include/yaoray/assets/imported_asset.hpp`
- Modify: `src/assets/gltf_loader.cpp`
- Create: `tests/fixtures/assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Add failing glTF wrap tests**

In `tests/assets_tests.cpp`, update `gltf_loader_loads_base_color_texture_material`:

```cpp
YR_EXPECT_EQ(result.mesh->materials[0].diffuse_texture_wrap_s, yr::TextureWrap::MirroredRepeat);
YR_EXPECT_EQ(result.mesh->materials[0].diffuse_texture_wrap_t, yr::TextureWrap::MirroredRepeat);
```

Create `tests/fixtures/assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf`:

```json
{
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ {
      "attributes": {
        "POSITION": 1,
        "TEXCOORD_0": 2
      },
      "indices": 0,
      "material": 0
    } ]
  } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
    }
  } ],
  "textures": [ {
    "sampler": 0,
    "source": 0
  } ],
  "images": [ {
    "uri": "../../SimpleTexture/glTF/testTexture.png"
  } ],
  "samplers": [ {
    "wrapS": 12345,
    "wrapT": 54321
  } ],
  "buffers": [ {
    "uri": "../../SimpleTexture/glTF/SimpleTexture.bin",
    "byteLength": 108
  } ],
  "bufferViews": [ {
    "buffer": 0,
    "byteOffset": 0,
    "byteLength": 12,
    "target": 34963
  }, {
    "buffer": 0,
    "byteOffset": 12,
    "byteLength": 96,
    "byteStride": 12,
    "target": 34962
  } ],
  "accessors": [ {
    "bufferView": 0,
    "byteOffset": 0,
    "componentType": 5123,
    "count": 6,
    "type": "SCALAR"
  }, {
    "bufferView": 1,
    "byteOffset": 0,
    "componentType": 5126,
    "count": 4,
    "type": "VEC3"
  }, {
    "bufferView": 1,
    "byteOffset": 48,
    "componentType": 5126,
    "count": 4,
    "type": "VEC2"
  } ],
  "asset": { "version": "2.0" }
}
```

Add this test after the base-color texture test:

```cpp
YR_TEST(gltf_loader_warns_and_defaults_for_unsupported_texture_wraps) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!result.warnings.empty());
    YR_EXPECT_TRUE(result.warnings[0].find("unsupported glTF texture wrap") != std::string::npos);
    YR_EXPECT_EQ(result.mesh->materials[0].diffuse_texture_wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.mesh->materials[0].diffuse_texture_wrap_t, yr::TextureWrap::Repeat);
}
```

- [ ] **Step 2: Run tests and verify expected failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `ImportedMaterial` does not expose texture wrap fields.

- [ ] **Step 3: Add imported wrap fields**

Modify `include/yaoray/assets/imported_asset.hpp`:

```cpp
#include <yaoray/core/texture_sampler.hpp>
```

Add fields to `ImportedMaterial`:

```cpp
TextureWrap diffuse_texture_wrap_s = TextureWrap::Repeat;
TextureWrap diffuse_texture_wrap_t = TextureWrap::Repeat;
```

- [ ] **Step 4: Import glTF sampler wrap values**

Modify `src/assets/gltf_loader.cpp`.

Add this include near the existing standard-library includes:

```cpp
#include <string_view>
```

Add this helper in the anonymous namespace:

```cpp
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
```

Change `ConvertMaterial()` to accept diagnostics:

```cpp
ImportedMaterial ConvertMaterial(
    const tinygltf::Model& model,
    const tinygltf::Material& material,
    const std::filesystem::path& asset_dir,
    AssetLoadResult& result
)
```

Inside the existing `baseColorTexture` block, after `const tinygltf::Texture& texture = ...`, add:

```cpp
if (texture.sampler >= 0 && static_cast<std::size_t>(texture.sampler) < model.samplers.size()) {
    const tinygltf::Sampler& sampler = model.samplers[static_cast<std::size_t>(texture.sampler)];
    imported.diffuse_texture_wrap_s = ConvertTextureWrap(sampler.wrapS, "wrapS", result);
    imported.diffuse_texture_wrap_t = ConvertTextureWrap(sampler.wrapT, "wrapT", result);
}
```

Update the call site that fills `mesh.materials`:

```cpp
mesh.materials.push_back(ConvertMaterial(model, material, asset_dir, result));
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: glTF loader tests pass, including mirrored repeat on `SimpleTexture` and repeat fallback warnings for `SimpleTextureBadWrap`.

- [ ] **Step 6: Commit**

```powershell
git add include/yaoray/assets/imported_asset.hpp src/assets/gltf_loader.cpp tests/assets_tests.cpp tests/fixtures/assets/gltf/SimpleTextureBadWrap/glTF/SimpleTextureBadWrap.gltf
git commit -m "feat: import gltf texture wrap modes"
```

---

## Task 4: Propagate Wrap State Through Scene Compilation

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Create: `tests/fixtures/assets/gltf/SimpleTextureClamp/glTF/SimpleTextureClamp.gltf`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add scene compiler tests for wrap propagation and texture-cache keys**

Create `tests/fixtures/assets/gltf/SimpleTextureClamp/glTF/SimpleTextureClamp.gltf`:

```json
{
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ {
      "attributes": {
        "POSITION": 1,
        "TEXCOORD_0": 2
      },
      "indices": 0,
      "material": 0
    } ]
  } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.0,
      "roughnessFactor": 1.0
    }
  } ],
  "textures": [ {
    "sampler": 0,
    "source": 0
  } ],
  "images": [ {
    "uri": "../../SimpleTexture/glTF/testTexture.png"
  } ],
  "samplers": [ {
    "wrapS": 33071,
    "wrapT": 33071
  } ],
  "buffers": [ {
    "uri": "../../SimpleTexture/glTF/SimpleTexture.bin",
    "byteLength": 108
  } ],
  "bufferViews": [ {
    "buffer": 0,
    "byteOffset": 0,
    "byteLength": 12,
    "target": 34963
  }, {
    "buffer": 0,
    "byteOffset": 12,
    "byteLength": 96,
    "byteStride": 12,
    "target": 34962
  } ],
  "accessors": [ {
    "bufferView": 0,
    "byteOffset": 0,
    "componentType": 5123,
    "count": 6,
    "type": "SCALAR"
  }, {
    "bufferView": 1,
    "byteOffset": 0,
    "componentType": 5126,
    "count": 4,
    "type": "VEC3"
  }, {
    "bufferView": 1,
    "byteOffset": 48,
    "componentType": 5126,
    "count": 4,
    "type": "VEC2"
  } ],
  "asset": { "version": "2.0" }
}
```

Add these tests after `scene_compiler_imports_gltf_texture_and_uvs` in `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_propagates_gltf_texture_wrap_modes) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"textured", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.textures[0].filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(compiled.textures[0].wrap_s, yr::TextureWrap::MirroredRepeat);
    YR_EXPECT_EQ(compiled.textures[0].wrap_t, yr::TextureWrap::MirroredRepeat);
}

YR_TEST(scene_compiler_texture_cache_keeps_distinct_sampler_state) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "mirrored",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.assets.push_back(yr::AssetDescription{
        "clamped",
        FixturePath("assets/gltf/SimpleTextureClamp/glTF/SimpleTextureClamp.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"mirrored", {}});
    yr::InstanceDescription second;
    second.asset = "clamped";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.textures[0].wrap_s, yr::TextureWrap::MirroredRepeat);
    YR_EXPECT_EQ(compiled.textures[1].wrap_s, yr::TextureWrap::ClampToEdge);
}
```

- [ ] **Step 2: Run tests and verify expected failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: scene compiler tests fail because loaded `RenderTexture` instances still use default repeat wrapping and the texture cache keys only by path.

- [ ] **Step 3: Add sampler-aware texture cache keys**

Modify `src/render/scene_compiler.cpp`.

Change the texture key helper:

```cpp
std::string TextureWrapName(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap::Repeat:
            return "repeat";
        case TextureWrap::ClampToEdge:
            return "clamp";
        case TextureWrap::MirroredRepeat:
            return "mirror";
    }
    return "repeat";
}

std::string CanonicalTextureKey(const std::filesystem::path& path, TextureWrap wrap_s, TextureWrap wrap_t) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    const std::string normalized = ec ? path.lexically_normal().generic_string() : canonical.generic_string();
    return normalized + "|s=" + TextureWrapName(wrap_s) + "|t=" + TextureWrapName(wrap_t);
}
```

Change `LoadTextureIndex()` signature:

```cpp
std::optional<int> LoadTextureIndex(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& path,
    TextureWrap wrap_s,
    TextureWrap wrap_t,
    TextureCache& texture_cache,
    std::vector<SceneDiagnostic>& diagnostics
)
```

Inside `LoadTextureIndex()`, build the key and apply sampler state:

```cpp
const std::string key = CanonicalTextureKey(path, wrap_s, wrap_t);
```

After successful `LoadPngTexture(path)` and before pushing:

```cpp
load.texture.wrap_s = wrap_s;
load.texture.wrap_t = wrap_t;
load.texture.filter = TextureFilter::Bilinear;
```

- [ ] **Step 4: Pass imported sampler state from materials**

In `CompileImportedMaterials()`, replace the existing call:

```cpp
LoadTextureIndex(scene, compiled, material.diffuse_texture_path, texture_cache, diagnostics);
```

with:

```cpp
LoadTextureIndex(
    scene,
    compiled,
    material.diffuse_texture_path,
    material.diffuse_texture_wrap_s,
    material.diffuse_texture_wrap_t,
    texture_cache,
    diagnostics
);
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: scene compiler tests pass, including two texture entries for the same image path with different wrap modes.

- [ ] **Step 6: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp tests/fixtures/assets/gltf/SimpleTextureClamp/glTF/SimpleTextureClamp.gltf
git commit -m "feat: propagate texture sampler state"
```

---

## Task 5: Documentation and Manual Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README current status and run notes**

In `README.md`, update the current status bullet:

```markdown
- PNG albedo/base-color texture loading with sRGB-to-linear conversion, bilinear filtering, and repeat/clamp/mirrored wrap support
```

Update the long `render` command paragraph so the texture sentence reads:

```markdown
OBJ assets can carry vertex normals, UV coordinates, and basic MTL diffuse textures through `map_Kd`; glTF/GLB assets can carry static mesh primitives, node transforms, vertex normals, UVs, base color factors, metallic/roughness factors approximated onto current material kinds, emissive factors, external PNG base-color textures, and base-color sampler wrap modes. PNG albedo/base-color textures are stored in linear RGB, sampled with bilinear filtering by default, and support repeat, clamp-to-edge, and mirrored-repeat wrapping. Texture v1 still does not implement mipmaps, anisotropic filtering, normal maps, alpha masks, bilinear user controls, imported roughness/metallic textures, CUDA texture parity, or HDRI textures.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, replace the current glTF importer paragraph sentence about textures with:

```markdown
The glTF importer converts static `.gltf` and `.glb` assets into the same shared imported-mesh representation used by OBJ. It supports default or first scenes, node hierarchy transforms, `TRIANGLES` primitives, positions, optional normals, optional UVs, indexed and non-indexed geometry, base-color factors, external PNG base-color textures, base-color sampler wrap modes, emissive factors, and conservative metallic/roughness mapping onto current diffuse/metal/plastic material kinds. PNG albedo/base-color textures are converted from sRGB to linear RGB on load, bilinear-filtered by default, and can repeat, clamp to edge, or mirror repeat according to imported sampler state.
```

Add this limitation sentence to the material showcase paragraph:

```markdown
Texture Quality v1 still has no mipmaps, anisotropic filtering, normal maps, alpha handling, HDRI textures, imported roughness/metallic textures, or CUDA texture sampling parity.
```

- [ ] **Step 3: Run full Debug verification**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `ctest` reports `100% tests passed`.

- [ ] **Step 4: Run Release manual render checks**

Run:

```powershell
cmake --build build-release --config Release
.\build-release\Release\yaoray.exe render .\scenes\examples\gltf_textured_asset.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

Expected: all three commands exit 0 and write PNG files under `scenes/examples/out/`.

If local Duck files exist, run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\duck_gltf.toml --backend cpu
```

Expected: command exits 0 and writes `scenes/examples/out/duck_gltf.png`. Do not stage Duck files as part of this task.

- [ ] **Step 5: Commit**

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: document texture quality v1"
```

---

## Task 6: Final Integration Check

**Files:**
- Verify only.

- [ ] **Step 1: Confirm untracked Duck files remain unstaged**

Run:

```powershell
git status --short
```

Expected output includes untracked Duck files only if they exist locally:

```text
?? scenes/examples/assets/gltf/Duck/
?? scenes/examples/duck_gltf.toml
```

Expected output does not include staged Duck files.

- [ ] **Step 2: Inspect recent commits**

Run:

```powershell
git log --oneline -8
```

Expected: recent commits include:

```text
docs: document texture quality v1
feat: propagate texture sampler state
feat: import gltf texture wrap modes
feat: sample textures through render sampler
feat: add bilinear texture sampling
```

- [ ] **Step 3: Run final test suite**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `100% tests passed`.

- [ ] **Step 4: Finish the branch**

Use `superpowers:finishing-a-development-branch` after tests pass. Offer the standard options to merge locally, push/create PR, keep branch, or discard work.
