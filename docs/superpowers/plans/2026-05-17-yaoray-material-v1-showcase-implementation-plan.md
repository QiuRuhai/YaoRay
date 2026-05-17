# YaoRay Material v1 Showcase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add explicit diffuse/mirror material kinds and a Cornell-style showcase scene that demonstrates perfect mirror reflection.

**Architecture:** Extend the existing scene and render material structs with a small `MaterialKind` enum, parsed from `[[materials]] type` with default `diffuse`. Keep the debug renderer non-recursive, and branch only the CPU path tracer between the existing diffuse path and a new perfect mirror reflection path.

**Tech Stack:** C++20, TOML parser with toml++, CMake/CTest, current in-repo `yr_test` harness, CPU path tracer, inline quad assets, PNG output.

---

## Scope

This plan implements the approved spec:

- `docs/superpowers/specs/2026-05-17-yaoray-material-v1-showcase-design.md`

It does not implement glass, roughness, GGX, metal Fresnel, texture maps, normal maps, OBJ `.mtl`, sphere primitives, MIS, spectral rendering, CUDA materials, or a broad material module refactor.

## File Structure

- Modify `include/yaoray/scene/scene.hpp`
  - Add `MaterialKind`.
  - Add `MaterialDescription::type = MaterialKind::Diffuse`.
  - Declare material kind name/parse helpers.
- Modify `src/scene/scene.cpp`
  - Implement `MaterialKindName` and `ParseMaterialKindName`.
- Modify `src/scene/scene_parser.cpp`
  - Allow and parse `[[materials]] type`.
- Modify `include/yaoray/render/render_scene.hpp`
  - Add `RenderMaterial::type = MaterialKind::Diffuse`.
- Modify `src/render/scene_compiler.cpp`
  - Copy material type to compiled materials.
- Modify `tests/scene_tests.cpp`
  - Add material type enum/default/parser/rejection tests.
- Modify `tests/render_scene_tests.cpp`
  - Add render material default and compiler propagation tests.
  - Update aggregate initializers affected by the new `MaterialKind` field.
- Modify `tests/backend_tests.cpp`, `tests/cpu_debug_renderer_tests.cpp`, and `tests/cpu_path_tracer_tests.cpp`
  - Update `RenderMaterial` aggregate initializers affected by the new field.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Add mirror reflection scattering in `TracePath`.
- Create `scenes/examples/material_showcase.toml`
  - Cornell-style material showcase with diffuse and mirror blocks.
- Modify `README.md`
  - Document material kinds and the showcase.
- Modify `docs/architecture/overview.md`
  - Document material v1 behavior and current material limitations.

## Task 1: Add Material Kind To Scene Parsing And Compilation

**Files:**
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `tests/cpu_debug_renderer_tests.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene.cpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`

- [ ] **Step 1: Add failing scene enum and parser tests**

In `tests/scene_tests.cpp`, update `scene_enum_names_are_stable` by adding:

```cpp
YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Diffuse), std::string_view{"diffuse"});
YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Mirror), std::string_view{"mirror"});
```

Update `scene_enum_parsers_accept_stable_names` by adding:

```cpp
YR_EXPECT_EQ(yr::ParseMaterialKindName("diffuse").value(), yr::MaterialKind::Diffuse);
YR_EXPECT_EQ(yr::ParseMaterialKindName("mirror").value(), yr::MaterialKind::Mirror);
```

Update `scene_enum_parsers_reject_unknown_names` by adding:

```cpp
YR_EXPECT_TRUE(!yr::ParseMaterialKindName("glass").has_value());
```

Update `scene_enum_names_return_unknown_for_invalid_values` by adding:

```cpp
YR_EXPECT_EQ(yr::MaterialKindName(static_cast<yr::MaterialKind>(999)), std::string_view{"unknown"});
```

Update `scene_parser_applies_material_defaults` by adding this expectation for the loaded/defaulted material:

```cpp
YR_EXPECT_EQ(material.type, yr::MaterialKind::Diffuse);
```

Add these tests after `scene_parser_loads_materials_and_instance_material_binding`:

```cpp
YR_TEST(scene_parser_loads_diffuse_material_type) {
    const std::filesystem::path path = WriteTempScene(
        "diffuse_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "matte"
type = "diffuse"
albedo = [0.7, 0.6, 0.5]
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().materials[0].type, yr::MaterialKind::Diffuse);
}

YR_TEST(scene_parser_loads_mirror_material_type) {
    const std::filesystem::path path = WriteTempScene(
        "mirror_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "mirror"
type = "mirror"
albedo = [0.95, 0.95, 0.95]
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().materials[0].type, yr::MaterialKind::Mirror);
}
```

Add these tests near `scene_parser_rejects_bad_material_entries`:

```cpp
YR_TEST(scene_parser_rejects_unknown_material_type) {
    const std::filesystem::path path = WriteTempScene(
        "unknown_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "glass_for_later"
type = "glass"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.type", "unknown material type"));
}

YR_TEST(scene_parser_rejects_non_string_material_type) {
    const std::filesystem::path path = WriteTempScene(
        "non_string_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_type"
type = 7
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.type", "must be a string"));
}
```

- [ ] **Step 2: Add failing render material compiler tests**

In `tests/render_scene_tests.cpp`, update `render_scene_defaults_are_backend_friendly` by adding this assertion after the materials vector default checks or after constructing a default `RenderMaterial`:

```cpp
const yr::RenderMaterial material;
YR_EXPECT_EQ(material.type, yr::MaterialKind::Diffuse);
```

Add this test after `scene_compiler_compiles_named_materials`:

```cpp
YR_TEST(scene_compiler_copies_material_type) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "mirror",
        yr::MaterialKind::Mirror,
        yr::Color3f{0.95f, 0.95f, 0.95f},
        yr::Color3f{}
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials[0].type, yr::MaterialKind::Mirror);
    YR_EXPECT_NEAR(compiled.materials[0].albedo.x, 0.95, 1e-6);
}
```

Update `scene_compiler_preserves_default_material_for_unbound_instances` by adding:

```cpp
YR_EXPECT_EQ(compiled.materials[1].type, yr::MaterialKind::Diffuse);
```

before checking the default material albedo.

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build fails because `MaterialKind`, `MaterialKindName`, `ParseMaterialKindName`, `MaterialDescription::type`, and `RenderMaterial::type` do not exist yet.

- [ ] **Step 4: Add material kind to scene data model**

In `include/yaoray/scene/scene.hpp`, add this enum after `LightKind`:

```cpp
enum class MaterialKind {
    Diffuse,
    Mirror,
};
```

Replace `MaterialDescription` with:

```cpp
struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

Add these declarations near the other enum helper declarations:

```cpp
std::string_view MaterialKindName(MaterialKind kind);
std::optional<MaterialKind> ParseMaterialKindName(std::string_view name);
```

- [ ] **Step 5: Implement material kind helper functions**

In `src/scene/scene.cpp`, add these functions after `ParseLightKindName`:

```cpp
std::string_view MaterialKindName(MaterialKind kind) {
    switch (kind) {
        case MaterialKind::Diffuse:
            return "diffuse";
        case MaterialKind::Mirror:
            return "mirror";
    }
    return "unknown";
}

std::optional<MaterialKind> ParseMaterialKindName(std::string_view name) {
    if (name == "diffuse") {
        return MaterialKind::Diffuse;
    }
    if (name == "mirror") {
        return MaterialKind::Mirror;
    }
    return std::nullopt;
}
```

- [ ] **Step 6: Parse `materials.type`**

In `src/scene/scene_parser.cpp`, change the allowed material fields from:

```cpp
CheckUnknownFields(*table, "materials", {"name", "albedo", "emission"}, file, diagnostics);
```

to:

```cpp
CheckUnknownFields(*table, "materials", {"name", "type", "albedo", "emission"}, file, diagnostics);
```

After the material name parsing block and before albedo parsing, add:

```cpp
if (const auto type = ReadString(*table, "type", file, "materials.type", diagnostics)) {
    if (const auto parsed = ParseMaterialKindName(*type)) {
        material.type = *parsed;
    } else {
        diagnostics.push_back(Error(file, "materials.type", "unknown material type"));
    }
}
```

- [ ] **Step 7: Add material kind to compiled render material**

In `include/yaoray/render/render_scene.hpp`, replace `RenderMaterial` with:

```cpp
struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

In `src/render/scene_compiler.cpp`, update material compilation from:

```cpp
compiled.materials.push_back(RenderMaterial{material.albedo, material.emission});
```

to:

```cpp
compiled.materials.push_back(RenderMaterial{material.type, material.albedo, material.emission});
```

- [ ] **Step 8: Update affected aggregate initializers**

Because `MaterialKind type` is inserted before `albedo`, update direct aggregate initializers.

In `tests/render_scene_tests.cpp`, change existing `yr::MaterialDescription` initializers such as:

```cpp
yr::MaterialDescription{
    "red",
    yr::Color3f{0.9f, 0.1f, 0.05f},
    yr::Color3f{0.0f, 0.0f, 0.0f}
}
```

to:

```cpp
yr::MaterialDescription{
    "red",
    yr::MaterialKind::Diffuse,
    yr::Color3f{0.9f, 0.1f, 0.05f},
    yr::Color3f{0.0f, 0.0f, 0.0f}
}
```

In `tests/backend_tests.cpp`, `tests/cpu_debug_renderer_tests.cpp`, and `tests/cpu_path_tracer_tests.cpp`, change existing `yr::RenderMaterial` initializers such as:

```cpp
yr::RenderMaterial{yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}}
```

to:

```cpp
yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}}
```

Do the same for every `yr::RenderMaterial{albedo, emission}` and every `yr::MaterialDescription{name, albedo, emission}` occurrence. Do not change semantic values while adding the new enum argument.

- [ ] **Step 9: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing scenes without `materials.type` still parse.
- New parser tests accept `diffuse` and `mirror`, and reject unknown/non-string material types.
- Compiler tests prove material kinds are propagated and default materials remain diffuse.

- [ ] **Step 10: Commit material parsing and compiler support**

Run:

```powershell
git add include\yaoray\scene\scene.hpp src\scene\scene.cpp src\scene\scene_parser.cpp include\yaoray\render\render_scene.hpp src\render\scene_compiler.cpp tests\scene_tests.cpp tests\render_scene_tests.cpp tests\backend_tests.cpp tests\cpu_debug_renderer_tests.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: parse material kinds"
```

## Task 2: Add Mirror Scattering To CPU Path Tracer

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add failing mirror path tracer tests**

In `tests/cpu_path_tracer_tests.cpp`, add this helper near the existing scene helpers:

```cpp
yr::RenderScene MakeMirrorTriangleScene() {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.max_depth = 2;
    scene.environment.radiance = yr::Color3f{0.2f, 0.4f, 0.6f};
    scene.environment.strength = 1.0f;
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.5f, 0.5f, 0.5f},
        yr::Color3f{}
    };
    return scene;
}
```

Add these tests after `cpu_path_tracer_sees_emissive_surfaces`:

```cpp
YR_TEST(cpu_path_tracer_mirror_reflects_environment) {
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(MakeMirrorTriangleScene());
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.1, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.3, 1e-6);
}

YR_TEST(cpu_path_tracer_mirror_skips_diffuse_direct_lighting) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.max_depth = 1;
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    };

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_black_mirror_stops_after_emission) {
    yr::RenderScene scene = MakeBaseScene(3, 3);
    scene.max_depth = 2;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{},
        yr::Color3f{0.25f, 0.5f, 0.75f}
    };

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}
```

Update `MakeThreadedDeterminismScene()` by changing its material to mirror after the albedo assignment:

```cpp
scene.materials[0].type = yr::MaterialKind::Mirror;
```

- [ ] **Step 2: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- `cpu_path_tracer_mirror_reflects_environment` fails because mirror materials are still treated as diffuse.
- `cpu_path_tracer_mirror_skips_diffuse_direct_lighting` fails because the current direct-light estimator runs for all material types.

- [ ] **Step 3: Add mirror reflection helper**

In `src/backends/cpu/cpu_path_tracer.cpp`, add this helper after `FaceForward`:

```cpp
Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}
```

- [ ] **Step 4: Branch `TracePath` by material type**

In `src/backends/cpu/cpu_path_tracer.cpp`, replace the material scattering block inside `TracePath`:

```cpp
radiance = radiance + Multiply(throughput, material.emission);
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, sampler, stats));

if (depth + 1 >= max_depth || IsNearBlack(material.albedo)) {
    break;
}

throughput = Multiply(throughput, material.albedo);
const Vec3f bounce_direction = SampleCosineHemisphere(normal, sampler);
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bounce_direction};
```

with:

```cpp
radiance = radiance + Multiply(throughput, material.emission);

if (material.type == MaterialKind::Diffuse) {
    radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, sampler, stats));
}

if (depth + 1 >= max_depth || IsNearBlack(material.albedo)) {
    break;
}

throughput = Multiply(throughput, material.albedo);
if (material.type == MaterialKind::Mirror) {
    const Vec3f reflected_direction = Reflect(ray.direction, normal);
    ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), reflected_direction};
    continue;
}

const Vec3f bounce_direction = SampleCosineHemisphere(normal, sampler);
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bounce_direction};
```

This preserves the current diffuse direct-lighting order and keeps mirror as the only new scattering branch. Do not add a generic BSDF module in this slice.

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Mirror tests pass.
- Existing diffuse direct-light and bounce tests continue to pass.
- Thread-count determinism still passes with a mirror material in the determinism scene.

- [ ] **Step 6: Commit mirror path scattering**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: trace perfect mirror materials"
```

## Task 3: Add Material Showcase Scene And Documentation

**Files:**
- Create: `scenes/examples/material_showcase.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Create the material showcase scene**

Create `scenes/examples/material_showcase.toml` by copying `scenes/examples/cornell_box_path.toml` as a starting point, then make these exact changes:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 256
height = 256
spp = 32
max_depth = 8
seed = 1
light_samples = 4
sampler = "stratified"

[film]
output = "out/material_showcase.png"
tone_mapper = "aces"
exposure = 0.0
```

In the material list, keep the Cornell red/green/white/light materials and add explicit diffuse types:

```toml
[[materials]]
name = "cornell_white"
type = "diffuse"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]
```

Apply the same `type = "diffuse"` field to `cornell_red`, `cornell_green`, and `cornell_light`.

Add this mirror material:

```toml
[[materials]]
name = "polished_mirror"
type = "mirror"
albedo = [0.95, 0.95, 0.95]
emission = [0, 0, 0]
```

Keep the existing Cornell room, wall, light panel, short block, and tall block inline quad assets. In the instances section, bind the tall block to the mirror material:

```toml
[[instances]]
asset = "cornell_tall_block"
material = "polished_mirror"
```

Keep the short block bound to `cornell_white` so the scene contains both diffuse and mirror objects. Do not modify `scenes/examples/cornell_box.toml`, `scenes/examples/cornell_box_path.toml`, or `scenes/examples/cornell_box_path_threaded.toml` in this task.

- [ ] **Step 2: Update README**

In `README.md`, update the implemented features list by replacing:

```markdown
- TOML named diffuse/emissive materials with instance material binding
```

with:

```markdown
- TOML named diffuse, emissive, and perfect mirror materials with instance material binding
```

Add the showcase render command after the Cornell path command:

```powershell
build\Debug\yaoray.exe render scenes\examples\material_showcase.toml --backend cpu
```

Update the final render paragraph to include:

```markdown
Materials default to `type = "diffuse"`; `type = "mirror"` enables perfect specular reflection in the CPU path integrator, while `emission` remains an additive material property. `scenes/examples/material_showcase.toml` demonstrates diffuse, emissive, and mirror materials in a Cornell-style scene.
```

Keep the existing limitation statement that advanced material models are not implemented, and mention glass, roughness, textures, imported materials, and CUDA materials as future work.

- [ ] **Step 3: Update architecture overview**

In `docs/architecture/overview.md`, update the implemented slices list item:

```markdown
- TOML named diffuse/emissive materials with instance-level material binding
```

to:

```markdown
- TOML named diffuse, emissive, and perfect mirror materials with instance-level material binding
```

Update the CPU backend paragraph so it says `path` supports diffuse and perfect mirror scattering, and `debug_direct` does not recursively reflect mirror materials.

Add a short paragraph after the Cornell paragraph:

```markdown
The material showcase scene is a Cornell-style manual preview for material behavior. It uses inline quads and the CPU path tracer to show diffuse surfaces, emissive light panels, and a perfect mirror block. Glass, roughness, texture maps, imported material files, and CUDA material evaluation remain future work.
```

- [ ] **Step 4: Run docs and scene checks**

Run:

```powershell
rg -n "material_showcase|type = \"mirror\"|type = \"diffuse\"|polished_mirror|glass|roughness|texture|CUDA|debug_direct" README.md docs\architecture\overview.md scenes\examples\material_showcase.toml
```

Expected:

- `material_showcase.toml` contains `type = "mirror"` and `polished_mirror`.
- README includes the material showcase command and explains `type = "mirror"`.
- Architecture overview says `debug_direct` does not recursively reflect mirror materials.
- Future-work wording mentions glass, roughness, textures, and CUDA materials.

- [ ] **Step 5: Commit showcase scene and docs**

Run:

```powershell
git add scenes\examples\material_showcase.toml README.md docs\architecture\overview.md
git commit -m "docs: add material showcase scene"
```

## Task 4: Full Verification And Manual Showcase Render

**Files:**
- No source edits expected.
- Do not commit generated files under `scenes/examples/out/`.

- [ ] **Step 1: Run full Debug configure, build, and tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

- Configure succeeds.
- Build succeeds.
- All CTest tests pass.

- [ ] **Step 2: Run Release build**

Run:

```powershell
cmake --build build-release --config Release
```

If `build-release` does not exist, create it first:

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build-release --config Release
```

Expected:

- Release `yaoray.exe` builds successfully.

- [ ] **Step 3: Render the material showcase**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\material_showcase.toml --backend cpu
```

If the local generator produced a single-config Ninja build instead of Visual Studio, use:

```powershell
.\build-release\yaoray.exe render .\scenes\examples\material_showcase.toml --backend cpu
```

Expected output contains:

```text
Integrator: path
Threads:
Samples/sec:
Rays/sec:
Shadow rays:
Rendered image: scenes/examples/out/material_showcase.png
```

The render should succeed and produce a PNG. The mirror block should visibly reflect room colors or nearby scene structure at the current quality level.

- [ ] **Step 4: Check implementation scope**

Run:

```powershell
rg -n "MaterialKind|MaterialKindName|ParseMaterialKindName|type = \"mirror\"|type = \"diffuse\"|Reflect\\(|EstimateDirectLight|glass|roughness|GGX|texture|normal map|mtl|MIS|spectral|CUDA|material_showcase" include src tests README.md docs\architecture\overview.md scenes\examples\material_showcase.toml docs\superpowers\specs\2026-05-17-yaoray-material-v1-showcase-design.md docs\superpowers\plans\2026-05-17-yaoray-material-v1-showcase-implementation-plan.md
```

Expected:

- `MaterialKind` appears in scene settings, render material, parser/name helpers, compiler, tests, and CPU path tracer.
- `Reflect` appears only in CPU path tracer.
- `EstimateDirectLight` still implements diffuse direct lighting.
- Glass, roughness, GGX, textures, normal maps, `.mtl`, MIS, spectral rendering, and CUDA material work appear only in non-goals, limitations, or future-work docs.

- [ ] **Step 5: Confirm clean git state and recent commits**

Run:

```powershell
git status --short --branch
git log --oneline --decorate -8
```

Expected:

- Working tree is clean except for any pre-existing user edits that were explicitly not part of this plan.
- Recent commits include:
  - `feat: parse material kinds`
  - `feat: trace perfect mirror materials`
  - `docs: add material showcase scene`

## Self-Review Checklist

- Spec coverage:
  - `materials.type` with default `diffuse`: Task 1.
  - `diffuse` and `mirror` parser support: Task 1.
  - Invalid material type diagnostics: Task 1.
  - Existing material syntax preserved: Task 1 parser defaults and full tests.
  - Compiler propagation to `RenderMaterial`: Task 1.
  - CPU path tracer branches between diffuse and mirror: Task 2.
  - Mirror skips diffuse direct lighting: Task 2 tests.
  - Mirror reflects rays perfectly: Task 2 implementation and tests.
  - `debug_direct` remains non-recursive: Task 3 docs and no debug renderer implementation change.
  - Cornell-style material showcase scene: Task 3.
  - No glass/roughness/textures/CUDA/MIS/spectral work: Task 3 docs and Task 4 scope check.
- Placeholder scan:
  - Every task names concrete files, code snippets, commands, and expected outcomes.
- Type consistency:
  - `MaterialKind`, `MaterialDescription::type`, `RenderMaterial::type`, `MaterialKindName`, and `ParseMaterialKindName` are used consistently.
  - Aggregate initializer changes are explicitly called out because `type` is inserted before `albedo`.
