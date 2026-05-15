# YaoRay Cornell Box Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add scene-authored inline quad assets and a Cornell Box example scene based on Cornell's measured geometry, using current RGB diffuse/emissive materials.

**Architecture:** Extend the semantic `AssetDescription` so an asset is either path-based or inline quad-based. Parse `[[assets]].quads` in `yaoray_scene`, then tessellate inline quads into two render triangles per quad inside `yaoray_render`, reusing existing instance transforms, material binding, BVH construction, CPU backend, and PNG output.

**Tech Stack:** C++20, CMake 3.24+, CTest, toml++, existing YaoRay `scene`, `render`, `backends`, `film`, and custom `yr_test` harness.

---

## Scope Check

This plan implements only the approved Cornell Box foundation:

- inline quad asset schema in TOML
- parser support and diagnostics for `[[assets]].quads`
- compiler tessellation of inline quads into render triangles
- material binding and transforms for inline quads
- Cornell Box example scene using Cornell measured geometry and RGB material approximations
- CLI smoke test that renders the Cornell scene and checks the PNG signature
- README and architecture documentation updates

It does not implement spectral rendering, spectral-to-RGB conversion, BRDF import, path tracing, indirect lighting, color bleeding, textures, imported material files, generic polygon triangulation, polygon holes, or per-face material IDs.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/scene/scene.hpp
src/scene/scene_parser.cpp
src/render/scene_compiler.cpp
tests/scene_tests.cpp
tests/render_scene_tests.cpp
scenes/examples/cornell_box.toml
```

Responsibilities:

- `include/yaoray/scene/scene.hpp`: add semantic `QuadDescription` and store inline quads on `AssetDescription`.
- `src/scene/scene_parser.cpp`: parse `assets.quads`, enforce path-vs-quads exclusivity, and report shape diagnostics.
- `src/render/scene_compiler.cpp`: keep path assets working and add inline quad tessellation.
- `tests/scene_tests.cpp`: parser coverage for inline quad schema and bad TOML inputs.
- `tests/render_scene_tests.cpp`: compiler coverage for tessellation, transforms, material indices, and degenerate quads.
- `scenes/examples/cornell_box.toml`: official measured Cornell Box geometry with RGB material approximations.
- `CMakeLists.txt`: add a Cornell CLI render smoke test.
- Docs: describe the Cornell scene as geometry/material foundation, not physical spectral matching.

## Task 1: Add Inline Quad Asset Scene Schema And Parser Support

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add failing parser/schema tests**

In `tests/scene_tests.cpp`, in `scene_defaults_match_schema`, after:

```cpp
    YR_EXPECT_TRUE(scene.materials.empty());
```

add:

```cpp
    YR_EXPECT_TRUE(scene.assets.empty());
```

In `scene_parser_loads_minimal_scene_file`, after the existing asset path assertion:

```cpp
    YR_EXPECT_EQ(scene.assets[0].path.generic_string(), (scene_path.parent_path() / "assets" / "models" / "model.glb").lexically_normal().generic_string());
```

add:

```cpp
    YR_EXPECT_TRUE(scene.assets[0].quads.empty());
```

After `scene_parser_preserves_builtin_asset_paths`, add these tests:

```cpp
YR_TEST(scene_parser_loads_inline_quad_asset) {
    const std::filesystem::path path = WriteTempScene(
        "inline_quad_asset.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "panel"
quads = [
  [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]]
]
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::AssetDescription& asset = result.scene.value().assets[0];
    YR_EXPECT_EQ(asset.name, std::string{"panel"});
    YR_EXPECT_EQ(asset.path.generic_string(), std::string{});
    YR_EXPECT_EQ(asset.quads.size(), std::size_t{1});
    YR_EXPECT_NEAR(asset.quads[0].p0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(asset.quads[0].p1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(asset.quads[0].p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(asset.quads[0].p3.y, 1.0, 1e-6);
}

YR_TEST(scene_parser_loads_multiple_inline_quads) {
    const std::filesystem::path path = WriteTempScene(
        "inline_quad_asset_multiple.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "two_panels"
quads = [
  [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]],
  [[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]]
]
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::AssetDescription& asset = result.scene.value().assets[0];
    YR_EXPECT_EQ(asset.quads.size(), std::size_t{2});
    YR_EXPECT_NEAR(asset.quads[1].p0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(asset.quads[1].p2.z, 1.0, 1e-6);
}

YR_TEST(scene_parser_rejects_invalid_inline_quad_asset_shapes) {
    const std::filesystem::path both_path = WriteTempScene(
        "asset_path_and_quads.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "bad"
path = "builtin:triangle"
quads = [
  [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]]
]
)toml")
    );
    const std::filesystem::path neither_path = WriteTempScene(
        "asset_neither_path_nor_quads.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "bad"
)toml")
    );
    const std::filesystem::path malformed_path = WriteTempScene(
        "asset_bad_quad_shapes.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "bad"
quads = [
  [[0, 0, 0], [1, 0, 0], [1, 1, 0]],
  [[0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]],
  "not a quad"
]
)toml")
    );
    const std::filesystem::path empty_path = WriteTempScene(
        "asset_empty_quads.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "bad"
quads = []
)toml")
    );
    const std::filesystem::path wrong_type_path = WriteTempScene(
        "asset_quads_wrong_type.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "bad"
quads = "not an array"
)toml")
    );

    const yr::SceneLoadResult both_result = yr::LoadSceneFile(both_path);
    const yr::SceneLoadResult neither_result = yr::LoadSceneFile(neither_path);
    const yr::SceneLoadResult malformed_result = yr::LoadSceneFile(malformed_path);
    const yr::SceneLoadResult empty_result = yr::LoadSceneFile(empty_path);
    const yr::SceneLoadResult wrong_type_result = yr::LoadSceneFile(wrong_type_path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(both_result.diagnostics));
    YR_EXPECT_TRUE(!both_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(both_result.diagnostics, "assets", "must define exactly one of path or quads"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(neither_result.diagnostics));
    YR_EXPECT_TRUE(!neither_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(neither_result.diagnostics, "assets", "must define exactly one of path or quads"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(malformed_result.diagnostics));
    YR_EXPECT_TRUE(!malformed_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(malformed_result.diagnostics, "assets.quads", "quad must contain exactly four points"));
    YR_EXPECT_TRUE(DiagnosticsContain(malformed_result.diagnostics, "assets.quads", "point must contain exactly three numeric values"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(empty_result.diagnostics));
    YR_EXPECT_TRUE(!empty_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(empty_result.diagnostics, "assets.quads", "must not be empty"));
    YR_EXPECT_TRUE(yr::HasSceneErrors(wrong_type_result.diagnostics));
    YR_EXPECT_TRUE(!wrong_type_result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(wrong_type_result.diagnostics, "assets.quads", "must be an array of quads"));
}
```

In `scene_parser_rejects_unknown_fields_inside_table_arrays`, the existing asset table uses `colour = "red"`. Keep that assertion and add a second bad field to the same table:

```toml
smooth = true
```

Then add:

```cpp
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.smooth", "unknown field"));
```

- [ ] **Step 2: Run build to verify parser tests fail**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `AssetDescription::quads` and `QuadDescription` do not exist.

- [ ] **Step 3: Add semantic quad types**

In `include/yaoray/scene/scene.hpp`, replace:

```cpp
struct AssetDescription {
    std::string name;
    std::filesystem::path path;
};
```

with:

```cpp
struct QuadDescription {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Point3f p3;
};

struct AssetDescription {
    std::string name;
    std::filesystem::path path;
    std::vector<QuadDescription> quads;
};
```

This preserves existing aggregate initializers such as `yr::AssetDescription{"triangle", "builtin:triangle"}` because `quads` defaults to an empty vector.

- [ ] **Step 4: Add parser helpers for inline quads**

In `src/scene/scene_parser.cpp`, after `ReadVec2(...)`, add:

```cpp
std::optional<Point3f> ReadPoint3Node(
    const toml::node& node,
    const std::filesystem::path& file,
    std::string field,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* array = node.as_array();
    if (array == nullptr || array->size() != 3) {
        diagnostics.push_back(Error(file, std::move(field), "point must contain exactly three numeric values"));
        return std::nullopt;
    }

    Point3f value;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::optional<float> element = ReadNodeFloat((*array)[i]);
        if (!element) {
            diagnostics.push_back(Error(file, std::move(field), "point must contain exactly three numeric values"));
            return std::nullopt;
        }

        if (i == 0) {
            value.x = *element;
        } else if (i == 1) {
            value.y = *element;
        } else {
            value.z = *element;
        }
    }
    return value;
}

std::optional<QuadDescription> ReadQuadNode(
    const toml::node& node,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::array* array = node.as_array();
    if (array == nullptr || array->size() != 4) {
        diagnostics.push_back(Error(file, "assets.quads", "quad must contain exactly four points"));
        return std::nullopt;
    }

    std::array<Point3f, 4> points{};
    for (std::size_t i = 0; i < 4; ++i) {
        const std::optional<Point3f> point = ReadPoint3Node((*array)[i], file, "assets.quads", diagnostics);
        if (!point) {
            return std::nullopt;
        }
        points[i] = *point;
    }

    return QuadDescription{points[0], points[1], points[2], points[3]};
}

void ParseAssetQuads(
    const toml::table& table,
    AssetDescription& asset,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const toml::node* node = table.get("quads");
    if (node == nullptr) {
        return;
    }

    const toml::array* quads = node->as_array();
    if (quads == nullptr) {
        diagnostics.push_back(Error(file, "assets.quads", "must be an array of quads"));
        return;
    }
    if (quads->empty()) {
        diagnostics.push_back(Error(file, "assets.quads", "must not be empty"));
        return;
    }

    for (const toml::node& quad_node : *quads) {
        if (const std::optional<QuadDescription> quad = ReadQuadNode(quad_node, file, diagnostics)) {
            asset.quads.push_back(*quad);
        }
    }
}
```

- [ ] **Step 5: Extend `ParseAssets()`**

In `ParseAssets(...)`, change the unknown-field allow list from:

```cpp
        CheckUnknownFields(*table, "assets", {"name", "path"}, file, diagnostics);
```

to:

```cpp
        CheckUnknownFields(*table, "assets", {"name", "path", "quads"}, file, diagnostics);
```

Replace the existing path parse block:

```cpp
        if (const auto path = ReadValue<std::string>(*table, "path")) {
            if (path->empty()) {
                diagnostics.push_back(Error(file, "assets.path", "must not be empty"));
            } else {
                asset.path = NormalizeAssetPath(scene_dir, *path);
            }
        } else {
            diagnostics.push_back(Error(file, "assets.path", "missing required field"));
        }
```

with:

```cpp
        const bool has_path = table->contains("path");
        const bool has_quads = table->contains("quads");
        if (has_path == has_quads) {
            diagnostics.push_back(Error(file, "assets", "must define exactly one of path or quads"));
        }

        if (const auto path = ReadValue<std::string>(*table, "path")) {
            if (path->empty()) {
                diagnostics.push_back(Error(file, "assets.path", "must not be empty"));
            } else {
                asset.path = NormalizeAssetPath(scene_dir, *path);
            }
        } else if (has_path) {
            diagnostics.push_back(Error(file, "assets.path", "must be a string"));
        }

        ParseAssetQuads(*table, asset, file, diagnostics);
```

Do not change duplicate-name handling.

- [ ] **Step 6: Build and run full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and CTest reports `100% tests passed, 0 tests failed out of 10`.

- [ ] **Step 7: Commit parser/schema support**

Run:

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse inline quad assets"
```

## Task 2: Compile Inline Quad Assets Into Render Triangles

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing compiler tests**

In `tests/render_scene_tests.cpp`, after `scene_compiler_expands_obj_asset`, add:

```cpp
YR_TEST(scene_compiler_expands_inline_quad_asset) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            }
        }
    });
    scene.materials.push_back(yr::MaterialDescription{
        "white",
        yr::Color3f{0.7f, 0.7f, 0.7f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "panel";
    instance.material = "white";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
    YR_EXPECT_EQ(compiled.triangles[1].material_index, 0);
    YR_EXPECT_NEAR(compiled.triangles[0].p0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].p1.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p1.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[1].p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(compiled.triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(scene_compiler_expands_multiple_inline_quads) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panels",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            },
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 1.0f},
                yr::Point3f{1.0f, 0.0f, 1.0f},
                yr::Point3f{1.0f, 1.0f, 1.0f},
                yr::Point3f{0.0f, 1.0f, 1.0f}
            }
        }
    });
    scene.instances.push_back(yr::InstanceDescription{"panels", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{4});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_EQ(result.scene.value().triangles[2].material_index, 0);
}

YR_TEST(scene_compiler_applies_inline_quad_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 0.0f, 0.0f},
                yr::Point3f{1.0f, 1.0f, 0.0f},
                yr::Point3f{0.0f, 1.0f, 0.0f}
            }
        }
    });
    yr::InstanceDescription instance;
    instance.asset = "panel";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 2.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 4.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 4.0, 1e-5);
}

YR_TEST(scene_compiler_rejects_degenerate_inline_quad) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "bad_panel",
        {},
        std::vector<yr::QuadDescription>{
            yr::QuadDescription{
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f},
                yr::Point3f{0.0f, 0.0f, 0.0f}
            }
        }
    });
    scene.instances.push_back(yr::InstanceDescription{"bad_panel", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.quads", "quad produces degenerate triangle"));
}
```

- [ ] **Step 2: Run tests to verify compiler red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` fails because inline quad assets still reach the path-only compiler path and produce an unsupported asset diagnostic or zero triangles.

- [ ] **Step 3: Change asset lookup to keep full asset descriptions**

In `src/render/scene_compiler.cpp`, replace:

```cpp
std::unordered_map<std::string, std::filesystem::path> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, std::filesystem::path> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, asset.path);
    }
    return assets;
}
```

with:

```cpp
std::unordered_map<std::string, const AssetDescription*> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, const AssetDescription*> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, &asset);
    }
    return assets;
}
```

In `CompileScene(...)`, change:

```cpp
    const std::unordered_map<std::string, std::filesystem::path> assets = BuildAssetMap(scene);
```

to:

```cpp
    const std::unordered_map<std::string, const AssetDescription*> assets = BuildAssetMap(scene);
```

Then replace:

```cpp
        const std::filesystem::path& asset_path = asset->second;
        const std::string asset_path_string = asset_path.generic_string();
```

with:

```cpp
        const AssetDescription& asset_description = *asset->second;
        const std::filesystem::path& asset_path = asset_description.path;
        const std::string asset_path_string = asset_path.generic_string();
```

- [ ] **Step 4: Add checked triangle and quad append helpers**

In `src/render/scene_compiler.cpp`, after `ApplyTransform(...)`, add:

```cpp
constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

bool AppendTriangle(
    const SceneDescription& scene,
    RenderScene& compiled,
    Point3f p0,
    Point3f p1,
    Point3f p2,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const Vec3f normal = Cross(p1 - p0, p2 - p0);
    if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
        diagnostics.push_back(Error(scene, "assets.quads", "quad produces degenerate triangle"));
        return false;
    }

    compiled.triangles.push_back(RenderTriangle{
        p0,
        p1,
        p2,
        Normalize(normal),
        material_index
    });
    return true;
}
```

After `AppendImportedMesh(...)`, add:

```cpp
void AppendInlineQuadAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const AssetDescription& asset,
    const TransformDescription& transform,
    int material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    for (const QuadDescription& quad : asset.quads) {
        const Point3f p0 = ApplyTransform(quad.p0, transform);
        const Point3f p1 = ApplyTransform(quad.p1, transform);
        const Point3f p2 = ApplyTransform(quad.p2, transform);
        const Point3f p3 = ApplyTransform(quad.p3, transform);

        AppendTriangle(scene, compiled, p0, p1, p2, material_index, diagnostics);
        AppendTriangle(scene, compiled, p0, p2, p3, material_index, diagnostics);
    }
}
```

- [ ] **Step 5: Route inline quad assets in `CompileScene()`**

In `CompileScene(...)`, replace:

```cpp
        if (asset_path_string == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform, *material_index);
        } else if (HasObjExtension(asset_path)) {
            AppendObjAsset(scene, compiled, asset_path, instance.transform, *material_index, mesh_cache, result.diagnostics);
        } else {
            result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path_string));
        }
```

with:

```cpp
        if (!asset_description.quads.empty()) {
            AppendInlineQuadAsset(scene, compiled, asset_description, instance.transform, *material_index, result.diagnostics);
        } else if (asset_path_string == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform, *material_index);
        } else if (HasObjExtension(asset_path)) {
            AppendObjAsset(scene, compiled, asset_path, instance.transform, *material_index, mesh_cache, result.diagnostics);
        } else {
            result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path_string));
        }
```

- [ ] **Step 6: Build and run full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and CTest reports `100% tests passed, 0 tests failed out of 10`.

- [ ] **Step 7: Commit compiler support**

Run:

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile inline quad assets"
```

## Task 3: Add Cornell Box Example And CLI Smoke Test

**Files:**
- Create: `scenes/examples/cornell_box.toml`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create Cornell Box scene**

Create `scenes/examples/cornell_box.toml` with this exact content:

```toml
[render]
backend = "cpu"
width = 512
height = 512
spp = 1
max_depth = 5
seed = 1

[film]
output = "out/cornell_box.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [278.0, 273.0, -800.0]
target = [278.0, 273.0, 0.0]
fov_y = 39.3077

[[materials]]
name = "cornell_white"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]

[[materials]]
name = "cornell_red"
albedo = [0.630, 0.065, 0.050]
emission = [0, 0, 0]

[[materials]]
name = "cornell_green"
albedo = [0.140, 0.450, 0.091]
emission = [0, 0, 0]

[[materials]]
name = "cornell_light"
albedo = [0.780, 0.780, 0.780]
emission = [17.0, 12.0, 4.0]

[[assets]]
name = "cornell_room_white"
quads = [
  [[552.8, 0.0, 0.0], [0.0, 0.0, 0.0], [0.0, 0.0, 559.2], [549.6, 0.0, 559.2]],
  [[549.6, 0.0, 559.2], [0.0, 0.0, 559.2], [0.0, 548.8, 559.2], [556.0, 548.8, 559.2]],
  [[556.0, 548.8, 0.0], [556.0, 548.8, 227.0], [0.0, 548.8, 227.0], [0.0, 548.8, 0.0]],
  [[556.0, 548.8, 332.0], [556.0, 548.8, 559.2], [0.0, 548.8, 559.2], [0.0, 548.8, 332.0]],
  [[556.0, 548.8, 227.0], [556.0, 548.8, 332.0], [343.0, 548.8, 332.0], [343.0, 548.8, 227.0]],
  [[213.0, 548.8, 227.0], [213.0, 548.8, 332.0], [0.0, 548.8, 332.0], [0.0, 548.8, 227.0]]
]

[[assets]]
name = "cornell_green_wall"
quads = [
  [[0.0, 0.0, 559.2], [0.0, 0.0, 0.0], [0.0, 548.8, 0.0], [0.0, 548.8, 559.2]]
]

[[assets]]
name = "cornell_red_wall"
quads = [
  [[552.8, 0.0, 0.0], [549.6, 0.0, 559.2], [556.0, 548.8, 559.2], [556.0, 548.8, 0.0]]
]

[[assets]]
name = "cornell_short_block"
quads = [
  [[130.0, 165.0, 65.0], [82.0, 165.0, 225.0], [240.0, 165.0, 272.0], [290.0, 165.0, 114.0]],
  [[290.0, 0.0, 114.0], [290.0, 165.0, 114.0], [240.0, 165.0, 272.0], [240.0, 0.0, 272.0]],
  [[130.0, 0.0, 65.0], [130.0, 165.0, 65.0], [290.0, 165.0, 114.0], [290.0, 0.0, 114.0]],
  [[82.0, 0.0, 225.0], [82.0, 165.0, 225.0], [130.0, 165.0, 65.0], [130.0, 0.0, 65.0]],
  [[240.0, 0.0, 272.0], [240.0, 165.0, 272.0], [82.0, 165.0, 225.0], [82.0, 0.0, 225.0]]
]

[[assets]]
name = "cornell_tall_block"
quads = [
  [[423.0, 330.0, 247.0], [265.0, 330.0, 296.0], [314.0, 330.0, 456.0], [472.0, 330.0, 406.0]],
  [[423.0, 0.0, 247.0], [423.0, 330.0, 247.0], [472.0, 330.0, 406.0], [472.0, 0.0, 406.0]],
  [[472.0, 0.0, 406.0], [472.0, 330.0, 406.0], [314.0, 330.0, 456.0], [314.0, 0.0, 456.0]],
  [[314.0, 0.0, 456.0], [314.0, 330.0, 456.0], [265.0, 330.0, 296.0], [265.0, 0.0, 296.0]],
  [[265.0, 0.0, 296.0], [265.0, 330.0, 296.0], [423.0, 330.0, 247.0], [423.0, 0.0, 247.0]]
]

[[assets]]
name = "cornell_light_panel"
quads = [
  [[343.0, 548.8, 227.0], [343.0, 548.8, 332.0], [213.0, 548.8, 332.0], [213.0, 548.8, 227.0]]
]

[[instances]]
asset = "cornell_room_white"
material = "cornell_white"

[[instances]]
asset = "cornell_green_wall"
material = "cornell_green"

[[instances]]
asset = "cornell_red_wall"
material = "cornell_red"

[[instances]]
asset = "cornell_short_block"
material = "cornell_white"

[[instances]]
asset = "cornell_tall_block"
material = "cornell_white"

[[instances]]
asset = "cornell_light_panel"
material = "cornell_light"

[[lights]]
type = "area"
position = [278.0, 548.8, 279.5]
size = [130.0, 105.0]
radiance = [17.0, 12.0, 4.0]

[environment]
type = "constant"
radiance = [0, 0, 0]
strength = 1.0
```

- [ ] **Step 2: Add Cornell CLI render test**

In `CMakeLists.txt`, after `yaoray_cli_render_obj`, add:

```cmake
    add_test(NAME yaoray_cli_render_cornell_box
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/cornell_box.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/cornell_box.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 38') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 3: Build and run full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CTest reports `11/11` tests passed after the new `yaoray_cli_render_cornell_box` test is added.

- [ ] **Step 4: Manually render the Cornell scene**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\cornell_box.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\cornell_box.png")
($bytes[0..7] | ForEach-Object { $_.ToString("X2") }) -join " "
```

Expected output includes:

```text
Scene parsed successfully: scenes/examples/cornell_box.toml
Scene compiled successfully.
Compiled triangles: 38
Rendered image: scenes/examples/out/cornell_box.png
89 50 4E 47 0D 0A 1A 0A
```

- [ ] **Step 5: Commit Cornell scene and CLI test**

Run:

```powershell
git add CMakeLists.txt scenes/examples/cornell_box.toml
git commit -m "feat: add cornell box example scene"
```

## Task 4: Update Documentation And Final Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README**

In `README.md`, under "Current Status", after:

```markdown
- TOML named diffuse/emissive materials with instance material binding
```

add:

```markdown
- scene-authored inline quad assets and a Cornell Box geometry smoke scene
```

Change the future-work sentence from:

```markdown
Final path tracing quality, texture import, imported asset materials, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

to:

```markdown
Final path tracing quality, spectral rendering, texture import, imported asset materials, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

In the "Run" command block, after:

```powershell
build\Debug\yaoray.exe render scenes\examples\obj_pyramid.toml --backend cpu
```

add:

```powershell
build\Debug\yaoray.exe render scenes\examples\cornell_box.toml --backend cpu
```

Change the paragraph after the run block from:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU direct-lighting images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default and include named diffuse materials plus simple center-sampled area lights. This is still a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

to:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU direct-lighting images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default and include named diffuse materials plus simple center-sampled area lights. The Cornell Box example uses Cornell's measured geometry with RGB material approximations; it is a geometry and pipeline smoke scene, not a physically matched spectral render. This is still a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 2: Update architecture docs**

In `docs/architecture/overview.md`, under "Current implemented slices", after:

```markdown
- TOML named diffuse/emissive materials with instance-level material binding
```

add:

```markdown
- scene-authored inline quad assets and a Cornell Box example based on Cornell measured geometry
```

After the OBJ importer paragraph:

```markdown
The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It ignores OBJ `.mtl` files, textures, UVs, imported normals, and smoothing data in this slice; scene-authored named materials can still bind to a whole imported instance.
```

add:

```markdown
Inline quad assets let TOML scenes define small measured or hand-authored quad meshes directly. The Cornell Box example uses this path so the official measured vertices stay visible in the scene file. Its materials are current RGB diffuse/emissive approximations; spectral matching remains future work.
```

Change:

```markdown
Texture import, imported material files, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

to:

```markdown
Spectral rendering, texture import, imported material files, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 3: Run documentation and scope checks**

Run:

```powershell
rg -n "Cornell|inline quad|quads|spectral|path tracer|physical|RGB" README.md docs/architecture/overview.md scenes/examples/cornell_box.toml docs/superpowers/specs/2026-05-15-yaoray-cornell-box-foundation-design.md
rg -n "spectral rendering|spectral-to|BRDF import|color bleeding|Russian roulette|MIS|texture|gltf|mtl|per-face" include src tests scenes README.md docs/architecture/overview.md
```

Expected:

- README and architecture docs describe Cornell as implemented geometry/pipeline support.
- Spectral rendering and path tracing appear only as future work or non-goals.
- No production source files contain new spectral, texture, glTF, `.mtl`, or per-face material implementation.

- [ ] **Step 4: Run fresh full verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CTest reports `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 5: Re-render Cornell and check PNG signature**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\cornell_box.png) { Remove-Item -LiteralPath scenes\examples\out\cornell_box.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\cornell_box.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\cornell_box.png")
($bytes[0..7] | ForEach-Object { $_.ToString("X2") }) -join " "
```

Expected output includes:

```text
Compiled triangles: 38
Rendered image: scenes/examples/out/cornell_box.png
89 50 4E 47 0D 0A 1A 0A
```

- [ ] **Step 6: Commit documentation updates**

Run:

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: document cornell box foundation"
```

- [ ] **Step 7: Final git state check**

Run:

```powershell
git status --short --branch
git log --oneline --decorate --max-count 8
```

Expected:

- Working tree is clean.
- Latest commits include:
  - `docs: document cornell box foundation`
  - `feat: add cornell box example scene`
  - `feat: compile inline quad assets`
  - `feat: parse inline quad assets`

## Self-Review Checklist

- Spec coverage:
  - `[[assets]].quads` parser schema: Task 1.
  - path-vs-quads exclusivity: Task 1.
  - quad tessellation into two triangles: Task 2.
  - material binding and transforms for inline quads: Task 2.
  - degenerate quad diagnostics: Task 2.
  - Cornell scene with official measured geometry and RGB approximations: Task 3.
  - CLI render smoke test with `Compiled triangles: 38` and PNG signature: Task 3.
  - docs that state spectral/path-traced matching is future work: Task 4.

- Type consistency:
  - Semantic quad type is `QuadDescription`.
  - Inline asset storage is `AssetDescription::quads`.
  - TOML field is `assets.quads`.
  - Compiler diagnostic field for degenerate inline quads is `assets.quads`.
  - Scene file path is `scenes/examples/cornell_box.toml`.

- Scope boundaries:
  - Do not implement spectral rendering.
  - Do not implement path tracing.
  - Do not implement polygon holes.
  - Do not implement per-face materials.
  - Do not change OBJ `.mtl`, glTF, texture, or imported material behavior.

