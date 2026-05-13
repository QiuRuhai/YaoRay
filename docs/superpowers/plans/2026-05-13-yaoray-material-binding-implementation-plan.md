# YaoRay Material Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add named diffuse/emissive materials to TOML scenes and bind them to instances so Cornell-style scenes can express red, green, white, and emissive surfaces.

**Architecture:** Extend the semantic scene model with `MaterialDescription` and optional `InstanceDescription::material`, parse `[[materials]]` in `yaoray_scene`, then compile named material records into `RenderScene::materials` in `yaoray_render`. Preserve existing unbound-instance behavior by still creating a default `RenderMaterial{}` per unbound instance.

**Tech Stack:** C++20, CMake 3.24+, CTest, toml++, existing YaoRay `scene`, `render`, `backends`, and custom `yr_test` harness.

---

## Scope Check

This plan implements only the approved material binding design:

- `[[materials]]` TOML parsing
- material fields `name`, `albedo`, and `emission`
- `instances.material` TOML parsing
- duplicate, empty, unknown-field, and misdeclared material diagnostics
- scene compiler named-material conversion
- instance material index binding
- default material preservation for unbound instances
- docs and fixture/example updates

It does not implement roughness, metallic, specular, glass, textures, UVs, OBJ `.mtl`, glTF materials, per-face material IDs, material libraries, renderer behavior changes, or a Cornell Box scene.

## File Structure

Create or modify these files:

```text
README.md
docs/architecture/overview.md
include/yaoray/scene/scene.hpp
src/scene/scene_parser.cpp
src/render/scene_compiler.cpp
tests/scene_tests.cpp
tests/render_scene_tests.cpp
tests/fixtures/scene/builtin_triangle.toml
tests/fixtures/scene/obj_quad.toml
scenes/examples/minimal.toml
scenes/examples/obj_pyramid.toml
```

Responsibilities:

- `include/yaoray/scene/scene.hpp`: semantic `MaterialDescription`, scene material list, and optional instance material name.
- `src/scene/scene_parser.cpp`: TOML parsing and diagnostics for `[[materials]]` and `instances.material`.
- `src/render/scene_compiler.cpp`: maps named materials to `RenderMaterial` indices and assigns instance triangle material indices.
- `tests/scene_tests.cpp`: parser coverage for material schema and diagnostics.
- `tests/render_scene_tests.cpp`: compiler coverage for material records, sharing, defaults, and unknown references.
- Scene fixtures/examples: demonstrate material binding end-to-end while preserving existing render flows.
- Docs: record that named diffuse/emissive material binding is implemented and Cornell Box remains a follow-up scene.

## Task 1: Add Material Scene Schema And Parser Support

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add failing parser/schema tests**

In `tests/scene_tests.cpp`, in `scene_defaults_match_schema`, add:

```cpp
    YR_EXPECT_TRUE(scene.materials.empty());
```

In `scene_parser_loads_minimal_scene_file`, after the asset assertions and before the instance assertions, add:

```cpp
    YR_EXPECT_TRUE(scene.materials.empty());
```

In `scene_parser_loads_minimal_scene_file`, after:

```cpp
    YR_EXPECT_EQ(scene.instances[0].asset, std::string{"model"});
```

add:

```cpp
    YR_EXPECT_EQ(scene.instances[0].material, std::string{});
```

Append these parser tests to `tests/scene_tests.cpp`, after `scene_parser_preserves_builtin_asset_paths`:

```cpp
YR_TEST(scene_parser_loads_materials_and_instance_material_binding) {
    const std::filesystem::path path = WriteTempScene(
        "materials.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "red"
albedo = [0.9, 0.1, 0.05]
emission = [0.01, 0.02, 0.03]

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
material = "red"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(scene.materials[0].name, std::string{"red"});
    YR_EXPECT_NEAR(scene.materials[0].albedo.x, 0.9, 1e-6);
    YR_EXPECT_NEAR(scene.materials[0].albedo.y, 0.1, 1e-6);
    YR_EXPECT_NEAR(scene.materials[0].albedo.z, 0.05, 1e-6);
    YR_EXPECT_NEAR(scene.materials[0].emission.x, 0.01, 1e-6);
    YR_EXPECT_NEAR(scene.materials[0].emission.y, 0.02, 1e-6);
    YR_EXPECT_NEAR(scene.materials[0].emission.z, 0.03, 1e-6);
    YR_EXPECT_EQ(scene.instances.size(), std::size_t{1});
    YR_EXPECT_EQ(scene.instances[0].material, std::string{"red"});
}

YR_TEST(scene_parser_applies_material_defaults) {
    const std::filesystem::path path = WriteTempScene(
        "material_defaults.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = "defaulted"

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
material = "defaulted"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::MaterialDescription& material = result.scene.value().materials[0];
    YR_EXPECT_NEAR(material.albedo.x, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.albedo.y, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.albedo.z, 0.8, 1e-6);
    YR_EXPECT_NEAR(material.emission.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.emission.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.emission.z, 0.0, 1e-6);
}

YR_TEST(scene_parser_rejects_bad_material_entries) {
    const std::filesystem::path path = WriteTempScene(
        "bad_materials.toml",
        ValidSceneWith(R"toml(
[[materials]]
name = ""
roughness = 0.5

[[materials]]
name = "dup"

[[materials]]
name = "dup"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.name", "must not be empty"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.roughness", "unknown field"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "materials.name", "duplicate material name"));
}

YR_TEST(scene_parser_rejects_empty_and_non_string_instance_material) {
    const std::filesystem::path empty_path = WriteTempScene(
        "empty_instance_material.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
material = ""
)toml")
    );
    const std::filesystem::path bad_type_path = WriteTempScene(
        "bad_instance_material_type.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
material = 123
)toml")
    );

    const yr::SceneLoadResult empty_result = yr::LoadSceneFile(empty_path);
    const yr::SceneLoadResult bad_type_result = yr::LoadSceneFile(bad_type_path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(empty_result.diagnostics));
    YR_EXPECT_TRUE(!empty_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(empty_result.diagnostics, "instances.material", "must not be empty"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(bad_type_result.diagnostics));
    YR_EXPECT_TRUE(!bad_type_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(bad_type_result.diagnostics, "instances.material", "must be a string"));
}
```

In `scene_parser_rejects_misdeclared_table_array_sections`, add a materials case before the asset case:

```cpp
    const std::filesystem::path materials_path = WriteTempScene(
        "misdeclared_materials.toml",
        ValidSceneWith(R"toml(
[materials]
name = "mat"
)toml")
    );
```

Then load it:

```cpp
    const yr::SceneLoadResult materials_result = yr::LoadSceneFile(materials_path);
```

And assert it:

```cpp
    YR_EXPECT_TRUE(yr::HasSceneErrors(materials_result.diagnostics));
    YR_EXPECT_TRUE(!materials_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(materials_result.diagnostics, "materials", "must be an array of tables"));
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `SceneDescription::materials`, `MaterialDescription`, and `InstanceDescription::material` do not exist.

Do not commit this failing state.

- [ ] **Step 3: Add semantic material data types**

In `include/yaoray/scene/scene.hpp`, add after `struct TransformDescription`:

```cpp
struct MaterialDescription {
    std::string name;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

Extend `InstanceDescription` so it becomes:

```cpp
struct InstanceDescription {
    std::string asset;
    TransformDescription transform;
    std::string material;
};
```

In `SceneDescription`, add `materials` before `instances`:

```cpp
    std::vector<MaterialDescription> materials;
```

- [ ] **Step 4: Parse `[[materials]]`**

In `src/scene/scene_parser.cpp`, add this function after `ParseAssets(...)`:

```cpp
void ParseMaterials(
    const toml::table& root,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* materials = OptionalTableArray(root, "materials", file, diagnostics);
    if (materials == nullptr) {
        return;
    }

    std::unordered_set<std::string> names;
    for (const toml::node& node : *materials) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            diagnostics.push_back(Error(file, "materials", "material entry must be a table"));
            continue;
        }
        CheckUnknownFields(*table, "materials", {"name", "albedo", "emission"}, file, diagnostics);

        MaterialDescription material;
        if (const auto name = ReadValue<std::string>(*table, "name")) {
            if (name->empty()) {
                diagnostics.push_back(Error(file, "materials.name", "must not be empty"));
            } else {
                material.name = *name;
            }
        } else {
            diagnostics.push_back(Error(file, "materials.name", "missing required field"));
        }

        if (const auto albedo = ReadVec3(*table, "albedo", file, "materials.albedo", diagnostics)) {
            material.albedo = *albedo;
        }
        if (const auto emission = ReadVec3(*table, "emission", file, "materials.emission", diagnostics)) {
            material.emission = *emission;
        }

        if (!material.name.empty() && !names.insert(material.name).second) {
            diagnostics.push_back(Error(file, "materials.name", "duplicate material name"));
        }
        scene.materials.push_back(std::move(material));
    }
}
```

- [ ] **Step 5: Parse `instances.material`**

In `ParseInstances(...)`, change the allowed fields list from:

```cpp
            {"asset", "translate", "rotate_degrees", "scale"},
```

to:

```cpp
            {"asset", "material", "translate", "rotate_degrees", "scale"},
```

After parsing `asset`, add:

```cpp
        if (const auto material = ReadValue<std::string>(*table, "material")) {
            if (material->empty()) {
                diagnostics.push_back(Error(file, "instances.material", "must not be empty"));
            } else {
                instance.material = *material;
            }
        } else if (table->contains("material")) {
            diagnostics.push_back(Error(file, "instances.material", "must be a string"));
        }
```

- [ ] **Step 6: Wire material parsing into `LoadSceneFile()`**

In the root `CheckUnknownFields(...)` call inside `LoadSceneFile(...)`, change:

```cpp
    CheckUnknownFields(root, "", {"render", "film", "camera", "assets", "instances", "lights", "environment"}, file, result.diagnostics);
```

to:

```cpp
    CheckUnknownFields(root, "", {"render", "film", "camera", "assets", "materials", "instances", "lights", "environment"}, file, result.diagnostics);
```

Then call `ParseMaterials(...)` between `ParseAssets(...)` and `ParseInstances(...)`:

```cpp
    ParseAssets(root, scene, scene_dir, file, result.diagnostics);
    ParseMaterials(root, scene, file, result.diagnostics);
    ParseInstances(root, scene, file, result.diagnostics);
```

- [ ] **Step 7: Run parser tests and full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 8: Commit**

Run:

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse scene materials"
```

## Task 2: Bind Parsed Materials During Scene Compilation

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing compiler material tests**

Append these tests to `tests/render_scene_tests.cpp`, after `scene_compiler_expands_two_obj_instances` and before the BVH tests:

```cpp
YR_TEST(scene_compiler_compiles_named_materials) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{
        "red",
        yr::Color3f{0.9f, 0.1f, 0.05f},
        yr::Color3f{0.01f, 0.02f, 0.03f}
    });

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene.value().materials[0].albedo.x, 0.9, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().materials[0].albedo.y, 0.1, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().materials[0].albedo.z, 0.05, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().materials[0].emission.x, 0.01, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().materials[0].emission.y, 0.02, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().materials[0].emission.z, 0.03, 1e-6);
}

YR_TEST(scene_compiler_binds_builtin_instance_to_named_material) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{"green", yr::Color3f{0.1f, 0.8f, 0.2f}, yr::Color3f{}});
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "green";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_NEAR(result.scene.value().materials[0].albedo.y, 0.8, 1e-6);
}

YR_TEST(scene_compiler_shares_named_material_between_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{"white", yr::Color3f{0.75f, 0.75f, 0.75f}, yr::Color3f{}});
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});

    yr::InstanceDescription first;
    first.asset = "triangle";
    first.material = "white";
    scene.instances.push_back(first);

    yr::InstanceDescription second;
    second.asset = "triangle";
    second.material = "white";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_EQ(result.scene.value().triangles[1].material_index, 0);
}

YR_TEST(scene_compiler_preserves_default_material_for_unbound_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.materials.push_back(yr::MaterialDescription{"red", yr::Color3f{1.0f, 0.0f, 0.0f}, yr::Color3f{}});
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 1);
    YR_EXPECT_NEAR(result.scene.value().materials[1].albedo.x, 0.8, 1e-6);
}

YR_TEST(scene_compiler_rejects_unknown_material_reference) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "missing";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "instances.material", "references unknown material"));
}
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `yaoray_tests` fails because the compiler still ignores `scene.materials` and still creates a default material inside `AppendBuiltinTriangle()` / `AppendImportedMesh()`.

Do not commit this failing state.

- [ ] **Step 3: Add compiler material map helpers**

In `src/render/scene_compiler.cpp`, add after `BuildAssetMap(...)`:

```cpp
std::unordered_map<std::string, int> BuildMaterialMap(const SceneDescription& scene, RenderScene& compiled) {
    std::unordered_map<std::string, int> materials;
    for (const MaterialDescription& material : scene.materials) {
        const int index = static_cast<int>(compiled.materials.size());
        compiled.materials.push_back(RenderMaterial{material.albedo, material.emission});
        materials.emplace(material.name, index);
    }
    return materials;
}

int AddDefaultMaterial(RenderScene& compiled) {
    const int index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
    return index;
}

int ResolveMaterialIndex(
    const SceneDescription& scene,
    const InstanceDescription& instance,
    const std::unordered_map<std::string, int>& materials,
    RenderScene& compiled,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (instance.material.empty()) {
        return AddDefaultMaterial(compiled);
    }

    const auto found = materials.find(instance.material);
    if (found == materials.end()) {
        diagnostics.push_back(Error(scene, "instances.material", "references unknown material"));
        return -1;
    }
    return found->second;
}
```

- [ ] **Step 4: Pass material index into geometry append helpers**

Change `AppendBuiltinTriangle(...)` signature from:

```cpp
void AppendBuiltinTriangle(RenderScene& compiled, const TransformDescription& transform) {
```

to:

```cpp
void AppendBuiltinTriangle(RenderScene& compiled, const TransformDescription& transform, int material_index) {
```

Remove these lines from inside `AppendBuiltinTriangle(...)`:

```cpp
    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
```

Change `AppendImportedMesh(...)` signature from:

```cpp
void AppendImportedMesh(RenderScene& compiled, const ImportedMesh& mesh, const TransformDescription& transform) {
```

to:

```cpp
void AppendImportedMesh(RenderScene& compiled, const ImportedMesh& mesh, const TransformDescription& transform, int material_index) {
```

Remove these lines from inside `AppendImportedMesh(...)`:

```cpp
    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
```

Change `AppendObjAsset(...)` signature by adding `int material_index` before `mesh_cache`:

```cpp
    const TransformDescription& transform,
    int material_index,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
```

Then change the call at the end of `AppendObjAsset(...)` from:

```cpp
    AppendImportedMesh(compiled, cached->second, transform);
```

to:

```cpp
    AppendImportedMesh(compiled, cached->second, transform, material_index);
```

- [ ] **Step 5: Resolve materials in `CompileScene()`**

In `CompileScene(...)`, after:

```cpp
    const std::unordered_map<std::string, std::filesystem::path> assets = BuildAssetMap(scene);
```

add:

```cpp
    const std::unordered_map<std::string, int> materials = BuildMaterialMap(scene, compiled);
```

Inside the instance loop, after the asset lookup succeeds and before asset dispatch, add:

```cpp
        const int material_index = ResolveMaterialIndex(scene, instance, materials, compiled, result.diagnostics);
        if (material_index < 0) {
            continue;
        }
```

Change the builtin append call from:

```cpp
            AppendBuiltinTriangle(compiled, instance.transform);
```

to:

```cpp
            AppendBuiltinTriangle(compiled, instance.transform, material_index);
```

Change the OBJ append call from:

```cpp
            AppendObjAsset(scene, compiled, asset_path, instance.transform, mesh_cache, result.diagnostics);
```

to:

```cpp
            AppendObjAsset(scene, compiled, asset_path, instance.transform, material_index, mesh_cache, result.diagnostics);
```

- [ ] **Step 6: Run compiler tests and full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: bind scene materials to instances"
```

## Task 3: Update Fixtures, Examples, And Documentation

**Files:**
- Modify: `tests/fixtures/scene/builtin_triangle.toml`
- Modify: `tests/fixtures/scene/obj_quad.toml`
- Modify: `scenes/examples/minimal.toml`
- Modify: `scenes/examples/obj_pyramid.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add material bindings to render fixtures**

In `tests/fixtures/scene/builtin_triangle.toml`, add this block before `[[assets]]`:

```toml
[[materials]]
name = "warm"
albedo = [0.9, 0.25, 0.12]
emission = [0, 0, 0]
```

Then add `material = "warm"` to the existing `[[instances]]` block:

```toml
material = "warm"
```

In `tests/fixtures/scene/obj_quad.toml`, add this block before `[[assets]]`:

```toml
[[materials]]
name = "matte_white"
albedo = [0.75, 0.75, 0.75]
emission = [0, 0, 0]
```

Then add `material = "matte_white"` to the existing `[[instances]]` block.

- [ ] **Step 2: Add material bindings to examples**

In `scenes/examples/minimal.toml`, add this block before `[[assets]]`:

```toml
[[materials]]
name = "warm_triangle"
albedo = [0.9, 0.25, 0.12]
emission = [0, 0, 0]
```

Then add `material = "warm_triangle"` to the existing `[[instances]]` block.

In `scenes/examples/obj_pyramid.toml`, add this block before `[[assets]]`:

```toml
[[materials]]
name = "pyramid_clay"
albedo = [0.8, 0.45, 0.25]
emission = [0, 0, 0]
```

Then add `material = "pyramid_clay"` to the existing `[[instances]]` block.

- [ ] **Step 3: Update README**

In `README.md`, add this status bullet after the direct lighting bullet:

```markdown
- TOML named diffuse/emissive materials with instance material binding
```

Update the future-work sentence from:

```markdown
Final path tracing quality, material and texture import, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

to:

```markdown
Final path tracing quality, textures, imported asset materials, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

Update the run description from:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU direct-lighting images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default and include simple center-sampled area lights. This is still a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

to:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU direct-lighting images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default and include named diffuse materials plus simple center-sampled area lights. This is still a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 4: Update architecture overview**

In `docs/architecture/overview.md`, add this implemented slice bullet after the direct lighting bullet:

```markdown
- TOML named diffuse/emissive materials with instance material binding
```

Change this paragraph:

```markdown
The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It ignores materials, textures, UVs, imported normals, and smoothing data in this slice; those concerns are deferred until the asset and material boundaries are more mature.
```

to:

```markdown
The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It ignores OBJ `.mtl` materials, textures, UVs, imported normals, and smoothing data in this slice; scene-authored named materials can still be bound to whole instances.
```

Update the future-work sentence from:

```markdown
Material and texture import, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

to:

```markdown
Textures, imported asset materials, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 5: Run docs smoke check and full tests**

Run:

```powershell
rg -n "materials|material =|diffuse|emissive|instance material|imported asset materials|Cornell" README.md docs/architecture/overview.md scenes/examples tests/fixtures/scene
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Manually render examples**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
```

Expected output includes:

```text
Rendered image: scenes/examples/out/minimal.png
Rendered image: scenes/examples/out/obj_pyramid.png
Shadow rays:
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add README.md docs/architecture/overview.md tests/fixtures/scene/builtin_triangle.toml tests/fixtures/scene/obj_quad.toml scenes/examples/minimal.toml scenes/examples/obj_pyramid.toml
git commit -m "docs: document scene material binding"
```

## Task 4: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm parser/compiler ownership**

Run:

```powershell
rg -n "MaterialDescription|ParseMaterials|materials|instances.material|BuildMaterialMap|ResolveMaterialIndex|RenderMaterial" include src tests CMakeLists.txt
```

Expected:

- `MaterialDescription` appears in `include/yaoray/scene/scene.hpp`, parser/compiler/tests.
- `ParseMaterials` appears only in `src/scene/scene_parser.cpp`.
- `BuildMaterialMap` and `ResolveMaterialIndex` appear only in `src/render/scene_compiler.cpp`.
- `RenderMaterial` remains render-layer data.

- [ ] **Step 2: Confirm non-goals are not implemented**

Run:

```powershell
rg -n "roughness|metallic|specular|glass|texture|uv|mtl|gltf|material_id|per-face|Cornell" include src tests scenes README.md docs/architecture/overview.md
```

Expected:

- Matches may appear in docs as future work or in existing OBJ loader config.
- There are no new production roughness, metallic, specular, texture, UV, `.mtl`, glTF material, per-face material, or Cornell Box scene implementations.

- [ ] **Step 3: Run full Debug verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Verify built-in example PNG output**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\minimal.png) { Remove-Item -LiteralPath scenes\examples\out\minimal.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\minimal.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Compiled triangles: 1
Shadow rays:
Rendered image: scenes/examples/out/minimal.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
```

- [ ] **Step 5: Verify OBJ example PNG output**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\obj_pyramid.png) { Remove-Item -LiteralPath scenes\examples\out\obj_pyramid.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\obj_pyramid.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Compiled triangles: 6
Shadow rays:
Rendered image: scenes/examples/out/obj_pyramid.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
```

- [ ] **Step 6: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- `[[materials]]` parsing: Task 1.
- `name`, `albedo`, `emission`: Task 1.
- `instances.material`: Task 1.
- Duplicate/empty/misdeclared diagnostics: Task 1.
- Compiler named material conversion: Task 2.
- Instance material index binding: Task 2.
- Default material preservation: Task 2 tests.
- Unknown material reference diagnostic: Task 2.
- Docs and examples: Task 3.

Type consistency:

- Semantic material type is `MaterialDescription`.
- `SceneDescription::materials` stores semantic material descriptions.
- `InstanceDescription::material` is a string and is appended after `transform` to preserve existing aggregate initializers.
- Renderer-facing material type remains `RenderMaterial`.
- Compiler diagnostic field for unknown material references is `instances.material`.

Implementation guardrails:

- Do not change CPU renderer shading in this plan.
- Do not implement Cornell Box in this plan.
- Do not add roughness, metallic, specular, textures, UVs, `.mtl`, glTF materials, or per-face material IDs.
- Do not remove default material behavior for unbound instances.
