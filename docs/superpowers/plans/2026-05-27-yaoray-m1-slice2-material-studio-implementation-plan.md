# YaoRay M1 Slice 2 — `material_studio` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `Texture "imagemap"`, `Texture "constant"`, `LightSource "infinite"` (HDRI), and named-texture references inside material parameters through the YaoRay PBRT pipeline so a hand-authored `scenes/pbrt/material_studio/material_studio.pbrt` renders correctly end-to-end on the CPU backend.

**Architecture:** All the heavy lifting already exists from M0. The HDR/LDR loaders (`LoadHdrTexture`, `LoadLdrTexture`) are in `texture.cpp`. The environment importance-sampling distribution builder (`BuildEnvironmentDistribution`) and the CPU environment sampler (`SampleEnvironment`, `EvaluateEnvironment`, `PdfEnvironment`) are in `environment.cpp`. The CPU material resolver (`ResolveCpuMaterialSample`) already samples textures from `TexParam.texture` indices. M0 left every `TexParam.texture = -1` and only used `.value`. This slice fills the data path: the scene compiler now (1) builds an `ir.textures` table from `scene.named_textures`, (2) sets `TexParam.texture = <index>` on material params that reference a named texture, and (3) populates `ir.environment` + `ir.environment_distributions` from a `LightSource "infinite"` directive.

**Tech Stack:** C++20, CMake 3.24, stb_image (already linked), custom `yr_test.hpp` test harness, CTest. No new third-party libraries.

---

## Spec Coverage

This plan implements the following items from `docs/superpowers/specs/2026-05-27-yaoray-m1-dining-room-design.md` §"Implementation Slices — Slice 2":

1. `LightSource "infinite"` wiring (parser already captures it in M0; scene compiler now consumes it).
2. `Texture "imagemap"` loader → `RenderSceneIR.textures`, with auto color-space (LDR ⇒ sRGB, HDR ⇒ linear) and explicit `encoding` override.
3. `Texture "constant"` folded into `TexParam.value` (no `RenderTexture` allocated).
4. Scene compiler resolves named-texture references in material parameters (`reflectance`, `uroughness`, `vroughness`, `eta`, `k`, `coating_roughness`).
5. The `scenes/pbrt/material_studio/material_studio.pbrt` asset.

Out of scope (deferred to other slices): normal maps (Slice 3), vertex N/uv/S pass-through (Slice 4), `LightSource "distant"` / `"spot"` (Slice 4), procedural textures `Texture "scale"` / `"mix"` / `"checkerboard"` (M2), dining-room (Slice 4).

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `tests/scene_compiler_texture_tests.cpp` | Unit tests for the new texture-table builder + material texture binding. |
| `tests/scene_compiler_environment_tests.cpp` | Unit tests for `LightSource "infinite"` → `RenderEnvironment` wiring. |
| `scenes/pbrt/material_studio/material_studio.pbrt` | Full demo scene (5 spheres + ground plane + HDRI sky). |
| `scenes/pbrt/material_studio/material_studio_smoke.pbrt` | Low-resolution variant for CTest. |
| `scenes/pbrt/material_studio/env/sky.hdr` | Bundled HDR sky asset (small CC0). |

**Modified files:**

| Path | Change |
|------|--------|
| `src/render/scene_compiler.cpp` | Add `CompileTextures` (loads named imagemaps + folds constants into a name-to-index map), `TextureNameInParam` / `TexParam1fFromParams` / `TexParam3fFromParams` helpers, route material parameter reads through the helpers, add `CompileEnvironmentLight` and dispatch from `CompileAnalyticLights`. |
| `CMakeLists.txt` | Register the two new test files; add the `yaoray_cli_render_pbrt_material_studio` CTest entry. |
| `README.md` | Add `material_studio` to the runnable scenes list. |

---

## Setting up the worktree

Before starting Task 1, create an isolated worktree off of local `main` (which is at the post-Slice-1 commit `1abd75f`). Use the harness-native `EnterWorktree` tool with name `m1-material-studio` (or have a controller run `superpowers:using-git-worktrees` skill). The worktree must include the M1 spec at `docs/superpowers/specs/2026-05-27-yaoray-m1-dining-room-design.md` and this plan; if `EnterWorktree` branched from `origin/main` rather than local `main`, run `git merge main --ff-only` inside the worktree before Task 1 to bring the Slice-1 and M1-doc commits in.

Then verify baseline:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe       # Must show 137/137 PASS (post-Slice-1 baseline).
cd build && ctest --output-on-failure -C Release  # Must show 6/6 PASS.
```

All commits in this plan land on the worktree branch.

---

## Task 1: `CompileTextures` — load named imagemaps and fold constants

**Files:**
- Create: `tests/scene_compiler_texture_tests.cpp`
- Modify: `src/render/scene_compiler.cpp` (add `CompileTextures` + helpers; call it from `CompilePbrtScene` before materials)
- Modify: `CMakeLists.txt` (register new test file)

This task builds the **texture table**: a name → index map from each `PbrtScene.named_textures` entry into `ir.textures`. Constants are folded into a "constant value" side-map (returned from the function for the next task to consume).

PBRT v4 `Texture` syntax: `Texture "name" "type" "class" "string filename" ["x.png"] ...`. The parser stores this as `scene.named_textures["name"] = PbrtEntity{type="class", params}`. The `value_type` ("float" vs "rgb" / "spectrum" / "color") is currently discarded by the parser — for M1, we look only at `class` (imagemap / constant) and the params.

- [ ] **Step 1: Write the failing tests**

Create `tests/scene_compiler_texture_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

const std::filesystem::path& TestDataDir() {
    static const std::filesystem::path dir = [] {
        const char* env = std::getenv("YAORAY_TEST_DATA_DIR");
        return std::filesystem::path{env != nullptr ? env : ""};
    }();
    return dir;
}

yr::PbrtScene MinimalScene() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = TestDataDir();
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere so the empty-geometry check passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_loads_imagemap_texture_into_ir_textures) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["checker"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});

    const yr::RenderTexture& loaded = result.scene->textures[0];
    YR_EXPECT_EQ(loaded.width, 2);
    YR_EXPECT_EQ(loaded.height, 2);
    YR_EXPECT_TRUE(!loaded.texels.empty());
    // PNG defaults to sRGB.
    YR_EXPECT_TRUE(loaded.color_space == yr::TextureColorSpace::Srgb);
}

YR_TEST(scene_compiler_imagemap_respects_explicit_linear_encoding) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    tex.params.push_back(yr::PbrtParam{"string", "encoding", {}, {}, {"linear"}, {}});
    pbrt.named_textures["linear_data"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    YR_EXPECT_TRUE(result.scene->textures[0].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_imagemap_hdr_loads_as_linear) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/tiny_env.hdr"}, {}});
    pbrt.named_textures["env"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->textures.size() >= std::size_t{1});
    YR_EXPECT_TRUE(result.scene->textures[0].color_space == yr::TextureColorSpace::Linear);
}

YR_TEST(scene_compiler_constant_texture_does_not_allocate_render_texture) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "constant";
    tex.params.push_back(yr::PbrtParam{"rgb", "value", {0.7f, 0.2f, 0.1f}, {}, {}, {}});
    pbrt.named_textures["solid_red"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // Constant textures fold to values and do NOT push a RenderTexture.
    YR_EXPECT_EQ(result.scene->textures.size(), std::size_t{0});
}

YR_TEST(scene_compiler_imagemap_missing_file_emits_error_diagnostic) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/no_such_texture.png"}, {}});
    pbrt.named_textures["broken"] = tex;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    // Missing texture is an Error — compilation fails.
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}
```

Register the new test file in `CMakeLists.txt`:

```cmake
add_executable(yaoray_tests
    ...
    tests/scene_compiler_texture_tests.cpp   # <-- new
    ...
)
```

- [ ] **Step 2: Run the tests and verify they fail**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all five `scene_compiler_*_texture*` tests fail because `ir.textures` is empty after compilation (no Texture handling exists yet).

- [ ] **Step 3: Implement `CompileTextures`**

In `src/render/scene_compiler.cpp`, add to the anonymous namespace (place after the existing param helpers, before `CompileMaterial`):

```cpp
struct TextureBindings {
    // Texture name -> ir.textures index. -1 means "folded constant"; look in constant_values.
    std::unordered_map<std::string, int> name_to_index;
    // Texture name -> constant Color3f for folded constants.
    std::unordered_map<std::string, Color3f> constant_values;
};

TextureColorSpace InferTextureColorSpace(
    const std::filesystem::path& path,
    const std::string& explicit_encoding
) {
    if (explicit_encoding == "linear") return TextureColorSpace::Linear;
    if (explicit_encoding == "sRGB" || explicit_encoding == "srgb") return TextureColorSpace::Srgb;

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    // HDR float formats default to linear; LDR formats default to sRGB.
    if (ext == ".hdr" || ext == ".exr") return TextureColorSpace::Linear;
    return TextureColorSpace::Srgb;
}

bool CompileImagemapTexture(
    const std::string& name,
    const PbrtEntity& entity,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    TextureBindings& bindings,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const PbrtParam* filename = FindParam(entity.params, "filename");
    if (filename == nullptr || filename->strings.empty()) {
        diagnostics.push_back(Error(scene, "Texture." + name,
            "imagemap texture requires a filename"));
        return false;
    }

    const std::filesystem::path resolved = scene.source_root / filename->strings[0];

    std::string explicit_encoding;
    const PbrtParam* encoding = FindParam(entity.params, "encoding");
    if (encoding != nullptr && !encoding->strings.empty()) {
        explicit_encoding = encoding->strings[0];
    }
    const TextureColorSpace color_space = InferTextureColorSpace(resolved, explicit_encoding);

    std::string ext = resolved.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    TextureLoadResult load;
    if (ext == ".hdr") {
        load = LoadHdrTexture(resolved);
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        load = LoadLdrTexture(resolved, color_space);
    } else {
        diagnostics.push_back(Error(scene, "Texture." + name,
            "unsupported texture extension: " + ext));
        return false;
    }

    if (!load.ok) {
        diagnostics.push_back(Error(scene, "Texture." + name, load.error));
        return false;
    }
    load.texture.color_space = color_space;
    bindings.name_to_index[name] = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));
    return true;
}

void CompileConstantTexture(
    const std::string& name,
    const PbrtEntity& entity,
    TextureBindings& bindings
) {
    const PbrtParam* value_param = FindParam(entity.params, "value");
    Color3f value{1.0f, 1.0f, 1.0f};
    if (value_param != nullptr && !value_param->floats.empty()) {
        if (value_param->floats.size() >= 3) {
            value = Color3f{value_param->floats[0], value_param->floats[1], value_param->floats[2]};
        } else {
            const float scalar = value_param->floats[0];
            value = Color3f{scalar, scalar, scalar};
        }
    }
    bindings.name_to_index[name] = -1;          // -1 == folded constant.
    bindings.constant_values[name] = value;
}

TextureBindings CompileTextures(
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TextureBindings bindings;
    for (const auto& [name, entity] : scene.named_textures) {
        if (entity.type == "imagemap") {
            CompileImagemapTexture(name, entity, scene, ir, bindings, diagnostics);
        } else if (entity.type == "constant") {
            CompileConstantTexture(name, entity, bindings);
        } else {
            diagnostics.push_back(Warning(scene, "Texture." + name,
                "unsupported texture class '" + entity.type + "' is ignored in M1; "
                "callers will see the parameter fall back to its inline constant"));
        }
    }
    return bindings;
}
```

You will need `#include <algorithm>` and `#include <yaoray/render/texture.hpp>` at the top of the file if they are not already present (texture.hpp is already pulled in transitively, but it is cleaner to be explicit).

Now extend `CompilePbrtScene` to call `CompileTextures` before materials. Around line ~430:

```cpp
SceneCompileResult CompilePbrtScene(const PbrtScene& scene) {
    SceneCompileResult result;
    RenderSceneIR ir;
    auto& diagnostics = result.diagnostics;

    // 1. Film, camera, integrator, sampler
    CompileFilm(scene, ir);
    CompileCamera(scene, ir);
    CompileIntegrator(scene, ir);
    CompileSampler(scene, ir);

    // 2. Compile named textures -> name-to-index map (+ constant value side-map).
    TextureBindings texture_bindings = CompileTextures(scene, ir, diagnostics);

    // 3. Compile named materials -> build name->index map (kept as M0)
    std::unordered_map<std::string, int> material_name_to_index;
    for (const auto& [name, entity] : scene.named_materials) {
        int idx = CompileMaterial(entity, texture_bindings, ir, scene, diagnostics);
        material_name_to_index[name] = idx;
    }
    // ... rest unchanged for now (the next task wires the bindings into CompileMaterial properly).
```

For this task, **change `CompileMaterial`'s signature to take a `const TextureBindings&` parameter, but do not use the bindings yet** — the actual binding happens in Task 2. The body stays the same; only the signature changes. Update every call site (the named-material loop above, and `CompileInstances`'s inline-material call).

If `texture_bindings` is unused inside `CompileMaterial` and the compiler warns about unused parameters, add `(void)bindings;` at the top of the function body.

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the five `scene_compiler_*_texture*` tests pass. All previous tests still pass. New total: 137 + 5 = 142.

- [ ] **Step 5: Commit**

```bash
git add include/yaoray/render/scene_compiler.hpp \
        src/render/scene_compiler.cpp \
        tests/scene_compiler_texture_tests.cpp \
        CMakeLists.txt
git commit -m "feat: CompileTextures loads named imagemaps and folds constants"
```

(Include `scene_compiler.hpp` only if you had to change a public declaration; otherwise drop it.)

---

## Task 2: Material parameter texture binding

**Files:**
- Modify: `src/render/scene_compiler.cpp` (add `TexParam1fFromParams` / `TexParam3fFromParams` helpers; route every material's textureable slot through them)
- Modify: `tests/scene_compiler_texture_tests.cpp` (add binding tests)

After this task, when a material says `"texture reflectance" ["wood"]`, the compiler resolves "wood" via the texture bindings and sets `RenderMaterial.reflectance.texture = <index>` (or copies the folded constant value into `.value` if "wood" was a `Texture "constant"`). The CPU material resolver already handles `.texture >= 0` — no shader work needed.

The textureable slots in M1 are: `reflectance`, `eta`, `k`, `uroughness`, `vroughness`, `coating_roughness`. (Normal maps are Slice 3; `alpha` and `mix_amount` are not exercised by material_studio.)

- [ ] **Step 1: Write the failing tests**

Append to `tests/scene_compiler_texture_tests.cpp`:

```cpp
YR_TEST(scene_compiler_binds_imagemap_to_diffuse_reflectance) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["wood"] = tex;

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"wood"}, {}});
    pbrt.named_materials["floor"] = mat;
    pbrt.shapes[0].material_name = "floor";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.reflectance.texture, 0);
}

YR_TEST(scene_compiler_binds_constant_texture_to_diffuse_reflectance_value) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "constant";
    tex.params.push_back(yr::PbrtParam{"rgb", "value", {0.4f, 0.5f, 0.6f}, {}, {}, {}});
    pbrt.named_textures["bluish"] = tex;

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"bluish"}, {}});
    pbrt.named_materials["wall"] = mat;
    pbrt.shapes[0].material_name = "wall";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    const yr::RenderMaterial& m = result.scene->materials[0];
    // Constant texture folds into .value; .texture stays -1.
    YR_EXPECT_EQ(m.reflectance.texture, -1);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.4f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.5f, 1.0e-5);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.6f, 1.0e-5);
}

YR_TEST(scene_compiler_binds_imagemap_to_conductor_roughness) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity tex;
    tex.type = "imagemap";
    tex.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {"assets/checker_2x2.png"}, {}});
    pbrt.named_textures["microfacets"] = tex;

    yr::PbrtEntity mat;
    mat.type = "conductor";
    mat.params.push_back(yr::PbrtParam{"texture", "uroughness", {}, {}, {"microfacets"}, {}});
    pbrt.named_materials["bumpy_gold"] = mat;
    pbrt.shapes[0].material_name = "bumpy_gold";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.uroughness.texture, 0);
}

YR_TEST(scene_compiler_unknown_texture_name_emits_warning_and_uses_fallback) {
    yr::PbrtScene pbrt = MinimalScene();

    yr::PbrtEntity mat;
    mat.type = "diffuse";
    // No Texture directive created, but the material references "missing".
    mat.params.push_back(yr::PbrtParam{"texture", "reflectance", {}, {}, {"missing"}, {}});
    pbrt.named_materials["broken"] = mat;
    pbrt.shapes[0].material_name = "broken";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // No fatal error; compilation succeeds with the default reflectance and a Warning.
    bool found_warning = false;
    for (const yr::SceneDiagnostic& d : result.diagnostics) {
        if (d.severity == yr::DiagnosticSeverity::Warning &&
            d.message.find("missing") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    YR_EXPECT_TRUE(found_warning);
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_EQ(m.reflectance.texture, -1);
}
```

- [ ] **Step 2: Run the tests and verify they fail**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the four new binding tests fail because `CompileMaterial` does not consult the bindings yet (texture-typed params silently fall back to default values).

- [ ] **Step 3: Implement the helpers and route material reads through them**

In `src/render/scene_compiler.cpp`, add helpers near the existing `RgbParam` / `FloatParam` (anonymous namespace):

```cpp
// Returns the texture name referenced by a "texture"-typed param, or empty if not a texture ref.
std::string TextureNameInParam(const PbrtParam* param) {
    if (param == nullptr) return {};
    if (param->type != "texture") return {};
    if (param->strings.empty()) return {};
    return param->strings[0];
}

TexParam3f TexParam3fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    Color3f fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam3f result;
    result.value = fallback_value;
    result.texture = -1;

    const PbrtParam* p = FindParam(params, param_name);
    if (p == nullptr) {
        return result;
    }

    const std::string texture_name = TextureNameInParam(p);
    if (!texture_name.empty()) {
        auto it = bindings.name_to_index.find(texture_name);
        if (it == bindings.name_to_index.end()) {
            diagnostics.push_back(Warning(scene, "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"));
            return result;
        }
        if (it->second >= 0) {
            // Real RenderTexture index.
            result.texture = it->second;
        } else {
            // Folded constant -- look up the value.
            auto cv = bindings.constant_values.find(texture_name);
            if (cv != bindings.constant_values.end()) {
                result.value = cv->second;
            }
        }
        return result;
    }

    // Fall back to the inline RGB / spectrum value.
    if (!p->floats.empty()) {
        if (p->floats.size() >= 3) {
            result.value = Color3f{p->floats[0], p->floats[1], p->floats[2]};
        } else {
            const float scalar = p->floats[0];
            result.value = Color3f{scalar, scalar, scalar};
        }
    }
    return result;
}

TexParam1f TexParam1fFromParams(
    const std::vector<PbrtParam>& params,
    const std::string& param_name,
    float fallback_value,
    const TextureBindings& bindings,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    TexParam1f result;
    result.value = fallback_value;
    result.texture = -1;

    const PbrtParam* p = FindParam(params, param_name);
    if (p == nullptr) {
        return result;
    }

    const std::string texture_name = TextureNameInParam(p);
    if (!texture_name.empty()) {
        auto it = bindings.name_to_index.find(texture_name);
        if (it == bindings.name_to_index.end()) {
            diagnostics.push_back(Warning(scene, "Material." + param_name,
                "texture '" + texture_name + "' is not defined; using fallback value"));
            return result;
        }
        if (it->second >= 0) {
            result.texture = it->second;
        } else {
            auto cv = bindings.constant_values.find(texture_name);
            if (cv != bindings.constant_values.end()) {
                // Take the X channel of an RGB constant as the scalar.
                result.value = cv->second.x;
            }
        }
        return result;
    }

    if (!p->floats.empty()) {
        result.value = p->floats[0];
    }
    return result;
}
```

Now update `CompileMaterial` to use these helpers. The body becomes:

```cpp
int CompileMaterial(
    const PbrtEntity& entity,
    const TextureBindings& bindings,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    RenderMaterial material;
    const auto& params = entity.params;
    const std::string& type = entity.type;

    if (type == "matte" || type == "diffuse") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        // PBRT v3 "Kd" alias only matters when explicit; if present, override.
        if (FindParam(params, "Kd") != nullptr) {
            material.reflectance = TexParam3fFromParams(params, "Kd",
                material.reflectance.value, bindings, scene, diagnostics);
        }
    } else if (type == "conductor" || type == "metal") {
        material.kind = RenderMaterialKind::Conductor;
        material.eta = TexParam3fFromParams(params, "eta",
            Color3f{0.2f, 0.2f, 0.2f}, bindings, scene, diagnostics);
        material.k = TexParam3fFromParams(params, "k",
            Color3f{1.0f, 1.0f, 1.0f}, bindings, scene, diagnostics);
        const float fallback_u = FloatParam(FindParam(params, "roughness"), 0.0f);
        material.uroughness = TexParam1fFromParams(params, "uroughness",
            fallback_u, bindings, scene, diagnostics);
        material.vroughness = TexParam1fFromParams(params, "vroughness",
            material.uroughness.value, bindings, scene, diagnostics);
    } else if (type == "dielectric" || type == "glass") {
        material.kind = RenderMaterialKind::Dielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.ior = FloatParam(FindParam(params, "index"), material.ior);
        material.uroughness = TexParam1fFromParams(params, "uroughness",
            0.0f, bindings, scene, diagnostics);
        material.vroughness = TexParam1fFromParams(params, "vroughness",
            material.uroughness.value, bindings, scene, diagnostics);
    } else if (type == "thindielectric") {
        material.kind = RenderMaterialKind::ThinDielectric;
        material.ior = FloatParam(FindParam(params, "eta"), 1.5f);
    } else if (type == "coateddiffuse") {
        material.kind = RenderMaterialKind::CoatedDiffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
    } else if (type == "coatedconductor") {
        material.kind = RenderMaterialKind::CoatedConductor;
        material.eta = TexParam3fFromParams(params, "conductor.eta",
            Color3f{0.2f, 0.2f, 0.2f}, bindings, scene, diagnostics);
        material.k = TexParam3fFromParams(params, "conductor.k",
            Color3f{1.0f, 1.0f, 1.0f}, bindings, scene, diagnostics);
        material.uroughness = TexParam1fFromParams(params, "conductor.roughness",
            0.0f, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
    } else if (type == "diffusetransmission") {
        material.kind = RenderMaterialKind::DiffuseTransmission;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.25f, 0.25f, 0.25f}, bindings, scene, diagnostics);
    } else if (type == "plastic" || type == "uber" || type == "substrate") {
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "Kd",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        if (FindParam(params, "reflectance") != nullptr) {
            material.reflectance = TexParam3fFromParams(params, "reflectance",
                material.reflectance.value, bindings, scene, diagnostics);
        }
    } else {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    }

    int index = static_cast<int>(ir.materials.size());
    ir.materials.push_back(material);
    return index;
}
```

Update call sites in `CompilePbrtScene` and `CompileInstances` to pass `texture_bindings` as the second argument. The signature change cascades — find each call.

- [ ] **Step 4: Run tests and verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all four new binding tests pass. Previous tests still pass. New total: 142 + 4 = 146.

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_texture_tests.cpp
git commit -m "feat: bind named textures into RenderMaterial TexParam slots"
```

---

## Task 3: `LightSource "infinite"` environment wiring

**Files:**
- Modify: `src/render/scene_compiler.cpp` (add `CompileEnvironmentLight`; dispatch from `CompileAnalyticLights`)
- Create: `tests/scene_compiler_environment_tests.cpp`
- Modify: `CMakeLists.txt` (register new test file)

PBRT v4 `LightSource "infinite"` parameters:
- `filename` (string) — path to the HDR/EXR file. (PBRT also accepts `mapname` as an alias; honor both.)
- `L` (rgb, default `(1,1,1)`) — radiance multiplier.
- `scale` (rgb or float, default `1`) — additional scale.
- `portals` (point3 list) — explicit portal sampling. **Out of scope for M1; warn.**

For M1 we map this to `RenderEnvironment`:
- `texture_index`: push the loaded HDR to `ir.textures`, store the index.
- `distribution_index`: build a `RenderEnvironmentDistribution` from the texture and push to `ir.environment_distributions`.
- `radiance`: `L * scale` component-wise. (We fold the multiplier into `RenderEnvironment.radiance` and leave `strength = 1.0`.)
- `active = true`.

- [ ] **Step 1: Write the failing tests**

Create `tests/scene_compiler_environment_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

const std::filesystem::path& TestDataDir() {
    static const std::filesystem::path dir = [] {
        const char* env = std::getenv("YAORAY_TEST_DATA_DIR");
        return std::filesystem::path{env != nullptr ? env : ""};
    }();
    return dir;
}

yr::PbrtScene MinimalSceneWithInfinite(const std::string& filename, yr::Color3f L_value, float scale_value) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = TestDataDir();
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtLightRecord lr;
    lr.light.type = "infinite";
    lr.light.params.push_back(yr::PbrtParam{"string", "filename", {}, {}, {filename}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "L", {L_value.x, L_value.y, L_value.z}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"float", "scale", {scale_value}, {}, {}, {}});
    lr.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(lr);

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_lightsource_infinite_activates_environment) {
    const yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/tiny_env.hdr",
        yr::Color3f{1.0f, 1.0f, 1.0f},
        1.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->environment.active);
    YR_EXPECT_TRUE(result.scene->environment.texture_index >= 0);
    YR_EXPECT_TRUE(result.scene->environment.distribution_index >= 0);
    YR_EXPECT_TRUE(!result.scene->environment_distributions.empty());
}

YR_TEST(scene_compiler_lightsource_infinite_scales_radiance) {
    const yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/tiny_env.hdr",
        yr::Color3f{2.0f, 3.0f, 4.0f},
        0.5f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    // radiance = L * scale = (1.0, 1.5, 2.0).
    YR_EXPECT_NEAR(result.scene->environment.radiance.x, 1.0f, 1.0e-5);
    YR_EXPECT_NEAR(result.scene->environment.radiance.y, 1.5f, 1.0e-5);
    YR_EXPECT_NEAR(result.scene->environment.radiance.z, 2.0f, 1.0e-5);
}

YR_TEST(scene_compiler_lightsource_infinite_missing_filename_emits_error) {
    yr::PbrtScene pbrt = MinimalSceneWithInfinite(
        "assets/no_such_environment.hdr",
        yr::Color3f{1.0f, 1.0f, 1.0f},
        1.0f);
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
}
```

Register the new test file in `CMakeLists.txt`:

```cmake
add_executable(yaoray_tests
    ...
    tests/scene_compiler_environment_tests.cpp   # <-- new
    ...
)
```

- [ ] **Step 2: Run tests, verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the three new environment tests fail because `LightSource "infinite"` is currently warned-and-ignored in `CompileAnalyticLights`.

- [ ] **Step 3: Implement `CompileEnvironmentLight` and dispatch**

In `src/render/scene_compiler.cpp`, add the helper to the anonymous namespace (place near `CompileAnalyticLights`):

```cpp
bool CompileEnvironmentLight(
    const PbrtLightRecord& record,
    const PbrtScene& scene,
    RenderSceneIR& ir,
    std::vector<SceneDiagnostic>& diagnostics
) {
    // Filename: PBRT v4 accepts "filename"; PBRT v3 used "mapname". Honor both.
    const PbrtParam* fname = FindParam(record.light.params, "filename");
    if (fname == nullptr || fname->strings.empty()) {
        fname = FindParam(record.light.params, "mapname");
    }
    if (fname == nullptr || fname->strings.empty()) {
        diagnostics.push_back(Error(scene, "LightSource.infinite",
            "infinite light requires a filename (or mapname)"));
        return false;
    }

    const std::filesystem::path resolved = scene.source_root / fname->strings[0];
    TextureLoadResult load = LoadHdrTexture(resolved);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "LightSource.infinite", load.error));
        return false;
    }

    const int texture_index = static_cast<int>(ir.textures.size());
    ir.textures.push_back(std::move(load.texture));

    RenderEnvironmentDistribution dist = BuildEnvironmentDistribution(ir.textures[texture_index]);
    const int dist_index = static_cast<int>(ir.environment_distributions.size());
    ir.environment_distributions.push_back(std::move(dist));

    const Color3f L = RgbParam(FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
    Color3f scale{1.0f, 1.0f, 1.0f};
    const PbrtParam* scale_param = FindParam(record.light.params, "scale");
    if (scale_param != nullptr) {
        if (scale_param->floats.size() >= 3) {
            scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
        } else if (!scale_param->floats.empty()) {
            const float s = scale_param->floats[0];
            scale = Color3f{s, s, s};
        }
    }

    ir.environment.active = true;
    ir.environment.radiance = Color3f{L.x * scale.x, L.y * scale.y, L.z * scale.z};
    ir.environment.strength = 1.0f;
    ir.environment.rotation_radians = 0.0f;   // Slice 4 may wire this from light_to_world's Y rotation.
    ir.environment.texture_index = texture_index;
    ir.environment.distribution_index = dist_index;

    if (FindParam(record.light.params, "portals") != nullptr) {
        diagnostics.push_back(Warning(scene, "LightSource.infinite",
            "portal sampling is not supported in M1; portals parameter ignored"));
    }
    return true;
}
```

Update `CompileAnalyticLights` (or rename to `CompileLights` if you prefer; the current name is fine) so that the `"infinite"` branch calls `CompileEnvironmentLight`:

```cpp
void CompileAnalyticLights(const PbrtScene& scene, RenderSceneIR& ir, std::vector<SceneDiagnostic>& diagnostics) {
    for (const PbrtLightRecord& record : scene.lights) {
        if (record.light.type == "point") {
            // ... existing point-light handling (unchanged) ...
        } else if (record.light.type == "infinite") {
            CompileEnvironmentLight(record, scene, ir, diagnostics);
        } else if (record.light.type == "distant" ||
                   record.light.type == "spot") {
            diagnostics.push_back(Warning(scene, "LightSource",
                "LightSource type '" + record.light.type + "' is not yet supported in M1 Slice 2 and was ignored"));
        } else {
            diagnostics.push_back(Warning(scene, "LightSource",
                "unsupported LightSource type: " + record.light.type));
        }
    }
}
```

Add `#include <yaoray/render/environment.hpp>` at the top of `scene_compiler.cpp` if not already present (for `BuildEnvironmentDistribution`).

- [ ] **Step 4: Run tests and verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the three new environment tests pass. Previous tests still pass. New total: 146 + 3 = 149.

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp \
        tests/scene_compiler_environment_tests.cpp \
        CMakeLists.txt
git commit -m "feat: compile LightSource \"infinite\" into RenderEnvironment + distribution"
```

---

## Task 4: `material_studio` scene + HDR asset + CTest + README

**Files:**
- Create: `scenes/pbrt/material_studio/material_studio.pbrt`
- Create: `scenes/pbrt/material_studio/material_studio_smoke.pbrt`
- Create: `scenes/pbrt/material_studio/env/sky.hdr` (copy from `tests/fixtures/assets/tiny_env.hdr` — see Step 1 below)
- Modify: `CMakeLists.txt` (CTest entry)
- Modify: `README.md` (add material_studio to runnable scenes)

**HDR asset note:** Slice 2 establishes the wiring with a tiny placeholder HDR. A real "sky" download from polyhaven can be substituted in a follow-up. The placeholder is `tests/fixtures/assets/tiny_env.hdr` — a 1×1 or 4×2 HDR fixture that already lives in the repo. The image quality is non-photogenic but the pipeline is exercised end to end.

If you want a slightly more interesting bundled HDR, you may instead generate a 64×32 procedural gradient HDR via a one-off script (e.g. a small C++ program that writes the RGBE format). That's out of scope for this task — proceed with the placeholder.

- [ ] **Step 1: Copy the HDR placeholder into the scene's assets folder**

```bash
mkdir -p scenes/pbrt/material_studio/env
cp tests/fixtures/assets/tiny_env.hdr scenes/pbrt/material_studio/env/sky.hdr
```

(Use the shell equivalent on Windows: `New-Item -ItemType Directory ...` + `Copy-Item ...`.)

- [ ] **Step 2: Author `material_studio.pbrt`**

Create `scenes/pbrt/material_studio/material_studio.pbrt`:

```pbrt
# YaoRay M1 Slice 2 — material studio.
#
# Five spheres of different BSDFs on a wide diffuse ground plane,
# illuminated by an HDRI sky. Exercises:
#   - Shape "sphere"                            (Slice 1)
#   - Texture "imagemap"                        (Slice 2)
#   - LightSource "infinite"                    (Slice 2)
#   - Material texture binding via TexParam     (Slice 2)
#   - Materials: diffuse, conductor, dielectric,
#     coateddiffuse, coatedconductor

LookAt 0 1.0 4.5  0 0.4 0  0 1 0
Camera "perspective" "float fov" [38]
Sampler "independent" "integer pixelsamples" [128]
Integrator "path" "integer maxdepth" [6]
Film "rgb"
    "integer xresolution" [400]
    "integer yresolution" [200]
    "string filename" ["out/material_studio.png"]

WorldBegin

# --- environment light (HDRI sky) -------------------------------------
LightSource "infinite"
    "string filename" ["env/sky.hdr"]
    "rgb L" [1 1 1]
    "float scale" [1]

# --- materials --------------------------------------------------------
MakeNamedMaterial "ground"
    "string type" ["diffuse"]
    "rgb reflectance" [0.6 0.6 0.6]

MakeNamedMaterial "matte_red"
    "string type" ["diffuse"]
    "rgb reflectance" [0.7 0.15 0.12]

MakeNamedMaterial "gold"
    "string type" ["conductor"]
    "rgb eta" [0.18 0.42 1.37]
    "rgb k"   [3.98 2.41 1.60]
    "float uroughness" [0.05]
    "float vroughness" [0.05]

MakeNamedMaterial "clear_glass"
    "string type" ["dielectric"]
    "float eta" [1.5]

MakeNamedMaterial "porcelain"
    "string type" ["coateddiffuse"]
    "rgb reflectance" [0.92 0.92 0.92]
    "float eta" [1.46]
    "float roughness" [0.06]

MakeNamedMaterial "car_paint"
    "string type" ["coatedconductor"]
    "rgb conductor.eta" [1.30 0.45 0.10]
    "rgb conductor.k"   [4.20 2.30 1.10]
    "float conductor.roughness" [0.25]
    "float eta" [1.5]
    "float roughness" [0.04]

# --- ground -----------------------------------------------------------
AttributeBegin
NamedMaterial "ground"
Shape "trianglemesh"
    "point3 P" [-4 0 -3   4 0 -3   4 0 3   -4 0 3]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- five spheres in a row --------------------------------------------
AttributeBegin
NamedMaterial "matte_red"
Translate -2.0 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

AttributeBegin
NamedMaterial "gold"
Translate -1.0 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

AttributeBegin
NamedMaterial "clear_glass"
Translate 0.0 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

AttributeBegin
NamedMaterial "porcelain"
Translate 1.0 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

AttributeBegin
NamedMaterial "car_paint"
Translate 2.0 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

WorldEnd
```

- [ ] **Step 3: Render manually and eyeball the result**

```bash
cmake --build build --config Release
./build/Release/yaoray.exe render scenes/pbrt/material_studio/material_studio.pbrt --backend cpu
```

Expected stdout: `Compiled triangles: 2` (ground), `Compiled spheres: 5`, `Compiled materials: 6`, `Compiled textures: 1` (the HDRI), `Rendered image: scenes/pbrt/material_studio/out/material_studio.png`, `Hits: > 0`.

(If the CLI reports a different sphere-count field, that's fine — what matters is the render completes without `Error:` diagnostics. `Compiled triangles` in the existing CLI counts only mesh triangles; the spheres are tracked separately as `ir.spheres.size()` — the CLI's stats line will need to be updated only if you want sphere counts surfaced; out of scope for this task.)

Open the PNG. You should see a row of five spheres on a grey floor under sky light. Don't worry about beauty — the placeholder HDR is tiny; just confirm:
- All five spheres render (not just one or two).
- Each sphere is **visually distinct**: red matte, gold reflective, clear glass, white porcelain, colored car paint.
- The ground plane is visible and lit by the HDRI from above (not pitch black).

If anything is missing or the image is mostly black:
- Check the environment is active: re-render with a small print-stats option if available; or render the smoke variant (Step 4) which has higher signal-to-noise per pixel at low resolution.
- Try a brighter HDR (raise `"float scale" [10]` temporarily) to confirm the env is reachable.

- [ ] **Step 4: Author the smoke variant**

Create `scenes/pbrt/material_studio/material_studio_smoke.pbrt` by copying `material_studio.pbrt` and changing:
- `"integer pixelsamples" [128]` → `"integer pixelsamples" [4]`
- `"integer xresolution" [400]` → `"integer xresolution" [80]`
- `"integer yresolution" [200]` → `"integer yresolution" [40]`
- `"string filename" ["out/material_studio.png"]` → `"string filename" ["out/material_studio_smoke.png"]`

Also update the leading comment to say "low-resolution smoke variant for CTest" instead of the full-render header.

- [ ] **Step 5: Add CTest entry**

In `CMakeLists.txt`, after `yaoray_cli_render_pbrt_cornell_box` (around line ~180), add:

```cmake
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_material_studio
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/material_studio/material_studio_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/material_studio/out/material_studio_smoke.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [^0]"
            "Compiled textures: [^0]"
    )
```

The `"Compiled textures: [^0]"` regex ensures the HDRI was actually loaded into `ir.textures`. (If your CLI's stats line uses a different label, adjust the regex; or omit that pattern if the CLI doesn't surface texture counts. The first three patterns are mandatory.)

- [ ] **Step 6: Run CTest and confirm**

```bash
cd build && ctest --output-on-failure -C Release
```

Expected: 7/7 pass. `yaoray_cli_render_pbrt_material_studio` should be one of them.

- [ ] **Step 7: Update README**

In `README.md`, find the "Run" section (lightly updated in Slice 1) and add a line for `material_studio` after `cornell_box_pbrt`. Both OS blocks:

```markdown
macOS/Linux:

```bash
./build/yaoray --help
./build/yaoray --version
./build/yaoray render scenes/pbrt/hello_emissive/hello_emissive.pbrt --backend cpu
./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu
./build/yaoray render scenes/pbrt/material_studio/material_studio.pbrt --backend cpu
```

Windows:

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\pbrt\hello_emissive\hello_emissive.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\cornell_box_pbrt\cornell_box_pbrt.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\material_studio\material_studio.pbrt --backend cpu
```
```

(Don't attempt a full README rewrite — leave it for Slice 4.)

- [ ] **Step 8: Commit**

```bash
git add scenes/pbrt/material_studio/ CMakeLists.txt README.md
git commit -m "feat: material_studio PBRT scene + HDRI sky + CTest + README pointer"
```

---

## Wrap-up checklist

After all 4 tasks:

- [ ] `./build/Release/yaoray_tests.exe` — all previous tests plus 12 new tests (5 in Task 1, 4 in Task 2, 3 in Task 3) pass. Total ≈ 149/149.
- [ ] `cd build && ctest --output-on-failure -C Release` — 7/7 (the 6 from Slice 1 + the new `yaoray_cli_render_pbrt_material_studio`).
- [ ] `./build/Release/yaoray.exe render scenes/pbrt/material_studio/material_studio.pbrt --backend cpu` produces a recognizable row-of-spheres image with the ground lit by HDRI.
- [ ] `git log --oneline | head -10` shows ~4 atomic commits, one per task.
- [ ] Local `main` has not been touched — all commits live on the slice's worktree branch.

After verification, the controller invokes `superpowers:finishing-a-development-branch` to merge the slice back to local `main`. (Per the user's instructions, `origin/main` will not be pushed until all of M1 is complete.)
