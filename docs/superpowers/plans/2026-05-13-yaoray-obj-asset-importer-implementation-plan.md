# YaoRay OBJ Asset Importer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add geometry-only Wavefront OBJ import so scene assets can compile into multiple `RenderScene::triangles` and render through the existing CPU debug backend.

**Architecture:** Add a focused `yaoray_assets` library that depends only on `yaoray_core` and vendored tinyobjloader. `yaoray_render` calls `LoadObjMesh()` during `CompileScene()`, converts loader errors into `SceneDiagnostic`, applies existing instance transforms, and expands imported geometry into world-space triangles. The app, film, backend, and scene parser layers do not learn OBJ parsing details.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC, existing YaoRay modules, tinyobjloader single-header OBJ parser vendored under `external/tinyobjloader/`.

---

## Scope Check

This plan implements only the approved OBJ Asset Importer design:

- vendor tinyobjloader
- create `yaoray_assets`
- implement `LoadObjMesh()`
- load positions and faces from `.obj`
- use tinyobjloader triangulation
- ignore materials, UVs, imported normals, smoothing groups, textures, and vertex colors
- integrate `.obj` assets into `CompileScene()`
- keep `builtin:triangle` behavior intact
- add compiler and CLI coverage for OBJ scenes
- update docs

It does not implement BVH, path tracing changes, CUDA asset upload, glTF/GLB, `.mtl` import, texture loading, persistent asset caching, or a large showcase scene.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
external/tinyobjloader/README.md
external/tinyobjloader/tiny_obj_loader.h
include/yaoray/assets/obj_loader.hpp
src/assets/obj_loader.cpp
src/render/scene_compiler.cpp
scenes/examples/assets/pyramid.obj
scenes/examples/obj_pyramid.toml
tests/assets_tests.cpp
tests/fixtures/assets/empty.obj
tests/fixtures/assets/not_obj.txt
tests/fixtures/assets/quad.obj
tests/fixtures/assets/triangle.obj
tests/fixtures/scene/obj_quad.toml
tests/render_scene_tests.cpp
```

Responsibilities:

- `external/tinyobjloader/tiny_obj_loader.h`: vendored third-party OBJ parser.
- `external/tinyobjloader/README.md`: source, license, retrieval, and usage notes for the vendored header.
- `include/yaoray/assets/obj_loader.hpp`: public imported mesh types and `LoadObjMesh()`.
- `src/assets/obj_loader.cpp`: tinyobjloader integration, OBJ geometry extraction, normal computation, and loader diagnostics.
- `src/render/scene_compiler.cpp`: converts `.obj` assets into world-space `RenderTriangle` data and wraps loader errors as `SceneDiagnostic`.
- `tests/assets_tests.cpp`: focused unit tests for OBJ loader behavior.
- `tests/render_scene_tests.cpp`: scene compiler integration tests for OBJ assets and transforms.
- `tests/fixtures/assets/*`: tiny OBJ/text fixtures.
- `tests/fixtures/scene/obj_quad.toml`: CLI and parser-facing OBJ scene fixture.
- `scenes/examples/*`: small human-facing OBJ example that remains renderable without BVH.

## Task 1: Add OBJ Loader Module With Tests

**Files:**
- Create: `external/tinyobjloader/README.md`
- Create: `external/tinyobjloader/tiny_obj_loader.h`
- Create: `include/yaoray/assets/obj_loader.hpp`
- Create: `src/assets/obj_loader.cpp`
- Create: `tests/assets_tests.cpp`
- Create: `tests/fixtures/assets/empty.obj`
- Create: `tests/fixtures/assets/not_obj.txt`
- Create: `tests/fixtures/assets/quad.obj`
- Create: `tests/fixtures/assets/triangle.obj`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add OBJ fixture files**

Create `tests/fixtures/assets/triangle.obj`:

```text
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
f 1 2 3
```

Create `tests/fixtures/assets/quad.obj`:

```text
v -0.5 -0.5 0.0
v 0.5 -0.5 0.0
v 0.5 0.5 0.0
v -0.5 0.5 0.0
f 1 2 3 4
```

Create `tests/fixtures/assets/empty.obj`:

```text
# Deliberately contains no faces.
```

Create `tests/fixtures/assets/not_obj.txt`:

```text
v 0.0 0.0 0.0
```

- [ ] **Step 2: Write failing OBJ loader tests**

Create `tests/assets_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <yaoray/assets/obj_loader.hpp>

#ifndef YAORAY_TEST_DATA_DIR
#error "YAORAY_TEST_DATA_DIR must be defined"
#endif

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

bool ErrorContains(const yr::AssetLoadResult& result, std::string_view text) {
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(obj_loader_loads_triangle_obj) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});

    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, -0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p1.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_obj) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});
}

YR_TEST(obj_loader_rejects_non_obj_extension) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".obj"));
}

YR_TEST(obj_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/missing.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "OBJ file not found"));
}

YR_TEST(obj_loader_returns_error_when_obj_has_no_triangles) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/empty.obj"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "no triangles"));
}
```

- [ ] **Step 3: Add CMake wiring for the new asset module**

Modify `CMakeLists.txt` so the third-party and asset targets appear after `tomlplusplus` and before `yaoray_render`:

```cmake
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE external/tinyobjloader)

add_library(yaoray_assets STATIC
    src/assets/obj_loader.cpp
)
target_include_directories(yaoray_assets PUBLIC include)
target_link_libraries(yaoray_assets PUBLIC yaoray_core PRIVATE tinyobjloader)
```

Keep `yaoray_scene` as it is. Update the render target link line:

```cmake
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets)
```

Add `tests/assets_tests.cpp` to `yaoray_tests`:

```cmake
    tests/assets_tests.cpp
```

Update the `yaoray_tests` link line:

```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_assets yaoray_backends)
```

Add this compile definition after the `yaoray_tests` link line:

```cmake
target_compile_definitions(yaoray_tests PRIVATE YAORAY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
```

- [ ] **Step 4: Run build to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: configure or build fails because `src/assets/obj_loader.cpp`, `include/yaoray/assets/obj_loader.hpp`, and `external/tinyobjloader/tiny_obj_loader.h` do not exist yet.

Do not commit this failing state.

- [ ] **Step 5: Vendor tinyobjloader**

Create `external/tinyobjloader/` and download the header:

```powershell
New-Item -ItemType Directory -Force external\tinyobjloader
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/release/tiny_obj_loader.h" -OutFile "external\tinyobjloader\tiny_obj_loader.h"
```

Create `external/tinyobjloader/README.md`:

```markdown
# tinyobjloader

Vendored header for geometry-only Wavefront OBJ loading.

- Source repository: https://github.com/tinyobjloader/tinyobjloader
- Header URL: https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/release/tiny_obj_loader.h
- Retrieved: 2026-05-13
- License: MIT, as stated in `tiny_obj_loader.h`

YaoRay uses tinyobjloader only in `src/assets/obj_loader.cpp` for OBJ position and face loading. Materials, textures, UVs, imported normals, and smoothing groups are intentionally ignored in the first OBJ importer slice.
```

- [ ] **Step 6: Add the OBJ loader public API**

Create `include/yaoray/assets/obj_loader.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
};

struct AssetLoadResult {
    std::optional<ImportedMesh> mesh;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 7: Implement `LoadObjMesh()`**

Create `src/assets/obj_loader.cpp`:

```cpp
#include <yaoray/assets/obj_loader.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace yr {
namespace {

constexpr float DegenerateTriangleEpsilon = 1.0e-12f;

bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
}

Point3f ReadPosition(const tinyobj::attrib_t& attrib, int vertex_index, bool& ok) {
    if (vertex_index < 0) {
        ok = false;
        return {};
    }

    const std::size_t base = static_cast<std::size_t>(vertex_index) * 3;
    if (base + 2 >= attrib.vertices.size()) {
        ok = false;
        return {};
    }

    return Point3f{
        attrib.vertices[base + 0],
        attrib.vertices[base + 1],
        attrib.vertices[base + 2]
    };
}

void AddIfNotEmpty(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty()) {
        values.push_back(value);
    }
}

} // namespace

AssetLoadResult LoadObjMesh(const std::filesystem::path& path) {
    AssetLoadResult result;

    if (!HasObjExtension(path)) {
        result.errors.push_back("OBJ asset path must use a .obj extension: " + path.generic_string());
        return result;
    }

    if (!std::filesystem::exists(path)) {
        result.errors.push_back("OBJ file not found: " + path.generic_string());
        return result;
    }

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;
    config.mtl_search_path = path.parent_path().string();

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string(), config)) {
        AddIfNotEmpty(result.errors, reader.Error());
        if (result.errors.empty()) {
            result.errors.push_back("failed to parse OBJ file: " + path.generic_string());
        }
        AddIfNotEmpty(result.warnings, reader.Warning());
        return result;
    }

    AddIfNotEmpty(result.warnings, reader.Warning());

    ImportedMesh mesh;
    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    for (const tinyobj::shape_t& shape : shapes) {
        std::size_t index_offset = 0;
        for (std::size_t face_index = 0; face_index < shape.mesh.num_face_vertices.size(); ++face_index) {
            const int face_vertices = shape.mesh.num_face_vertices[face_index];
            if (face_vertices != 3) {
                result.warnings.push_back("skipping non-triangle OBJ face after triangulation: " + path.generic_string());
                index_offset += static_cast<std::size_t>(face_vertices);
                continue;
            }

            if (index_offset + 2 >= shape.mesh.indices.size()) {
                result.errors.push_back("OBJ face index data is incomplete: " + path.generic_string());
                return result;
            }

            bool positions_ok = true;
            const Point3f p0 = ReadPosition(attrib, shape.mesh.indices[index_offset + 0].vertex_index, positions_ok);
            const Point3f p1 = ReadPosition(attrib, shape.mesh.indices[index_offset + 1].vertex_index, positions_ok);
            const Point3f p2 = ReadPosition(attrib, shape.mesh.indices[index_offset + 2].vertex_index, positions_ok);
            index_offset += 3;

            if (!positions_ok) {
                result.errors.push_back("OBJ face references an invalid vertex index: " + path.generic_string());
                return result;
            }

            const Vec3f normal = Normalize(Cross(p1 - p0, p2 - p0));
            if (LengthSquared(normal) <= DegenerateTriangleEpsilon) {
                result.warnings.push_back("skipping degenerate OBJ triangle: " + path.generic_string());
                continue;
            }

            mesh.triangles.push_back(ImportedTriangle{p0, p1, p2, normal});
        }
    }

    if (mesh.triangles.empty()) {
        result.errors.push_back("OBJ mesh contains no triangles: " + path.generic_string());
        return result;
    }

    result.mesh = std::move(mesh);
    return result;
}

} // namespace yr
```

- [ ] **Step 8: Run asset tests and full tests**

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

- [ ] **Step 9: Commit**

Run:

```powershell
git add CMakeLists.txt external/tinyobjloader include/yaoray/assets src/assets tests/assets_tests.cpp tests/fixtures/assets
git commit -m "feat: add obj asset loader"
```

## Task 2: Integrate OBJ Assets Into Scene Compilation

**Files:**
- Create: `tests/fixtures/scene/obj_quad.toml`
- Modify: `CMakeLists.txt`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add an OBJ scene fixture**

Create `tests/fixtures/scene/obj_quad.toml`:

```toml
[render]
backend = "cpu"
width = 64
height = 32

[film]
output = "out/obj_quad.ppm"

[camera]
type = "perspective"
position = [0, 0, 4]
target = [0, 0, 0]
fov_y = 60

[[assets]]
name = "quad"
path = "../assets/quad.obj"

[[instances]]
asset = "quad"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[environment]
type = "constant"
radiance = [0.02, 0.02, 0.02]
```

- [ ] **Step 2: Add failing scene compiler OBJ tests**

Modify `tests/render_scene_tests.cpp` includes:

```cpp
#include <filesystem>
```

Add this helper near the existing `DiagnosticsContain()` helper:

```cpp
std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}
```

Append these tests to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_expands_obj_asset) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_NEAR(result.scene.value().triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(scene_compiler_applies_obj_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/triangle.obj")});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});

    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 2.0, 1e-5);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-5);
}

YR_TEST(scene_compiler_expands_two_obj_instances) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    yr::InstanceDescription second;
    second.asset = "quad";
    second.transform.translate = yr::Vec3f{2.0f, 0.0f, 0.0f};
    scene.instances.push_back(second);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{4});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_EQ(result.scene.value().triangles[2].material_index, 1);
    YR_EXPECT_NEAR(result.scene.value().triangles[2].p0.x, 1.5, 1e-6);
}
```

- [ ] **Step 3: Add a failing CLI OBJ render test**

Add this CTest case after `yaoray_cli_render_cpu` in `CMakeLists.txt`:

```cmake
    add_test(NAME yaoray_cli_render_obj
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
```

- [ ] **Step 4: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: unit or CLI tests fail because `CompileScene()` still reports `asset import not implemented yet` for `.obj` assets.

Do not commit this failing state.

- [ ] **Step 5: Include the asset loader in the scene compiler**

At the top of `src/render/scene_compiler.cpp`, add:

```cpp
#include <yaoray/assets/obj_loader.hpp>
```

Keep the existing includes, and add `<vector>` if it is not already present:

```cpp
#include <vector>
```

- [ ] **Step 6: Add compiler helpers for warnings, OBJ detection, and imported meshes**

In the anonymous namespace in `src/render/scene_compiler.cpp`, after `Error()`, add:

```cpp
SceneDiagnostic Warning(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, scene.source_path, std::move(field), std::move(message)};
}

bool HasObjExtension(const std::filesystem::path& path) {
    return path.extension() == ".obj";
}
```

After `AppendBuiltinTriangle()`, add:

```cpp
void AppendImportedMesh(RenderScene& compiled, const ImportedMesh& mesh, const TransformDescription& transform) {
    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});

    for (const ImportedTriangle& triangle : mesh.triangles) {
        const Point3f world_p0 = ApplyTransform(triangle.p0, transform);
        const Point3f world_p1 = ApplyTransform(triangle.p1, transform);
        const Point3f world_p2 = ApplyTransform(triangle.p2, transform);

        compiled.triangles.push_back(RenderTriangle{
            world_p0,
            world_p1,
            world_p2,
            Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
            material_index
        });
    }
}

void AppendObjAsset(
    const SceneDescription& scene,
    RenderScene& compiled,
    const std::filesystem::path& asset_path,
    const TransformDescription& transform,
    std::unordered_map<std::string, ImportedMesh>& mesh_cache,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const std::string cache_key = asset_path.generic_string();
    auto cached = mesh_cache.find(cache_key);
    if (cached == mesh_cache.end()) {
        AssetLoadResult load_result = LoadObjMesh(asset_path);
        for (const std::string& warning : load_result.warnings) {
            diagnostics.push_back(Warning(scene, "assets.path", warning));
        }
        for (const std::string& error : load_result.errors) {
            diagnostics.push_back(Error(scene, "assets.path", error));
        }
        if (!load_result.errors.empty()) {
            return;
        }
        if (!load_result.mesh.has_value()) {
            diagnostics.push_back(Error(scene, "assets.path", "OBJ loader returned no mesh: " + cache_key));
            return;
        }
        cached = mesh_cache.emplace(cache_key, std::move(load_result.mesh.value())).first;
    }

    AppendImportedMesh(compiled, cached->second, transform);
}
```

- [ ] **Step 7: Route `.obj` assets through the loader**

In `CompileScene()`, after the `assets` map is built, add a compile-local mesh cache:

```cpp
    std::unordered_map<std::string, ImportedMesh> mesh_cache;
```

Replace the asset handling block:

```cpp
        const std::string asset_path = asset->second.generic_string();
        if (asset_path == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform);
        } else {
            result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path));
        }
```

with:

```cpp
        const std::filesystem::path& asset_path = asset->second;
        const std::string asset_path_string = asset_path.generic_string();
        if (asset_path_string == "builtin:triangle") {
            AppendBuiltinTriangle(compiled, instance.transform);
        } else if (HasObjExtension(asset_path)) {
            AppendObjAsset(scene, compiled, asset_path, instance.transform, mesh_cache, result.diagnostics);
        } else {
            result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path_string));
        }
```

- [ ] **Step 8: Run tests and manual OBJ CLI check**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
.\build\yaoray.exe render tests\fixtures\scene\obj_quad.toml --backend cpu
```

Expected CTest result:

```text
100% tests passed
```

Expected manual OBJ output includes:

```text
Compiled triangles: 2
Rendered image: tests/fixtures/scene/out/obj_quad.ppm
```

If the configured generator places the executable under `build\Debug\yaoray.exe`, use that path for the manual check.

- [ ] **Step 9: Commit**

Run:

```powershell
git add CMakeLists.txt src/render/scene_compiler.cpp tests/render_scene_tests.cpp tests/fixtures/scene/obj_quad.toml
git commit -m "feat: compile obj assets into render scenes"
```

## Task 3: Add Example Scene And Documentation

**Files:**
- Create: `scenes/examples/assets/pyramid.obj`
- Create: `scenes/examples/obj_pyramid.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add a small human-facing OBJ example**

Create `scenes/examples/assets/pyramid.obj`:

```text
v 0.0 1.0 0.0
v -1.0 -1.0 -1.0
v 1.0 -1.0 -1.0
v 1.0 -1.0 1.0
v -1.0 -1.0 1.0
f 1 2 3
f 1 3 4
f 1 4 5
f 1 5 2
f 2 5 4
f 2 4 3
```

Create `scenes/examples/obj_pyramid.toml`:

```toml
[render]
backend = "cpu"
width = 640
height = 360
spp = 1
max_depth = 5
seed = 1

[film]
output = "out/obj_pyramid.ppm"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0, 0.3, 5]
target = [0, 0, 0]
fov_y = 45

[[assets]]
name = "pyramid"
path = "assets/pyramid.obj"

[[instances]]
asset = "pyramid"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[environment]
type = "constant"
radiance = [0.02, 0.025, 0.03]
strength = 1.0
```

- [ ] **Step 2: Update README status and run instructions**

In `README.md`, add this status bullet:

```markdown
- geometry-only OBJ asset import for small mesh scenes
```

Update the future-work sentence so it says:

```markdown
Final path tracing quality, material and texture import, BVH construction, PNG output, glTF/GLB import, and real CUDA backend support are planned as separate implementation slices.
```

Add this run command:

```powershell
build\Debug\yaoray.exe render scenes\examples\obj_pyramid.toml --backend cpu
```

Update the run description:

```markdown
The `render` command currently parses, compiles, and renders CPU debug images to ASCII PPM. It supports the built-in triangle and small geometry-only OBJ meshes. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 3: Update architecture overview**

In `docs/architecture/overview.md`, update the implemented slices list with:

```markdown
- geometry-only OBJ asset import through the `yaoray_assets` module
```

Add this paragraph after the CPU debug renderer paragraph:

```markdown
The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It ignores materials, textures, UVs, imported normals, and smoothing data in this slice; those concerns are deferred until the asset and material boundaries are more mature.
```

Update the future-work sentence so it says:

```markdown
BVH construction, PNG output, material and texture import, glTF/GLB import, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 4: Run docs smoke check, tests, and example render**

Run:

```powershell
rg -n "OBJ|yaoray_assets|geometry-only|BVH|glTF" README.md docs/architecture/overview.md
ctest --test-dir build --output-on-failure -C Debug
.\build\yaoray.exe render scenes\examples\obj_pyramid.toml --backend cpu
Get-Content -Path scenes\examples\out\obj_pyramid.ppm -TotalCount 4
```

Expected CTest result:

```text
100% tests passed
```

Expected example output includes:

```text
Compiled triangles: 6
Rendered image: scenes/examples/out/obj_pyramid.ppm
```

Expected PPM header:

```text
P3
640 360
255
```

If the configured generator places the executable under `build\Debug\yaoray.exe`, use that path for the manual check.

- [ ] **Step 5: Commit**

Run:

```powershell
git add README.md docs/architecture/overview.md scenes/examples/assets/pyramid.obj scenes/examples/obj_pyramid.toml
git commit -m "docs: describe obj asset import"
```

## Task 4: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm dependency direction**

Run:

```powershell
rg -n "yaoray/assets|tiny_obj_loader|LoadObjMesh" include\yaoray\scene src\scene include\yaoray\backends src\backends src\app
rg -n "yaoray/assets|LoadObjMesh" include\yaoray\render src\render
```

Expected:

- First command prints no output.
- Second command shows only render-layer usage, especially `src/render/scene_compiler.cpp`.

- [ ] **Step 2: Confirm API discoverability and tinyobjloader vendoring**

Run:

```powershell
rg -n "ImportedTriangle|ImportedMesh|AssetLoadResult|LoadObjMesh|tinyobjloader|TINYOBJLOADER_IMPLEMENTATION" include src tests external
```

Expected: matches appear in the asset public header, loader implementation, tests, and vendored README/header.

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

- [ ] **Step 4: Verify built-in triangle still renders**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\minimal.ppm) { Remove-Item -LiteralPath scenes\examples\out\minimal.ppm -Force }
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cpu
Get-Content -Path scenes\examples\out\minimal.ppm -TotalCount 4
```

Expected output includes:

```text
Compiled triangles: 1
Rendered image: scenes/examples/out/minimal.ppm
```

Expected PPM header:

```text
P3
640 360
255
```

- [ ] **Step 5: Verify OBJ fixture and example render**

Run:

```powershell
if (Test-Path -LiteralPath tests\fixtures\scene\out\obj_quad.ppm) { Remove-Item -LiteralPath tests\fixtures\scene\out\obj_quad.ppm -Force }
.\build\yaoray.exe render tests\fixtures\scene\obj_quad.toml --backend cpu
Get-Content -Path tests\fixtures\scene\out\obj_quad.ppm -TotalCount 4
if (Test-Path -LiteralPath scenes\examples\out\obj_pyramid.ppm) { Remove-Item -LiteralPath scenes\examples\out\obj_pyramid.ppm -Force }
.\build\yaoray.exe render scenes\examples\obj_pyramid.toml --backend cpu
Get-Content -Path scenes\examples\out\obj_pyramid.ppm -TotalCount 4
```

Expected OBJ fixture output includes:

```text
Compiled triangles: 2
Rendered image: tests/fixtures/scene/out/obj_quad.ppm
```

Expected OBJ example output includes:

```text
Compiled triangles: 6
Rendered image: scenes/examples/out/obj_pyramid.ppm
```

Both PPM headers should begin:

```text
P3
```

- [ ] **Step 6: Verify CUDA and unsupported asset failures**

Run:

```powershell
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cuda
.\build\yaoray.exe render tests\fixtures\scene\minimal.toml --backend cpu
```

Expected first command exits non-zero and includes:

```text
CUDA backend not implemented yet.
```

Expected second command exits non-zero and includes:

```text
asset import not implemented yet
```

This confirms the CUDA stub and unsupported `.glb` asset diagnostic still behave as before.

- [ ] **Step 7: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- Vendored tinyobjloader: Task 1.
- `yaoray_assets` module and CMake target: Task 1.
- `LoadObjMesh()` API and loader behavior: Task 1.
- Triangle and quad fixtures: Task 1.
- Missing file, extension, and no-triangle errors: Task 1.
- SceneCompiler `.obj` handling, default material, transforms, and compile-local cache: Task 2.
- CLI OBJ render coverage and `Compiled triangles: N`: Task 2.
- Built-in triangle and `.glb` compatibility: Task 2 and Task 4.
- Human-facing example and documentation: Task 3.
- Final dependency and smoke checks: Task 4.

Type consistency:

- `ImportedTriangle`, `ImportedMesh`, `AssetLoadResult`, and `LoadObjMesh()` live in `include/yaoray/assets/obj_loader.hpp`.
- `LoadObjMesh()` returns plain strings, not `SceneDiagnostic`.
- `SceneCompiler` converts asset loader strings into `SceneDiagnostic` values.
- The asset library target is named `yaoray_assets`.
- The third-party interface target is named `tinyobjloader`.

Implementation guardrails:

- Do not add BVH in this plan.
- Do not add material, texture, UV, imported-normal, smoothing, or `.mtl` support in this plan.
- Do not make `scene`, `backends`, `film`, or `app` depend on `yaoray_assets`.
- Keep `RenderScene` as flat world-space triangles.
- Keep OBJ examples small enough to render with the direct triangle loop.
