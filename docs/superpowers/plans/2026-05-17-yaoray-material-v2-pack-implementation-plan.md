# YaoRay Material v2 Pack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `metal` and `plastic` material kinds, with `roughness` and `specular` scene fields, while keeping the CPU path tracer material-generic through the existing BSDF API.

**Architecture:** Extend semantic and render material data with appended scalar fields so existing scenes remain valid. Keep all material-kind dispatch inside parser/name helpers and `src/render/bsdf.cpp`; the CPU path tracer continues to call `IsDeltaBsdf`, `EvaluateBsdf`, `PdfBsdf`, and `SampleBsdf` without new material branches. Use a compact GGX microfacet implementation for rough metal and plastic.

**Tech Stack:** C++20, TOML parser with toml++, CMake/CTest, in-repo `yr_test`, `yaoray_render` BSDF API, CPU path tracer, PNG manual render.

---

## Scope

This plan implements:

- `docs/superpowers/specs/2026-05-17-yaoray-material-v2-pack-design.md`

It does not implement glass, dielectric refraction, texture maps, normal maps, complex conductor IOR tables, MIS, Russian roulette, sphere primitives, CUDA material evaluation, or a broad material module refactor.

## File Structure

- Modify `include/yaoray/scene/scene.hpp`
  - Add `MaterialKind::Metal` and `MaterialKind::Plastic`.
  - Append `roughness` and `specular` to `MaterialDescription`.
- Modify `src/scene/scene.cpp`
  - Add stable names and parsers for `metal` and `plastic`.
- Modify `src/scene/scene_parser.cpp`
  - Allow and validate `roughness` and `specular`.
  - Apply material-kind-specific roughness default for plastic.
- Modify `include/yaoray/render/render_scene.hpp`
  - Append `roughness` and `specular` to `RenderMaterial`.
- Modify `src/render/scene_compiler.cpp`
  - Copy `roughness` and `specular` to compiled materials.
- Modify `src/render/bsdf.cpp`
  - Add polished metal, rough GGX metal, and simple plastic behavior.
- Modify `tests/scene_tests.cpp`
  - Add parser/name/default/rejection coverage.
- Modify `tests/render_scene_tests.cpp`
  - Add compiler propagation and default material coverage.
- Modify `tests/bsdf_tests.cpp`
  - Add metal and plastic BSDF behavior coverage.
- Create `scenes/examples/material_v2_showcase.toml`
  - Cornell-style material showcase with diffuse, emissive, mirror, polished metal, rough metal, and plastic materials.
- Modify `README.md`
  - Document material v2 user-facing scene fields and showcase command.
- Modify `docs/architecture/overview.md`
  - Document supported material kinds and limitations.

## Task 1: Extend Material Schema, Parser, And Compiler

**Files:**
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene.cpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`

- [ ] **Step 1: Add failing enum and parser tests**

In `tests/scene_tests.cpp`, update `scene_enum_names_are_stable` by adding:

```cpp
YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Metal), std::string_view{"metal"});
YR_EXPECT_EQ(yr::MaterialKindName(yr::MaterialKind::Plastic), std::string_view{"plastic"});
```

Update `scene_enum_parsers_accept_stable_names` by adding:

```cpp
YR_EXPECT_EQ(yr::ParseMaterialKindName("metal").value(), yr::MaterialKind::Metal);
YR_EXPECT_EQ(yr::ParseMaterialKindName("plastic").value(), yr::MaterialKind::Plastic);
```

Update `scene_enum_parsers_reject_unknown_names` by keeping the existing `"glass"` rejection and adding:

```cpp
YR_EXPECT_TRUE(!yr::ParseMaterialKindName("rough_metal").has_value());
```

- [ ] **Step 2: Add failing material scalar parser tests**

In `tests/scene_tests.cpp`, add these tests after `scene_parser_loads_mirror_material_type`:

```cpp
YR_TEST(scene_parser_loads_metal_material_type_and_roughness) {
    const std::filesystem::path path = WriteTempScene(
        "metal_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "brushed_gold"
type = "metal"
albedo = [1.0, 0.72, 0.32]
roughness = 0.35
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Metal);
    YR_EXPECT_NEAR(material.roughness, 0.35, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
}

YR_TEST(scene_parser_loads_plastic_material_type_roughness_and_specular) {
    const std::filesystem::path path = WriteTempScene(
        "plastic_material_type.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "red_plastic"
type = "plastic"
albedo = [0.8, 0.05, 0.03]
roughness = 0.25
specular = 0.08
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Plastic);
    YR_EXPECT_NEAR(material.roughness, 0.25, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.08, 1e-6);
}

YR_TEST(scene_parser_applies_plastic_roughness_default) {
    const std::filesystem::path path = WriteTempScene(
        "plastic_default_roughness.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "default_plastic"
type = "plastic"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Plastic);
    YR_EXPECT_NEAR(material.roughness, 0.25, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
}
```

Update `scene_parser_applies_material_defaults` by adding:

```cpp
YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
```

- [ ] **Step 3: Add failing scalar rejection tests**

In `tests/scene_tests.cpp`, add these tests near `scene_parser_rejects_non_string_material_type`:

```cpp
YR_TEST(scene_parser_rejects_non_numeric_material_roughness) {
    const std::filesystem::path path = WriteTempScene(
        "non_numeric_material_roughness.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_roughness"
type = "metal"
roughness = "smooth"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.roughness", "must be a finite float in [0, 1]"));
}

YR_TEST(scene_parser_rejects_out_of_range_material_roughness) {
    const std::filesystem::path path = WriteTempScene(
        "out_of_range_material_roughness.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_roughness"
type = "metal"
roughness = 1.25
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.roughness", "must be in [0, 1]"));
}

YR_TEST(scene_parser_rejects_non_numeric_material_specular) {
    const std::filesystem::path path = WriteTempScene(
        "non_numeric_material_specular.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_specular"
type = "plastic"
specular = "bright"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.specular", "must be a finite float in [0, 1]"));
}

YR_TEST(scene_parser_rejects_out_of_range_material_specular) {
    const std::filesystem::path path = WriteTempScene(
        "out_of_range_material_specular.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "bad_specular"
type = "plastic"
specular = -0.1
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.specular", "must be in [0, 1]"));
}
```

- [ ] **Step 4: Add failing render compiler tests**

In `tests/render_scene_tests.cpp`, add this assertion after the default `RenderMaterial material;` assertions in `render_scene_defaults_are_backend_friendly`:

```cpp
YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
```

Add this test after `scene_compiler_copies_material_type`:

```cpp
YR_TEST(scene_compiler_copies_material_scalars) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "plastic",
        yr::MaterialKind::Plastic,
        yr::Color3f{0.8f, 0.05f, 0.03f},
        yr::Color3f{},
        0.25f,
        0.08f
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials[0].type, yr::MaterialKind::Plastic);
    YR_EXPECT_NEAR(compiled.materials[0].roughness, 0.25, 1e-6);
    YR_EXPECT_NEAR(compiled.materials[0].specular, 0.08, 1e-6);
}
```

Update `scene_compiler_preserves_default_material_for_unbound_instances` by adding:

```cpp
YR_EXPECT_NEAR(compiled.materials[1].roughness, 0.0, 1e-6);
YR_EXPECT_NEAR(compiled.materials[1].specular, 0.04, 1e-6);
```

- [ ] **Step 5: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected:

- Build fails because `MaterialKind::Metal`, `MaterialKind::Plastic`, `MaterialDescription::roughness`, `MaterialDescription::specular`, `RenderMaterial::roughness`, and `RenderMaterial::specular` do not exist yet.

- [ ] **Step 6: Extend material data model**

In `include/yaoray/scene/scene.hpp`, replace `MaterialKind` with:

```cpp
enum class MaterialKind {
    Diffuse,
    Mirror,
    Metal,
    Plastic,
};
```

Replace `MaterialDescription` with:

```cpp
struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
};
```

In `include/yaoray/render/render_scene.hpp`, replace `RenderMaterial` with:

```cpp
struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
};
```

- [ ] **Step 7: Extend material kind helpers**

In `src/scene/scene.cpp`, update `MaterialKindName`:

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
    }
    return "unknown";
}
```

Update `ParseMaterialKindName`:

```cpp
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
    return std::nullopt;
}
```

- [ ] **Step 8: Add material scalar parser helper**

In `src/scene/scene_parser.cpp`, add this helper after `ReadFloat`:

```cpp
std::optional<float> ReadUnitFloat(
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
        diagnostics.push_back(Error(file, field, "must be a finite float in [0, 1]"));
        return std::nullopt;
    }
    if (*value < 0.0f || *value > 1.0f) {
        diagnostics.push_back(Error(file, std::move(field), "must be in [0, 1]"));
        return std::nullopt;
    }
    return value;
}
```

- [ ] **Step 9: Parse `materials.roughness` and `materials.specular`**

In `src/scene/scene_parser.cpp`, change material allowed fields from:

```cpp
CheckUnknownFields(*table, "materials", {"name", "type", "albedo", "emission"}, file, diagnostics);
```

to:

```cpp
CheckUnknownFields(*table, "materials", {"name", "type", "albedo", "emission", "roughness", "specular"}, file, diagnostics);
```

After emission parsing, add:

```cpp
const bool roughness_authored = table->contains("roughness");
if (const auto roughness = ReadUnitFloat(*table, "roughness", file, "materials.roughness", diagnostics)) {
    material.roughness = *roughness;
}
if (const auto specular = ReadUnitFloat(*table, "specular", file, "materials.specular", diagnostics)) {
    material.specular = *specular;
}
if (!roughness_authored && material.type == MaterialKind::Plastic) {
    material.roughness = 0.25f;
}
```

- [ ] **Step 10: Copy material scalars during scene compilation**

In `src/render/scene_compiler.cpp`, replace:

```cpp
compiled.materials.push_back(RenderMaterial{material.type, material.albedo, material.emission});
```

with:

```cpp
compiled.materials.push_back(RenderMaterial{
    material.type,
    material.albedo,
    material.emission,
    material.roughness,
    material.specular
});
```

- [ ] **Step 11: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing scenes without `roughness` and `specular` still parse.
- `plastic` defaults to roughness `0.25`.
- `metal` defaults to roughness `0.0`.
- Compiler propagates roughness and specular.

- [ ] **Step 12: Commit schema/parser/compiler support**

Run:

```powershell
git add include\yaoray\scene\scene.hpp src\scene\scene.cpp src\scene\scene_parser.cpp include\yaoray\render\render_scene.hpp src\render\scene_compiler.cpp tests\scene_tests.cpp tests\render_scene_tests.cpp
git commit -m "feat: parse material v2 fields"
```

## Task 2: Implement Metal And Plastic BSDF Behavior

**Files:**
- Modify: `tests/bsdf_tests.cpp`
- Modify: `src/render/bsdf.cpp`

- [ ] **Step 1: Add failing polished metal BSDF test**

In `tests/bsdf_tests.cpp`, add these helpers after `MirrorMaterial()`:

```cpp
yr::RenderMaterial PolishedMetalMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Metal,
        yr::Color3f{1.0f, 0.72f, 0.32f},
        yr::Color3f{},
        0.0f,
        0.04f
    };
}

yr::RenderMaterial RoughMetalMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Metal,
        yr::Color3f{1.0f, 0.72f, 0.32f},
        yr::Color3f{},
        0.35f,
        0.04f
    };
}

yr::RenderMaterial PlasticMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Plastic,
        yr::Color3f{0.8f, 0.05f, 0.03f},
        yr::Color3f{},
        0.25f,
        0.04f
    };
}
```

Add this test after `bsdf_mirror_sample_reflects_incident_direction`:

```cpp
YR_TEST(bsdf_polished_metal_samples_tinted_delta_reflection) {
    const yr::RenderMaterial material = PolishedMetalMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.0f});

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.72, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.32, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, sample.wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, sample.wi, normal), 0.0, 1e-6);
}
```

- [ ] **Step 2: Add failing rough metal and plastic BSDF tests**

In `tests/bsdf_tests.cpp`, add these tests after the polished metal test:

```cpp
YR_TEST(bsdf_rough_metal_has_finite_non_delta_response) {
    const yr::RenderMaterial material = RoughMetalMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);
    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > 0.0f);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_TRUE(sample.weight.x >= sample.weight.y);
    YR_EXPECT_TRUE(sample.weight.y >= sample.weight.z);
}

YR_TEST(bsdf_plastic_has_finite_diffuse_and_glossy_response) {
    const yr::RenderMaterial material = PlasticMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.25f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    const float pdf = yr::PdfBsdf(material, wo, wi, normal);
    const yr::BsdfSample diffuse_sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});
    const yr::BsdfSample specular_sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.75f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(value.x > value.y);
    YR_EXPECT_TRUE(value.y > 0.0f);
    YR_EXPECT_TRUE(value.z > 0.0f);
    YR_EXPECT_TRUE(pdf > 0.0f);
    YR_EXPECT_TRUE(diffuse_sample.valid);
    YR_EXPECT_TRUE(specular_sample.valid);
    YR_EXPECT_TRUE(!diffuse_sample.specular);
    YR_EXPECT_TRUE(!specular_sample.specular);
    YR_EXPECT_TRUE(diffuse_sample.pdf > 0.0f);
    YR_EXPECT_TRUE(specular_sample.pdf > 0.0f);
}
```

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- New metal and plastic BSDF tests fail because `src/render/bsdf.cpp` still returns closed results for unhandled non-diffuse/non-mirror material behavior.

- [ ] **Step 4: Add GGX helpers to BSDF implementation**

In `src/render/bsdf.cpp`, add these helpers after `LambertianBrdf`:

```cpp
constexpr float DeltaRoughness = 1.0e-3f;
constexpr float PlasticMinRoughness = 0.05f;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float AbsDot(Vec3f a, Vec3f b) {
    return std::fabs(Dot(a, b));
}

Color3f Add(Color3f a, Color3f b) {
    return Color3f{a.x + b.x, a.y + b.y, a.z + b.z};
}

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

Color3f Lerp(Color3f a, Color3f b, float t) {
    return a * (1.0f - t) + b * t;
}

float RoughnessToAlpha(float roughness) {
    const float clamped = std::max(roughness, DeltaRoughness);
    return std::max(clamped * clamped, DeltaRoughness);
}

float GgxDistribution(float cos_theta_h, float alpha) {
    const float cos2 = cos_theta_h * cos_theta_h;
    const float alpha2 = alpha * alpha;
    const float denom = cos2 * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (Pi * denom * denom);
}

float SmithG1(float cos_theta, float alpha) {
    const float cos2 = cos_theta * cos_theta;
    if (cos2 <= 0.0f) {
        return 0.0f;
    }
    const float tan2 = (1.0f - cos2) / cos2;
    return 2.0f / (1.0f + std::sqrt(1.0f + alpha * alpha * tan2));
}

Color3f SchlickFresnel(Color3f f0, float cos_theta) {
    const float t = std::pow(1.0f - Clamp01(cos_theta), 5.0f);
    return Lerp(f0, Color3f{1.0f, 1.0f, 1.0f}, t);
}

Color3f GgxSpecularBrdf(Color3f f0, float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return Color3f{};
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return Color3f{};
    }

    const float cos_o = std::max(0.0f, Dot(normal, wo));
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_o <= 0.0f || cos_i <= 0.0f || cos_oh <= 0.0f) {
        return Color3f{};
    }

    const float alpha = RoughnessToAlpha(roughness);
    const float d = GgxDistribution(cos_h, alpha);
    const float g = SmithG1(cos_o, alpha) * SmithG1(cos_i, alpha);
    const Color3f f = SchlickFresnel(f0, cos_oh);
    return f * (d * g / (4.0f * cos_o * cos_i));
}

float GgxReflectionPdf(float roughness, Vec3f wo, Vec3f wi, Vec3f normal) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return 0.0f;
    }

    const Vec3f half_vector = Normalize(wo + wi);
    if (LengthSquared(half_vector) <= 0.0f || !IsAboveSurface(half_vector, normal)) {
        return 0.0f;
    }

    const float cos_h = std::max(0.0f, Dot(normal, half_vector));
    const float cos_oh = std::max(0.0f, Dot(wo, half_vector));
    if (cos_h <= 0.0f || cos_oh <= 0.0f) {
        return 0.0f;
    }

    const float alpha = RoughnessToAlpha(roughness);
    return GgxDistribution(cos_h, alpha) * cos_h / (4.0f * cos_oh);
}

Vec3f SampleGgxHalfVector(Vec3f normal, float roughness, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 0.999999f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float alpha = RoughnessToAlpha(roughness);
    const float alpha2 = alpha * alpha;
    const float tan2_theta = alpha2 * u1 / std::max(1.0e-6f, 1.0f - u1);
    const float cos_theta = 1.0f / std::sqrt(1.0f + tan2_theta);
    const float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    const float phi = 2.0f * Pi * u2;

    const float local_x = sin_theta * std::cos(phi);
    const float local_y = sin_theta * std::sin(phi);
    const float local_z = cos_theta;
    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

BsdfSample SampleGgxReflection(Vec3f wo, Vec3f normal, Vec2f sample, Color3f f0, float roughness) {
    if (!IsAboveSurface(wo, normal) || IsBlack(f0)) {
        return BsdfSample{};
    }

    const Vec3f half_vector = SampleGgxHalfVector(normal, roughness, sample);
    const Vec3f wi = Reflect(-wo, half_vector);
    if (!IsAboveSurface(wi, normal)) {
        return BsdfSample{};
    }

    const float pdf = GgxReflectionPdf(roughness, wo, wi, normal);
    if (pdf <= 0.0f) {
        return BsdfSample{};
    }

    const Color3f f = GgxSpecularBrdf(f0, roughness, wo, wi, normal);
    const float cos_i = std::max(0.0f, Dot(normal, wi));
    return BsdfSample{
        wi,
        f * (cos_i / pdf),
        pdf,
        true,
        false
    };
}
```

- [ ] **Step 5: Extend `EvaluateBsdf()`**

In `src/render/bsdf.cpp`, update `EvaluateBsdf`:

```cpp
Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            return LambertianBrdf(material.albedo);
        case MaterialKind::Mirror:
            return Color3f{};
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                return Color3f{};
            }
            return GgxSpecularBrdf(material.albedo, material.roughness, wo, wi, normal);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            const float specular = Clamp01(material.specular);
            const Color3f diffuse = LambertianBrdf(material.albedo) * (1.0f - specular);
            const Color3f f0{specular, specular, specular};
            const Color3f glossy = GgxSpecularBrdf(f0, std::max(material.roughness, PlasticMinRoughness), wo, wi, normal);
            return Add(diffuse, glossy);
        }
    }
    return Color3f{};
}
```

- [ ] **Step 6: Extend `PdfBsdf()`**

In `src/render/bsdf.cpp`, update `PdfBsdf`:

```cpp
float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case MaterialKind::Mirror:
            return 0.0f;
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                return 0.0f;
            }
            return GgxReflectionPdf(material.roughness, wo, wi, normal);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            const float specular = Clamp01(material.specular);
            const float diffuse_pdf = std::max(0.0f, Dot(normal, wi)) / Pi;
            if (specular <= 0.0f) {
                return diffuse_pdf;
            }
            const float glossy_pdf = GgxReflectionPdf(std::max(material.roughness, PlasticMinRoughness), wo, wi, normal);
            return 0.5f * diffuse_pdf + 0.5f * glossy_pdf;
        }
    }
    return 0.0f;
}
```

- [ ] **Step 7: Extend `SampleBsdf()` and `IsDeltaBsdf()`**

In `src/render/bsdf.cpp`, update `SampleBsdf`:

```cpp
BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample) {
    switch (material.type) {
        case MaterialKind::Diffuse: {
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            return BsdfSample{
                wi,
                material.albedo,
                PdfBsdf(material, wo, wi, normal),
                true,
                false
            };
        }
        case MaterialKind::Mirror:
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            return BsdfSample{
                Reflect(-wo, normal),
                material.albedo,
                1.0f,
                true,
                true
            };
        case MaterialKind::Metal:
            if (material.roughness <= DeltaRoughness) {
                if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                    return BsdfSample{};
                }
                return BsdfSample{
                    Reflect(-wo, normal),
                    material.albedo,
                    1.0f,
                    true,
                    true
                };
            }
            return SampleGgxReflection(wo, normal, sample, material.albedo, material.roughness);
        case MaterialKind::Plastic: {
            if (!IsAboveSurface(wo, normal)) {
                return BsdfSample{};
            }
            const float specular = Clamp01(material.specular);
            Vec2f remapped = sample;
            if (specular <= 0.0f || sample.x < 0.5f) {
                remapped.x = specular <= 0.0f ? sample.x : sample.x * 2.0f;
                const Vec3f wi = SampleCosineHemisphere(normal, remapped);
                const float pdf = PdfBsdf(material, wo, wi, normal);
                if (pdf <= 0.0f) {
                    return BsdfSample{};
                }
                const Color3f f = EvaluateBsdf(material, wo, wi, normal);
                const float cos_i = std::max(0.0f, Dot(normal, wi));
                return BsdfSample{wi, f * (cos_i / pdf), pdf, true, false};
            }
            remapped.x = (sample.x - 0.5f) * 2.0f;
            const Color3f f0{specular, specular, specular};
            BsdfSample result = SampleGgxReflection(
                wo,
                normal,
                remapped,
                f0,
                std::max(material.roughness, PlasticMinRoughness)
            );
            if (!result.valid) {
                return result;
            }
            const float pdf = PdfBsdf(material, wo, result.wi, normal);
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            const Color3f f = EvaluateBsdf(material, wo, result.wi, normal);
            const float cos_i = std::max(0.0f, Dot(normal, result.wi));
            result.weight = f * (cos_i / pdf);
            result.pdf = pdf;
            result.specular = false;
            return result;
        }
    }
    return BsdfSample{};
}
```

Update `IsDeltaBsdf`:

```cpp
bool IsDeltaBsdf(const RenderMaterial& material) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            return false;
        case MaterialKind::Mirror:
            return true;
        case MaterialKind::Metal:
            return material.roughness <= DeltaRoughness;
        case MaterialKind::Plastic:
            return false;
    }
    return false;
}
```

- [ ] **Step 8: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing diffuse and mirror tests still pass.
- New polished metal, rough metal, and plastic tests pass.
- CPU path tracer tests still pass because integrator control flow remains material-generic.

- [ ] **Step 9: Commit BSDF material pack**

Run:

```powershell
git add src\render\bsdf.cpp tests\bsdf_tests.cpp
git commit -m "feat: add metal and plastic bsdfs"
```

## Task 3: Add Material v2 Showcase And Documentation

**Files:**
- Create: `scenes/examples/material_v2_showcase.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Create material v2 showcase scene**

Create `scenes/examples/material_v2_showcase.toml` by copying the structure of `scenes/examples/material_showcase.toml`, then use these render and film settings:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 256
height = 256
spp = 64
max_depth = 8
seed = 1
light_samples = 4
sampler = "stratified"

[film]
output = "out/material_v2_showcase.png"
tone_mapper = "aces"
exposure = 0.0
```

Include these materials:

```toml
[[materials]]
name = "cornell_white"
type = "diffuse"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]

[[materials]]
name = "cornell_red"
type = "diffuse"
albedo = [0.630, 0.065, 0.050]
emission = [0, 0, 0]

[[materials]]
name = "cornell_green"
type = "diffuse"
albedo = [0.140, 0.450, 0.091]
emission = [0, 0, 0]

[[materials]]
name = "cornell_light"
type = "diffuse"
albedo = [0.780, 0.780, 0.780]
emission = [17.0, 12.0, 4.0]

[[materials]]
name = "polished_mirror"
type = "mirror"
albedo = [0.95, 0.95, 0.95]
emission = [0, 0, 0]

[[materials]]
name = "polished_gold"
type = "metal"
albedo = [1.0, 0.72, 0.32]
roughness = 0.0
emission = [0, 0, 0]

[[materials]]
name = "brushed_copper"
type = "metal"
albedo = [0.95, 0.55, 0.28]
roughness = 0.35
emission = [0, 0, 0]

[[materials]]
name = "red_plastic"
type = "plastic"
albedo = [0.8, 0.05, 0.03]
roughness = 0.25
specular = 0.04
emission = [0, 0, 0]
```

Use the same Cornell room, wall, light panel, and box assets from `material_showcase.toml`. Add two small extra inline quad block assets if needed to show all material kinds. Keep the final scene inside the existing Cornell box scale and avoid editing `material_showcase.toml`.

Bind at least:

```toml
[[instances]]
asset = "cornell_short_block"
material = "red_plastic"

[[instances]]
asset = "cornell_tall_block"
material = "brushed_copper"
```

Add one smaller block or panel instance bound to `polished_gold`, and keep a visible `polished_mirror` instance if the layout can do so without overlapping. The exact vertex coordinates may be copied and scaled from the existing Cornell block quads; do not introduce sphere primitives in this task.

- [ ] **Step 2: Update README**

In `README.md`, replace the implemented feature bullet:

```markdown
- TOML named diffuse, emissive, and perfect mirror materials with instance material binding
```

with:

```markdown
- TOML named diffuse, emissive, mirror, metal, and plastic materials with instance material binding
```

Add the showcase command after the existing material showcase command:

```powershell
build\Debug\yaoray.exe render scenes\examples\material_v2_showcase.toml --backend cpu
```

Update the render paragraph material sentence from:

```markdown
Materials default to `type = "diffuse"`; `type = "mirror"` enables perfect specular reflection in the CPU path integrator, while `emission` remains an additive material property.
```

to:

```markdown
Materials default to `type = "diffuse"`; `type = "mirror"` enables perfect specular reflection, `type = "metal"` supports polished and rough tinted reflection through `roughness`, and `type = "plastic"` adds a simple diffuse plus glossy model through `roughness` and `specular`, while `emission` remains an additive material property.
```

Keep limitations clear that glass, textures, complex conductor IOR, normal maps, MIS, spectral rendering, and CUDA materials remain future work.

- [ ] **Step 3: Update architecture overview**

In `docs/architecture/overview.md`, replace the implemented material bullet:

```markdown
- TOML named diffuse, emissive, and perfect mirror materials with instance-level material binding
```

with:

```markdown
- TOML named diffuse, emissive, mirror, metal, and plastic materials with instance-level material binding
```

Update the BSDF sentence:

```markdown
Material scattering for `path` is routed through a small render-level BSDF API that currently implements Lambertian diffuse and perfect mirror behavior with data-driven `MaterialKind` dispatch.
```

to:

```markdown
Material scattering for `path` is routed through a small render-level BSDF API that currently implements Lambertian diffuse, perfect mirror, GGX-style metal, and simple plastic behavior with data-driven `MaterialKind` dispatch.
```

Update the material showcase paragraph to mention `material_v2_showcase.toml`.

- [ ] **Step 4: Run docs and scene scope check**

Run:

```powershell
rg -n "material_v2_showcase|type = \"metal\"|type = \"plastic\"|roughness|specular|glass|texture|MIS|CUDA material" README.md docs\architecture\overview.md scenes\examples\material_v2_showcase.toml docs\superpowers\specs\2026-05-17-yaoray-material-v2-pack-design.md
```

Expected:

- The new scene contains `type = "metal"` and `type = "plastic"`.
- README includes the material v2 showcase command and describes roughness/specular.
- Architecture docs mention the BSDF material pack.
- Future-work wording still excludes glass, textures, MIS, and CUDA material evaluation.

- [ ] **Step 5: Commit showcase and docs**

Run:

```powershell
git add scenes\examples\material_v2_showcase.toml README.md docs\architecture\overview.md
git commit -m "docs: add material v2 showcase"
```

## Task 4: Full Verification And Manual Render

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

If `build-release` does not exist, run:

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build-release --config Release
```

Expected:

- Release `yaoray.exe` builds successfully.
- If MSBuild cannot access the Windows SDK under sandboxing, rerun the same command with escalated permissions.

- [ ] **Step 3: Render material v2 showcase**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

If the local generator produced a single-config Ninja build instead of Visual Studio, use:

```powershell
.\build-release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

Expected output contains:

```text
Integrator: path
Threads:
Samples/sec:
Rays/sec:
Shadow rays:
Rendered image: scenes/examples/out/material_v2_showcase.png
```

Do not commit `scenes/examples/out/material_v2_showcase.png`.

- [ ] **Step 4: Run final scope checks**

Run:

```powershell
rg -n "MaterialKind::Metal|MaterialKind::Plastic|rough_metal|glass|dielectric|texture|normal map|complex conductor|MIS|Russian roulette|sphere|CUDA material|virtual|new |delete " include src tests README.md docs\architecture\overview.md scenes\examples\material_v2_showcase.toml docs\superpowers\specs\2026-05-17-yaoray-material-v2-pack-design.md
git status --short --branch
git log --oneline --decorate -10
```

Expected:

- `MaterialKind::Metal` and `MaterialKind::Plastic` appear in scene helpers, parser/compiler tests, BSDF tests, and `src/render/bsdf.cpp`.
- `rough_metal` appears only as a rejected/non-goal wording if at all.
- Glass, dielectric, textures, normal maps, complex conductor IOR, MIS, Russian roulette, sphere, and CUDA material work appear only in limitations/docs/tests for rejection.
- No virtual material hierarchy was introduced.
- Working tree is clean except generated output files that are ignored or intentionally untracked.
- Recent commits include:
  - `feat: parse material v2 fields`
  - `feat: add metal and plastic bsdfs`
  - `docs: add material v2 showcase`

## Self-Review Checklist

- Spec coverage:
  - `metal` and `plastic` material kinds: Task 1 and Task 2.
  - Rough metal through `metal + roughness > 0`: Task 1 parser, Task 2 BSDF, Task 3 scene.
  - `roughness` and `specular` scene fields: Task 1 parser/compiler tests.
  - Existing scenes remain valid: Task 1 defaults and Task 4 full tests.
  - Data-driven `MaterialKind` dispatch: Task 2 BSDF and Task 4 scope check.
  - CPU path tracer material-generic control flow: Task 2 uses BSDF only; Task 4 scope check.
  - Showcase scene: Task 3 and Task 4 render.
  - No glass/textures/MIS/sphere/CUDA implementation: Task 4 scope check.
- Placeholder scan:
  - Every task names concrete files, code snippets, commands, and expected outcomes.
- Type consistency:
  - `roughness` and `specular` are `float` fields appended after `emission`.
  - `MaterialKind::Metal` maps to `"metal"`.
  - `MaterialKind::Plastic` maps to `"plastic"`.
  - Plastic default roughness is applied by parser when not authored.
