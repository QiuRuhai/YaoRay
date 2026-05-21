# YaoRay Dielectric Material Pack v3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a dielectric/glass material pack with smooth glass, GGX rough glass, thin glass, tests, showcase rendering, and documentation.

**Architecture:** Keep material authoring in the semantic scene layer and compile all aliases to one render-level `MaterialKind::Dielectric`. Extend the existing render-level BSDF API rather than introducing a new integrator interface. The CPU path tracer should continue to orchestrate transport through `EvaluateBsdf()`, `PdfBsdf()`, `SampleBsdf()`, and `IsDeltaBsdf()`, with one local ray-bias adjustment for transmitted paths.

**Tech Stack:** C++20, CMake, toml++, custom `YR_TEST` harness, CPU path tracer, existing OBJ asset pipeline, PNG output.

---

## Scope Check

This plan implements one subsystem: dielectric material support in the current CPU path tracer. It does not implement Beer-Lambert absorption, a medium stack, caustic algorithms, VNDF sampling, CUDA parity, glTF glass extension import, or analytic sphere primitives.

## File Structure

- Modify `include/yaoray/scene/scene.hpp`: add `MaterialKind::Dielectric`, semantic `ior`, and semantic `thin`.
- Modify `src/scene/scene.cpp`: add canonical dielectric material name and parser aliases.
- Modify `src/scene/scene_parser.cpp`: parse `ior` and `thin`, validate IOR, and apply alias defaults.
- Modify `include/yaoray/render/render_scene.hpp`: append render material `ior` and `thin`.
- Modify `src/render/scene_compiler.cpp`: copy semantic dielectric fields into `RenderMaterial`.
- Modify `src/render/bsdf.cpp`: add dielectric Fresnel/refraction helpers, smooth dielectric, rough dielectric, and thin glass sampling/evaluation/PDF logic.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: bias next ray origin along the sampled direction side so transmitted rays do not self-intersect from the wrong side.
- Modify `tests/scene_tests.cpp`: material enum, parser, alias, IOR, and thin bool tests.
- Modify `tests/render_scene_tests.cpp`: compiler/default propagation tests.
- Modify `tests/bsdf_tests.cpp`: smooth, rough, and thin dielectric BSDF tests.
- Modify `tests/cpu_path_tracer_tests.cpp`: glass path behavior and transmission ray-bias tests.
- Create `scenes/examples/assets/glass_sphere.obj`: deterministic mesh sphere with vertex normals.
- Create `scenes/examples/glass_showcase.toml`: manual showcase scene.
- Modify `CMakeLists.txt`: add CLI smoke test for the showcase.
- Modify `README.md` and `docs/architecture/overview.md`: document dielectric material support and remaining limits.
- Modify `docs/superpowers/specs/2026-05-21-yaoray-dielectric-material-pack-v3-design.md`: append implementation status.

---

## Task 1: Scene Schema And Material Parser

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene.cpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add failing schema and parser tests**

In `tests/scene_tests.cpp`, update existing enum tests:

```cpp
YR_TEST(scene_enum_names_are_stable) {
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cpu), std::string_view{"cpu"});
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cuda), std::string_view{"cuda"});
    YR_EXPECT_EQ(yr::RenderIntegratorName(yr::RenderIntegratorKind::DebugDirect), std::string_view{"debug_direct"});
    YR_EXPECT_EQ(yr::RenderIntegratorName(yr::RenderIntegratorKind::Path), std::string_view{"path"});
    YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::Independent), std::string_view{"independent"});
    YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::Stratified), std::string_view{"stratified"});
    YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Diffuse), std::string_view{"diffuse"});
    YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Mirror), std::string_view{"mirror"});
    YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Metal), std::string_view{"metal"});
    YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Plastic), std::string_view{"plastic"});
    YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Dielectric), std::string_view{"dielectric"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::None), std::string_view{"none"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Reinhard), std::string_view{"reinhard"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Aces), std::string_view{"aces"});
    YR_EXPECT_EQ(yr::CameraKindName(yr::CameraKind::Perspective), std::string_view{"perspective"});
    YR_EXPECT_EQ(yr::LightKindName(yr::LightKind::Area), std::string_view{"area"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::None), std::string_view{"none"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Constant), std::string_view{"constant"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Hdri), std::string_view{"hdri"});
}

YR_TEST(scene_enum_parsers_accept_stable_names) {
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cpu").value(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cuda").value(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(yr::ParseRenderIntegratorName("debug_direct").value(), yr::RenderIntegratorKind::DebugDirect);
    YR_EXPECT_EQ(yr::ParseRenderIntegratorName("path").value(), yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(yr::ParseRenderSamplerName("independent").value(), yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(yr::ParseRenderSamplerName("stratified").value(), yr::RenderSamplerKind::Stratified);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("diffuse").value(), yr::MaterialKind::Diffuse);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("mirror").value(), yr::MaterialKind::Mirror);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("metal").value(), yr::MaterialKind::Metal);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("plastic").value(), yr::MaterialKind::Plastic);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("dielectric").value(), yr::MaterialKind::Dielectric);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("glass").value(), yr::MaterialKind::Dielectric);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("rough_glass").value(), yr::MaterialKind::Dielectric);
    YR_EXPECT_EQ(yr::ParseMaterialKindName("thin_glass").value(), yr::MaterialKind::Dielectric);
    YR_EXPECT_EQ(yr::ParseToneMapperName("none").value(), yr::ToneMapperKind::None);
    YR_EXPECT_EQ(yr::ParseToneMapperName("reinhard").value(), yr::ToneMapperKind::Reinhard);
    YR_EXPECT_EQ(yr::ParseToneMapperName("aces").value(), yr::ToneMapperKind::Aces);
    YR_EXPECT_EQ(yr::ParseCameraKindName("perspective").value(), yr::CameraKind::Perspective);
    YR_EXPECT_EQ(yr::ParseLightKindName("area").value(), yr::LightKind::Area);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("none").value(), yr::EnvironmentKind::None);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("constant").value(), yr::EnvironmentKind::Constant);
    YR_EXPECT_EQ(yr::ParseEnvironmentKindName("hdri").value(), yr::EnvironmentKind::Hdri);
}

YR_TEST(scene_enum_parsers_reject_unknown_names) {
    YR_EXPECT_TRUE(!yr::ParseRenderBackendName("metal").has_value());
    YR_EXPECT_TRUE(!yr::ParseRenderIntegratorName("bidirectional").has_value());
    YR_EXPECT_TRUE(!yr::ParseRenderSamplerName("sobol").has_value());
    YR_EXPECT_TRUE(!yr::ParseMaterialKindName("velvet").has_value());
    YR_EXPECT_TRUE(!yr::ParseMaterialKindName("rough_metal").has_value());
    YR_EXPECT_TRUE(!yr::ParseToneMapperName("filmic").has_value());
    YR_EXPECT_TRUE(!yr::ParseCameraKindName("orthographic").has_value());
    YR_EXPECT_TRUE(!yr::ParseLightKindName("point").has_value());
    YR_EXPECT_TRUE(!yr::ParseEnvironmentKindName("sky").has_value());
}
```

Add these tests near the other material parser tests:

```cpp
YR_TEST(scene_defaults_include_dielectric_material_fields) {
    const yr::MaterialDescription material;

    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
}

YR_TEST(scene_parser_loads_dielectric_material_fields) {
    const std::filesystem::path path = WriteTempScene(
        "dielectric_material.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "clear_glass"
type = "dielectric"
albedo = [0.95, 0.98, 1.0]
ior = 1.45
roughness = 0.1
thin = true
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.albedo.x, 0.95, 1e-6);
    YR_EXPECT_NEAR(material.albedo.y, 0.98, 1e-6);
    YR_EXPECT_NEAR(material.albedo.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.ior, 1.45, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 0.1, 1e-6);
    YR_EXPECT_TRUE(material.thin);
}

YR_TEST(scene_parser_loads_glass_alias_defaults) {
    const std::filesystem::path path = WriteTempScene(
        "glass_alias.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "glass"
type = "glass"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
}

YR_TEST(scene_parser_loads_rough_glass_alias_default_roughness) {
    const std::filesystem::path path = WriteTempScene(
        "rough_glass_alias.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "rough_glass"
type = "rough_glass"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 0.25, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
}

YR_TEST(scene_parser_loads_thin_glass_alias_default_thin) {
    const std::filesystem::path path = WriteTempScene(
        "thin_glass_alias.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "thin_glass"
type = "thin_glass"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
    YR_EXPECT_TRUE(material.thin);
}

YR_TEST(scene_parser_rejects_non_numeric_material_ior) {
    const std::filesystem::path path = WriteTempScene(
        "non_numeric_material_ior.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_ior"
type = "dielectric"
ior = "dense"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.ior", "must be a finite float in [1, 3]"));
}

YR_TEST(scene_parser_rejects_out_of_range_material_ior) {
    const std::filesystem::path path = WriteTempScene(
        "out_of_range_material_ior.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_ior"
type = "dielectric"
ior = 0.8
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.ior", "must be in [1, 3]"));
}

YR_TEST(scene_parser_rejects_non_bool_material_thin) {
    const std::filesystem::path path = WriteTempScene(
        "non_bool_material_thin.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_thin"
type = "dielectric"
thin = "yes"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.thin", "must be a bool"));
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `MaterialKind::Dielectric`, `MaterialDescription::ior`, and `MaterialDescription::thin` do not exist.

- [ ] **Step 3: Add semantic material fields**

In `include/yaoray/scene/scene.hpp`, change the enum and material struct:

```cpp
enum class MaterialKind {
    Diffuse,
    Mirror,
    Metal,
    Plastic,
    Dielectric,
};

struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    float ior = 1.5f;
    bool thin = false;
};
```

- [ ] **Step 4: Add canonical material name and aliases**

In `src/scene/scene.cpp`, update material name helpers:

```cpp
std::string_view MaterialKindName(MaterialKind kind) {
    switch (kind) {
        case MaterialKind::Diffuse:
            return "diffuse";
        case MaterialKind::Mirror:
            return "mirror";
        case MaterialKind::Metal:
            return "metal";
        case MaterialKind::Plastic:
            return "plastic";
        case MaterialKind::Dielectric:
            return "dielectric";
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
    if (name == "metal") {
        return MaterialKind::Metal;
    }
    if (name == "plastic") {
        return MaterialKind::Plastic;
    }
    if (name == "dielectric" || name == "glass" || name == "rough_glass" || name == "thin_glass") {
        return MaterialKind::Dielectric;
    }
    return std::nullopt;
}
```

- [ ] **Step 5: Add bool and IOR readers**

In `src/scene/scene_parser.cpp`, add this helper after `ReadUnitFloat()`:

```cpp
std::optional<float> ReadIorFloat(
    const toml::table& table,
    std::string_view key,
    const std::filesystem::path& file,
    std::string field,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    const std::optional<float> value = ReadNodeFloat(*node);
    if (!value) {
        diagnostics.push_back(Error(file, field, "must be a finite float in [1, 3]"));
        return std::nullopt;
    }
    if (*value < 1.0f || *value > 3.0f) {
        diagnostics.push_back(Error(file, std::move(field), "must be in [1, 3]"));
        return std::nullopt;
    }
    return value;
}

std::optional<bool> ReadBool(
    const toml::table& table,
    std::string_view key,
    const std::filesystem::path& file,
    std::string field,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::node* node = table.get(key);
    if (node == nullptr) {
        return std::nullopt;
    }
    if (const auto value = node->value<bool>()) {
        return *value;
    }
    diagnostics.push_back(Error(file, std::move(field), "must be a bool"));
    return std::nullopt;
}
```

- [ ] **Step 6: Parse dielectric material fields and alias defaults**

In `ParseMaterials()`, update the allowed field list:

```cpp
CheckUnknownFields(
    *table,
    "materials",
    {"name", "type", "albedo", "emission", "roughness", "specular", "ior", "thin"},
    file,
    diagnostics
);
```

Replace the material type and scalar parsing block with:

```cpp
std::string material_type_name;
if (const auto type = ReadString(*table, "type", file, "materials.type", diagnostics)) {
    material_type_name = *type;
    if (const auto parsed = ParseMaterialKindName(*type)) {
        material.type = *parsed;
    } else {
        diagnostics.push_back(Error(file, "materials.type", "unknown material type"));
    }
}
if (const auto albedo = ReadVec3(*table, "albedo", file, "materials.albedo", diagnostics)) {
    material.albedo = *albedo;
}
if (const auto emission = ReadVec3(*table, "emission", file, "materials.emission", diagnostics)) {
    material.emission = *emission;
}
const bool roughness_authored = table->contains("roughness");
const bool thin_authored = table->contains("thin");
if (const auto roughness = ReadUnitFloat(*table, "roughness", file, "materials.roughness", diagnostics)) {
    material.roughness = *roughness;
}
if (const auto specular = ReadUnitFloat(*table, "specular", file, "materials.specular", diagnostics)) {
    material.specular = *specular;
}
if (const auto ior = ReadIorFloat(*table, "ior", file, "materials.ior", diagnostics)) {
    material.ior = *ior;
}
if (const auto thin = ReadBool(*table, "thin", file, "materials.thin", diagnostics)) {
    material.thin = *thin;
}
if (!roughness_authored && material.type == MaterialKind::Plastic) {
    material.roughness = 0.25f;
}
if (material.type == MaterialKind::Dielectric) {
    if (!roughness_authored && material_type_name == "rough_glass") {
        material.roughness = 0.25f;
    }
    if (!thin_authored && material_type_name == "thin_glass") {
        material.thin = true;
    }
}
```

- [ ] **Step 7: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 8: Commit**

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene.cpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse dielectric material settings"
```

---

## Task 2: Render Material Compilation

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing compiler tests**

In `tests/render_scene_tests.cpp`, update `render_scene_defaults_are_backend_friendly` to include the new defaults:

```cpp
YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
YR_EXPECT_TRUE(!material.thin);
```

Add these tests near the material compiler tests:

```cpp
YR_TEST(scene_compiler_copies_dielectric_material_fields) {
    yr::SceneDescription scene = MakeSceneWithBuiltinTriangle(
        yr::MaterialDescription{
            "clear_glass",
            yr::MaterialKind::Dielectric,
            yr::Color3f{0.95f, 0.98f, 1.0f},
            yr::Color3f{},
            0.15f,
            0.04f,
            1.45f,
            true
        }
    );

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.albedo.x, 0.95, 1e-6);
    YR_EXPECT_NEAR(material.albedo.y, 0.98, 1e-6);
    YR_EXPECT_NEAR(material.albedo.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 0.15, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
    YR_EXPECT_NEAR(material.ior, 1.45, 1e-6);
    YR_EXPECT_TRUE(material.thin);
}

YR_TEST(scene_compiler_preserves_default_dielectric_fields) {
    yr::SceneDescription scene = MakeSceneWithBuiltinTriangle(
        yr::MaterialDescription{
            "glass",
            yr::MaterialKind::Dielectric
        }
    );

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Dielectric);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `RenderMaterial::ior` and `RenderMaterial::thin` do not exist.

- [ ] **Step 3: Append render material fields**

In `include/yaoray/render/render_scene.hpp`, append fields after `albedo_texture` to preserve current aggregate initializers:

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
};
```

- [ ] **Step 4: Copy fields in named material compilation**

In `src/render/scene_compiler.cpp`, replace the `compiled.materials.push_back(RenderMaterial{...})` block in `BuildMaterialMap()` with explicit assignment:

```cpp
RenderMaterial render_material;
render_material.type = material.type;
render_material.albedo = material.albedo;
render_material.emission = material.emission;
render_material.roughness = material.roughness;
render_material.specular = material.specular;
render_material.ior = material.ior;
render_material.thin = material.thin;
compiled.materials.push_back(render_material);
```

- [ ] **Step 5: Preserve imported material defaults**

In `CompileImportedMaterials()`, leave imported OBJ/glTF materials with default dielectric fields by not setting `ior` or `thin`. The function should still explicitly assign existing imported fields:

```cpp
RenderMaterial render_material;
render_material.type = material.type;
render_material.albedo = material.diffuse;
render_material.emission = material.emission;
render_material.roughness = material.roughness;
render_material.specular = material.specular;
```

- [ ] **Step 6: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 7: Commit**

```powershell
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile dielectric material fields"
```

---

## Task 3: Smooth Dielectric BSDF

**Files:**
- Modify: `src/render/bsdf.cpp`
- Modify: `tests/bsdf_tests.cpp`

- [ ] **Step 1: Add failing smooth dielectric BSDF tests**

In `tests/bsdf_tests.cpp`, add helpers near the existing material factories:

```cpp
yr::RenderMaterial SmoothGlassMaterial() {
    yr::RenderMaterial material;
    material.type = yr::MaterialKind::Dielectric;
    material.albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    material.roughness = 0.0f;
    material.ior = 1.5f;
    material.thin = false;
    return material;
}

yr::RenderMaterial TintedSmoothGlassMaterial() {
    yr::RenderMaterial material = SmoothGlassMaterial();
    material.albedo = yr::Color3f{0.8f, 0.9f, 1.0f};
    return material;
}
```

Add these tests after `bsdf_mirror_is_delta_and_has_no_finite_brdf_pdf`:

```cpp
YR_TEST(bsdf_smooth_dielectric_refracts_at_normal_incidence) {
    const yr::RenderMaterial material = SmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.25f});

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal), 0.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_reflects_when_fresnel_sample_selects_reflection) {
    const yr::RenderMaterial material = TintedSmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.25f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.9, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 1.0, 1e-6);
}

YR_TEST(bsdf_smooth_dielectric_total_internal_reflection_reflects) {
    const yr::RenderMaterial material = SmoothGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, -0.4358899f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.9f, 0.25f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) < 0.0f);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: new smooth dielectric tests fail because `SampleBsdf()` does not handle `MaterialKind::Dielectric`.

- [ ] **Step 3: Add dielectric helper functions**

In `src/render/bsdf.cpp`, add these helpers in the anonymous namespace after `Reflect()`:

```cpp
float AbsDot(Vec3f a, Vec3f b) {
    return std::fabs(Dot(a, b));
}

float Clamp(float value, float low, float high) {
    return std::clamp(value, low, high);
}

float FresnelDielectric(float cos_theta_i, float eta_i, float eta_t) {
    cos_theta_i = Clamp(cos_theta_i, -1.0f, 1.0f);
    bool entering = cos_theta_i > 0.0f;
    if (!entering) {
        std::swap(eta_i, eta_t);
        cos_theta_i = std::fabs(cos_theta_i);
    }

    const float sin_theta_i = std::sqrt(std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    const float sin_theta_t = eta_i / eta_t * sin_theta_i;
    if (sin_theta_t >= 1.0f) {
        return 1.0f;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    const float r_parallel =
        ((eta_t * cos_theta_i) - (eta_i * cos_theta_t)) /
        ((eta_t * cos_theta_i) + (eta_i * cos_theta_t));
    const float r_perpendicular =
        ((eta_i * cos_theta_i) - (eta_t * cos_theta_t)) /
        ((eta_i * cos_theta_i) + (eta_t * cos_theta_t));
    return 0.5f * (r_parallel * r_parallel + r_perpendicular * r_perpendicular);
}

struct DielectricFrame {
    Vec3f normal;
    float eta_i = 1.0f;
    float eta_t = 1.0f;
    float eta = 1.0f;
    float cos_o = 0.0f;
};

DielectricFrame MakeDielectricFrame(Vec3f wo, Vec3f normal, float ior) {
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    return DielectricFrame{
        oriented_normal,
        eta_i,
        eta_t,
        eta_i / eta_t,
        std::max(0.0f, Dot(oriented_normal, wo))
    };
}

bool Refract(Vec3f wo, Vec3f normal, float eta, Vec3f& wi) {
    const float cos_theta_i = Dot(normal, wo);
    const float sin2_theta_i = std::max(0.0f, 1.0f - cos_theta_i * cos_theta_i);
    const float sin2_theta_t = eta * eta * sin2_theta_i;
    if (sin2_theta_t >= 1.0f) {
        return false;
    }

    const float cos_theta_t = std::sqrt(std::max(0.0f, 1.0f - sin2_theta_t));
    wi = Normalize(-wo * eta + normal * (eta * cos_theta_i - cos_theta_t));
    return true;
}
```

- [ ] **Step 4: Add smooth dielectric sampling branch**

In `SampleBsdf()`, add this branch before the final default return:

```cpp
case MaterialKind::Dielectric: {
    if (IsBlack(material.albedo)) {
        return BsdfSample{};
    }
    if (material.thin) {
        const float fresnel = FresnelDielectric(std::fabs(Dot(normal, wo)), 1.0f, std::max(1.0f, material.ior));
        if (sample.x < fresnel) {
            return BsdfSample{Reflect(-wo, Dot(normal, wo) >= 0.0f ? normal : -normal), material.albedo, 1.0f, true, true};
        }
        return BsdfSample{Normalize(-wo), material.albedo, 1.0f, true, true};
    }
    if (material.roughness <= DeltaRoughness) {
        const DielectricFrame frame = MakeDielectricFrame(wo, normal, material.ior);
        Vec3f refracted;
        const bool can_refract = Refract(wo, frame.normal, frame.eta, refracted);
        const float fresnel = can_refract ? FresnelDielectric(frame.cos_o, frame.eta_i, frame.eta_t) : 1.0f;
        if (!can_refract || sample.x < fresnel) {
            return BsdfSample{Reflect(-wo, frame.normal), material.albedo, 1.0f, true, true};
        }
        return BsdfSample{refracted, material.albedo, 1.0f, true, true};
    }
    return BsdfSample{};
}
```

- [ ] **Step 5: Add dielectric evaluate, pdf, and delta cases**

In `EvaluateBsdf()`, add:

```cpp
case MaterialKind::Dielectric:
    return Color3f{};
```

In `PdfBsdf()`, add:

```cpp
case MaterialKind::Dielectric:
    return 0.0f;
```

In `IsDeltaBsdf()`, add:

```cpp
case MaterialKind::Dielectric:
    return material.roughness <= DeltaRoughness;
```

- [ ] **Step 6: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 7: Commit**

```powershell
git add src/render/bsdf.cpp tests/bsdf_tests.cpp
git commit -m "feat: add smooth dielectric bsdf"
```

---

## Task 4: Rough Dielectric BSDF

**Files:**
- Modify: `src/render/bsdf.cpp`
- Modify: `tests/bsdf_tests.cpp`

- [ ] **Step 1: Add failing rough dielectric tests**

In `tests/bsdf_tests.cpp`, add:

```cpp
yr::RenderMaterial RoughGlassMaterial() {
    yr::RenderMaterial material = SmoothGlassMaterial();
    material.roughness = 0.35f;
    return material;
}
```

Add tests:

```cpp
YR_TEST(bsdf_rough_dielectric_has_finite_reflection_response) {
    const yr::RenderMaterial material = RoughGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
}

YR_TEST(bsdf_rough_dielectric_has_finite_transmission_response) {
    const yr::RenderMaterial material = RoughGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, -0.05f, -1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);

    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
}

YR_TEST(bsdf_rough_dielectric_samples_reflection_and_transmission) {
    const yr::RenderMaterial material = RoughGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.2f, 1.0f});

    const yr::BsdfSample reflection = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.35f});
    const yr::BsdfSample transmission = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.95f, 0.35f});

    YR_EXPECT_TRUE(reflection.valid);
    YR_EXPECT_TRUE(transmission.valid);
    YR_EXPECT_TRUE(!reflection.specular);
    YR_EXPECT_TRUE(!transmission.specular);
    YR_EXPECT_TRUE(yr::Dot(reflection.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(yr::Dot(transmission.wi, normal) < 0.0f);
    YR_EXPECT_TRUE(reflection.pdf > 0.0f);
    YR_EXPECT_TRUE(transmission.pdf > 0.0f);
    YR_EXPECT_TRUE(reflection.weight.x > 0.0f);
    YR_EXPECT_TRUE(transmission.weight.x > 0.0f);
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: rough dielectric tests fail because rough dielectric returns invalid or black.

- [ ] **Step 3: Add rough dielectric helpers**

In `src/render/bsdf.cpp`, add these helpers after `GgxReflectionPdf()`:

```cpp
bool SameHemisphere(Vec3f a, Vec3f b, Vec3f normal) {
    return Dot(a, normal) * Dot(b, normal) > 0.0f;
}

Color3f GgxDielectricReflection(Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!SameHemisphere(wo, wi, normal)) {
        return Color3f{};
    }
    const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return Color3f{};
    }
    const float cos_o = AbsDot(oriented_normal, wo);
    const float cos_i = AbsDot(oriented_normal, wi);
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float cos_oh = AbsDot(wo, half_vector);
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_oh <= 0.0f) {
        return Color3f{};
    }
    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const float f = FresnelDielectric(cos_oh, 1.0f, std::max(1.0f, ior));
    return albedo * (f * d * g / (4.0f * cos_o * cos_i));
}

float GgxDielectricReflectionPdf(float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!SameHemisphere(wo, wi, normal)) {
        return 0.0f;
    }
    const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return 0.0f;
    }
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float cos_oh = AbsDot(wo, half_vector);
    if (cos_h <= 0.0f || cos_oh <= 0.0f) {
        return 0.0f;
    }
    const float f = FresnelDielectric(cos_oh, 1.0f, std::max(1.0f, ior));
    return f * GgxDistribution(cos_h, RoughnessToAlpha(roughness)) * cos_h / (4.0f * cos_oh);
}

Vec3f TransmissionHalfVector(Vec3f wo, Vec3f wi, Vec3f normal, float ior) {
    const bool entering = Dot(normal, wo) >= 0.0f;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    Vec3f half_vector = Normalize(wo + wi * eta);
    if (Dot(half_vector, normal) < 0.0f) {
        half_vector = -half_vector;
    }
    return half_vector;
}
```

- [ ] **Step 4: Add rough transmission evaluation and PDF**

Add these helpers after `TransmissionHalfVector()`:

```cpp
Color3f GgxDielectricTransmission(Color3f albedo, float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (SameHemisphere(wo, wi, normal)) {
        return Color3f{};
    }
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    const Vec3f half_vector = TransmissionHalfVector(wo, wi, oriented_normal, ior);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return Color3f{};
    }

    const float cos_o = AbsDot(oriented_normal, wo);
    const float cos_i = AbsDot(oriented_normal, wi);
    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float wo_h = Dot(wo, half_vector);
    const float wi_h = Dot(wi, half_vector);
    const float sqrt_denom = wo_h + eta * wi_h;
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_h <= 0.0f || wo_h <= 0.0f || wi_h >= 0.0f || sqrt_denom == 0.0f) {
        return Color3f{};
    }

    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const float f = FresnelDielectric(std::fabs(wo_h), eta_i, eta_t);
    const float numerator = std::fabs(wi_h) * std::fabs(wo_h) * eta * eta;
    const float denominator = cos_o * cos_i * sqrt_denom * sqrt_denom;
    return albedo * ((1.0f - f) * d * g * numerator / denominator);
}

float GgxDielectricTransmissionPdf(float ior, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (SameHemisphere(wo, wi, normal)) {
        return 0.0f;
    }
    const bool entering = Dot(normal, wo) >= 0.0f;
    const Vec3f oriented_normal = entering ? normal : -normal;
    const float eta_i = entering ? 1.0f : std::max(1.0f, ior);
    const float eta_t = entering ? std::max(1.0f, ior) : 1.0f;
    const float eta = eta_t / eta_i;
    const Vec3f half_vector = TransmissionHalfVector(wo, wi, oriented_normal, ior);
    if (LengthSquared(half_vector) <= 0.0f || Dot(half_vector, oriented_normal) <= 0.0f) {
        return 0.0f;
    }

    const float cos_h = AbsDot(oriented_normal, half_vector);
    const float wo_h = Dot(wo, half_vector);
    const float wi_h = Dot(wi, half_vector);
    const float sqrt_denom = wo_h + eta * wi_h;
    if (cos_h <= 0.0f || wo_h <= 0.0f || wi_h >= 0.0f || sqrt_denom == 0.0f) {
        return 0.0f;
    }

    const float f = FresnelDielectric(std::fabs(wo_h), eta_i, eta_t);
    const float dwh_dwi = std::fabs((eta * eta * wi_h) / (sqrt_denom * sqrt_denom));
    return (1.0f - f) * GgxDistribution(cos_h, RoughnessToAlpha(roughness)) * cos_h * dwh_dwi;
}
```

- [ ] **Step 5: Add rough dielectric evaluate and PDF branches**

In the `MaterialKind::Dielectric` branch of `EvaluateBsdf()`, replace the black-only return with:

```cpp
case MaterialKind::Dielectric:
    if (material.thin || material.roughness <= DeltaRoughness) {
        return Color3f{};
    }
    return SameHemisphere(wo, wi, normal)
        ? GgxDielectricReflection(material.albedo, material.ior, material.roughness, wo, wi, normal)
        : GgxDielectricTransmission(material.albedo, material.ior, material.roughness, wo, wi, normal);
```

In the `MaterialKind::Dielectric` branch of `PdfBsdf()`, replace the zero-only return with:

```cpp
case MaterialKind::Dielectric:
    if (material.thin || material.roughness <= DeltaRoughness) {
        return 0.0f;
    }
    return SameHemisphere(wo, wi, normal)
        ? GgxDielectricReflectionPdf(material.ior, material.roughness, wo, wi, normal)
        : GgxDielectricTransmissionPdf(material.ior, material.roughness, wo, wi, normal);
```

- [ ] **Step 6: Add rough dielectric sampling**

In the `MaterialKind::Dielectric` branch of `SampleBsdf()`, replace the final `return BsdfSample{};` for rough non-thin material with:

```cpp
const DielectricFrame frame = MakeDielectricFrame(wo, normal, material.ior);
Vec2f remapped = sample;
Vec3f half_vector = SampleGgxHalfVector(frame.normal, material.roughness, remapped);
if (Dot(half_vector, wo) < 0.0f) {
    half_vector = -half_vector;
}
const float fresnel = FresnelDielectric(std::fabs(Dot(wo, half_vector)), frame.eta_i, frame.eta_t);
if (sample.x < fresnel) {
    remapped.x = fresnel > 0.0f ? sample.x / fresnel : sample.x;
    half_vector = SampleGgxHalfVector(frame.normal, material.roughness, remapped);
    if (Dot(half_vector, wo) < 0.0f) {
        half_vector = -half_vector;
    }
    const Vec3f wi = Reflect(-wo, half_vector);
    const float pdf = PdfBsdf(material, wo, wi, normal);
    if (pdf <= 0.0f) {
        return BsdfSample{};
    }
    const Color3f f = EvaluateBsdf(material, wo, wi, normal);
    const float cos_i = AbsDot(normal, wi);
    return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
}

remapped.x = (1.0f - fresnel) > 0.0f ? (sample.x - fresnel) / (1.0f - fresnel) : sample.x;
half_vector = SampleGgxHalfVector(frame.normal, material.roughness, remapped);
if (Dot(half_vector, wo) < 0.0f) {
    half_vector = -half_vector;
}
Vec3f wi;
if (!Refract(wo, half_vector, frame.eta, wi)) {
    return BsdfSample{Reflect(-wo, half_vector), material.albedo, 1.0f, true, true};
}
const float pdf = PdfBsdf(material, wo, wi, normal);
if (pdf <= 0.0f) {
    return BsdfSample{};
}
const Color3f f = EvaluateBsdf(material, wo, wi, normal);
const float cos_i = AbsDot(normal, wi);
return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
```

- [ ] **Step 7: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass. If a rough dielectric test is flaky or fails for a sampled direction with zero PDF, adjust the test sample values and the implementation together so `SampleBsdf()`, `EvaluateBsdf()`, and `PdfBsdf()` agree for that deterministic direction.

- [ ] **Step 8: Commit**

```powershell
git add src/render/bsdf.cpp tests/bsdf_tests.cpp
git commit -m "feat: add rough dielectric bsdf"
```

---

## Task 5: Thin Glass And Transmission Ray Bias

**Files:**
- Modify: `src/render/bsdf.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/bsdf_tests.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add failing thin glass BSDF tests**

In `tests/bsdf_tests.cpp`, add helper:

```cpp
yr::RenderMaterial ThinGlassMaterial(float roughness = 0.0f) {
    yr::RenderMaterial material = SmoothGlassMaterial();
    material.thin = true;
    material.roughness = roughness;
    return material;
}
```

Add tests:

```cpp
YR_TEST(bsdf_thin_glass_transmits_straight_through) {
    const yr::RenderMaterial material = ThinGlassMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.8f, 0.5f});

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, -wo.x, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, -wo.y, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, -wo.z, 1e-6);
}

YR_TEST(bsdf_rough_thin_glass_is_non_delta_and_finite) {
    const yr::RenderMaterial material = ThinGlassMaterial(0.3f);
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.1f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.95f, 0.4f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_TRUE(sample.weight.x > 0.0f);
}
```

- [ ] **Step 2: Add failing CPU path tracer tests**

In `tests/cpu_path_tracer_tests.cpp`, add helpers near the other scene factories:

```cpp
yr::RenderScene MakeGlassPanelScene(yr::MaterialKind type, float roughness, bool thin) {
    yr::RenderScene scene = MakeBaseScene(1, 1);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 16;
    scene.max_depth = 3;
    scene.seed = 71;
    scene.threads = 1;
    scene.light_samples = 4;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.radiance = yr::Color3f{0.25f, 0.5f, 1.0f};
    scene.materials[0].type = type;
    scene.materials[0].albedo = yr::Color3f{1.0f, 1.0f, 1.0f};
    scene.materials[0].roughness = roughness;
    scene.materials[0].ior = 1.5f;
    scene.materials[0].thin = thin;
    scene.triangles[0] = yr::RenderTriangle{
        yr::Point3f{-2.0f, -2.0f, 0.0f},
        yr::Point3f{2.0f, -2.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    };
    RebuildBvh(scene);
    return scene;
}
```

Add tests:

```cpp
YR_TEST(cpu_path_tracer_smooth_glass_transmits_environment) {
    const yr::CpuPathTraceResult result =
        yr::RenderCpuPathTrace(MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.0f, false));
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.z > 0.1f);
    YR_EXPECT_TRUE(result.stats.rays_traced > 1);
}

YR_TEST(cpu_path_tracer_thin_glass_panel_does_not_black_out_environment) {
    const yr::CpuPathTraceResult result =
        yr::RenderCpuPathTrace(MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.0f, true));
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.z > 0.1f);
    YR_EXPECT_TRUE(result.stats.rays_traced > 1);
}

YR_TEST(cpu_path_tracer_rough_glass_is_deterministic) {
    const yr::RenderScene scene = MakeGlassPanelScene(yr::MaterialKind::Dielectric, 0.35f, false);

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
    YR_EXPECT_TRUE(Luminance(first.film.LinearPixel(0, 0)) > 0.0f);
}
```

- [ ] **Step 3: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: thin rough glass fails or CPU glass tests fail due to missing thin rough behavior and transmitted ray bias.

- [ ] **Step 4: Implement rough thin glass sampling**

In the `material.thin` branch of `SampleBsdf()`, replace the smooth-only return block with:

```cpp
if (material.thin) {
    const Vec3f oriented_normal = Dot(normal, wo) >= 0.0f ? normal : -normal;
    const float fresnel = FresnelDielectric(std::fabs(Dot(oriented_normal, wo)), 1.0f, std::max(1.0f, material.ior));
    if (material.roughness <= DeltaRoughness) {
        if (sample.x < fresnel) {
            return BsdfSample{Reflect(-wo, oriented_normal), material.albedo, 1.0f, true, true};
        }
        return BsdfSample{Normalize(-wo), material.albedo, 1.0f, true, true};
    }

    Vec2f remapped = sample;
    if (sample.x < fresnel) {
        remapped.x = fresnel > 0.0f ? sample.x / fresnel : sample.x;
        BsdfSample reflection = SampleGgxReflection(wo, oriented_normal, remapped, material.albedo * fresnel, material.roughness);
        if (reflection.valid) {
            reflection.specular = false;
        }
        return reflection;
    }

    remapped.x = (1.0f - fresnel) > 0.0f ? (sample.x - fresnel) / (1.0f - fresnel) : sample.x;
    const Vec3f forward = Normalize(-wo);
    const Vec3f wi = SampleCosineHemisphere(forward, remapped);
    const float pdf = std::max(0.0f, Dot(forward, wi)) / Pi;
    if (pdf <= 0.0f) {
        return BsdfSample{};
    }
    return BsdfSample{wi, material.albedo * (1.0f - fresnel), pdf, true, false};
}
```

In `EvaluateBsdf()` and `PdfBsdf()`, keep smooth thin as delta and let rough thin use a finite forward lobe:

```cpp
case MaterialKind::Dielectric:
    if (material.thin) {
        if (material.roughness <= DeltaRoughness) {
            return Color3f{};
        }
        const Vec3f forward = Normalize(-wo);
        const float cos_forward = std::max(0.0f, Dot(forward, wi));
        if (cos_forward <= 0.0f) {
            return Color3f{};
        }
        const float fresnel = FresnelDielectric(std::fabs(Dot(normal, wo)), 1.0f, std::max(1.0f, material.ior));
        return LambertianBrdf(material.albedo) * (1.0f - fresnel);
    }
    if (material.roughness <= DeltaRoughness) {
        return Color3f{};
    }
    return SameHemisphere(wo, wi, normal)
        ? GgxDielectricReflection(material.albedo, material.ior, material.roughness, wo, wi, normal)
        : GgxDielectricTransmission(material.albedo, material.ior, material.roughness, wo, wi, normal);
```

```cpp
case MaterialKind::Dielectric:
    if (material.thin) {
        if (material.roughness <= DeltaRoughness) {
            return 0.0f;
        }
        const Vec3f forward = Normalize(-wo);
        return std::max(0.0f, Dot(forward, wi)) / Pi;
    }
    if (material.roughness <= DeltaRoughness) {
        return 0.0f;
    }
    return SameHemisphere(wo, wi, normal)
        ? GgxDielectricReflectionPdf(material.ior, material.roughness, wo, wi, normal)
        : GgxDielectricTransmissionPdf(material.ior, material.roughness, wo, wi, normal);
```

- [ ] **Step 5: Fix `IsDeltaBsdf()` for rough thin glass**

In `IsDeltaBsdf()`, make the dielectric case:

```cpp
case MaterialKind::Dielectric:
    return material.roughness <= DeltaRoughness;
```

- [ ] **Step 6: Bias transmitted rays on the outgoing side**

In `src/backends/cpu/cpu_path_tracer.cpp`, replace:

```cpp
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bsdf_sample.wi};
```

with:

```cpp
const Vec3f bias_normal = Dot(bsdf_sample.wi, normal) >= 0.0f ? normal : -normal;
ray = Ray3f{hit_point + bias_normal * SurfaceBias(hit_point), bsdf_sample.wi};
```

- [ ] **Step 7: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 8: Commit**

```powershell
git add src/render/bsdf.cpp src/backends/cpu/cpu_path_tracer.cpp tests/bsdf_tests.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add thin glass path tracing"
```

---

## Task 6: Glass Showcase Mesh, Scene, And CLI Smoke Test

**Files:**
- Create: `scenes/examples/assets/glass_sphere.obj`
- Create: `scenes/examples/glass_showcase.toml`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Generate deterministic glass sphere OBJ**

Run this PowerShell script from the repository root:

```powershell
$path = Join-Path (Get-Location) "scenes\examples\assets\glass_sphere.obj"
$segments = 32
$rings = 16
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Deterministic UV sphere for YaoRay glass showcase")
for ($r = 0; $r -le $rings; $r++) {
    $v = [double]$r / [double]$rings
    $theta = [Math]::PI * $v
    $y = [Math]::Cos($theta)
    $radius = [Math]::Sin($theta)
    for ($s = 0; $s -lt $segments; $s++) {
        $u = [double]$s / [double]$segments
        $phi = 2.0 * [Math]::PI * $u
        $x = $radius * [Math]::Cos($phi)
        $z = $radius * [Math]::Sin($phi)
        $lines.Add(("v {0:R} {1:R} {2:R}" -f $x, $y, $z))
    }
}
for ($r = 0; $r -le $rings; $r++) {
    for ($s = 0; $s -lt $segments; $s++) {
        $index = $r * $segments + $s
        $parts = $lines[$index + 1].Split(" ")
        $lines.Add(("vn {0} {1} {2}" -f $parts[1], $parts[2], $parts[3]))
    }
}
$lines.Add("usemtl glass")
for ($r = 0; $r -lt $rings; $r++) {
    for ($s = 0; $s -lt $segments; $s++) {
        $a = $r * $segments + $s + 1
        $b = $r * $segments + (($s + 1) % $segments) + 1
        $c = ($r + 1) * $segments + (($s + 1) % $segments) + 1
        $d = ($r + 1) * $segments + $s + 1
        if ($r -ne 0) {
            $lines.Add(("f {0}//{0} {1}//{1} {2}//{2}" -f $a, $b, $c))
        }
        if ($r -ne ($rings - 1)) {
            $lines.Add(("f {0}//{0} {1}//{1} {2}//{2}" -f $a, $c, $d))
        }
    }
}
Set-Content -Path $path -Value $lines -Encoding ASCII
```

Expected file:

```text
scenes/examples/assets/glass_sphere.obj
```

- [ ] **Step 2: Add showcase scene**

Create `scenes/examples/glass_showcase.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
sampler = "stratified"
width = 256
height = 256
spp = 24
max_depth = 8
seed = 37
threads = 0
light_samples = 4

[film]
output = "out/glass_showcase.png"
tone_mapper = "aces"
exposure = -0.5

[camera]
type = "perspective"
position = [0.0, 1.1, 5.0]
target = [0.0, 0.7, 0.0]
fov_y = 38.0

[environment]
type = "hdri"
path = "assets/env/tiny_studio.hdr"
strength = 1.1
rotation_degrees = 0.0

[[materials]]
name = "matte_floor"
type = "diffuse"
albedo = [0.72, 0.72, 0.68]

[[materials]]
name = "clear_glass"
type = "glass"
ior = 1.5
albedo = [1.0, 1.0, 1.0]

[[materials]]
name = "rough_glass"
type = "rough_glass"
ior = 1.5
roughness = 0.35
albedo = [0.9, 0.96, 1.0]

[[materials]]
name = "thin_glass"
type = "thin_glass"
ior = 1.45
albedo = [0.9, 1.0, 0.95]

[[assets]]
name = "floor"
quads = [
  [[-3.0, 0.0, -3.0], [3.0, 0.0, -3.0], [3.0, 0.0, 3.0], [-3.0, 0.0, 3.0]]
]

[[assets]]
name = "sphere"
path = "assets/glass_sphere.obj"

[[assets]]
name = "thin_panel"
quads = [
  [[-0.65, 0.0, 0.0], [0.65, 0.0, 0.0], [0.65, 1.3, 0.0], [-0.65, 1.3, 0.0]]
]

[[instances]]
asset = "floor"
material = "matte_floor"

[[instances]]
asset = "sphere"
material = "clear_glass"
translate = [-1.0, 0.75, 0.0]
scale = [0.7, 0.7, 0.7]

[[instances]]
asset = "sphere"
material = "rough_glass"
translate = [1.0, 0.75, 0.0]
scale = [0.7, 0.7, 0.7]

[[instances]]
asset = "thin_panel"
material = "thin_glass"
translate = [0.0, 0.15, -0.85]
rotate_degrees = [0.0, 18.0, 0.0]
```

- [ ] **Step 3: Add CLI smoke test**

In `CMakeLists.txt`, add after `yaoray_cli_render_hdri_showcase`:

```cmake
    add_test(NAME yaoray_cli_render_glass_showcase
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/glass_showcase.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/glass_showcase.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Integrator: path') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 4: Run showcase smoke test**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_cli_render_glass_showcase
```

Expected: CTest reports `100% tests passed` for `yaoray_cli_render_glass_showcase`.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt scenes/examples/assets/glass_sphere.obj scenes/examples/glass_showcase.toml
git commit -m "test: add glass material showcase"
```

---

## Task 7: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-21-yaoray-dielectric-material-pack-v3-design.md`

- [ ] **Step 1: Update README**

In `README.md`, add dielectric support to the current status bullet list:

```markdown
- TOML named diffuse, emissive, mirror, metal, plastic, and dielectric/glass materials with instance material binding
```

Add this command in the Run section:

```powershell
build\Debug\yaoray.exe render scenes\examples\glass_showcase.toml --backend cpu
```

Update the material paragraph to include:

```markdown
`type = "dielectric"` adds glass-style reflection and transmission through
`ior`, `roughness`, and `thin`; `glass`, `rough_glass`, and `thin_glass` are
authoring aliases for the same render material kind.
```

Update limitations to mention:

```markdown
Dielectric materials do not yet include Beer-Lambert absorption, medium stacks,
caustic-specific sampling, glTF glass extension import, or CUDA parity.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, update the BSDF paragraph:

```markdown
Material scattering for `path` is routed through a small render-level BSDF API
that currently implements Lambertian diffuse, perfect mirror, GGX-style metal,
simple plastic behavior, and dielectric glass reflection/transmission with
smooth, rough, and thin variants through data-driven `MaterialKind` dispatch.
```

Update showcase paragraph:

```markdown
`glass_showcase.toml` uses mesh spheres and a thin pane to verify clear glass,
rough glass, and thin glass through the CPU path tracer.
```

Update limits:

```markdown
Dielectric rendering remains a surface model: Beer-Lambert absorption, nested
medium stacks, caustic-specific algorithms, glTF glass extension import, and
CUDA parity remain future work.
```

- [ ] **Step 3: Mark spec as implemented**

Append this section to `docs/superpowers/specs/2026-05-21-yaoray-dielectric-material-pack-v3-design.md`:

```markdown
## Implementation Status

Implemented in Dielectric Material Pack v3:

- Scene parser support for `dielectric`, `glass`, `rough_glass`, and
  `thin_glass`.
- Material `ior` and `thin` fields in semantic and render material data.
- Smooth dielectric Fresnel reflection, Snell refraction, and total internal
  reflection.
- Classic GGX rough dielectric reflection and transmission.
- Thin glass pane behavior for smooth and rough materials.
- CPU path tracer transmitted-ray biasing.
- Parser, compiler, BSDF, path tracer, and CLI showcase tests.
```

- [ ] **Step 4: Run documentation grep**

Run:

```powershell
rg -n "dielectric|glass|rough_glass|thin_glass|Beer-Lambert|medium stack|CUDA parity" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-21-yaoray-dielectric-material-pack-v3-design.md
```

Expected: output includes the new capability and limitations in all three files.

- [ ] **Step 5: Commit**

```powershell
git add README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-21-yaoray-dielectric-material-pack-v3-design.md
git commit -m "docs: document dielectric material pack"
```

---

## Task 8: Full Verification And Final Cleanup

**Files:**
- Inspect all changed files.

- [ ] **Step 1: Run full Debug build**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build succeeds. Existing third-party warnings from toml++ or nlohmann are acceptable; new YaoRay source warnings are not acceptable.

- [ ] **Step 2: Run full Debug test suite**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 3: Run manual glass showcase render**

Run:

```powershell
.\build\Debug\yaoray.exe render .\scenes\examples\glass_showcase.toml --backend cpu
```

Expected output includes:

```text
Integrator: path
Rendered image: scenes/examples/out/glass_showcase.png
Shadow rays:
Rays traced:
```

Expected file:

```text
scenes/examples/out/glass_showcase.png
```

- [ ] **Step 4: Inspect changed files**

Run:

```powershell
git status --short
git diff --stat
git diff --check
```

Expected:

- no unstaged changes after Task 7 commits unless final cleanup is intentionally needed.
- `git diff --check` reports no whitespace errors.
- generated render outputs under `scenes/examples/out/` remain ignored or untracked and are not committed.

- [ ] **Step 5: Commit final cleanup when needed**

If Step 4 shows small intentional cleanup changes, commit them:

```powershell
git add CMakeLists.txt README.md docs include src tests scenes/examples
git commit -m "chore: finish dielectric material pack v3"
```

Expected: create this commit only when there are leftover intentional changes after Tasks 1-7.

---

## Execution Notes

- Use TDD at each task boundary: write the failing test, run it, implement the smallest passing change, rerun, commit.
- Use an isolated worktree at execution time through `superpowers:using-git-worktrees`.
- Keep all dielectric aliases compiled to `MaterialKind::Dielectric`; do not add separate render material kinds for `glass`, `rough_glass`, or `thin_glass`.
- Keep Beer-Lambert absorption and medium stack work out of this implementation.
- Preserve existing diffuse, mirror, metal, plastic, texture, HDRI, MIS, and threading behavior.
- If rough dielectric math produces a deterministic zero-PDF sample in tests, adjust the sample coordinate and implementation together so `SampleBsdf()`, `EvaluateBsdf()`, and `PdfBsdf()` agree for valid directions.
