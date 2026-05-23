# YaoRay PBRT SceneWorld Frontend v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add direct `.pbrt` scene loading through a new `SceneWorld` frontend layer, with Breakfast documented and downloaded as the first local PBRT benchmark target.

**Architecture:** TOML and PBRT become peer scene frontends that produce `SceneWorld`. The render compiler consumes `SceneWorld` and produces backend-neutral `RenderSceneIR`; CPU preparation and rendering remain unchanged except for receiving scenes through the new frontend path. PBRT support is a practical subset with inline triangle meshes, PLY meshes, transform stack handling, and approximate material lowering.

**Tech Stack:** C++20, CMake, existing `yr_test` test harness, existing `SceneDiagnostic`, `AssetResource`, `RenderSceneIR`, CPU backend, PowerShell for local Breakfast asset setup.

---

## File Structure

- Create `include/yaoray/scene/scene_world.hpp`: shared high-level scene semantic structs and TOML adapter declaration.
- Create `src/scene/scene_world.cpp`: TOML `SceneDescription` to `SceneWorld` adapter.
- Modify `CMakeLists.txt`: add new sources, tests, and libraries.
- Modify `include/yaoray/render/scene_compiler.hpp`: expose `CompileSceneWorld`.
- Modify `src/render/scene_compiler.cpp`: compile from `SceneWorld`, keep `CompileScene(SceneDescription)` as compatibility wrapper.
- Create `include/yaoray/assets/ply_loader.hpp`: PLY loader API.
- Create `src/assets/ply_loader.cpp`: ASCII and binary little-endian PLY to `AssetResource`.
- Modify `tests/assets_tests.cpp`: PLY loader tests.
- Create `tests/fixtures/assets/ply/triangle_ascii.ply`, `quad_ascii.ply`, and `bad_face.ply`.
- Create `include/yaoray/pbrt/pbrt_scene.hpp`: PBRT frontend API.
- Create `src/pbrt/pbrt_scene.cpp`: PBRT tokenizer/parser/frontend.
- Create `tests/pbrt_tests.cpp`: PBRT frontend tests.
- Create `tests/fixtures/pbrt/minimal_triangle.pbrt`, `include_root.pbrt`, `included_shape.pbrt`, and `ply_scene.pbrt`.
- Create `include/yaoray/frontends/scene_frontend.hpp`: extension-based scene frontend dispatcher.
- Create `src/frontends/scene_frontend.cpp`: TOML/PBRT dispatch and backend override helper.
- Modify `src/app/main.cpp`: load `SceneWorld` through dispatcher and render it.
- Modify `tests/run_cli_render_test.cmake` and CMake test registrations only if current helper cannot cover `.pbrt`; otherwise add one `.pbrt` CLI test through the existing helper.
- Create `docs/assets/pbrt-breakfast-local-benchmark.md`: Breakfast source, attribution, local layout, and commands.
- Modify `docs/architecture/overview.md`: update architecture flow from TOML-only to frontend -> `SceneWorld` -> `RenderSceneIR`.

---

### Task 1: Add `SceneWorld` and TOML Adapter

**Files:**
- Create: `include/yaoray/scene/scene_world.hpp`
- Create: `src/scene/scene_world.cpp`
- Create: `tests/scene_world_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing `SceneWorld` adapter tests**

Add `tests/scene_world_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

#include <yaoray/scene/scene.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace {

yr::SceneDescription MakeTomlStyleScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated.toml";
    scene.render.backend = yr::RenderBackendKind::Cpu;
    scene.render.integrator = yr::RenderIntegratorKind::Path;
    scene.render.width = 64;
    scene.render.height = 32;
    scene.render.spp = 2;
    scene.film.output = "out/generated.png";
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 1.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 1.0f, 0.0f};

    scene.materials.push_back(yr::MaterialDescription{
        "white",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.7f, 0.7f, 0.7f},
        yr::Color3f{}
    });
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.material = "white";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    scene.instances.push_back(instance);

    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{0.0f, 3.0f, 0.0f};
    light.area.size = {2.0f, 2.0f};
    light.area.radiance = yr::Color3f{8.0f, 7.0f, 6.0f};
    scene.lights.push_back(light);
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.01f, 0.02f, 0.03f};
    return scene;
}

} // namespace

YR_TEST(scene_world_defaults_are_empty_frontend_output) {
    const yr::SceneWorld world;

    YR_EXPECT_TRUE(world.source_path.empty());
    YR_EXPECT_TRUE(world.source_root.empty());
    YR_EXPECT_EQ(world.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_TRUE(!world.camera.has_value());
    YR_EXPECT_TRUE(world.assets.empty());
    YR_EXPECT_TRUE(world.materials.empty());
    YR_EXPECT_TRUE(world.instances.empty());
    YR_EXPECT_TRUE(world.lights.empty());
}

YR_TEST(scene_world_adapter_preserves_toml_scene_fields) {
    const yr::SceneDescription scene = MakeTomlStyleScene();

    const yr::SceneWorld world = yr::BuildSceneWorld(scene);

    YR_EXPECT_EQ(world.source_path.generic_string(), scene.source_path.generic_string());
    YR_EXPECT_EQ(world.source_root.generic_string(), scene.source_path.parent_path().generic_string());
    YR_EXPECT_EQ(world.render.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_EQ(world.render.width, 64);
    YR_EXPECT_EQ(world.render.height, 32);
    YR_EXPECT_EQ(world.film.output.generic_string(), std::string{"out/generated.png"});
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.y, 1.0, 1e-6);
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.materials[0].name, std::string{"white"});
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].name, std::string{"triangle"});
    YR_EXPECT_EQ(world.assets[0].path.generic_string(), std::string{"builtin:triangle"});
    YR_EXPECT_TRUE(world.assets[0].meshes.empty());
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
    YR_EXPECT_EQ(world.instances[0].asset, std::string{"triangle"});
    YR_EXPECT_EQ(world.instances[0].material, std::string{"white"});
    YR_EXPECT_NEAR(world.instances[0].transform.translate.z, 3.0, 1e-6);
    YR_EXPECT_EQ(world.lights.size(), std::size_t{1});
    YR_EXPECT_NEAR(world.lights[0].area.radiance.x, 8.0, 1e-6);
    YR_EXPECT_EQ(world.environment.type, yr::EnvironmentKind::Constant);
}
```

- [ ] **Step 2: Run the new test target and confirm it fails to compile**

Run:

```powershell
cmake --build build
```

Expected: compile fails because `yaoray/scene/scene_world.hpp` does not exist.

- [ ] **Step 3: Add the `SceneWorld` model**

Create `include/yaoray/scene/scene_world.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneWorldMesh {
    std::string material;
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> texcoords0;
    std::vector<std::uint32_t> indices;
};

struct SceneWorldAsset {
    std::string name;
    std::filesystem::path path;
    std::vector<QuadDescription> quads;
    std::vector<SceneWorldMesh> meshes;
};

struct SceneWorldInstance {
    std::string asset;
    TransformDescription transform;
    std::string material;
};

struct SceneWorld {
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    RenderSettings render;
    FilmSettings film;
    OfflineSettings offline;
    std::optional<CameraDescription> camera;
    std::vector<SceneWorldAsset> assets;
    std::vector<MaterialDescription> materials;
    std::vector<SceneWorldInstance> instances;
    std::vector<LightDescription> lights;
    EnvironmentDescription environment;
};

SceneWorld BuildSceneWorld(const SceneDescription& scene);

} // namespace yr
```

- [ ] **Step 4: Implement the TOML adapter**

Create `src/scene/scene_world.cpp`:

```cpp
#include <yaoray/scene/scene_world.hpp>

#include <utility>

namespace yr {

SceneWorld BuildSceneWorld(const SceneDescription& scene) {
    SceneWorld world;
    world.source_path = scene.source_path;
    world.source_root = scene.source_path.parent_path();
    world.render = scene.render;
    world.film = scene.film;
    world.offline = scene.offline;
    world.camera = scene.camera;
    world.materials = scene.materials;
    world.lights = scene.lights;
    world.environment = scene.environment;

    world.assets.reserve(scene.assets.size());
    for (const AssetDescription& asset : scene.assets) {
        SceneWorldAsset world_asset;
        world_asset.name = asset.name;
        world_asset.path = asset.path;
        world_asset.quads = asset.quads;
        world.assets.push_back(std::move(world_asset));
    }

    world.instances.reserve(scene.instances.size());
    for (const InstanceDescription& instance : scene.instances) {
        SceneWorldInstance world_instance;
        world_instance.asset = instance.asset;
        world_instance.transform = instance.transform;
        world_instance.material = instance.material;
        world.instances.push_back(world_instance);
    }

    return world;
}

} // namespace yr
```

- [ ] **Step 5: Add the source and test to CMake**

Modify `CMakeLists.txt`:

```cmake
add_library(yaoray_scene STATIC
    src/scene/diagnostic.cpp
    src/scene/scene.cpp
    src/scene/scene_parser.cpp
    src/scene/scene_world.cpp
)
```

Add `tests/scene_world_tests.cpp` to `yaoray_tests`:

```cmake
add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/scene_world_tests.cpp
    tests/render_scene_tests.cpp
    tests/bsdf_tests.cpp
    tests/environment_tests.cpp
    tests/mis_tests.cpp
    tests/light_sampling_tests.cpp
    tests/texture_tests.cpp
    tests/backend_tests.cpp
    tests/cpu_material_tests.cpp
    tests/cpu_surface_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
    tests/cpu_tile_scheduler_tests.cpp
    tests/cpu_sampler_tests.cpp
    tests/cpu_path_tracer_tests.cpp
)
```

- [ ] **Step 6: Run tests for the adapter**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: build succeeds and all tests pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add include/yaoray/scene/scene_world.hpp src/scene/scene_world.cpp tests/scene_world_tests.cpp CMakeLists.txt
git commit -m "feat: add scene world frontend model"
```

---

### Task 2: Compile `SceneWorld` to `RenderSceneIR`

**Files:**
- Modify: `include/yaoray/render/scene_compiler.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add a failing direct `SceneWorld` compiler test**

Append to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_accepts_scene_world_directly) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});
    const yr::SceneWorld world = yr::BuildSceneWorld(scene);

    const yr::SceneCompileResult result = yr::CompileSceneWorld(world);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().vertices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().indices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives.size(), std::size_t{1});
}

YR_TEST(scene_compiler_expands_scene_world_inline_mesh) {
    yr::SceneWorld world;
    world.source_path = "tests/fixtures/pbrt/generated.pbrt";
    world.render.width = 64;
    world.render.height = 32;
    world.camera = yr::CameraDescription{};
    world.camera->position = yr::Point3f{0.0f, 0.0f, 3.0f};
    world.camera->target = yr::Point3f{0.0f, 0.0f, 0.0f};
    world.materials.push_back(yr::MaterialDescription{
        "white",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.7f, 0.7f, 0.7f},
        yr::Color3f{}
    });
    yr::SceneWorldMesh mesh;
    mesh.material = "white";
    mesh.positions = {
        yr::Point3f{-0.5f, 0.0f, 0.0f},
        yr::Point3f{0.5f, 0.0f, 0.0f},
        yr::Point3f{0.0f, 1.0f, 0.0f}
    };
    mesh.indices = {0, 1, 2};
    yr::SceneWorldAsset asset;
    asset.name = "mesh";
    asset.meshes.push_back(mesh);
    world.assets.push_back(asset);
    world.instances.push_back(yr::SceneWorldInstance{"mesh", {}, ""});

    const yr::SceneCompileResult result = yr::CompileSceneWorld(world);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles[0].material_index, 0);
    YR_EXPECT_TRUE(!result.scene.value().triangles[0].has_uv);
}
```

Add this include near the existing scene includes:

```cpp
#include <yaoray/scene/scene_world.hpp>
```

- [ ] **Step 2: Run the tests and confirm missing API failure**

Run:

```powershell
cmake --build build
```

Expected: compile fails because `CompileSceneWorld` is not declared.

- [ ] **Step 3: Expose `CompileSceneWorld`**

Modify `include/yaoray/render/scene_compiler.hpp`:

```cpp
#include <yaoray/scene/scene_world.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderSceneIR> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompileSceneWorld(const SceneWorld& scene);
SceneCompileResult CompileScene(const SceneDescription& scene);

} // namespace yr
```

- [ ] **Step 4: Move compiler internals to `SceneWorld`**

Modify `src/render/scene_compiler.cpp`:

```cpp
#include <yaoray/scene/scene_world.hpp>
```

Change helper functions that only read common scene fields from `const SceneDescription&` to `const SceneWorld&`:

```cpp
SceneDiagnostic Error(const SceneWorld& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

SceneDiagnostic Warning(const SceneWorld& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, scene.source_path, std::move(field), std::move(message)};
}
```

Add a direct inline mesh append path:

```cpp
bool AppendSceneWorldMesh(
    const SceneWorld& scene,
    RenderSceneIR& compiled,
    const SceneWorldMesh& mesh,
    const TransformDescription& transform,
    std::optional<int> override_material_index,
    const std::unordered_map<std::string, int>& materials,
    int& fallback_material_index,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (mesh.indices.size() % 3 != 0) {
        diagnostics.push_back(Error(scene, "assets.meshes.indices", "mesh index count is not divisible by three"));
        return false;
    }
    if (!mesh.normals.empty() && mesh.normals.size() != mesh.positions.size()) {
        diagnostics.push_back(Error(scene, "assets.meshes.normals", "mesh normal count does not match positions"));
        return false;
    }
    if (!mesh.texcoords0.empty() && mesh.texcoords0.size() != mesh.positions.size()) {
        diagnostics.push_back(Error(scene, "assets.meshes.texcoords0", "mesh texcoord count does not match positions"));
        return false;
    }

    std::optional<int> mesh_material_index = override_material_index;
    if (!mesh_material_index.has_value() && !mesh.material.empty()) {
        const auto found = materials.find(mesh.material);
        if (found == materials.end()) {
            diagnostics.push_back(Error(scene, "assets.meshes.material", "references unknown material"));
            return false;
        }
        mesh_material_index = found->second;
    }
    if (!mesh_material_index.has_value()) {
        if (fallback_material_index < 0) {
            fallback_material_index = AddDefaultMaterial(compiled);
        }
        mesh_material_index = fallback_material_index;
    }

    const bool has_normals = mesh.normals.size() == mesh.positions.size();
    const bool has_uv = mesh.texcoords0.size() == mesh.positions.size();
    for (std::size_t offset = 0; offset < mesh.indices.size(); offset += 3) {
        const std::uint32_t i0 = mesh.indices[offset + 0];
        const std::uint32_t i1 = mesh.indices[offset + 1];
        const std::uint32_t i2 = mesh.indices[offset + 2];
        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) {
            diagnostics.push_back(Error(scene, "assets.meshes.indices", "mesh triangle index references an invalid position"));
            return false;
        }

        const Point3f p0 = ApplyTransform(mesh.positions[i0], transform);
        const Point3f p1 = ApplyTransform(mesh.positions[i1], transform);
        const Point3f p2 = ApplyTransform(mesh.positions[i2], transform);
        const Vec3f face_normal = Cross(p1 - p0, p2 - p0);
        if (LengthSquared(face_normal) <= DegenerateTriangleEpsilon) {
            diagnostics.push_back(Warning(scene, "assets.meshes", "skipping degenerate scene world mesh triangle"));
            continue;
        }

        RenderTriangle triangle;
        triangle.p0 = p0;
        triangle.p1 = p1;
        triangle.p2 = p2;
        triangle.normal = Normalize(face_normal);
        triangle.material_index = *mesh_material_index;
        if (has_uv) {
            triangle.uv0 = mesh.texcoords0[i0];
            triangle.uv1 = mesh.texcoords0[i1];
            triangle.uv2 = mesh.texcoords0[i2];
            triangle.has_uv = true;
        }
        if (has_normals) {
            triangle.n0 = ApplyNormalTransform(mesh.normals[i0], transform);
            triangle.n1 = ApplyNormalTransform(mesh.normals[i1], transform);
            triangle.n2 = ApplyNormalTransform(mesh.normals[i2], transform);
            triangle.has_vertex_normals =
                LengthSquared(triangle.n0) > 0.0f &&
                LengthSquared(triangle.n1) > 0.0f &&
                LengthSquared(triangle.n2) > 0.0f;
        }
        AppendRenderTriangle(compiled, triangle);
    }
    return true;
}
```

In the asset loop, use `SceneWorldAsset` and `SceneWorldInstance`. Before path-based import handling, add:

```cpp
        if (!asset_description.meshes.empty()) {
            int fallback_material_index = -1;
            for (const SceneWorldMesh& mesh : asset_description.meshes) {
                if (!AppendSceneWorldMesh(
                        scene,
                        compiled,
                        mesh,
                        instance.transform,
                        material_index,
                        materials,
                        fallback_material_index,
                        result.diagnostics
                    )) {
                    break;
                }
            }
            continue;
        }
```

Rename the current body:

```cpp
SceneCompileResult CompileSceneWorld(const SceneWorld& scene) {
    SceneCompileResult result;
    RenderSceneIR compiled;
    // keep the existing CompileScene body here, reading from SceneWorld fields
}

SceneCompileResult CompileScene(const SceneDescription& scene) {
    return CompileSceneWorld(BuildSceneWorld(scene));
}
```

- [ ] **Step 5: Run compiler tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all tests pass, including existing TOML/OBJ/glTF render compiler tests and new `SceneWorld` tests.

- [ ] **Step 6: Commit**

Run:

```powershell
git add include/yaoray/render/scene_compiler.hpp src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "refactor: compile render scenes from scene world"
```

---

### Task 3: Add PLY Mesh Loading

**Files:**
- Create: `include/yaoray/assets/ply_loader.hpp`
- Create: `src/assets/ply_loader.cpp`
- Create: `tests/fixtures/assets/ply/triangle_ascii.ply`
- Create: `tests/fixtures/assets/ply/quad_ascii.ply`
- Create: `tests/fixtures/assets/ply/bad_face.ply`
- Modify: `tests/assets_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add PLY fixture files**

Create `tests/fixtures/assets/ply/triangle_ascii.ply`:

```text
ply
format ascii 1.0
element vertex 3
property float x
property float y
property float z
property float nx
property float ny
property float nz
property float s
property float t
element face 1
property list uchar int vertex_indices
end_header
-0.5 0 0 0 0 1 0 0
0.5 0 0 0 0 1 1 0
0 1 0 0 0 1 0.5 1
3 0 1 2
```

Create `tests/fixtures/assets/ply/quad_ascii.ply`:

```text
ply
format ascii 1.0
element vertex 4
property float x
property float y
property float z
element face 1
property list uchar int vertex_indices
end_header
0 0 0
1 0 0
1 1 0
0 1 0
4 0 1 2 3
```

Create `tests/fixtures/assets/ply/bad_face.ply`:

```text
ply
format ascii 1.0
element vertex 3
property float x
property float y
property float z
element face 1
property list uchar int vertex_indices
end_header
0 0 0
1 0 0
0 1 0
2 0 1
```

- [ ] **Step 2: Write failing PLY loader tests**

Add to `tests/assets_tests.cpp`:

```cpp
#include <fstream>
#include <vector>

#include <yaoray/assets/ply_loader.hpp>
```

Append tests:

```cpp
YR_TEST(ply_loader_loads_ascii_triangle_with_normals_and_uvs) {
    const yr::AssetLoadResult result = yr::LoadPlyResource(FixturePath("assets/ply/triangle_ascii.ply"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetResource& resource = ResourceValue(result);
    const yr::AssetPrimitive& primitive = FirstPrimitive(resource);
    YR_EXPECT_EQ(resource.scenes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(resource.meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.normals.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.texcoords0.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
    YR_EXPECT_NEAR(primitive.positions[0].x, -0.5, 1e-6);
    YR_EXPECT_NEAR(primitive.normals[0].z, 1.0, 1e-6);
    YR_EXPECT_NEAR(primitive.texcoords0[1].x, 1.0, 1e-6);
}

YR_TEST(ply_loader_triangulates_ascii_quad_face) {
    const yr::AssetLoadResult result = yr::LoadPlyResource(FixturePath("assets/ply/quad_ascii.ply"));

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{4});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{6});
    YR_EXPECT_EQ(primitive.indices[0], std::uint32_t{0});
    YR_EXPECT_EQ(primitive.indices[1], std::uint32_t{1});
    YR_EXPECT_EQ(primitive.indices[2], std::uint32_t{2});
    YR_EXPECT_EQ(primitive.indices[3], std::uint32_t{0});
    YR_EXPECT_EQ(primitive.indices[4], std::uint32_t{2});
    YR_EXPECT_EQ(primitive.indices[5], std::uint32_t{3});
}

YR_TEST(ply_loader_rejects_non_triangle_or_quad_face) {
    const yr::AssetLoadResult result = yr::LoadPlyResource(FixturePath("assets/ply/bad_face.ply"));

    YR_EXPECT_TRUE(!result.resource.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "only triangle and quad faces are supported"));
}

YR_TEST(ply_loader_loads_binary_little_endian_triangle) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "yaoray_ply_tests";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / "triangle_binary.ply";
    {
        std::ofstream out{path, std::ios::binary};
        out << "ply\n";
        out << "format binary_little_endian 1.0\n";
        out << "element vertex 3\n";
        out << "property float x\n";
        out << "property float y\n";
        out << "property float z\n";
        out << "element face 1\n";
        out << "property list uchar int vertex_indices\n";
        out << "end_header\n";
        const float values[] = {-0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        out.write(reinterpret_cast<const char*>(values), sizeof(values));
        const unsigned char count = 3;
        const std::int32_t indices[] = {0, 1, 2};
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        out.write(reinterpret_cast<const char*>(indices), sizeof(indices));
    }

    const yr::AssetLoadResult result = yr::LoadPlyResource(path);

    YR_EXPECT_TRUE(result.resource.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    const yr::AssetPrimitive& primitive = FirstPrimitive(ResourceValue(result));
    YR_EXPECT_EQ(primitive.positions.size(), std::size_t{3});
    YR_EXPECT_EQ(primitive.indices.size(), std::size_t{3});
    YR_EXPECT_NEAR(primitive.positions[2].y, 1.0, 1e-6);
}
```

- [ ] **Step 3: Run tests and confirm missing API failure**

Run:

```powershell
cmake --build build
```

Expected: compile fails because `yaoray/assets/ply_loader.hpp` does not exist.

- [ ] **Step 4: Add the PLY loader API**

Create `include/yaoray/assets/ply_loader.hpp`:

```cpp
#pragma once

#include <filesystem>

#include <yaoray/assets/asset_resource.hpp>

namespace yr {

AssetLoadResult LoadPlyResource(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 5: Implement PLY loading**

Create `src/assets/ply_loader.cpp` with these required behaviors:

```cpp
#include <yaoray/assets/ply_loader.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace yr {
namespace {

enum class PlyFormat {
    Ascii,
    BinaryLittleEndian,
};

struct PlyProperty {
    std::string name;
};

struct PlyHeader {
    PlyFormat format = PlyFormat::Ascii;
    int vertex_count = 0;
    int face_count = 0;
    std::vector<PlyProperty> vertex_properties;
};

void PushError(AssetLoadResult& result, std::string message) {
    result.errors.push_back(std::move(message));
}

bool HasPlyExtension(const std::filesystem::path& path) {
    return path.extension() == ".ply";
}

std::optional<PlyHeader> ReadHeader(std::istream& in, AssetLoadResult& result) {
    std::string line;
    if (!std::getline(in, line) || line != "ply") {
        PushError(result, "PLY file must start with ply header");
        return std::nullopt;
    }

    PlyHeader header;
    std::string active_element;
    while (std::getline(in, line)) {
        if (line == "end_header") {
            return header;
        }
        std::istringstream row{line};
        std::string keyword;
        row >> keyword;
        if (keyword == "format") {
            std::string format;
            row >> format;
            if (format == "ascii") {
                header.format = PlyFormat::Ascii;
            } else if (format == "binary_little_endian") {
                header.format = PlyFormat::BinaryLittleEndian;
            } else {
                PushError(result, "unsupported PLY format: " + format);
                return std::nullopt;
            }
        } else if (keyword == "element") {
            row >> active_element;
            int count = 0;
            row >> count;
            if (active_element == "vertex") {
                header.vertex_count = count;
            } else if (active_element == "face") {
                header.face_count = count;
            }
        } else if (keyword == "property" && active_element == "vertex") {
            std::string type;
            std::string name;
            row >> type >> name;
            header.vertex_properties.push_back(PlyProperty{name});
        }
    }

    PushError(result, "PLY header missing end_header");
    return std::nullopt;
}

AssetResource MakeSingleMeshResource(AssetPrimitive primitive) {
    AssetResource resource;
    AssetMesh mesh;
    mesh.primitives.push_back(std::move(primitive));
    resource.meshes.push_back(std::move(mesh));
    AssetNode node;
    node.mesh = 0;
    resource.nodes.push_back(node);
    AssetScene scene;
    scene.root_nodes.push_back(0);
    resource.scenes.push_back(scene);
    resource.default_scene = 0;
    return resource;
}

} // namespace

AssetLoadResult LoadPlyResource(const std::filesystem::path& path) {
    AssetLoadResult result;
    if (!HasPlyExtension(path)) {
        PushError(result, "expected .ply file");
        return result;
    }
    if (!std::filesystem::exists(path)) {
        PushError(result, "PLY file not found: " + path.generic_string());
        return result;
    }

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        PushError(result, "failed to open PLY file: " + path.generic_string());
        return result;
    }

    std::optional<PlyHeader> header = ReadHeader(in, result);
    if (!header.has_value()) {
        return result;
    }

    AssetPrimitive primitive;
    primitive.topology = AssetPrimitiveTopology::Triangles;
    primitive.positions.reserve(static_cast<std::size_t>(header->vertex_count));

    const auto has_property = [&](const char* name) {
        for (const PlyProperty& property : header->vertex_properties) {
            if (property.name == name) {
                return true;
            }
        }
        return false;
    };
    const bool has_normals = has_property("nx") && has_property("ny") && has_property("nz");
    const bool has_st = has_property("s") && has_property("t");
    const bool has_uv = has_st || (has_property("u") && has_property("v"));
    if (has_normals) {
        primitive.normals.reserve(static_cast<std::size_t>(header->vertex_count));
    }
    if (has_uv) {
        primitive.texcoords0.reserve(static_cast<std::size_t>(header->vertex_count));
    }

    auto append_face = [&](const std::vector<std::uint32_t>& face) {
        if (face.size() != 3 && face.size() != 4) {
            PushError(result, "only triangle and quad faces are supported");
            return false;
        }
        for (std::uint32_t index : face) {
            if (index >= primitive.positions.size()) {
                PushError(result, "PLY face index references an invalid vertex");
                return false;
            }
        }
        primitive.indices.push_back(face[0]);
        primitive.indices.push_back(face[1]);
        primitive.indices.push_back(face[2]);
        if (face.size() == 4) {
            primitive.indices.push_back(face[0]);
            primitive.indices.push_back(face[2]);
            primitive.indices.push_back(face[3]);
        }
        return true;
    };

    if (header->format == PlyFormat::Ascii) {
        std::string line;
        for (int vertex = 0; vertex < header->vertex_count; ++vertex) {
            if (!std::getline(in, line)) {
                PushError(result, "unexpected end of PLY vertex data");
                return result;
            }
            std::istringstream row{line};
            Point3f position;
            Vec3f normal;
            Vec2f uv;
            for (const PlyProperty& property : header->vertex_properties) {
                float value = 0.0f;
                row >> value;
                if (!row) {
                    PushError(result, "invalid PLY vertex row");
                    return result;
                }
                if (property.name == "x") {
                    position.x = value;
                } else if (property.name == "y") {
                    position.y = value;
                } else if (property.name == "z") {
                    position.z = value;
                } else if (property.name == "nx") {
                    normal.x = value;
                } else if (property.name == "ny") {
                    normal.y = value;
                } else if (property.name == "nz") {
                    normal.z = value;
                } else if (property.name == "s" || property.name == "u") {
                    uv.x = value;
                } else if (property.name == "t" || property.name == "v") {
                    uv.y = value;
                }
            }
            primitive.positions.push_back(position);
            if (has_normals) {
                primitive.normals.push_back(normal);
            }
            if (has_uv) {
                primitive.texcoords0.push_back(uv);
            }
        }

        for (int face_index = 0; face_index < header->face_count; ++face_index) {
            if (!std::getline(in, line)) {
                PushError(result, "unexpected end of PLY face data");
                return result;
            }
            std::istringstream row{line};
            int count = 0;
            row >> count;
            std::vector<std::uint32_t> face(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                row >> face[static_cast<std::size_t>(i)];
            }
            if (!row || !append_face(face)) {
                return result;
            }
        }
    } else {
        auto read_float = [&]() {
            float value = 0.0f;
            in.read(reinterpret_cast<char*>(&value), sizeof(value));
            return value;
        };
        auto read_u8 = [&]() {
            unsigned char value = 0;
            in.read(reinterpret_cast<char*>(&value), sizeof(value));
            return value;
        };
        auto read_i32 = [&]() {
            std::int32_t value = 0;
            in.read(reinterpret_cast<char*>(&value), sizeof(value));
            return value;
        };

        for (int vertex = 0; vertex < header->vertex_count; ++vertex) {
            Point3f position;
            Vec3f normal;
            Vec2f uv;
            for (const PlyProperty& property : header->vertex_properties) {
                const float value = read_float();
                if (!in) {
                    PushError(result, "unexpected end of binary PLY vertex data");
                    return result;
                }
                if (property.name == "x") {
                    position.x = value;
                } else if (property.name == "y") {
                    position.y = value;
                } else if (property.name == "z") {
                    position.z = value;
                } else if (property.name == "nx") {
                    normal.x = value;
                } else if (property.name == "ny") {
                    normal.y = value;
                } else if (property.name == "nz") {
                    normal.z = value;
                } else if (property.name == "s" || property.name == "u") {
                    uv.x = value;
                } else if (property.name == "t" || property.name == "v") {
                    uv.y = value;
                }
            }
            primitive.positions.push_back(position);
            if (has_normals) {
                primitive.normals.push_back(normal);
            }
            if (has_uv) {
                primitive.texcoords0.push_back(uv);
            }
        }

        for (int face_index = 0; face_index < header->face_count; ++face_index) {
            const unsigned char count = read_u8();
            std::vector<std::uint32_t> face;
            face.reserve(count);
            for (unsigned char i = 0; i < count; ++i) {
                const std::int32_t index = read_i32();
                if (index < 0) {
                    PushError(result, "PLY face index references an invalid vertex");
                    return result;
                }
                face.push_back(static_cast<std::uint32_t>(index));
            }
            if (!in || !append_face(face)) {
                return result;
            }
        }
    }

    if (primitive.positions.empty()) {
        PushError(result, "PLY file has no vertices");
        return result;
    }
    if (primitive.indices.empty()) {
        PushError(result, "PLY file has no triangles");
        return result;
    }

    result.resource = MakeSingleMeshResource(std::move(primitive));
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Add PLY source to CMake**

Modify `CMakeLists.txt`:

```cmake
add_library(yaoray_assets STATIC
    src/assets/gltf_loader.cpp
    src/assets/obj_loader.cpp
    src/assets/ply_loader.cpp
)
```

- [ ] **Step 7: Run PLY tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all tests pass, including PLY ASCII triangle, ASCII quad triangulation, bad face rejection, and binary little-endian triangle.

- [ ] **Step 8: Commit**

Run:

```powershell
git add include/yaoray/assets/ply_loader.hpp src/assets/ply_loader.cpp tests/assets_tests.cpp tests/fixtures/assets/ply CMakeLists.txt
git commit -m "feat: add ply mesh loader"
```

---

### Task 4: Add Minimal PBRT Frontend

**Files:**
- Create: `include/yaoray/pbrt/pbrt_scene.hpp`
- Create: `src/pbrt/pbrt_scene.cpp`
- Create: `tests/pbrt_tests.cpp`
- Create: `tests/fixtures/pbrt/minimal_triangle.pbrt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add a minimal PBRT fixture**

Create `tests/fixtures/pbrt/minimal_triangle.pbrt`:

```text
Film "rgb" "integer xresolution" [64] "integer yresolution" [32] "string filename" ["out/minimal_pbrt.png"]
Camera "perspective" "float fov" [45]
LookAt 0 0 3  0 0 0  0 1 0
WorldBegin
MakeNamedMaterial "white" "string type" ["matte"] "rgb reflectance" [0.7 0.7 0.7]
NamedMaterial "white"
Shape "trianglemesh" "point3 P" [-0.5 0 0 0.5 0 0 0 1 0] "integer indices" [0 1 2]
WorldEnd
```

- [ ] **Step 2: Write failing PBRT frontend tests**

Create `tests/pbrt_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>

namespace {

std::filesystem::path FixturePath(std::string_view relative) {
    return std::filesystem::path{YAORAY_TEST_DATA_DIR} / std::string{relative};
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(pbrt_frontend_loads_minimal_triangle_scene_world) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneWorld& world = result.scene.value();
    YR_EXPECT_EQ(world.render.width, 64);
    YR_EXPECT_EQ(world.render.height, 32);
    YR_EXPECT_EQ(world.render.integrator, yr::RenderIntegratorKind::Path);
    YR_EXPECT_TRUE(world.camera.has_value());
    YR_EXPECT_NEAR(world.camera->position.z, 3.0, 1e-6);
    YR_EXPECT_EQ(world.materials.size(), std::size_t{1});
    YR_EXPECT_EQ(world.materials[0].name, std::string{"white"});
    YR_EXPECT_EQ(world.materials[0].type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(world.materials[0].albedo.x, 0.7, 1e-6);
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes[0].positions.size(), std::size_t{3});
    YR_EXPECT_EQ(world.assets[0].meshes[0].indices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_minimal_triangle_compiles_to_render_scene) {
    const yr::SceneWorldLoadResult load =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/minimal_triangle.pbrt"));
    YR_EXPECT_TRUE(load.scene.has_value());
    if (!load.scene.has_value()) {
        return;
    }

    const yr::SceneCompileResult compiled = yr::CompileSceneWorld(load.scene.value());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(compiled.diagnostics));
    YR_EXPECT_TRUE(compiled.scene.has_value());
    YR_EXPECT_EQ(compiled.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.scene.value().materials.size(), std::size_t{1});
}

YR_TEST(pbrt_frontend_reports_missing_file) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/no_such_scene.pbrt"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "PBRT file not found"));
}
```

- [ ] **Step 3: Run tests and confirm missing API failure**

Run:

```powershell
cmake --build build
```

Expected: compile fails because `yaoray/pbrt/pbrt_scene.hpp` does not exist.

- [ ] **Step 4: Add PBRT frontend API**

Create `include/yaoray/pbrt/pbrt_scene.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace yr {

struct SceneWorldLoadResult {
    std::optional<SceneWorld> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 5: Implement minimal PBRT parsing**

Create `src/pbrt/pbrt_scene.cpp` with:

```cpp
#include <yaoray/pbrt/pbrt_scene.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yr {
namespace {

SceneDiagnostic PbrtError(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

SceneDiagnostic PbrtWarning(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, file, std::move(field), std::move(message)};
}

struct PbrtParam {
    std::string type;
    std::string name;
    std::vector<std::string> values;
};

struct PbrtCommand {
    std::string name;
    std::vector<std::string> args;
    std::vector<PbrtParam> params;
};

std::vector<std::string> TokenizePbrt(std::string_view text) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '#') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '[' || c == ']') {
            tokens.push_back(std::string(1, c));
            ++i;
            continue;
        }
        if (c == '"') {
            ++i;
            std::string value;
            while (i < text.size() && text[i] != '"') {
                value.push_back(text[i++]);
            }
            if (i < text.size() && text[i] == '"') {
                ++i;
            }
            tokens.push_back(std::move(value));
            continue;
        }
        std::string value;
        while (i < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[i])) &&
               text[i] != '[' &&
               text[i] != ']' &&
               text[i] != '#') {
            value.push_back(text[i++]);
        }
        tokens.push_back(std::move(value));
    }
    return tokens;
}

bool SplitParamName(std::string_view token, std::string& type, std::string& name) {
    const std::size_t space = token.find(' ');
    if (space == std::string_view::npos || space + 1 >= token.size()) {
        return false;
    }
    type = std::string(token.substr(0, space));
    name = std::string(token.substr(space + 1));
    return true;
}

std::vector<std::string> ReadValueList(const std::vector<std::string>& tokens, std::size_t& index) {
    std::vector<std::string> values;
    if (index < tokens.size() && tokens[index] == "[") {
        ++index;
        while (index < tokens.size() && tokens[index] != "]") {
            values.push_back(tokens[index++]);
        }
        if (index < tokens.size() && tokens[index] == "]") {
            ++index;
        }
    } else if (index < tokens.size()) {
        values.push_back(tokens[index++]);
    }
    return values;
}

std::vector<PbrtParam> ReadParams(const std::vector<std::string>& tokens, std::size_t& index) {
    std::vector<PbrtParam> params;
    while (index < tokens.size()) {
        std::string type;
        std::string name;
        if (!SplitParamName(tokens[index], type, name)) {
            break;
        }
        ++index;
        PbrtParam param;
        param.type = std::move(type);
        param.name = std::move(name);
        param.values = ReadValueList(tokens, index);
        params.push_back(std::move(param));
    }
    return params;
}

const PbrtParam* FindParam(const std::vector<PbrtParam>& params, std::string_view name) {
    for (const PbrtParam& param : params) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

float FloatAt(const std::vector<std::string>& values, std::size_t index, float fallback = 0.0f) {
    return index < values.size() ? std::stof(values[index]) : fallback;
}

int IntAt(const std::vector<std::string>& values, std::size_t index, int fallback = 0) {
    return index < values.size() ? std::stoi(values[index]) : fallback;
}

Color3f ColorParam(const std::vector<PbrtParam>& params, std::string_view name, Color3f fallback) {
    const PbrtParam* param = FindParam(params, name);
    if (param == nullptr || param->values.size() < 3) {
        return fallback;
    }
    return Color3f{
        FloatAt(param->values, 0, fallback.x),
        FloatAt(param->values, 1, fallback.y),
        FloatAt(param->values, 2, fallback.z)
    };
}

} // namespace

SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path) {
    SceneWorldLoadResult result;
    if (!std::filesystem::exists(path)) {
        result.diagnostics.push_back(PbrtError(path, "", "PBRT file not found"));
        return result;
    }

    std::ifstream in{path};
    if (!in) {
        result.diagnostics.push_back(PbrtError(path, "", "failed to open PBRT file"));
        return result;
    }

    SceneWorld world;
    world.source_path = path;
    world.source_root = path.parent_path();
    world.render.backend = RenderBackendKind::Cpu;
    world.render.integrator = RenderIntegratorKind::Path;
    world.render.sampler = RenderSamplerKind::Independent;
    world.render.width = 1280;
    world.render.height = 720;
    world.render.spp = 1;
    world.render.max_depth = 5;
    world.film.output = path.parent_path() / "out" / (path.stem().string() + ".png");
    world.environment.type = EnvironmentKind::Constant;
    world.environment.radiance = Color3f{0.0f, 0.0f, 0.0f};

    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::vector<std::string> tokens = TokenizePbrt(text);
    std::unordered_map<std::string, MaterialDescription> named_materials;
    std::string current_material;
    float current_fov = 45.0f;
    bool inside_world = false;
    int next_asset_index = 0;

    for (std::size_t index = 0; index < tokens.size();) {
        const std::string command = tokens[index++];
        if (command == "Film") {
            if (index < tokens.size()) {
                ++index;
            }
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (const PbrtParam* x = FindParam(params, "xresolution")) {
                world.render.width = IntAt(x->values, 0, world.render.width);
            }
            if (const PbrtParam* y = FindParam(params, "yresolution")) {
                world.render.height = IntAt(y->values, 0, world.render.height);
            }
            if (const PbrtParam* filename = FindParam(params, "filename");
                filename != nullptr && !filename->values.empty()) {
                world.film.output = path.parent_path() / filename->values[0];
            }
        } else if (command == "Camera") {
            if (index < tokens.size()) {
                ++index;
            }
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (const PbrtParam* fov = FindParam(params, "fov")) {
                current_fov = FloatAt(fov->values, 0, current_fov);
            }
        } else if (command == "LookAt") {
            if (index + 8 >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "LookAt", "expected nine numeric values"));
                return result;
            }
            CameraDescription camera;
            camera.type = CameraKind::Perspective;
            camera.position = Point3f{std::stof(tokens[index + 0]), std::stof(tokens[index + 1]), std::stof(tokens[index + 2])};
            camera.target = Point3f{std::stof(tokens[index + 3]), std::stof(tokens[index + 4]), std::stof(tokens[index + 5])};
            camera.fov_y = current_fov;
            world.camera = camera;
            index += 9;
        } else if (command == "WorldBegin") {
            inside_world = true;
        } else if (command == "WorldEnd") {
            inside_world = false;
        } else if (command == "MakeNamedMaterial") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "MakeNamedMaterial", "missing material name"));
                return result;
            }
            MaterialDescription material;
            material.name = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            material.type = MaterialKind::Diffuse;
            if (const PbrtParam* type = FindParam(params, "type");
                type != nullptr && !type->values.empty()) {
                if (type->values[0] == "plastic") {
                    material.type = MaterialKind::Plastic;
                } else if (type->values[0] == "metal") {
                    material.type = MaterialKind::Metal;
                } else if (type->values[0] == "glass") {
                    material.type = MaterialKind::Dielectric;
                }
            }
            material.albedo = ColorParam(params, "reflectance", material.albedo);
            named_materials[material.name] = material;
            world.materials.push_back(material);
        } else if (command == "NamedMaterial") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "NamedMaterial", "missing material name"));
                return result;
            }
            current_material = tokens[index++];
        } else if (command == "Shape") {
            if (index >= tokens.size()) {
                result.diagnostics.push_back(PbrtError(path, "Shape", "missing shape type"));
                return result;
            }
            const std::string shape_type = tokens[index++];
            const std::vector<PbrtParam> params = ReadParams(tokens, index);
            if (shape_type != "trianglemesh") {
                result.diagnostics.push_back(PbrtError(path, "Shape", "unsupported PBRT shape: " + shape_type));
                return result;
            }
            const PbrtParam* p = FindParam(params, "P");
            const PbrtParam* indices = FindParam(params, "indices");
            if (p == nullptr || indices == nullptr) {
                result.diagnostics.push_back(PbrtError(path, "Shape", "trianglemesh requires P and indices parameters"));
                return result;
            }
            SceneWorldMesh mesh;
            mesh.material = current_material;
            for (std::size_t value = 0; value + 2 < p->values.size(); value += 3) {
                mesh.positions.push_back(Point3f{
                    FloatAt(p->values, value + 0),
                    FloatAt(p->values, value + 1),
                    FloatAt(p->values, value + 2)
                });
            }
            for (const std::string& value : indices->values) {
                mesh.indices.push_back(static_cast<std::uint32_t>(std::stoul(value)));
            }
            SceneWorldAsset asset;
            asset.name = "__pbrt_shape_" + std::to_string(next_asset_index++);
            asset.meshes.push_back(std::move(mesh));
            world.instances.push_back(SceneWorldInstance{asset.name, TransformDescription{}, ""});
            world.assets.push_back(std::move(asset));
        } else if (!inside_world) {
            result.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored before WorldBegin"));
        } else {
            result.diagnostics.push_back(PbrtWarning(path, command, "unsupported PBRT directive ignored"));
        }
    }

    if (!world.camera.has_value()) {
        result.diagnostics.push_back(PbrtError(path, "Camera", "PBRT scene did not define a supported camera"));
    }
    if (world.assets.empty()) {
        result.diagnostics.push_back(PbrtError(path, "Shape", "PBRT scene did not define supported geometry"));
    }
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    result.scene = std::move(world);
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Add `yaoray_pbrt` to CMake**

Modify `CMakeLists.txt`:

```cmake
add_library(yaoray_pbrt STATIC
    src/pbrt/pbrt_scene.cpp
)
target_include_directories(yaoray_pbrt PUBLIC include)
target_link_libraries(yaoray_pbrt PUBLIC yaoray_core yaoray_scene PRIVATE yaoray_assets)
```

Add `tests/pbrt_tests.cpp` to `yaoray_tests` and link `yaoray_pbrt`:

```cmake
add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/scene_world_tests.cpp
    tests/render_scene_tests.cpp
    tests/pbrt_tests.cpp
    tests/bsdf_tests.cpp
    tests/environment_tests.cpp
    tests/mis_tests.cpp
    tests/light_sampling_tests.cpp
    tests/texture_tests.cpp
    tests/backend_tests.cpp
    tests/cpu_material_tests.cpp
    tests/cpu_surface_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
    tests/cpu_tile_scheduler_tests.cpp
    tests/cpu_sampler_tests.cpp
    tests/cpu_path_tracer_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_assets yaoray_pbrt yaoray_backends)
```

- [ ] **Step 7: Run PBRT minimal tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all tests pass, including minimal PBRT parse and compile.

- [ ] **Step 8: Commit**

Run:

```powershell
git add include/yaoray/pbrt/pbrt_scene.hpp src/pbrt/pbrt_scene.cpp tests/pbrt_tests.cpp tests/fixtures/pbrt/minimal_triangle.pbrt CMakeLists.txt
git commit -m "feat: add minimal pbrt scene frontend"
```

---

### Task 5: Add PBRT Includes, Transform Stack, Named Materials, and PLY Shapes

**Files:**
- Modify: `src/pbrt/pbrt_scene.cpp`
- Modify: `tests/pbrt_tests.cpp`
- Create: `tests/fixtures/pbrt/include_root.pbrt`
- Create: `tests/fixtures/pbrt/included_shape.pbrt`
- Create: `tests/fixtures/pbrt/ply_scene.pbrt`

- [ ] **Step 1: Add PBRT include and PLY fixtures**

Create `tests/fixtures/pbrt/include_root.pbrt`:

```text
Film "rgb" "integer xresolution" [32] "integer yresolution" [16] "string filename" ["out/include_root.png"]
Camera "perspective" "float fov" [50]
LookAt 0 0 4  0 0 0  0 1 0
WorldBegin
MakeNamedMaterial "red" "string type" ["matte"] "rgb reflectance" [0.9 0.1 0.1]
NamedMaterial "red"
Translate 1 0 0
Include "included_shape.pbrt"
WorldEnd
```

Create `tests/fixtures/pbrt/included_shape.pbrt`:

```text
Shape "trianglemesh" "point3 P" [-0.5 0 0 0.5 0 0 0 1 0] "integer indices" [0 1 2]
```

Create `tests/fixtures/pbrt/ply_scene.pbrt`:

```text
Film "rgb" "integer xresolution" [32] "integer yresolution" [16] "string filename" ["out/ply_scene.png"]
Camera "perspective" "float fov" [45]
LookAt 0 0 4  0 0 0  0 1 0
WorldBegin
MakeNamedMaterial "green" "string type" ["matte"] "rgb reflectance" [0.1 0.8 0.2]
NamedMaterial "green"
Shape "plymesh" "string filename" ["../assets/ply/triangle_ascii.ply"]
WorldEnd
```

- [ ] **Step 2: Add failing PBRT feature tests**

Append to `tests/pbrt_tests.cpp`:

```cpp
YR_TEST(pbrt_frontend_resolves_include_relative_to_current_file) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/include_root.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneWorld& world = result.scene.value();
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.instances.size(), std::size_t{1});
    YR_EXPECT_NEAR(world.instances[0].transform.translate.x, 1.0, 1e-6);
    YR_EXPECT_EQ(world.assets[0].meshes[0].material, std::string{"red"});
}

YR_TEST(pbrt_frontend_loads_plymesh_shape) {
    const yr::SceneWorldLoadResult result =
        yr::LoadPbrtSceneFile(FixturePath("pbrt/ply_scene.pbrt"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneWorld& world = result.scene.value();
    YR_EXPECT_EQ(world.assets.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes.size(), std::size_t{1});
    YR_EXPECT_EQ(world.assets[0].meshes[0].positions.size(), std::size_t{3});
    YR_EXPECT_EQ(world.assets[0].meshes[0].indices.size(), std::size_t{3});
    YR_EXPECT_EQ(world.assets[0].meshes[0].material, std::string{"green"});
}
```

- [ ] **Step 3: Run tests and confirm feature failures**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: PBRT minimal tests pass; include and plymesh tests fail.

- [ ] **Step 4: Implement include stack with recursion guard**

In `src/pbrt/pbrt_scene.cpp`, add a parser state:

```cpp
struct PbrtParseState {
    SceneWorld world;
    std::vector<SceneDiagnostic> diagnostics;
    std::vector<std::filesystem::path> include_stack;
    std::string current_material;
    TransformDescription current_transform;
    int next_asset_index = 0;
};
```

Implement include handling:

```cpp
bool ParseFileIntoState(const std::filesystem::path& file, PbrtParseState& state) {
    const std::filesystem::path normalized = file.lexically_normal();
    for (const std::filesystem::path& active : state.include_stack) {
        if (active == normalized) {
            state.diagnostics.push_back(PbrtError(file, "Include", "include cycle detected"));
            return false;
        }
    }
    if (state.include_stack.size() >= 32) {
        state.diagnostics.push_back(PbrtError(file, "Include", "include nesting exceeds 32 files"));
        return false;
    }

    state.include_stack.push_back(normalized);
    std::ifstream in{normalized};
    if (!in) {
        state.diagnostics.push_back(PbrtError(file, "Include", "failed to open included PBRT file"));
        state.include_stack.pop_back();
        return false;
    }
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::vector<std::string> tokens = TokenizePbrt(text);
    for (std::size_t index = 0; index < tokens.size();) {
        const std::string command = tokens[index++];
        if (command == "Include") {
            if (index >= tokens.size()) {
                state.diagnostics.push_back(PbrtError(file, "Include", "missing include path"));
                break;
            }
            ParseFileIntoState(file.parent_path() / tokens[index++], state);
        } else {
            ParseCommandIntoState(file, command, tokens, index, state);
        }
    }
    state.include_stack.pop_back();
    return !HasSceneErrors(state.diagnostics);
}
```

Add `ParseCommandIntoState()` by moving the Task 4 command dispatch from
`LoadPbrtSceneFile()` into a private helper with this signature:

```cpp
void ParseCommandIntoState(
    const std::filesystem::path& file,
    const std::string& command,
    const std::vector<std::string>& tokens,
    std::size_t& index,
    PbrtParseState& state
);
```

The helper uses `state.world`, `state.current_material`,
`state.current_transform`, and `state.next_asset_index` instead of local
variables, so `Include` can share material and transform state with included
files.

- [ ] **Step 5: Implement transform stack subset**

Extend parser state:

```cpp
std::vector<TransformDescription> transform_stack;
```

For v1, keep transform state in `TransformDescription` and support `Translate`, `Scale`, and axis-aligned `Rotate`:

```cpp
void ApplyTranslate(PbrtParseState& state, float x, float y, float z) {
    state.current_transform.translate = state.current_transform.translate + Vec3f{x, y, z};
}

void ApplyScale(PbrtParseState& state, float x, float y, float z) {
    state.current_transform.scale = Vec3f{
        state.current_transform.scale.x * x,
        state.current_transform.scale.y * y,
        state.current_transform.scale.z * z
    };
}

void ApplyRotate(PbrtParseState& state, float degrees, float x, float y, float z) {
    if (x == 1.0f && y == 0.0f && z == 0.0f) {
        state.current_transform.rotate_degrees.x += degrees;
    } else if (x == 0.0f && y == 1.0f && z == 0.0f) {
        state.current_transform.rotate_degrees.y += degrees;
    } else if (x == 0.0f && y == 0.0f && z == 1.0f) {
        state.current_transform.rotate_degrees.z += degrees;
    } else {
        state.diagnostics.push_back(PbrtWarning(state.world.source_path, "Rotate", "non-axis-aligned PBRT rotate is ignored in v1"));
    }
}
```

For `AttributeBegin` and `TransformBegin`, push `current_transform` and `current_material`. For `AttributeEnd` and `TransformEnd`, pop them. Emit an error if the stack is empty.

- [ ] **Step 6: Implement `plymesh` conversion to `SceneWorldMesh`**

When parsing:

```text
Shape "plymesh" "string filename" ["../assets/ply/triangle_ascii.ply"]
```

Call `LoadPlyResource(current_file.parent_path() / filename)` and convert the first primitive of every mesh into `SceneWorldMesh`:

```cpp
SceneWorldMesh ConvertAssetPrimitiveToWorldMesh(const AssetPrimitive& primitive, std::string material) {
    SceneWorldMesh mesh;
    mesh.material = std::move(material);
    mesh.positions = primitive.positions;
    mesh.normals = primitive.normals;
    mesh.texcoords0 = primitive.texcoords0;
    mesh.indices = primitive.indices;
    return mesh;
}
```

Create one `SceneWorldAsset` per PBRT shape:

```cpp
SceneWorldAsset asset;
asset.name = "__pbrt_shape_" + std::to_string(state.next_asset_index++);
asset.meshes.push_back(ConvertAssetPrimitiveToWorldMesh(primitive, state.current_material));
state.world.assets.push_back(asset);
state.world.instances.push_back(SceneWorldInstance{asset.name, state.current_transform, ""});
```

- [ ] **Step 7: Run PBRT feature tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all PBRT tests pass.

- [ ] **Step 8: Commit**

Run:

```powershell
git add src/pbrt/pbrt_scene.cpp tests/pbrt_tests.cpp tests/fixtures/pbrt/include_root.pbrt tests/fixtures/pbrt/included_shape.pbrt tests/fixtures/pbrt/ply_scene.pbrt
git commit -m "feat: load pbrt includes transforms and ply meshes"
```

---

### Task 6: Add Scene Frontend Dispatch and `.pbrt` CLI Rendering

**Files:**
- Create: `include/yaoray/frontends/scene_frontend.hpp`
- Create: `src/frontends/scene_frontend.cpp`
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/run_cli_render_test.cmake` only if needed

- [ ] **Step 1: Add a failing CLI test for `.pbrt`**

Add this CMake test registration under existing CLI render tests:

```cmake
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_minimal
    SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pbrt/minimal_triangle.pbrt"
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/minimal_pbrt.png"
    BACKEND cpu
    EXPECT_REGEX
        "Scene parsed successfully:"
        "Integrator: path"
        "Compiled triangles: 1"
        "Rendered image:"
)
```

- [ ] **Step 2: Run the CLI test and confirm TOML-only failure**

Run:

```powershell
cmake --build build
ctest --test-dir build -R yaoray_cli_render_pbrt_minimal --output-on-failure
```

Expected: test fails because CLI tries to load `.pbrt` as TOML or rejects the extension.

- [ ] **Step 3: Add frontend dispatch API**

Create `include/yaoray/frontends/scene_frontend.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/scene/scene.hpp>
#include <yaoray/scene/scene_world.hpp>

namespace yr {

SceneWorldLoadResult LoadSceneWorldFile(const std::filesystem::path& path);
void ApplyBackendOverride(SceneWorld& scene, RenderBackendKind backend);

} // namespace yr
```

Create `src/frontends/scene_frontend.cpp`:

```cpp
#include <yaoray/frontends/scene_frontend.hpp>

#include <yaoray/scene/scene_parser.hpp>

namespace yr {

namespace {

SceneDiagnostic FrontendError(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

bool HasTomlExtension(const std::filesystem::path& path) {
    return path.extension() == ".toml";
}

bool HasPbrtExtension(const std::filesystem::path& path) {
    return path.extension() == ".pbrt";
}

} // namespace

SceneWorldLoadResult LoadSceneWorldFile(const std::filesystem::path& path) {
    if (HasTomlExtension(path)) {
        SceneLoadResult loaded = LoadSceneFile(path);
        SceneWorldLoadResult result;
        result.diagnostics = std::move(loaded.diagnostics);
        if (loaded.scene.has_value() && !HasSceneErrors(result.diagnostics)) {
            result.scene = BuildSceneWorld(loaded.scene.value());
        }
        return result;
    }
    if (HasPbrtExtension(path)) {
        return LoadPbrtSceneFile(path);
    }

    SceneWorldLoadResult result;
    result.diagnostics.push_back(FrontendError(path, "", "unsupported scene extension: " + path.extension().generic_string()));
    return result;
}

void ApplyBackendOverride(SceneWorld& scene, RenderBackendKind backend) {
    scene.render.backend = backend;
}

} // namespace yr
```

- [ ] **Step 4: Wire the frontend library into CMake**

Add:

```cmake
add_library(yaoray_frontends STATIC
    src/frontends/scene_frontend.cpp
)
target_include_directories(yaoray_frontends PUBLIC include)
target_link_libraries(yaoray_frontends PUBLIC yaoray_scene yaoray_pbrt)
```

Link app and tests:

```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_assets yaoray_pbrt yaoray_frontends yaoray_backends)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_frontends yaoray_backends)
```

- [ ] **Step 5: Update CLI to render `SceneWorld`**

Modify includes in `src/app/main.cpp`:

```cpp
#include <yaoray/frontends/scene_frontend.hpp>
```

Remove direct `scene_parser.hpp` include if no longer used.

Change helper signatures:

```cpp
bool OfflineRequested(const yr::SceneWorld& scene)

std::optional<std::string> ValidateOfflineWorkflow(
    const yr::SceneWorld& scene,
    const yr::RenderSceneIR& render_scene
)
```

Change loading and compiling in `RunRender`:

```cpp
    yr::SceneWorldLoadResult result = yr::LoadSceneWorldFile(scene_path);
    if (yr::HasSceneErrors(result.diagnostics) || !result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(result.diagnostics) << '\n';
        return 1;
    }

    yr::SceneWorld scene = std::move(result.scene.value());
    if (backend_override) {
        yr::ApplyBackendOverride(scene, *backend_override);
    }

    const yr::SceneCompileResult compile_result = yr::CompileSceneWorld(scene);
```

Update help text from `<scene.toml>` to `<scene.toml|scene.pbrt>`:

```cpp
        << "  yaoray render <scene.toml|scene.pbrt> [--backend cpu|cuda]\n";
```

- [ ] **Step 6: Run CLI and full tests**

Run:

```powershell
cmake --build build
ctest --test-dir build -R yaoray_cli_render_pbrt_minimal --output-on-failure
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Expected: PBRT CLI smoke test passes, unit tests pass, and existing TOML CLI tests still pass.

- [ ] **Step 7: Commit**

Run:

```powershell
git add include/yaoray/frontends/scene_frontend.hpp src/frontends/scene_frontend.cpp src/app/main.cpp CMakeLists.txt tests/run_cli_render_test.cmake
git commit -m "feat: dispatch render scene frontends by extension"
```

If `tests/run_cli_render_test.cmake` was not modified, omit it from `git add`.

---

### Task 7: Document and Download Breakfast Locally

**Files:**
- Create: `docs/assets/pbrt-breakfast-local-benchmark.md`
- Modify: `docs/architecture/overview.md`
- Local ignored files: `external/assets/pbrt/breakfast/`

- [ ] **Step 1: Add Breakfast local benchmark docs**

Create `docs/assets/pbrt-breakfast-local-benchmark.md`:

```markdown
# PBRT Breakfast Local Benchmark

Breakfast is the first large PBRT benchmark target for YaoRay's direct PBRT
frontend. Do not commit the downloaded archive, expanded meshes, or textures to
this repository.

## Source

- Asset: The Breakfast Room.
- Author attribution listed by PBRT v3 scenes: Wig42.
- PBRT v4 package from Benedikt Bitterli Rendering Resources:
  https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip
- Rendering Resources index:
  https://benedikt-bitterli.me/resources/
- PBRT v3 scene note:
  https://www.pbrt.org/scenes-v3

The PBRT v3 scenes page lists `breakfast` as CC-BY. Keep attribution in render
captions, benchmark notes, and any gallery page that uses the scene.

## Local Layout

Place files under:

```text
external/assets/pbrt/breakfast/
```

`external/assets/` is ignored by Git. Keep the archive and extracted files
local-only.

## Download

```powershell
New-Item -ItemType Directory -Force external/assets/pbrt/breakfast
Invoke-WebRequest -Uri https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip -OutFile external/assets/pbrt/breakfast/dining-room.zip
Expand-Archive -Force external/assets/pbrt/breakfast/dining-room.zip external/assets/pbrt/breakfast/extracted
```

## Inspection

```powershell
Get-ChildItem -Recurse external/assets/pbrt/breakfast/extracted -File | Select-Object FullName,Length
rg -n "Include|Shape|plymesh|trianglemesh|MakeNamedMaterial|AreaLightSource|LightSource|Texture|Material|Camera|Film" external/assets/pbrt/breakfast/extracted
```

## Smoke Render

After PBRT loading supports the scene's required subset, render a low-resolution
smoke image:

```powershell
$entrypoint = Get-ChildItem -Recurse external/assets/pbrt/breakfast/extracted -Filter *.pbrt | Select-Object -First 1 -ExpandProperty FullName
build\yaoray.exe render $entrypoint --backend cpu
```

The first `.pbrt` file is only a smoke target. After inspection identifies the
canonical Breakfast entrypoint, use that file explicitly for benchmark renders.
Keep smoke renders under ignored output directories.
```

- [ ] **Step 2: Update architecture overview**

Modify `docs/architecture/overview.md` near the opening architecture section:

```markdown
YaoRay scene loading now separates file frontends from renderer input. TOML
files and PBRT files are peer frontends that produce `SceneWorld`, a high-level
semantic scene layer. The render compiler lowers `SceneWorld` into
backend-neutral `RenderSceneIR`, and backends prepare their own runtime data
from that IR.
```

Add a note near asset limitations:

```markdown
PBRT support is a direct scene frontend, not a TOML conversion path. The v1
subset is aimed at small checked-in PBRT fixtures and the local-only Breakfast
benchmark. Large PBRT assets remain under `external/assets/` and are not part of
default CI.
```

- [ ] **Step 3: Download Breakfast to ignored local storage**

Run with network approval:

```powershell
New-Item -ItemType Directory -Force external/assets/pbrt/breakfast
Invoke-WebRequest -Uri https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip -OutFile external/assets/pbrt/breakfast/dining-room.zip
Expand-Archive -Force external/assets/pbrt/breakfast/dining-room.zip external/assets/pbrt/breakfast/extracted
```

Expected:

```text
dining-room.zip exists under external/assets/pbrt/breakfast/
extracted files exist under external/assets/pbrt/breakfast/extracted/
git status does not list external/assets/pbrt/breakfast/
```

- [ ] **Step 4: Inspect Breakfast feature usage**

Run:

```powershell
Get-ChildItem -Recurse external/assets/pbrt/breakfast/extracted -File | Select-Object FullName,Length
rg -n "Include|Shape|plymesh|trianglemesh|MakeNamedMaterial|NamedMaterial|AreaLightSource|LightSource|Texture|Material|Camera|Film|Transform|ConcatTransform|Rotate|Translate|Scale" external/assets/pbrt/breakfast/extracted
```

Expected:

```text
The output identifies the PBRT entrypoint files, mesh file extensions, texture file extensions, and unsupported directives that need follow-up slices.
```

- [ ] **Step 5: Commit docs only**

Run:

```powershell
git add docs/assets/pbrt-breakfast-local-benchmark.md docs/architecture/overview.md
git commit -m "docs: document pbrt breakfast benchmark"
```

Do not add files under `external/assets/`.

---

### Task 8: Final Verification and Local PBRT Smoke

**Files:**
- No required source edits unless verification exposes a defect.

- [ ] **Step 1: Run full build**

Run:

```powershell
cmake --build build
```

Expected: build succeeds. Existing tinygltf third-party warnings are acceptable if unchanged.

- [ ] **Step 2: Run unit tests**

Run:

```powershell
build\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 3: Run CTest**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass.

- [ ] **Step 4: Run PBRT CLI smoke directly**

Run:

```powershell
build\yaoray.exe render tests\fixtures\pbrt\minimal_triangle.pbrt --backend cpu
```

Expected output contains:

```text
Scene parsed successfully:
Scene compiled successfully.
Integrator: path
Compiled triangles: 1
Rendered image:
```

- [ ] **Step 5: Run Breakfast parser-only inspection command**

Run:

```powershell
rg -n "Shape|plymesh|trianglemesh|MakeNamedMaterial|AreaLightSource|LightSource|Texture|Material|Camera|Film" external/assets/pbrt/breakfast/extracted
```

Expected: command lists scene features. Use the list to decide the next implementation slice; do not claim Breakfast final rendering support until a real render command completes.

- [ ] **Step 6: Commit verification fixes**

If verification required fixes, stage the exact files changed by the verification
step. For example, when the fix touches the PBRT parser and parser tests, run:

```powershell
git add src/pbrt/pbrt_scene.cpp tests/pbrt_tests.cpp
git commit -m "fix: stabilize pbrt frontend verification"
```

If the fix touched different files, replace the `git add` path list with the
specific files from `git status --short`. If no files changed, skip this commit.
