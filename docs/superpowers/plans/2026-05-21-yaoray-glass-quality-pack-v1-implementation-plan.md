# YaoRay Glass Quality Pack v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add practical glass quality improvements: Beer-Lambert absorption for thick dielectric paths, optional firefly/radiance clamping, and a clearer glass showcase.

**Architecture:** Extend existing semantic and render material structs with neutral absorption defaults, then add one lightweight CPU path tracer medium state that toggles only on thick dielectric transmission. Keep this v1 single-medium and data-oriented; do not introduce virtual material classes, nested medium stacks, denoisers, caustic algorithms, or CUDA code.

**Tech Stack:** C++20, existing YaoRay test harness (`yaoray_tests` through CTest), TOML scene parser via toml++, CPU path tracer, PowerShell CTest CLI smoke tests.

---

## File Structure

- Modify `include/yaoray/scene/scene.hpp`: add `RenderSettings::radiance_clamp`, `MaterialDescription::absorption_color`, and `MaterialDescription::absorption_distance`.
- Modify `include/yaoray/render/render_scene.hpp`: add matching `RenderScene::radiance_clamp`, `RenderMaterial::absorption_color`, and `RenderMaterial::absorption_distance`.
- Modify `src/scene/scene_parser.cpp`: parse and validate `render.radiance_clamp`, `materials.absorption_color`, and `materials.absorption_distance`.
- Modify `src/render/scene_compiler.cpp`: copy the new render and material fields.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: add sample radiance clamping and one active dielectric medium state for Beer-Lambert attenuation.
- Modify `tests/scene_tests.cpp`: parser/default validation coverage.
- Modify `tests/render_scene_tests.cpp`: compiler copy coverage.
- Modify `tests/cpu_path_tracer_tests.cpp`: path absorption and radiance clamp behavior coverage.
- Modify `scenes/examples/glass_showcase.toml`: use tinted absorbing glass and optional clamp.
- Create or update `tests/check_glass_showcase_visual.ps1`: guard the showcase against overexposed or contrast-free output.
- Modify `CMakeLists.txt`: include the glass visual sanity CLI test if it is not already present.
- Modify `README.md`, `docs/architecture/overview.md`, and `docs/superpowers/specs/2026-05-21-yaoray-glass-quality-pack-v1-design.md`: document implementation status and limits.

## Pre-Flight

- [ ] **Step 1: Confirm the current dirty state before executing**

Run:

```powershell
git status --short --branch
```

Expected: note any unrelated dirty files. Do not revert user changes. If prior glass showcase visual sanity work is already present, keep it and adapt it instead of deleting it.

- [ ] **Step 2: Build the current baseline**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: the build succeeds and existing tests pass before this plan starts. If unrelated dirty files cause failures, stop and identify the file before changing implementation.

---

### Task 1: Add Data Model Defaults

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Write failing default tests**

In `tests/scene_tests.cpp`, extend `scene_defaults_match_documented_values`:

```cpp
YR_EXPECT_NEAR(scene.render.radiance_clamp, 0.0, 1e-6);
```

Extend `scene_defaults_include_dielectric_material_fields`:

```cpp
YR_EXPECT_NEAR(material.absorption_color.x, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.y, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
```

In `tests/render_scene_tests.cpp`, extend the existing render defaults test that creates `const yr::RenderScene scene;` and `const yr::RenderMaterial material;`:

```cpp
YR_EXPECT_NEAR(scene.radiance_clamp, 0.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.x, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.y, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
```

- [ ] **Step 2: Verify the tests fail to compile**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `radiance_clamp`, `absorption_color`, and `absorption_distance` are not defined yet.

- [ ] **Step 3: Add semantic defaults**

In `include/yaoray/scene/scene.hpp`, update `RenderSettings`:

```cpp
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
```

Update `MaterialDescription`:

```cpp
struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

- [ ] **Step 4: Add render defaults**

In `include/yaoray/render/render_scene.hpp`, update `RenderMaterial`:

```cpp
struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

Update `RenderScene`:

```cpp
struct RenderScene {
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
    // keep existing camera/environment/material/vector fields below unchanged
};
```

Do not reorder existing fields except appending the new defaults where shown. This preserves existing aggregate initialization call sites as much as possible.

- [ ] **Step 5: Verify defaults pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 6: Commit data model defaults**

```powershell
git add include/yaoray/scene/scene.hpp include/yaoray/render/render_scene.hpp tests/scene_tests.cpp tests/render_scene_tests.cpp
git commit -m "feat: add glass quality render fields"
```

---

### Task 2: Parse Absorption And Radiance Clamp

**Files:**
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add parser tests for valid fields**

In `tests/scene_tests.cpp`, add after `scene_parser_loads_render_light_samples`:

```cpp
YR_TEST(scene_parser_loads_render_radiance_clamp) {
    const std::filesystem::path path = WriteTempScene(
        "render_radiance_clamp.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
radiance_clamp = 12.5
)toml",
            R"toml(
[film]
output = "out/test.png"
)toml"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_NEAR(result.scene.value().render.radiance_clamp, 12.5, 1e-6);
}
```

In the existing `scene_parser_loads_dielectric_material_fields` test, add these authored fields to the TOML:

```toml
absorption_color = [0.55, 0.75, 1.0]
absorption_distance = 2.5
```

Then add expectations:

```cpp
YR_EXPECT_NEAR(material.absorption_color.x, 0.55, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.y, 0.75, 1e-6);
YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
YR_EXPECT_NEAR(material.absorption_distance, 2.5, 1e-6);
```

- [ ] **Step 2: Add parser rejection tests**

Add these tests near the existing material field rejection tests:

```cpp
YR_TEST(scene_parser_rejects_out_of_range_material_absorption_color) {
    const std::filesystem::path path = WriteTempScene(
        "out_of_range_material_absorption_color.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_absorption"
type = "dielectric"
absorption_color = [0.4, 1.2, 0.8]
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.absorption_color", "must be in [0, 1]"));
}

YR_TEST(scene_parser_rejects_non_positive_material_absorption_distance) {
    const std::filesystem::path path = WriteTempScene(
        "non_positive_material_absorption_distance.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_absorption_distance"
type = "dielectric"
absorption_distance = 0.0
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.absorption_distance", "must be positive"));
}

YR_TEST(scene_parser_rejects_negative_render_radiance_clamp) {
    const std::filesystem::path path = WriteTempScene(
        "negative_render_radiance_clamp.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
radiance_clamp = -1.0
)toml",
            R"toml(
[film]
output = "out/test.png"
)toml"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.radiance_clamp", "must be non-negative"));
}
```

- [ ] **Step 3: Verify parser tests fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build or test failure because the parser rejects unknown fields or does not populate the new values.

- [ ] **Step 4: Add a unit vec3 parser helper**

In `src/scene/scene_parser.cpp`, after the existing `ReadVec3()` helper definition, add:

```cpp
std::optional<Vec3f> ReadUnitVec3(
    const toml::table& table,
    std::string_view key,
    const std::filesystem::path& file,
    std::string field,
    std::vector<SceneDiagnostic>& diagnostics
) {
    std::optional<Vec3f> value = ReadVec3(table, key, file, field, diagnostics);
    if (!value.has_value()) {
        return std::nullopt;
    }
    if (value->x < 0.0f || value->x > 1.0f ||
        value->y < 0.0f || value->y > 1.0f ||
        value->z < 0.0f || value->z > 1.0f) {
        diagnostics.push_back(Error(file, std::move(field), "must be in [0, 1]"));
        return std::nullopt;
    }
    return value;
}
```

- [ ] **Step 5: Parse radiance clamp**

In `ParseRender()`, add `"radiance_clamp"` to the render known field list:

```cpp
{"backend", "integrator", "sampler", "width", "height", "spp", "max_depth", "seed", "threads", "light_samples", "radiance_clamp"}
```

After `light_samples` parsing, add:

```cpp
if (const auto radiance_clamp = ReadFloat(table, "radiance_clamp", file, "render.radiance_clamp", diagnostics)) {
    if (*radiance_clamp < 0.0f) {
        diagnostics.push_back(Error(file, "render.radiance_clamp", "must be non-negative"));
    } else {
        scene.render.radiance_clamp = *radiance_clamp;
    }
}
```

- [ ] **Step 6: Parse material absorption fields**

In `ParseMaterials()`, add the new known material fields:

```cpp
{"name", "type", "albedo", "emission", "roughness", "specular", "ior", "thin", "absorption_color", "absorption_distance"}
```

After `thin` parsing, add:

```cpp
if (const auto absorption_color =
        ReadUnitVec3(*table, "absorption_color", file, "materials.absorption_color", diagnostics)) {
    material.absorption_color = *absorption_color;
}
if (const auto absorption_distance =
        ReadFloat(*table, "absorption_distance", file, "materials.absorption_distance", diagnostics)) {
    if (*absorption_distance <= 0.0f) {
        diagnostics.push_back(Error(file, "materials.absorption_distance", "must be positive"));
    } else {
        material.absorption_distance = *absorption_distance;
    }
}
```

- [ ] **Step 7: Verify parser tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 8: Commit parser support**

```powershell
git add src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse glass absorption settings"
```

---

### Task 3: Compile New Fields Into Render Data

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add compiler tests**

In `tests/render_scene_tests.cpp`, update `MakeBaseScene()`:

```cpp
scene.render.radiance_clamp = 18.0f;
```

Then extend `scene_compiler_copies_render_settings`:

```cpp
YR_EXPECT_NEAR(compiled.radiance_clamp, 18.0, 1e-6);
```

Extend `scene_compiler_copies_dielectric_material_fields`:

```cpp
scene.materials[0].absorption_color = yr::Color3f{0.5f, 0.75f, 1.0f};
scene.materials[0].absorption_distance = 3.0f;
```

Add expectations:

```cpp
YR_EXPECT_NEAR(render_material.absorption_color.x, 0.5, 1e-6);
YR_EXPECT_NEAR(render_material.absorption_color.y, 0.75, 1e-6);
YR_EXPECT_NEAR(render_material.absorption_color.z, 1.0, 1e-6);
YR_EXPECT_NEAR(render_material.absorption_distance, 3.0, 1e-6);
```

Add a focused imported-material default test near other OBJ/glTF material tests:

```cpp
YR_TEST(scene_compiler_keeps_imported_material_absorption_neutral) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& material = result.scene.value().materials[0];
    YR_EXPECT_NEAR(material.absorption_color.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_color.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
}
```

- [ ] **Step 2: Verify compiler tests fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: compiler tests fail because `CompileScene()` is not copying all new fields yet.

- [ ] **Step 3: Copy render settings**

In `src/render/scene_compiler.cpp`, inside the render settings copy block in `CompileScene()`, add:

```cpp
compiled.radiance_clamp = scene.render.radiance_clamp;
```

Keep it near existing assignments for `threads` and `light_samples`.

- [ ] **Step 4: Copy material settings**

In `BuildMaterialMap()`, after `render_material.thin = material.thin;`, add:

```cpp
render_material.absorption_color = material.absorption_color;
render_material.absorption_distance = material.absorption_distance;
```

Do not add absorption import logic to `CompileImportedMaterials()` in this slice; the default `RenderMaterial` values are the intended imported-material behavior.

- [ ] **Step 5: Verify compiler tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 6: Commit compiler support**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile glass quality settings"
```

---

### Task 4: Apply Beer-Lambert Absorption In CPU Paths

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add absorbing slab scene helper**

In `tests/cpu_path_tracer_tests.cpp`, add this helper near `MakeGlassPanelScene()`:

```cpp
yr::RenderScene MakeAbsorbingGlassSlabScene(bool thin = false) {
    yr::RenderScene scene = MakeBaseScene(1, 1);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 32;
    scene.max_depth = 4;
    scene.seed = 5;
    scene.threads = 1;
    scene.area_lights.clear();
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.environment.strength = 1.0f;
    scene.materials[0].type = yr::MaterialKind::Dielectric;
    scene.materials[0].albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0].roughness = 0.0f;
    scene.materials[0].ior = 1.5f;
    scene.materials[0].thin = thin;
    scene.materials[0].absorption_color = yr::Color3f{0.25f, 0.70f, 1.0f};
    scene.materials[0].absorption_distance = 1.0f;
    scene.triangles = {
        yr::RenderTriangle{
            yr::Point3f{-2.0f, -2.0f, 0.0f},
            yr::Point3f{2.0f, -2.0f, 0.0f},
            yr::Point3f{0.0f, 2.0f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        },
        yr::RenderTriangle{
            yr::Point3f{-2.0f, -2.0f, -1.0f},
            yr::Point3f{0.0f, 2.0f, -1.0f},
            yr::Point3f{2.0f, -2.0f, -1.0f},
            yr::Vec3f{0.0f, 0.0f, -1.0f},
            0
        }
    };
    RebuildBvh(scene);
    return scene;
}
```

- [ ] **Step 2: Add failing path absorption tests**

Add these tests after existing glass tests:

```cpp
YR_TEST(cpu_path_tracer_absorbing_glass_tints_transmitted_environment) {
    yr::RenderScene neutral = MakeAbsorbingGlassSlabScene(false);
    neutral.materials[0].absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult neutral_result = yr::RenderCpuPathTrace(neutral);
    const yr::CpuPathTraceResult tinted_result = yr::RenderCpuPathTrace(MakeAbsorbingGlassSlabScene(false));
    const yr::Color3f neutral_pixel = neutral_result.film.LinearPixel(0, 0);
    const yr::Color3f tinted_pixel = tinted_result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(tinted_pixel.x < neutral_pixel.x * 0.8f);
    YR_EXPECT_TRUE(tinted_pixel.y < neutral_pixel.y);
    YR_EXPECT_TRUE(tinted_pixel.z > tinted_pixel.x * 1.5f);
    YR_EXPECT_TRUE(tinted_pixel.z > tinted_pixel.y);
}

YR_TEST(cpu_path_tracer_thin_glass_ignores_thickness_absorption) {
    yr::RenderScene thin_absorbing = MakeAbsorbingGlassSlabScene(true);
    yr::RenderScene thin_neutral = thin_absorbing;
    thin_neutral.materials[0].absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult absorbing_result = yr::RenderCpuPathTrace(thin_absorbing);
    const yr::CpuPathTraceResult neutral_result = yr::RenderCpuPathTrace(thin_neutral);
    const yr::Color3f absorbing_pixel = absorbing_result.film.LinearPixel(0, 0);
    const yr::Color3f neutral_pixel = neutral_result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(absorbing_pixel.x, neutral_pixel.x, 1e-5);
    YR_EXPECT_NEAR(absorbing_pixel.y, neutral_pixel.y, 1e-5);
    YR_EXPECT_NEAR(absorbing_pixel.z, neutral_pixel.z, 1e-5);
}
```

- [ ] **Step 3: Verify absorption tests fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: the new absorbing glass test fails because transmitted paths are not attenuated by distance.

- [ ] **Step 4: Add medium helper types and functions**

In `src/backends/cpu/cpu_path_tracer.cpp`, inside the anonymous namespace near `PreviousBounce`, add:

```cpp
constexpr float AbsorptionEpsilon = 1.0e-6f;

struct PathMediumState {
    bool active = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

float SafeAbsorptionChannel(float value) {
    return std::clamp(value, AbsorptionEpsilon, 1.0f);
}

Color3f BeerLambertTransmittance(Color3f absorption_color, float absorption_distance, float distance) {
    if (distance <= 0.0f || absorption_distance <= 0.0f) {
        return Color3f{1.0f, 1.0f, 1.0f};
    }
    const float inverse_distance = 1.0f / absorption_distance;
    return Color3f{
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.x)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.y)) * distance * inverse_distance),
        std::exp(std::log(SafeAbsorptionChannel(absorption_color.z)) * distance * inverse_distance)
    };
}

void ApplyMediumAttenuation(Color3f& throughput, const PathMediumState& medium, float distance) {
    if (!medium.active) {
        return;
    }
    throughput = Multiply(
        throughput,
        BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, distance)
    );
}

bool IsThickDielectricTransmission(const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    return material.type == MaterialKind::Dielectric &&
           !material.thin &&
           Dot(wi, normal) < 0.0f;
}

void UpdateMediumStateAfterBsdf(PathMediumState& medium, const RenderMaterial& material, Vec3f normal, Vec3f wi) {
    if (!IsThickDielectricTransmission(material, normal, wi)) {
        return;
    }
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
}
```

If `std::clamp`, `std::exp`, or `std::log` are not already available in the file, ensure the existing includes contain `<algorithm>` and `<cmath>`.

- [ ] **Step 5: Apply attenuation and state updates in `TracePath()`**

In `TracePath()`, initialize medium state after `PreviousBounce previous_bounce;`:

```cpp
PathMediumState medium;
```

After computing `hit` and before material validity checks or emission handling, add:

```cpp
ApplyMediumAttenuation(throughput, medium, hit.t);
if (IsNearBlack(throughput)) {
    break;
}
```

After a valid `bsdf_sample` is produced and before assigning the next ray, add:

```cpp
UpdateMediumStateAfterBsdf(medium, material, normal, bsdf_sample.wi);
```

Keep the previous-bounce MIS state unchanged. Reflection events must not toggle the medium because `Dot(bsdf_sample.wi, normal) >= 0`.

- [ ] **Step 6: Verify absorption tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all `yaoray_tests` pass. If the slab test is unstable because a low sample count misses enough transmission events, increase only that helper's `scene.spp` to `64` and keep `threads = 1`.

- [ ] **Step 7: Commit CPU absorption**

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add dielectric absorption paths"
```

---

### Task 5: Add Optional Radiance Clamp

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add radiance clamp tests**

In `tests/cpu_path_tracer_tests.cpp`, add these tests near the emissive surface tests:

```cpp
YR_TEST(cpu_path_tracer_radiance_clamp_limits_sample_max_component) {
    yr::RenderScene scene = MakeEmissiveTriangleScene();
    scene.radiance_clamp = 10.0f;
    scene.materials[0].emission = yr::Color3f{100.0f, 50.0f, 25.0f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 10.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 5.0, 1e-5);
    YR_EXPECT_NEAR(center.z, 2.5, 1e-5);
}

YR_TEST(cpu_path_tracer_radiance_clamp_is_disabled_by_default) {
    yr::RenderScene scene = MakeEmissiveTriangleScene();
    scene.materials[0].emission = yr::Color3f{100.0f, 50.0f, 25.0f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 100.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 50.0, 1e-5);
    YR_EXPECT_NEAR(center.z, 25.0, 1e-5);
}
```

- [ ] **Step 2: Verify clamp tests fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: the enabled clamp test fails because sample radiance is not clamped.

- [ ] **Step 3: Add clamp helper**

In `src/backends/cpu/cpu_path_tracer.cpp`, inside the anonymous namespace near other color helpers, add:

```cpp
Color3f ClampMaxComponent(Color3f value, float limit) {
    if (limit <= 0.0f) {
        return value;
    }
    const float max_component = MaxComponent(value);
    if (max_component <= limit || max_component <= 0.0f) {
        return value;
    }
    return value * (limit / max_component);
}
```

- [ ] **Step 4: Apply clamp before film accumulation**

In `RenderCpuPathTrace()`, replace:

```cpp
result.film.AddSample(x, y, TracePath(scene, ray, sampler, stats));
```

with:

```cpp
Color3f sample_radiance = TracePath(scene, ray, sampler, stats);
sample_radiance = ClampMaxComponent(sample_radiance, scene.radiance_clamp);
result.film.AddSample(x, y, sample_radiance);
```

- [ ] **Step 5: Verify clamp tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: all `yaoray_tests` pass.

- [ ] **Step 6: Commit radiance clamp**

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add CPU radiance clamp"
```

---

### Task 6: Update Glass Showcase And CLI Visual Sanity

**Files:**
- Modify: `scenes/examples/glass_showcase.toml`
- Create or modify: `tests/check_glass_showcase_visual.ps1`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update showcase render settings**

In `scenes/examples/glass_showcase.toml`, keep the current CPU path setup and set:

```toml
[render]
backend = "cpu"
integrator = "path"
sampler = "stratified"
width = 256
height = 256
spp = 48
max_depth = 8
seed = 37
threads = 0
light_samples = 4
radiance_clamp = 12.0
```

The exact ordering may follow the existing file; the important addition is `radiance_clamp = 12.0` and a modest `spp` increase that keeps CLI tests quick.

- [ ] **Step 2: Author visibly distinct glass materials**

Update or add these material entries in `glass_showcase.toml`:

```toml
[[materials]]
name = "clear_glass"
type = "glass"
ior = 1.5
albedo = [1.0, 1.0, 1.0]
absorption_color = [1.0, 1.0, 1.0]
absorption_distance = 1.0

[[materials]]
name = "blue_absorbing_glass"
type = "glass"
ior = 1.5
albedo = [1.0, 1.0, 1.0]
absorption_color = [0.45, 0.72, 1.0]
absorption_distance = 1.0

[[materials]]
name = "amber_rough_glass"
type = "rough_glass"
ior = 1.5
roughness = 0.32
albedo = [1.0, 0.96, 0.9]
absorption_color = [1.0, 0.62, 0.28]
absorption_distance = 1.0

[[materials]]
name = "thin_glass"
type = "thin_glass"
ior = 1.45
albedo = [0.9, 1.0, 0.95]
absorption_color = [0.2, 0.8, 1.0]
absorption_distance = 0.5
```

Bind one sphere to `blue_absorbing_glass` and one sphere to `amber_rough_glass`. Keep a thin pane in front or between backdrop cards to demonstrate that thin glass does not gain thickness darkening.

- [ ] **Step 3: Keep high-contrast backdrop and stable lighting**

Ensure the scene includes a dark backdrop, colored cards, and a large area light:

```toml
[environment]
type = "constant"
radiance = [0.015, 0.018, 0.022]
strength = 1.0

[[lights]]
type = "area"
position = [0.0, 3.0, 1.8]
size = [4.5, 3.5]
radiance = [2.0, 2.0, 2.0]
```

Do not reintroduce `assets/env/tiny_studio.hdr` as the glass showcase environment; it is a 2x2 test HDR and produces misleading fireflies.

- [ ] **Step 4: Create or update the visual sanity script**

Create `tests/check_glass_showcase_visual.ps1` with:

```powershell
param(
    [Parameter(Mandatory = $true)]
    [string]$YaoRayExe,

    [Parameter(Mandatory = $true)]
    [string]$ScenePath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Remove-Item -Force -ErrorAction SilentlyContinue $OutputPath

$renderOutput = & $YaoRayExe render $ScenePath --backend cpu 2>&1 | Out-String
Write-Output $renderOutput
if ($LASTEXITCODE -ne 0) {
    exit 1
}
if ($renderOutput -notmatch "Integrator: path") {
    Write-Error "glass showcase did not use the path integrator"
    exit 1
}
if ($renderOutput -notmatch "Rendered image:") {
    Write-Error "glass showcase did not report a rendered image"
    exit 1
}
if (-not (Test-Path $OutputPath)) {
    Write-Error "glass showcase output was not written"
    exit 1
}

[byte[]]$bytes = [System.IO.File]::ReadAllBytes($OutputPath)
[byte[]]$expected = 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
if ($bytes.Length -lt 8) {
    Write-Error "glass showcase output is too short to be a PNG"
    exit 1
}
for ($i = 0; $i -lt 8; $i++) {
    if ($bytes[$i] -ne $expected[$i]) {
        Write-Error "glass showcase output does not have a PNG header"
        exit 1
    }
}

Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($OutputPath)
try {
    $nearWhite = 0
    $dark = 0
    $total = $bitmap.Width * $bitmap.Height
    for ($y = 0; $y -lt $bitmap.Height; $y++) {
        for ($x = 0; $x -lt $bitmap.Width; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            if ([Math]::Min($pixel.R, [Math]::Min($pixel.G, $pixel.B)) -ge 245) {
                $nearWhite++
            }
            if ([Math]::Max($pixel.R, [Math]::Max($pixel.G, $pixel.B)) -le 50) {
                $dark++
            }
        }
    }
    $nearWhiteFraction = $nearWhite / [double]$total
    $darkFraction = $dark / [double]$total
    Write-Output ("Glass visual sanity: near_white={0:N4} dark={1:N4}" -f $nearWhiteFraction, $darkFraction)
    if ($nearWhiteFraction -gt 0.70) {
        Write-Error "glass showcase is overexposed: too many near-white pixels"
        exit 1
    }
    if ($darkFraction -lt 0.03) {
        Write-Error "glass showcase has too little dark contrast for refraction to read"
        exit 1
    }
}
finally {
    $bitmap.Dispose()
}
```

- [ ] **Step 5: Add the CTest visual sanity target**

In `CMakeLists.txt`, after `yaoray_cli_render_glass_showcase`, ensure this test exists:

```cmake
add_test(NAME yaoray_cli_render_glass_showcase_visual_sanity
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_glass_showcase_visual.ps1"
        "$<TARGET_FILE:yaoray>"
        "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/glass_showcase.toml"
        "${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/glass_showcase.png"
)
```

- [ ] **Step 6: Verify the showcase renders and passes sanity**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R "yaoray_cli_render_glass_showcase|yaoray_cli_render_glass_showcase_visual_sanity"
```

Expected: both showcase CLI tests pass and `scenes/examples/out/glass_showcase.png` is written.

- [ ] **Step 7: Manually inspect the rendered PNG**

Open or view:

```powershell
Get-Item scenes\examples\out\glass_showcase.png
```

Expected: the image exists, the glass spheres have visible blue/amber tinting, and the thin pane remains mostly neutral. If the image is too noisy, keep `radiance_clamp = 12.0` and raise only showcase `spp` to `64` if the test runtime stays acceptable.

- [ ] **Step 8: Commit showcase updates**

```powershell
git add CMakeLists.txt scenes/examples/glass_showcase.toml tests/check_glass_showcase_visual.ps1
git commit -m "test: update glass quality showcase"
```

---

### Task 7: Documentation And Final Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-21-yaoray-glass-quality-pack-v1-design.md`

- [ ] **Step 1: Update README status**

In `README.md`, update the planned work line so Beer-Lambert absorption is no longer listed as future after this slice. Use this wording:

```markdown
Final path tracing quality, spectral rendering, nested media, full glTF PBR material import, advanced texture/material import, advanced BVH split methods, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

Update the materials paragraph to include:

```markdown
Dielectric materials support smooth, rough, and thin variants; thick dielectric variants can optionally use Beer-Lambert absorption through `absorption_color` and `absorption_distance`.
```

Update the path integrator limitations paragraph to include:

```markdown
Dielectric materials do not yet include nested medium stacks, caustic-specific sampling, glTF glass extension import, or CUDA parity. `render.radiance_clamp` is an optional biased preview control for firefly reduction and is disabled by default.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, update the BSDF/material paragraph to say:

```markdown
Material scattering for `path` is routed through a small render-level BSDF API that currently implements Lambertian diffuse, perfect mirror, GGX-style metal, simple plastic behavior, and dielectric glass reflection/transmission with smooth, rough, and thin variants through data-driven `MaterialKind` dispatch. Thick dielectric paths can opt into Beer-Lambert absorption through render material fields; this is a single active medium approximation, not a nested medium stack.
```

Update the future-work paragraph so Beer-Lambert absorption is removed from remaining dielectric work, while nested media and caustics remain.

- [ ] **Step 3: Append implementation status to the design spec**

At the end of `docs/superpowers/specs/2026-05-21-yaoray-glass-quality-pack-v1-design.md`, append:

```markdown
## Implementation Status

Implemented in Glass Quality Pack v1:

- Scene parser support for `materials.absorption_color`, `materials.absorption_distance`, and `render.radiance_clamp`.
- Semantic and render data fields for dielectric absorption and radiance clamp.
- Scene compiler copying for authored absorption and clamp settings.
- CPU path tracer Beer-Lambert attenuation for a single active thick dielectric medium.
- CPU path tracer sample radiance clamp, disabled by default.
- Updated glass showcase scene and visual sanity smoke test.

Remaining limitations:

- No nested medium stack or overlapping medium correctness.
- No transparent shadow rays or caustic-specific transport.
- No denoiser or advanced sampler variance reduction.
- No glTF volume/transmission import.
- No CUDA parity.
```

- [ ] **Step 4: Run targeted documentation search**

Run:

```powershell
rg -n "Beer-Lambert|absorption_color|absorption_distance|radiance_clamp|nested media|CUDA parity|caustic" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-21-yaoray-glass-quality-pack-v1-design.md
```

Expected: the three docs mention the implemented settings and still describe nested media, caustics, and CUDA parity as future work.

- [ ] **Step 5: Run full verification**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
.\build\yaoray.exe render .\scenes\examples\glass_showcase.toml --backend cpu
```

Expected:

- Build succeeds.
- All CTest tests pass.
- Manual render exits `0`.
- CLI output includes `Integrator: path`, `Rendered image: scenes/examples/out/glass_showcase.png`, `Samples/sec:`, and `Rays/sec:`.

- [ ] **Step 6: Inspect final git diff**

Run:

```powershell
git status --short --branch
git diff --stat
```

Expected: only intentional Glass Quality Pack v1 files are modified or added. Do not revert unrelated user files; leave them unstaged if they are outside this plan.

- [ ] **Step 7: Commit docs**

```powershell
git add README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-21-yaoray-glass-quality-pack-v1-design.md
git commit -m "docs: document glass quality pack"
```

---

## Final Handoff

- [ ] **Step 1: Report implemented behavior**

Summarize:

- Absorption parser/compiler fields.
- Single active dielectric medium attenuation in CPU path tracer.
- Optional radiance clamp.
- Updated glass showcase.
- Tests and render commands run.

- [ ] **Step 2: Ask whether to merge or push**

If executing in a feature branch/worktree, use the finishing branch workflow. If executing inline on `main`, ask before pushing unless the user has explicitly requested `merge and push`.
