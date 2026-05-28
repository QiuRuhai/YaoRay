# YaoRay M1 Slice 3 — `texture_test` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `Texture "imagemap"` wrap-mode parameter and the `"string normalmap"` material parameter through the YaoRay PBRT pipeline, and ship a small `texture_test` scene that exercises the texture pipeline in isolation (wrap modes visually + normal-map data path via unit tests).

**Architecture:** Two small additions to the scene compiler. (1) `CompileImagemapTexture` (introduced in Slice 2) gains a `wrap` param reader that maps PBRT's `"repeat" | "clamp" | "black"` strings to the existing `TextureWrap` enum, with `"black"` degrading to `ClampToEdge` plus a Warning (true black-border wrap lands in M2). (2) `CompileMaterial` gains a `CompileNormalMap` helper that detects `"string normalmap"` on any material kind, loads the file as a linear texture, and fills `RenderMaterial.normal_map` plus `RenderMaterial.normal_scale`. The CPU resolver path (`ResolveNormalMap` in `cpu_material.cpp`) is unchanged.

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. No new third-party libraries.

---

## Spec Coverage

This plan implements Slice 3 of `docs/superpowers/specs/2026-05-27-yaoray-m1-dining-room-design.md`:

1. **`Texture "imagemap"` wrap mode** — repeat / clamp / black (degraded to clamp).
2. **`"string normalmap"` material parameter** — end-to-end into `RenderMaterial.normal_map`.
3. **Color space full coverage** — explicit `encoding "linear"` already works (Slice 2). Slice 3 confirms with an additional test specifically targeting linear-encoded normal maps.
4. **The `scenes/pbrt/texture_test/texture_test.pbrt` scene asset** + small PNG textures.

### Out of scope (deferred to Slice 4)

- **Trianglemesh tangent (`S`) pass-through.** `ResolveNormalMap` in `cpu_material.cpp:42` requires `prim.has_tangents` before applying the normal map. M0's `CompileTriangleMeshShape` does not yet pass `S` arrays into `RenderVertex.tangent`. So the texture_test scene can compile its normal map data successfully and the index lands in `material.normal_map`, but the **shading effect of the normal map is not visible** in the rendered image yet. This is intentional — Slice 4 wires trianglemesh tangents, and the dining-room scene will show the visual effect.
- **MikkT-style auto-tangent generation** when meshes have UVs but no explicit S. Not in M1.
- **Black-border wrap mode** as a distinct sampler behavior. M1 degrades it to clamp.

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `tests/scene_compiler_wrap_mode_tests.cpp` | Unit tests: each PBRT wrap string maps to the right `TextureWrap` enum + black wrap emits a Warning. |
| `tests/scene_compiler_normal_map_tests.cpp` | Unit tests: `"string normalmap"` populates `RenderMaterial.normal_map` and `normal_scale`. |
| `scenes/pbrt/texture_test/texture_test.pbrt` | Three textured planes demonstrating Repeat / ClampToEdge / black wrap modes side by side. |
| `scenes/pbrt/texture_test/texture_test_smoke.pbrt` | Low-resolution variant for CTest. |
| `scenes/pbrt/texture_test/textures/checker.png` | Small (~150 B) checker PNG (copy of `tests/fixtures/assets/checker_2x2.png`). |
| `scenes/pbrt/texture_test/textures/flat_normal.png` | Small flat-normal PNG (copy of `tests/fixtures/assets/checker_2x2.png` — visual fidelity is not required because Slice 3 doesn't visually apply normal maps; the test material still needs *some* PNG to load). |

**Modified files:**

| Path | Change |
|------|--------|
| `src/render/scene_compiler.cpp` | Extend `CompileImagemapTexture` with wrap-mode handling. Add `CompileNormalMap` helper. Call `CompileNormalMap` from every branch of `CompileMaterial` that should support normal maps. |
| `CMakeLists.txt` | Register the two new test files. Add the `yaoray_cli_render_pbrt_texture_test` CTest entry. |
| `README.md` | Add `texture_test` to the runnable scenes list. |

---

## Setting up the worktree

Create an isolated worktree off of local `main` (HEAD `d284d91`, post-Slice-2). Use the harness-native `EnterWorktree` tool with name `m1-texture-test` (or have a controller run `superpowers:using-git-worktrees`). After entering the worktree, run `git merge main --ff-only` if needed to bring the Slice 2 + Slice 3 plan commits in (the worktree base may default to `origin/main`).

Verify baseline:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe       # 149/149 PASS (post-Slice-2 baseline).
cd build && ctest --output-on-failure -C Release  # 7/7 PASS.
```

All commits in this plan land on the worktree branch.

---

## Task 1: Wrap-mode support in `Texture "imagemap"`

**Files:**
- Modify: `src/render/scene_compiler.cpp` (extend `CompileImagemapTexture` to read `"string wrap"` and apply to the loaded texture).
- Create: `tests/scene_compiler_wrap_mode_tests.cpp`.
- Modify: `CMakeLists.txt` (register the new test file).

PBRT v4 `Texture "imagemap"` accepts `"string wrap" ["repeat" | "clamp" | "black"]`. The YaoRay `TextureWrap` enum (in `include/yaoray/core/texture_sampler.hpp`) has `Repeat`, `ClampToEdge`, `MirroredRepeat`. There is no `Black` — for M1 we map "black" to `ClampToEdge` and emit a Warning telling the author the M1-degraded behavior. (Adding a true black-border sampler is M2 work.)

PBRT applies the wrap value to **both** S and T axes — there is no separate `wraps`/`wrapt` directive in v4 syntax. Set `wrap_s == wrap_t`.

- [ ] **Step 1: Write the failing tests**

Create `tests/scene_compiler_wrap_mode_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <filesystem>
#include <string>

namespace {

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = YAORAY_TEST_DATA_DIR;
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

yr::PbrtEntity ImagemapTextureWithWrap(const std::string& wrap_value) {
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    tex.params.push_back(yr::PbrtParam{"string", "wrap", {}, {}, {wrap_value}, {}});
    return tex;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& substring
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_imagemap_wrap_repeat_sets_repeat_on_both_axes) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("repeat");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::Repeat);
}

YR_TEST(scene_compiler_imagemap_wrap_clamp_sets_clamp_on_both_axes) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("clamp");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::ClampToEdge);
}

YR_TEST(scene_compiler_imagemap_wrap_black_degrades_to_clamp_with_warning) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("black");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(tex.wrap_t == yr::TextureWrap::ClampToEdge);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "black"));
}

YR_TEST(scene_compiler_imagemap_wrap_unknown_value_warns_and_falls_back_to_repeat) {
    yr::PbrtScene pbrt = MinimalScene();
    pbrt.named_textures["t"] = ImagemapTextureWithWrap("totally-bogus-wrap");

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& tex = result.scene->textures[0];
    YR_EXPECT_TRUE(tex.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "totally-bogus-wrap"));
}

YR_TEST(scene_compiler_imagemap_no_wrap_param_defaults_to_repeat) {
    yr::PbrtScene pbrt = MinimalScene();
    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["t"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    const yr::RenderTexture& loaded = result.scene->textures[0];
    YR_EXPECT_TRUE(loaded.wrap_s == yr::TextureWrap::Repeat);
    YR_EXPECT_TRUE(loaded.wrap_t == yr::TextureWrap::Repeat);
}
```

Register the new test file in `CMakeLists.txt` next to the Slice 2 test files:

```cmake
add_executable(yaoray_tests
    ...
    tests/scene_compiler_wrap_mode_tests.cpp   # <-- new
    ...
)
```

- [ ] **Step 2: Run the tests and verify they fail**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the 5 new wrap-mode tests fail — the first three because `CompileImagemapTexture` ignores `wrap` and leaves the loader's default (Repeat for both axes, set by `LoadLdrTexture`), the fourth because `bogus` is also ignored silently, the fifth because the assertion against the default `Repeat` should already pass... wait, that last one will incidentally pass even before the implementation. That's fine — the failing tests are the first four.

- [ ] **Step 3: Implement the wrap-mode handler**

In `src/render/scene_compiler.cpp`, add a helper near the other texture helpers (anonymous namespace, alongside `InferTextureColorSpace`):

```cpp
TextureWrap ParseWrapMode(
    const std::string& wrap_value,
    const std::string& texture_name,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (wrap_value == "repeat" || wrap_value.empty()) {
        return TextureWrap::Repeat;
    }
    if (wrap_value == "clamp") {
        return TextureWrap::ClampToEdge;
    }
    if (wrap_value == "black") {
        diagnostics.push_back(Warning(scene, "Texture." + texture_name,
            "wrap mode 'black' is not fully supported in M1; degraded to 'clamp'. "
            "True black-border sampling lands in M2."));
        return TextureWrap::ClampToEdge;
    }
    diagnostics.push_back(Warning(scene, "Texture." + texture_name,
        "unknown wrap mode '" + wrap_value + "'; falling back to 'repeat'"));
    return TextureWrap::Repeat;
}
```

Now extend `CompileImagemapTexture` — between the load step and the push to `ir.textures`, read the wrap param and set both axes:

Locate the existing line that pushes the loaded texture into `ir.textures` (looks like `ir.textures.push_back(std::move(load.texture));`). Just before that line, add:

```cpp
    // PBRT applies the same wrap to both axes. Defaults to repeat.
    std::string wrap_value;
    const PbrtParam* wrap_param = FindParam(entity.params, "wrap");
    if (wrap_param != nullptr && !wrap_param->strings.empty()) {
        wrap_value = wrap_param->strings[0];
    }
    const TextureWrap wrap = ParseWrapMode(wrap_value, name, scene, diagnostics);
    load.texture.wrap_s = wrap;
    load.texture.wrap_t = wrap;
```

- [ ] **Step 4: Run tests and verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all 5 wrap-mode tests pass. Previous tests still pass. New total: 149 + 5 = 154.

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp \
        tests/scene_compiler_wrap_mode_tests.cpp \
        CMakeLists.txt
git commit -m "feat: imagemap textures honor PBRT wrap mode (repeat/clamp/black→clamp)"
```

---

## Task 2: `"string normalmap"` material parameter

**Files:**
- Modify: `src/render/scene_compiler.cpp` (add `CompileNormalMap` helper; call it from `CompileMaterial`).
- Create: `tests/scene_compiler_normal_map_tests.cpp`.
- Modify: `CMakeLists.txt` (register the new test file).

PBRT v4 normal maps live on the material as a `"string normalmap" ["path/to/normal.png"]` parameter, distinct from the `Texture "imagemap"` system. They are always loaded as **linear** (no sRGB decode — the values are direction components, not radiometric).

PBRT v4 also has a `"float normalscale"` parameter (default `1.0`) that lifts/dampens the perturbation. M0's `RenderMaterial.normal_scale` already exists; we wire that too.

**Crucial scope note:** Slice 3 only wires the data path (parser → IR). The CPU resolver (`cpu_material.cpp:42`) requires `prim.has_tangents` to actually apply the normal map. Trianglemesh tangent pass-through (the `S` parameter) is Slice 4. So a Slice 3 test scene that uses `normalmap` on a trianglemesh will compile cleanly, the texture will load into `ir.textures`, but the rendered image will look the same as if the normal map were absent. The unit tests below verify the compile-side wiring; the visual demonstration waits for Slice 4 + dining-room.

- [ ] **Step 1: Write the failing tests**

Create `tests/scene_compiler_normal_map_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <filesystem>
#include <string>

namespace {

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = YAORAY_TEST_DATA_DIR;
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.material_name = "test_mat";
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& substring
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_normalmap_loads_png_into_material_normal_map) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.normal_map >= 0);
    YR_EXPECT_TRUE(static_cast<std::size_t>(m.normal_map) < result.scene->textures.size());
    // Normal maps are always loaded as linear data.
    YR_EXPECT_TRUE(result.scene->textures[m.normal_map].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_normalmap_honors_explicit_normalscale) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    mat.params.push_back(yr::PbrtParam{"float", "normalscale", {0.5f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene->materials[0].normal_scale, 0.5f, 1.0e-6);
}

YR_TEST(scene_compiler_normalmap_without_param_keeps_index_minus_one) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials[0].normal_map, -1);
    YR_EXPECT_NEAR(result.scene->materials[0].normal_scale, 1.0f, 1.0e-6);
}

YR_TEST(scene_compiler_normalmap_missing_file_emits_error) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/no_such_normal.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}

YR_TEST(scene_compiler_normalmap_works_on_conductor_material) {
    // Normal maps are not exclusive to diffuse — every material kind that uses
    // shading_normal should pick up the normal map.
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "conductor";
    mat.params.push_back(yr::PbrtParam{"rgb", "eta", {0.2f, 0.4f, 1.3f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"rgb", "k", {3.9f, 2.4f, 1.6f}, {}, {}, {}});
    mat.params.push_back(yr::PbrtParam{"string", "normalmap", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_materials["test_mat"] = mat;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->materials[0].normal_map >= 0);
}
```

Register the new test file in `CMakeLists.txt`.

- [ ] **Step 2: Run tests, verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the 5 new normal-map tests fail — `CompileMaterial` currently ignores the `normalmap` and `normalscale` params, so `material.normal_map` stays at its default `-1`.

- [ ] **Step 3: Implement `CompileNormalMap`**

In `src/render/scene_compiler.cpp`, add a helper near the other material/texture helpers (anonymous namespace). Place it just before `CompileMaterial`:

```cpp
// Returns true if a normal map was loaded and assigned. Mutates material.normal_map
// and material.normal_scale. On load failure, pushes an Error and returns false.
bool CompileNormalMap(
    const std::vector<PbrtParam>& params,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    RenderMaterial& material,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* normalmap = FindParam(params, "normalmap");
    if (normalmap == nullptr || normalmap->strings.empty()) {
        return false;
    }

    const std::filesystem::path resolved = scene.source_root / normalmap->strings[0];

    std::string ext = resolved.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
        diagnostics.push_back(Error(scene, "Material.normalmap",
            "normal map must be a PNG or JPEG: " + resolved.generic_string()));
        return false;
    }

    // Normal maps are always linear data (not radiometric).
    TextureLoadResult load = LoadLdrTexture(resolved, TextureColorSpace::Linear);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "Material.normalmap", load.error));
        return false;
    }

    material.normal_map = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    material.normal_scale = FloatParam(FindParam(params, "normalscale"), 1.0f);
    return true;
}
```

Then call it at the end of every branch in `CompileMaterial` that should support normal maps. The simplest way: call it once just before `ir.materials.push_back(material);` at the very bottom of `CompileMaterial`. That way every material kind picks up the normal map uniformly.

Find the line near the bottom of `CompileMaterial`:

```cpp
    int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
```

Add the normal-map call just before the push:

```cpp
    CompileNormalMap(params, scene, ir, material, diagnostics);

    int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
```

- [ ] **Step 4: Run tests and verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the 5 new normal-map tests pass. Previous tests still pass. New total: 154 + 5 = 159.

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp \
        tests/scene_compiler_normal_map_tests.cpp \
        CMakeLists.txt
git commit -m "feat: \"string normalmap\" material parameter populates RenderMaterial.normal_map"
```

---

## Task 3: `texture_test` scene + CTest + README

**Files:**
- Create: `scenes/pbrt/texture_test/texture_test.pbrt`
- Create: `scenes/pbrt/texture_test/texture_test_smoke.pbrt`
- Create: `scenes/pbrt/texture_test/textures/checker.png` (copy of `tests/fixtures/assets/checker_2x2.png`)
- Create: `scenes/pbrt/texture_test/textures/flat_normal.png` (copy of `tests/fixtures/assets/checker_2x2.png`)
- Modify: `CMakeLists.txt` (CTest entry)
- Modify: `README.md` (add `texture_test` to runnable scenes)

The scene visually exercises **wrap mode** behavior. Three planes side by side, each with UV coordinates extending well beyond `[0,1]` so the wrap mode determines what's at the corners:

- **Left plane:** `wrap "repeat"` — checker pattern tiles across the surface.
- **Middle plane:** `wrap "clamp"` — the edge texel stretches outward beyond `[0,1]`.
- **Right plane:** `wrap "black"` — degraded to clamp in M1 (will visually look like the middle plane in this slice, with a Warning in the build log).

A fourth plane with a normal map declared **proves the data path compiles** (verified by the CTest regex `"Compiled textures:"` increment), even though Slice 3 doesn't yet apply the shading effect (trianglemesh tangents land in Slice 4).

- [ ] **Step 1: Set up texture assets**

```bash
mkdir -p scenes/pbrt/texture_test/textures
cp tests/fixtures/assets/checker_2x2.png scenes/pbrt/texture_test/textures/checker.png
cp tests/fixtures/assets/checker_2x2.png scenes/pbrt/texture_test/textures/flat_normal.png
```

Both PNGs are intentionally the same 2×2 checker — visual fidelity of the normal-map plane is not Slice 3's concern; the scene file just needs *a* PNG to load.

- [ ] **Step 2: Author `texture_test.pbrt`**

Create `scenes/pbrt/texture_test/texture_test.pbrt`:

```pbrt
# YaoRay M1 Slice 3 — texture pipeline validation.
#
# Four planes side by side under an HDRI sky:
#   - Plane 1: imagemap checker, wrap "repeat" — tiles across the surface.
#   - Plane 2: imagemap checker, wrap "clamp"  — edge texel stretches beyond [0,1].
#   - Plane 3: imagemap checker, wrap "black"  — M1 degrades to clamp + Warning.
#   - Plane 4: diffuse + normalmap PNG — proves the data path, not yet visually
#              demonstrated until Slice 4 wires trianglemesh tangents.
#
# UV coordinates on each plane go from -1 to 2, so the wrap mode determines
# what shows in the outer half-tiles.

LookAt 0 1.5 5.5  0 0 0  0 1 0
Camera "perspective" "float fov" [38]
Sampler "independent" "integer pixelsamples" [64]
Integrator "path" "integer maxdepth" [4]
Film "rgb"
    "integer xresolution" [600]
    "integer yresolution" [200]
    "string filename" ["out/texture_test.png"]

WorldBegin

# --- environment light (reuse the placeholder sky) --------------------
LightSource "infinite"
    "string filename" ["../material_studio/env/sky.hdr"]
    "float scale" [1.0]

# --- textures ---------------------------------------------------------
Texture "checker_repeat" "spectrum" "imagemap"
    "string filename" ["textures/checker.png"]
    "string wrap" ["repeat"]

Texture "checker_clamp" "spectrum" "imagemap"
    "string filename" ["textures/checker.png"]
    "string wrap" ["clamp"]

Texture "checker_black" "spectrum" "imagemap"
    "string filename" ["textures/checker.png"]
    "string wrap" ["black"]

# --- materials --------------------------------------------------------
MakeNamedMaterial "tile_repeat"
    "string type" ["diffuse"]
    "texture reflectance" ["checker_repeat"]

MakeNamedMaterial "tile_clamp"
    "string type" ["diffuse"]
    "texture reflectance" ["checker_clamp"]

MakeNamedMaterial "tile_black"
    "string type" ["diffuse"]
    "texture reflectance" ["checker_black"]

MakeNamedMaterial "normal_mapped"
    "string type" ["diffuse"]
    "rgb reflectance" [0.7 0.7 0.7]
    "string normalmap" ["textures/flat_normal.png"]

# --- plane 1: wrap "repeat" (left) ------------------------------------
AttributeBegin
NamedMaterial "tile_repeat"
Translate -3.0 0 0
Shape "trianglemesh"
    "point3 P"     [-0.5 0 -0.5   0.5 0 -0.5   0.5 0 0.5  -0.5 0 0.5]
    "point2 uv"    [-1 -1   2 -1   2 2   -1 2]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- plane 2: wrap "clamp" --------------------------------------------
AttributeBegin
NamedMaterial "tile_clamp"
Translate -1.0 0 0
Shape "trianglemesh"
    "point3 P"     [-0.5 0 -0.5   0.5 0 -0.5   0.5 0 0.5  -0.5 0 0.5]
    "point2 uv"    [-1 -1   2 -1   2 2   -1 2]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- plane 3: wrap "black" (degraded to clamp in M1) ------------------
AttributeBegin
NamedMaterial "tile_black"
Translate 1.0 0 0
Shape "trianglemesh"
    "point3 P"     [-0.5 0 -0.5   0.5 0 -0.5   0.5 0 0.5  -0.5 0 0.5]
    "point2 uv"    [-1 -1   2 -1   2 2   -1 2]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- plane 4: diffuse with normal map (data path only) ----------------
AttributeBegin
NamedMaterial "normal_mapped"
Translate 3.0 0 0
Shape "trianglemesh"
    "point3 P"     [-0.5 0 -0.5   0.5 0 -0.5   0.5 0 0.5  -0.5 0 0.5]
    "point2 uv"    [0 0   1 0   1 1   0 1]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

WorldEnd
```

- [ ] **Step 3: Render manually and eyeball**

```bash
cmake --build build --config Release
./build/Release/yaoray.exe render scenes/pbrt/texture_test/texture_test.pbrt --backend cpu
```

Expected stdout includes:
- `Compiled triangles: 8` (4 planes × 2 triangles each)
- `Compiled materials: 4`
- `Compiled textures: 5` (3 checker imagemaps with different wrap + 1 environment HDRI + 1 normal map)
- `Hits: > 0`
- `Rendered image: scenes/pbrt/texture_test/out/texture_test.png`

Open the PNG. The three left-side planes should look distinct from one another:
- Plane 1 (repeat): you should see multiple checker tiles across the plane.
- Plane 2 (clamp): the middle portion has a checker pattern, but the outer regions are a single solid color (the edge texel extended outward).
- Plane 3 (black-degraded): looks identical to plane 2 in M1; a Warning should appear in the build log mentioning "black".
- Plane 4 (normal map): looks like a flat grey diffuse plane. (The normal map is loaded but not applied — Slice 4 fixes this.)

Don't worry about exact pixel-perfection — the checker is 2×2, the visuals will be coarse.

- [ ] **Step 4: Author the smoke variant**

Create `scenes/pbrt/texture_test/texture_test_smoke.pbrt` by copying `texture_test.pbrt` and changing only:
- `"integer pixelsamples" [64]` → `"integer pixelsamples" [4]`
- `"integer xresolution" [600]` → `"integer xresolution" [120]`
- `"integer yresolution" [200]` → `"integer yresolution" [40]`
- `"string filename" ["out/texture_test.png"]` → `"string filename" ["out/texture_test_smoke.png"]`
- Update the header comment to say "low-resolution smoke variant for CTest".

- [ ] **Step 5: Add the CTest entry**

In `CMakeLists.txt`, after `yaoray_cli_render_pbrt_material_studio`, add:

```cmake
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_texture_test
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/texture_test/texture_test_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/texture_test/out/texture_test_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [1-9]"
            "Compiled textures: [1-9]"
    )
```

The `[1-9]` patterns (instead of `[^0]`) are slightly more robust — they explicitly match a leading non-zero digit.

- [ ] **Step 6: Run CTest and confirm**

```bash
cd build && ctest --output-on-failure -C Release
```

Expected: 8/8 pass (the 7 from Slice 2 + the new `yaoray_cli_render_pbrt_texture_test`).

- [ ] **Step 7: Update README**

In `README.md`, in both macOS/Linux and Windows "Run" blocks, add `texture_test.pbrt` after `material_studio.pbrt`:

```markdown
macOS/Linux:

```bash
./build/yaoray --help
./build/yaoray --version
./build/yaoray render scenes/pbrt/hello_emissive/hello_emissive.pbrt --backend cpu
./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu
./build/yaoray render scenes/pbrt/material_studio/material_studio.pbrt --backend cpu
./build/yaoray render scenes/pbrt/texture_test/texture_test.pbrt --backend cpu
```

Windows:

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\pbrt\hello_emissive\hello_emissive.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\cornell_box_pbrt\cornell_box_pbrt.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\material_studio\material_studio.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\texture_test\texture_test.pbrt --backend cpu
```
```

- [ ] **Step 8: Commit**

```bash
git add scenes/pbrt/texture_test/ CMakeLists.txt README.md
git commit -m "feat: texture_test PBRT scene + CTest + README pointer"
```

---

## Wrap-up checklist

After all 3 tasks:

- [ ] `./build/Release/yaoray_tests.exe` — 149 baseline + 5 (Task 1) + 5 (Task 2) = 159 PASS.
- [ ] `cd build && ctest --output-on-failure -C Release` — 8/8 (the 7 from Slice 2 + new texture_test).
- [ ] `./build/Release/yaoray.exe render scenes/pbrt/texture_test/texture_test.pbrt --backend cpu` renders four planes with visibly different left-three wrap behaviors.
- [ ] A build-log Warning mentions wrap mode `black` being degraded to `clamp`.
- [ ] `git log --oneline | head -5` shows 3 atomic commits, one per task.
- [ ] Local `main` has not been touched — all commits live on the slice's worktree branch.

After verification, the controller invokes `superpowers:finishing-a-development-branch` to merge the slice into local `main`. Per the user's instructions, `origin/main` is not pushed until M1 (Slice 4 + dining-room) is complete.
