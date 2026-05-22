# YaoRay Large glTF Sponza v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make YaoRay load and manually render Khronos Sponza as a local, ignored, native glTF large-scene validation target.

**Architecture:** Keep large assets outside Git under `external/assets/`. Generalize the render texture loader from PNG-only to LDR PNG/JPEG while preserving the render compiler's texture cache, sampler, and color-space policy. Add observability at the backend prepare boundary and CLI layer so large-scene failures can be attributed to load/compile, BVH prepare, or render.

**Tech Stack:** C++20, CMake/CTest, stb_image, tinygltf, TOML scene files, CPU backend/BVH path tracer, local sparse Git checkout for external assets.

---

## File Structure

- Create `docs/assets/sponza-local-benchmark.md`: local-only Sponza download and smoke-render instructions.
- Create `scenes/examples/local_sponza.toml`: committed manual scene template that points at ignored local Sponza assets.
- Create `tests/fixtures/assets/checker_2x2.jpg`: tiny committed JPEG fixture generated from the existing PNG fixture.
- Create `tests/fixtures/assets/gltf/JpegTexture/glTF/JpegTexture.gltf`: tiny glTF fixture that references the JPEG fixture.
- Modify `include/yaoray/render/texture.hpp`: expose `LoadLdrTexture()` while keeping `LoadPngTexture()` compatibility.
- Modify `src/render/texture.cpp`: implement PNG/JPG/JPEG LDR loading through one stb_image path.
- Modify `src/render/scene_compiler.cpp`: switch material texture image loading from `LoadPngTexture()` to `LoadLdrTexture()`.
- Modify `tests/texture_tests.cpp`: add JPEG unit coverage and keep PNG wrapper behavior covered.
- Modify `tests/render_scene_tests.cpp`: add scene compiler coverage for JPEG glTF texture images.
- Modify `include/yaoray/backends/backend.hpp`: add backend prepare timing to `BackendPrepareResult`.
- Modify `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`: add CPU prepare elapsed time to `CpuPrepareResult`.
- Modify `src/backends/cpu/cpu_prepared_scene.cpp`: time CPU BVH build and propagate elapsed seconds on success and failure.
- Modify `src/backends/cpu/cpu_debug_backend.cpp`: copy CPU prepare elapsed time into backend prepare result.
- Modify `tests/backend_tests.cpp`: assert CPU prepare timing is exposed through both CPU-specific and backend-generic APIs.
- Modify `src/app/main.cpp`: print material count, texture count, estimated decoded texture memory, and prepare seconds.
- Modify `CMakeLists.txt`: extend existing CLI regex checks for the new stats output.

## Task 1: Add JPEG Texture Loader Contract Tests

**Files:**
- Create: `tests/fixtures/assets/checker_2x2.jpg`
- Modify: `tests/texture_tests.cpp`

- [ ] **Step 1: Create a real JPEG fixture from the existing PNG fixture**

Run on macOS:

```bash
sips -s format jpeg tests/fixtures/assets/checker_2x2.png --out tests/fixtures/assets/checker_2x2.jpg
file tests/fixtures/assets/checker_2x2.jpg
```

Expected:

```text
tests/fixtures/assets/checker_2x2.jpg: JPEG image data
```

- [ ] **Step 2: Write failing JPEG loader tests**

Add these tests after `texture_loader_reads_png_texels` in `tests/texture_tests.cpp`:

```cpp
YR_TEST(texture_loader_reads_jpeg_texels) {
    const yr::TextureLoadResult result = yr::LoadLdrTexture(TextureFixturePath("assets/checker_2x2.jpg"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    for (const yr::Color4f& texel : result.texture.texels) {
        YR_EXPECT_TRUE(texel.x >= 0.0f);
        YR_EXPECT_TRUE(texel.x <= 1.0f);
        YR_EXPECT_TRUE(texel.y >= 0.0f);
        YR_EXPECT_TRUE(texel.y <= 1.0f);
        YR_EXPECT_TRUE(texel.z >= 0.0f);
        YR_EXPECT_TRUE(texel.z <= 1.0f);
        YR_EXPECT_NEAR(texel.w, 1.0, 1e-6);
    }
}

YR_TEST(texture_loader_uses_requested_color_space_for_jpeg) {
    const std::filesystem::path path = TextureFixturePath("assets/checker_2x2.jpg");
    const yr::TextureLoadResult srgb = yr::LoadLdrTexture(path, yr::TextureColorSpace::Srgb);
    const yr::TextureLoadResult linear = yr::LoadLdrTexture(path, yr::TextureColorSpace::Linear);

    YR_EXPECT_TRUE(srgb.ok);
    YR_EXPECT_TRUE(linear.ok);
    YR_EXPECT_EQ(srgb.texture.width, linear.texture.width);
    YR_EXPECT_EQ(srgb.texture.height, linear.texture.height);
    YR_EXPECT_EQ(srgb.texture.texels.size(), linear.texture.texels.size());
    YR_EXPECT_EQ(srgb.texture.color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(linear.texture.color_space, yr::TextureColorSpace::Linear);

    bool found_rgb_difference = false;
    for (std::size_t index = 0; index < srgb.texture.texels.size(); ++index) {
        const yr::Color4f srgb_texel = srgb.texture.texels[index];
        const yr::Color4f linear_texel = linear.texture.texels[index];
        YR_EXPECT_NEAR(srgb_texel.w, 1.0, 1e-6);
        YR_EXPECT_NEAR(linear_texel.w, 1.0, 1e-6);
        found_rgb_difference = found_rgb_difference ||
                               std::abs(srgb_texel.x - linear_texel.x) > 1.0e-6f ||
                               std::abs(srgb_texel.y - linear_texel.y) > 1.0e-6f ||
                               std::abs(srgb_texel.z - linear_texel.z) > 1.0e-6f;
    }
    YR_EXPECT_TRUE(found_rgb_difference);
}

YR_TEST(texture_loader_rejects_unsupported_ldr_extension) {
    const yr::TextureLoadResult result = yr::LoadLdrTexture(TextureFixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
    YR_EXPECT_TRUE(result.error.find(".jpg") != std::string::npos);
    YR_EXPECT_TRUE(result.error.find(".jpeg") != std::string::npos);
}
```

- [ ] **Step 3: Run the texture tests and verify they fail for the right reason**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: build fails because `yr::LoadLdrTexture` is not declared, or the test executable fails because JPEG loading is not implemented yet.

## Task 2: Implement General LDR Texture Loading

**Files:**
- Modify: `include/yaoray/render/texture.hpp`
- Modify: `src/render/texture.cpp`
- Modify: `tests/texture_tests.cpp`

- [ ] **Step 1: Add the public LDR texture loader API**

In `include/yaoray/render/texture.hpp`, add `LoadLdrTexture()` immediately before `LoadPngTexture()`:

```cpp
TextureLoadResult LoadLdrTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space = TextureColorSpace::Srgb);
```

- [ ] **Step 2: Replace PNG-only decode helpers with LDR helpers**

In `src/render/texture.cpp`, replace `DecodePngChannel()` with these helpers:

```cpp
bool IsSupportedLdrExtension(std::string_view extension) {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
}

float DecodeLdrChannel(unsigned char value, TextureColorSpace color_space) {
    const float normalized = static_cast<float>(value) / 255.0f;
    return color_space == TextureColorSpace::Srgb ? SrgbToLinear(normalized) : normalized;
}
```

Add `#include <string_view>` near the other standard includes.

- [ ] **Step 3: Implement `LoadLdrTexture()` and keep `LoadPngTexture()` as a compatibility wrapper**

Replace the current `LoadPngTexture()` implementation in `src/render/texture.cpp` with:

```cpp
TextureLoadResult LoadLdrTexture(const std::filesystem::path& path, TextureColorSpace color_space) {
    const std::string extension = LowerExtension(path);
    if (!IsSupportedLdrExtension(extension)) {
        return TextureLoadResult{
            RenderTexture{},
            false,
            "texture path must use a .png, .jpg, or .jpeg extension: " + path.generic_string()
        };
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
            "failed to load LDR texture: " + path.generic_string() +
                (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{
            RenderTexture{},
            false,
            "LDR texture has invalid dimensions: " + path.generic_string()
        };
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.color_space = color_space;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 4;
        texture.texels.push_back(Color4f{
            DecodeLdrChannel(pixels[base + 0], color_space),
            DecodeLdrChannel(pixels[base + 1], color_space),
            DecodeLdrChannel(pixels[base + 2], color_space),
            static_cast<float>(pixels[base + 3]) / 255.0f
        });
    }
    stbi_image_free(pixels);

    return TextureLoadResult{std::move(texture), true, {}};
}

TextureLoadResult LoadPngTexture(const std::filesystem::path& path, TextureColorSpace color_space) {
    if (LowerExtension(path) != ".png") {
        return TextureLoadResult{RenderTexture{}, false, "texture path must use a .png extension: " + path.generic_string()};
    }
    return LoadLdrTexture(path, color_space);
}
```

- [ ] **Step 4: Keep the existing PNG wrapper test and add one LDR missing-file test**

Leave `texture_loader_rejects_non_png_extension` using `LoadPngTexture()` unchanged. Update `texture_loader_reports_missing_file` to call `LoadLdrTexture()` and use an LDR extension:

```cpp
YR_TEST(texture_loader_reports_missing_file) {
    const yr::TextureLoadResult result = yr::LoadLdrTexture(TextureFixturePath("assets/missing_texture.jpg"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("not found") != std::string::npos);
}
```

- [ ] **Step 5: Run texture tests and commit**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass.

Commit:

```bash
git add include/yaoray/render/texture.hpp src/render/texture.cpp tests/texture_tests.cpp tests/fixtures/assets/checker_2x2.jpg
git commit -m "feat: add ldr texture loading"
```

## Task 3: Compile glTF Textures Referencing JPEG Images

**Files:**
- Create: `tests/fixtures/assets/gltf/JpegTexture/glTF/JpegTexture.gltf`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add a tiny glTF fixture that references the JPEG fixture**

Create `tests/fixtures/assets/gltf/JpegTexture/glTF/JpegTexture.gltf` with this exact content:

```json
{
  "scene": 0,
  "scenes": [
    {
      "nodes": [0]
    }
  ],
  "nodes": [
    {
      "mesh": 0
    }
  ],
  "meshes": [
    {
      "primitives": [
        {
          "attributes": {
            "POSITION": 1,
            "TEXCOORD_0": 2
          },
          "indices": 0,
          "material": 0
        }
      ]
    }
  ],
  "materials": [
    {
      "pbrMetallicRoughness": {
        "baseColorTexture": {
          "index": 0
        },
        "metallicFactor": 0.0,
        "roughnessFactor": 1.0
      }
    }
  ],
  "textures": [
    {
      "sampler": 0,
      "source": 0
    }
  ],
  "images": [
    {
      "uri": "../../../checker_2x2.jpg"
    }
  ],
  "samplers": [
    {
      "magFilter": 9729,
      "minFilter": 9987,
      "wrapS": 10497,
      "wrapT": 10497
    }
  ],
  "buffers": [
    {
      "uri": "../../SimpleTexture/glTF/SimpleTexture.bin",
      "byteLength": 108
    }
  ],
  "bufferViews": [
    {
      "buffer": 0,
      "byteOffset": 0,
      "byteLength": 12,
      "target": 34963
    },
    {
      "buffer": 0,
      "byteOffset": 12,
      "byteLength": 96,
      "byteStride": 12,
      "target": 34962
    }
  ],
  "accessors": [
    {
      "bufferView": 0,
      "byteOffset": 0,
      "componentType": 5123,
      "count": 6,
      "type": "SCALAR",
      "max": [3],
      "min": [0]
    },
    {
      "bufferView": 1,
      "byteOffset": 0,
      "componentType": 5126,
      "count": 4,
      "type": "VEC3",
      "max": [1.0, 1.0, 0.0],
      "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1,
      "byteOffset": 48,
      "componentType": 5126,
      "count": 4,
      "type": "VEC2",
      "max": [1.0, 1.0],
      "min": [0.0, 0.0]
    }
  ],
  "asset": {
    "version": "2.0"
  }
}
```

- [ ] **Step 2: Write the failing scene compiler test**

Add this test after `scene_compiler_imports_gltf_texture_and_uvs` in `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_imports_gltf_jpeg_texture) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured_jpeg",
        FixturePath("assets/gltf/JpegTexture/glTF/JpegTexture.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"textured_jpeg", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderSceneIR& compiled = result.scene.value();
    YR_EXPECT_TRUE(!compiled.triangles.empty());
    YR_EXPECT_TRUE(compiled.triangles[0].has_uv);
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.textures[0].color_space, yr::TextureColorSpace::Srgb);
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
}
```

- [ ] **Step 3: Run the new test and verify it fails before the compiler switch**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: test fails with a scene diagnostic containing the PNG-only extension error.

- [ ] **Step 4: Switch render compiler texture loading to `LoadLdrTexture()`**

In `src/render/scene_compiler.cpp`, change the loader call inside `LoadTextureIndex()`:

```cpp
TextureLoadResult load = LoadLdrTexture(path, TextureColorSpaceForUsage(usage));
```

- [ ] **Step 5: Run render scene tests and commit**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass, including `scene_compiler_imports_gltf_jpeg_texture` and `scene_compiler_texture_cache_keeps_distinct_color_spaces`.

Commit:

```bash
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp tests/fixtures/assets/gltf/JpegTexture/glTF/JpegTexture.gltf
git commit -m "feat: compile jpeg gltf textures"
```

## Task 4: Expose CPU Prepare Timing

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`
- Modify: `src/backends/cpu/cpu_prepared_scene.cpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Write failing prepare timing assertions**

In `tests/backend_tests.cpp`, add these assertions to `cpu_prepare_scene_builds_bvh_from_render_scene_ir` after the existing BVH assertions:

```cpp
    YR_EXPECT_TRUE(prepared.elapsed_seconds >= 0.0);
```

Add this assertion to `cpu_backend_prepare_builds_cpu_prepared_scene` after `YR_EXPECT_TRUE(prepared.scene != nullptr);`:

```cpp
    YR_EXPECT_TRUE(prepared.elapsed_seconds >= 0.0);
```

- [ ] **Step 2: Run backend tests and verify they fail to compile**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `CpuPrepareResult::elapsed_seconds` and `BackendPrepareResult::elapsed_seconds` do not exist.

- [ ] **Step 3: Add elapsed seconds fields**

In `include/yaoray/backends/backend.hpp`, update `BackendPrepareResult`:

```cpp
struct BackendPrepareResult {
    bool ok = false;
    std::string error;
    std::unique_ptr<PreparedScene> scene;
    double elapsed_seconds = 0.0;
};
```

In `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`, update `CpuPrepareResult`:

```cpp
struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
    double elapsed_seconds = 0.0;
};
```

- [ ] **Step 4: Measure CPU BVH build time**

In `src/backends/cpu/cpu_prepared_scene.cpp`, add `#include <chrono>` near the includes. Replace `PrepareCpuScene()` with:

```cpp
CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene) {
    CpuPrepareResult result;

    const auto start = std::chrono::steady_clock::now();
    BvhBuildResult build = BuildBvh(scene.triangles);
    const auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(scene, std::move(build.bvh));
    return result;
}
```

- [ ] **Step 5: Propagate prepare timing through the backend API**

In `src/backends/cpu/cpu_debug_backend.cpp`, set `elapsed_seconds` at the start of `ToBackendPrepareResult()`:

```cpp
BackendPrepareResult ToBackendPrepareResult(CpuPrepareResult prepared) {
    BackendPrepareResult result;
    result.elapsed_seconds = prepared.elapsed_seconds;
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    CpuPreparedScene& cpu_scene = prepared.scene.value();
    result.ok = true;
    result.scene = std::make_unique<CpuPreparedScene>(cpu_scene.Scene(), std::move(cpu_scene.bvh));
    return result;
}
```

- [ ] **Step 6: Run backend tests and commit**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass, including `cpu_prepare_scene_builds_bvh_from_render_scene_ir` and `cpu_backend_prepare_builds_cpu_prepared_scene`.

Commit:

```bash
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_prepared_scene.hpp src/backends/cpu/cpu_prepared_scene.cpp src/backends/cpu/cpu_debug_backend.cpp tests/backend_tests.cpp
git commit -m "feat: report backend prepare timing"
```

## Task 5: Print Large-Scene CLI Stats

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add texture memory helpers**

In `src/app/main.cpp`, add `#include <cstddef>` near the standard includes. Add these helpers after `TotalSamples()`:

```cpp
std::uint64_t EstimateTextureMemoryBytes(const yr::RenderSceneIR& scene) {
    std::uint64_t total = 0;
    for (const yr::RenderTexture& texture : scene.textures) {
        total += static_cast<std::uint64_t>(texture.texels.size() * sizeof(yr::Color4f));
    }
    return total;
}

double BytesToMiB(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
```

- [ ] **Step 2: Print compile-time resource counts**

In `RunRender()`, immediately after the existing triangle count line:

```cpp
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
    std::cout << "Compiled materials: " << render_scene.materials.size() << '\n';
    std::cout << "Compiled textures: " << render_scene.textures.size() << '\n';
    std::cout << "Texture memory MiB: " << BytesToMiB(EstimateTextureMemoryBytes(render_scene)) << '\n';
```

- [ ] **Step 3: Print backend prepare timing**

After successful `backend->Prepare(render_scene)` and before `backend->Render(...)`, add:

```cpp
    std::cout << "Prepare seconds: " << prepare_result.elapsed_seconds << '\n';
```

- [ ] **Step 4: Extend existing CLI regex checks**

In `CMakeLists.txt`, add these expected regex lines to the `yaoray_cli_render_gltf` test:

```cmake
            "Compiled materials:"
            "Compiled textures:"
            "Texture memory MiB:"
            "Prepare seconds:"
```

Add the same four regex lines to `yaoray_cli_render_path`, because it exercises CPU path rendering and already checks throughput stats.

- [ ] **Step 5: Run CLI tests and commit**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build -R "yaoray_cli_render_(gltf|path)" --output-on-failure -C Debug
```

Expected: both CLI render tests pass and output contains the new stats.

Commit:

```bash
git add src/app/main.cpp CMakeLists.txt
git commit -m "feat: print large scene render stats"
```

## Task 6: Add Local Sponza Download Documentation and Scene Template

**Files:**
- Create: `docs/assets/sponza-local-benchmark.md`
- Create: `scenes/examples/local_sponza.toml`

- [ ] **Step 1: Add the local benchmark document**

Create `docs/assets/sponza-local-benchmark.md`:

````markdown
# Local Sponza Benchmark

This scene is a manual large-glTF validation target for YaoRay. The model files
are intentionally not committed to Git.

## Source

- Repository: `https://github.com/KhronosGroup/glTF-Sample-Assets`
- Model: `Models/Sponza/glTF/Sponza.gltf`
- Local expected path: `external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf`

## Download

Run from the repository root:

```bash
mkdir -p external/assets/large-gltf/sponza
git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git external/assets/large-gltf/sponza/glTF-Sample-Assets
git -C external/assets/large-gltf/sponza/glTF-Sample-Assets sparse-checkout set --no-cone /Models/Sponza/glTF /Models/Sponza/LICENSE.md /Models/Sponza/README.md /Models/Sponza/metadata.json
cp -R external/assets/large-gltf/sponza/glTF-Sample-Assets/Models/Sponza external/assets/large-gltf/sponza/Sponza
```

The repository `.gitignore` excludes `external/assets/`, so the downloaded model
and textures stay local.

## Smoke Render

```bash
cmake --build build --config Debug
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
```

The smoke render is intentionally low resolution and low sample count. It proves
that the native glTF asset path, decoded texture path, CPU BVH prepare, and CPU
path tracer can handle a larger scene. It is not a final quality preset.
````

- [ ] **Step 2: Add the manual Sponza scene template**

Create `scenes/examples/local_sponza.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 400
height = 225
spp = 2
max_depth = 4
seed = 91
sampler = "stratified"
threads = 0
light_samples = 1
radiance_clamp = 20.0

[film]
output = "out/local_sponza.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0.0, 1.2, 6.0]
target = [0.0, 1.2, 0.0]
fov_y = 45.0

[[assets]]
name = "sponza"
path = "../../external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf"

[[instances]]
asset = "sponza"

[[lights]]
type = "area"
position = [0.0, 5.0, 0.0]
size = [6.0, 6.0]
radiance = [8.0, 8.0, 8.0]

[environment]
type = "constant"
radiance = [0.03, 0.035, 0.04]
strength = 1.0
```

- [ ] **Step 3: Commit docs and template**

Run:

```bash
git add docs/assets/sponza-local-benchmark.md scenes/examples/local_sponza.toml
git commit -m "docs: add local sponza benchmark"
```

## Task 7: Download Local Sponza Assets

**Files:**
- Local ignored directory: `external/assets/large-gltf/sponza/`

- [ ] **Step 1: Confirm the large asset directory is ignored**

Run:

```bash
git check-ignore -v external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf
```

Expected: command prints the `.gitignore` rule for `external/assets/`.

- [ ] **Step 2: Download the Sponza glTF subtree**

Run:

```bash
mkdir -p external/assets/large-gltf/sponza
git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git external/assets/large-gltf/sponza/glTF-Sample-Assets
git -C external/assets/large-gltf/sponza/glTF-Sample-Assets sparse-checkout set --no-cone /Models/Sponza/glTF /Models/Sponza/LICENSE.md /Models/Sponza/README.md /Models/Sponza/metadata.json
cp -R external/assets/large-gltf/sponza/glTF-Sample-Assets/Models/Sponza external/assets/large-gltf/sponza/Sponza
test -f external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf
```

Expected: `Sponza.gltf` exists at the expected local path.

- [ ] **Step 3: Verify no external model files are staged**

Run:

```bash
git status --short external/assets/large-gltf/sponza
```

Expected: no output.

## Task 8: Full Automated and Manual Verification

**Files:**
- No new file changes expected.

- [ ] **Step 1: Run full build and default tests**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all default tests pass. No test downloads or renders Sponza.

- [ ] **Step 2: Run the Sponza manual smoke render**

Run:

```bash
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
```

Expected: render exits with code 0, `scenes/examples/out/local_sponza.png` exists, and stdout includes:

```text
Compiled triangles:
Compiled materials:
Compiled textures:
Texture memory MiB:
Prepare seconds:
Elapsed seconds:
```

- [ ] **Step 3: Check Git does not include large assets**

Run:

```bash
git status --short
git status --short external/assets
```

Expected: only intentional source/docs/test fixture changes are visible in the first command before commits; the second command prints no tracked or untracked Sponza model files.

- [ ] **Step 4: Commit any final small fixes**

If Step 1 or Step 2 required source, scene-template, or docs tuning, commit those tracked changes:

```bash
git add CMakeLists.txt docs/assets/sponza-local-benchmark.md include src tests scenes/examples/local_sponza.toml
git commit -m "fix: tune sponza smoke render workflow"
```

Expected: no large files from `external/assets/` are included in the commit.

## Self-Review

- Spec coverage: the plan covers local Sponza download, keeping Sponza out of Git, JPG/JPEG loading, per-slot color-space policy, local TOML template, large-scene CLI stats, CPU smoke render, and fast default CTest.
- Non-goals preserved: no FBX/Bistro conversion, no DDS/TGA/KTX loader, no material extension implementation, no default Sponza CTest, and no large model commits.
- Placeholder scan: the plan contains concrete paths, commands, code snippets, expected outcomes, and commit messages; it does not contain deferred implementation placeholders.
- Type consistency: `LoadLdrTexture`, `BackendPrepareResult::elapsed_seconds`, `CpuPrepareResult::elapsed_seconds`, and CLI helper names are introduced before later tasks use them.
