# YaoRay Modern Asset Import v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add smooth imported normals plus static glTF/GLB asset import so YaoRay can render small modern textured assets through the existing CPU path tracer.

**Architecture:** Keep the current flat-triangle `RenderScene` backend contract. Move shared imported mesh/material structures into `yaoray_assets`, extend OBJ to fill normals, add a tinygltf-based glTF loader, and let `SceneCompiler` convert both OBJ and glTF assets into render materials, textures, UVs, vertex normals, and triangles.

**Tech Stack:** C++20, CMake, CTest, tinyobjloader, tinygltf, stb PNG loading, existing YaoRay CPU path tracer.

---

## File Structure

- Create: `include/yaoray/assets/imported_asset.hpp`
  - Owns shared `ImportedMaterial`, `ImportedTriangle`, `ImportedMesh`, and `AssetLoadResult` types used by OBJ and glTF loaders.
- Modify: `include/yaoray/assets/obj_loader.hpp`
  - Includes `imported_asset.hpp` and exposes only `LoadObjMesh`.
- Modify: `src/assets/obj_loader.cpp`
  - Reads OBJ vertex normals and fills imported vertex-normal fields.
- Create: `include/yaoray/assets/gltf_loader.hpp`
  - Public `LoadGltfMesh(const std::filesystem::path&)` API.
- Create: `src/assets/gltf_loader.cpp`
  - tinygltf-backed static mesh loader. Keeps tinygltf types private.
- Modify: `include/yaoray/render/render_scene.hpp`
  - Adds per-vertex normals to `RenderTriangle`.
- Create: `include/yaoray/render/shading.hpp`
  - Small render-level hit interpolation helpers.
- Create: `src/render/shading.cpp`
  - Implements barycentric coordinates, UV interpolation, and shading-normal resolution.
- Modify: `src/render/scene_compiler.cpp`
  - Dispatches OBJ/glTF asset loads, maps imported materials, transforms imported normals, and appends vertex normals into render triangles.
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
  - Uses render-level shading helpers and interpolated normals.
- Modify: `CMakeLists.txt`
  - Adds tinygltf include target, glTF loader source, and shading source.
- Create/modify test fixtures under `tests/fixtures/assets/`
  - Adds OBJ normal fixtures and small glTF/GLB compatibility assets.
- Modify: `tests/assets_tests.cpp`
  - Adds OBJ normal and glTF loader tests.
- Modify: `tests/render_scene_tests.cpp`
  - Adds compiler tests for imported normals and glTF material/texture mapping.
- Modify: `tests/cpu_path_tracer_tests.cpp`
  - Adds smooth-normal fallback/correction tests through render-level helpers or a tiny path scene.
- Create: `scenes/examples/gltf_textured_asset.toml`
  - Manual glTF render scene.
- Create: `docs/assets/khronos-sample-assets.md`
  - Records imported sample source and license notes.
- Modify: `README.md`
  - Documents glTF/GLB importer scope and limits.
- Modify: `docs/architecture/overview.md`
  - Documents modern asset import v1 and remaining importer limitations.

---

## Task 1: Shared Imported Asset Types

**Files:**
- Create: `include/yaoray/assets/imported_asset.hpp`
- Modify: `include/yaoray/assets/obj_loader.hpp`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Write the failing test for default imported normal fields**

Add this test to `tests/assets_tests.cpp` near the existing OBJ loader tests:

```cpp
YR_TEST(imported_triangle_defaults_do_not_claim_vertex_normals) {
    const yr::ImportedTriangle triangle;

    YR_EXPECT_TRUE(!triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.z, 0.0, 1e-6);
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `ImportedTriangle` has no `has_vertex_normals`, `n0`, `n1`, or `n2` members.

- [ ] **Step 3: Create shared imported asset header**

Create `include/yaoray/assets/imported_asset.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct ImportedMaterial {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f diffuse{0.8f, 0.8f, 0.8f};
    Color3f emission{};
    float roughness = 0.0f;
    float specular = 0.04f;
    std::filesystem::path diffuse_texture_path;
    bool has_diffuse_texture = false;
};

struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    Vec2f uv0;
    Vec2f uv1;
    Vec2f uv2;
    bool has_uv = false;
    Vec3f n0;
    Vec3f n1;
    Vec3f n2;
    bool has_vertex_normals = false;
    int material_index = -1;
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
    std::vector<ImportedMaterial> materials;
};

struct AssetLoadResult {
    std::optional<ImportedMesh> mesh;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

} // namespace yr
```

- [ ] **Step 4: Update OBJ loader header to use shared types**

Replace the type definitions in `include/yaoray/assets/obj_loader.hpp` with:

```cpp
#pragma once

#include <filesystem>

#include <yaoray/assets/imported_asset.hpp>

namespace yr {

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 5: Run tests and verify the new default fields compile**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all existing tests pass plus `imported_triangle_defaults_do_not_claim_vertex_normals` passes.

- [ ] **Step 6: Commit**

```powershell
git add include/yaoray/assets/imported_asset.hpp include/yaoray/assets/obj_loader.hpp tests/assets_tests.cpp
git commit -m "refactor: share imported asset types"
```

---

## Task 2: OBJ Vertex Normals

**Files:**
- Create: `tests/fixtures/assets/normal_triangle.obj`
- Create: `tests/fixtures/assets/normal_quad.obj`
- Modify: `src/assets/obj_loader.cpp`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Add OBJ normal fixtures**

Create `tests/fixtures/assets/normal_triangle.obj`:

```text
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
vn 0.0 0.0 1.0
vn 0.0 0.70710678 0.70710678
vn 0.70710678 0.0 0.70710678
f 1//1 2//2 3//3
```

Create `tests/fixtures/assets/normal_quad.obj`:

```text
v 0.0 0.0 0.0
v 1.0 0.0 0.0
v 1.0 1.0 0.0
v 0.0 1.0 0.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
vn 0.0 0.0 1.0
vn 0.0 0.2 0.9797959
vn 0.2 0.0 0.9797959
vn 0.0 -0.2 0.9797959
f 1/1/1 2/2/2 3/3/3 4/4/4
```

- [ ] **Step 2: Write failing OBJ normal tests**

Add to `tests/assets_tests.cpp`:

```cpp
YR_TEST(obj_loader_preserves_triangle_vertex_normals) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/normal_triangle.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});

    const yr::ImportedTriangle& triangle = result.mesh->triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.z, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.x, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.z, 0.70710678, 1e-6);
}

YR_TEST(obj_loader_triangulates_quad_with_uvs_and_normals) {
    const yr::AssetLoadResult result = yr::LoadObjMesh(FixturePath("assets/normal_quad.obj"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{2});

    const yr::ImportedTriangle& first = result.mesh->triangles[0];
    const yr::ImportedTriangle& second = result.mesh->triangles[1];
    YR_EXPECT_TRUE(first.has_uv);
    YR_EXPECT_TRUE(second.has_uv);
    YR_EXPECT_TRUE(first.has_vertex_normals);
    YR_EXPECT_TRUE(second.has_vertex_normals);
    YR_EXPECT_NEAR(first.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(first.n1.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(first.n2.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(second.n0.y, 0.2, 1e-6);
    YR_EXPECT_NEAR(second.n1.y, -0.2, 1e-6);
    YR_EXPECT_NEAR(second.n2.x, 0.2, 1e-6);
}
```

- [ ] **Step 3: Run focused tests and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: OBJ normal tests fail because the loader never reads `normal_index`.

- [ ] **Step 4: Implement OBJ normal reading**

Add this helper to `src/assets/obj_loader.cpp` after `ReadTexCoord`:

```cpp
Vec3f ReadNormal(const tinyobj::attrib_t& attrib, int normal_index, bool& ok) {
    if (normal_index < 0) {
        ok = false;
        return {};
    }

    const std::size_t base = static_cast<std::size_t>(normal_index) * 3;
    if (base + 2 >= attrib.normals.size()) {
        ok = false;
        return {};
    }

    return Normalize(Vec3f{
        attrib.normals[base + 0],
        attrib.normals[base + 1],
        attrib.normals[base + 2]
    });
}
```

Inside the face loop in `LoadObjMesh`, after UV reads, add:

```cpp
bool normals_ok = true;
const Vec3f n0 = ReadNormal(attrib, shape.mesh.indices[index_offset + 0].normal_index, normals_ok);
const Vec3f n1 = ReadNormal(attrib, shape.mesh.indices[index_offset + 1].normal_index, normals_ok);
const Vec3f n2 = ReadNormal(attrib, shape.mesh.indices[index_offset + 2].normal_index, normals_ok);
```

Replace the `mesh.triangles.push_back(ImportedTriangle{...})` call with:

```cpp
ImportedTriangle imported;
imported.p0 = p0;
imported.p1 = p1;
imported.p2 = p2;
imported.normal = normal;
imported.uv0 = uv0;
imported.uv1 = uv1;
imported.uv2 = uv2;
imported.has_uv = uvs_ok;
imported.n0 = n0;
imported.n1 = n1;
imported.n2 = n2;
imported.has_vertex_normals =
    normals_ok &&
    LengthSquared(n0) > 0.0f &&
    LengthSquared(n1) > 0.0f &&
    LengthSquared(n2) > 0.0f;
imported.material_index = material_index;
mesh.triangles.push_back(imported);
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all asset tests pass, including OBJ normal tests.

- [ ] **Step 6: Commit**

```powershell
git add src/assets/obj_loader.cpp tests/assets_tests.cpp tests/fixtures/assets/normal_triangle.obj tests/fixtures/assets/normal_quad.obj
git commit -m "feat: import obj vertex normals"
```

---

## Task 3: Render Smooth Shading Helpers

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Create: `include/yaoray/render/shading.hpp`
- Create: `src/render/shading.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Write failing shading helper tests**

Add this include to `tests/cpu_path_tracer_tests.cpp`:

```cpp
#include <yaoray/render/shading.hpp>
```

Add these tests near other CPU path tracer helper tests:

```cpp
YR_TEST(shading_normal_interpolates_vertex_normals) {
    yr::RenderTriangle triangle;
    triangle.p0 = yr::Point3f{0.0f, 0.0f, 0.0f};
    triangle.p1 = yr::Point3f{1.0f, 0.0f, 0.0f};
    triangle.p2 = yr::Point3f{0.0f, 1.0f, 0.0f};
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n0 = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n1 = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});
    triangle.n2 = yr::Normalize(yr::Vec3f{1.0f, 0.0f, 1.0f});
    triangle.has_vertex_normals = true;

    const yr::Vec3f barycentric{0.2f, 0.3f, 0.5f};
    const yr::Vec3f normal = yr::ResolveShadingNormal(triangle, barycentric, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_TRUE(normal.z > 0.8f);
    YR_EXPECT_TRUE(normal.x > 0.2f);
    YR_EXPECT_TRUE(normal.y > 0.1f);
}

YR_TEST(shading_normal_falls_back_to_geometric_normal) {
    yr::RenderTriangle triangle;
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};

    const yr::Vec3f normal = yr::ResolveShadingNormal(triangle, yr::Vec3f{0.2f, 0.3f, 0.5f}, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_NEAR(normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.z, 1.0, 1e-6);
}

YR_TEST(shading_normal_is_corrected_to_geometric_hemisphere) {
    yr::RenderTriangle triangle;
    triangle.normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    triangle.n0 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.n1 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.n2 = yr::Vec3f{0.0f, 0.0f, -1.0f};
    triangle.has_vertex_normals = true;

    const yr::Vec3f normal = yr::ResolveShadingNormal(triangle, yr::Vec3f{0.3f, 0.3f, 0.4f}, yr::Vec3f{0.0f, 0.0f, 1.0f});

    YR_EXPECT_NEAR(normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(normal.z, 1.0, 1e-6);
}
```

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `yaoray/render/shading.hpp` does not exist.

- [ ] **Step 3: Extend render triangles**

Add to `RenderTriangle` in `include/yaoray/render/render_scene.hpp` after `has_uv`:

```cpp
    Vec3f n0;
    Vec3f n1;
    Vec3f n2;
    bool has_vertex_normals = false;
```

- [ ] **Step 4: Add shading helper header**

Create `include/yaoray/render/shading.hpp`:

```cpp
#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

Vec3f BarycentricCoordinates(Point3f point, const RenderTriangle& triangle);
Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric);
Vec3f ResolveShadingNormal(const RenderTriangle& triangle, Vec3f barycentric, Vec3f geometric_normal);

} // namespace yr
```

- [ ] **Step 5: Add shading helper implementation**

Create `src/render/shading.cpp`:

```cpp
#include <yaoray/render/shading.hpp>

#include <cmath>

namespace yr {

Vec3f BarycentricCoordinates(Point3f point, const RenderTriangle& triangle) {
    const Vec3f v0 = triangle.p1 - triangle.p0;
    const Vec3f v1 = triangle.p2 - triangle.p0;
    const Vec3f v2 = point - triangle.p0;
    const float d00 = Dot(v0, v0);
    const float d01 = Dot(v0, v1);
    const float d11 = Dot(v1, v1);
    const float d20 = Dot(v2, v0);
    const float d21 = Dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) <= 1.0e-12f) {
        return Vec3f{1.0f, 0.0f, 0.0f};
    }
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return Vec3f{1.0f - v - w, v, w};
}

Vec2f InterpolateUv(const RenderTriangle& triangle, Vec3f barycentric) {
    return Vec2f{
        triangle.uv0.x * barycentric.x + triangle.uv1.x * barycentric.y + triangle.uv2.x * barycentric.z,
        triangle.uv0.y * barycentric.x + triangle.uv1.y * barycentric.y + triangle.uv2.y * barycentric.z
    };
}

Vec3f ResolveShadingNormal(const RenderTriangle& triangle, Vec3f barycentric, Vec3f geometric_normal) {
    Vec3f normal = Normalize(geometric_normal);
    if (triangle.has_vertex_normals) {
        const Vec3f interpolated = Normalize(
            triangle.n0 * barycentric.x +
            triangle.n1 * barycentric.y +
            triangle.n2 * barycentric.z
        );
        if (LengthSquared(interpolated) > 0.0f) {
            normal = interpolated;
        }
    }

    if (Dot(normal, geometric_normal) < 0.0f) {
        normal = -normal;
    }
    if (LengthSquared(normal) == 0.0f) {
        return Normalize(geometric_normal);
    }
    return normal;
}

} // namespace yr
```

- [ ] **Step 6: Add source to CMake**

In `CMakeLists.txt`, add `src/render/shading.cpp` to `yaoray_render`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
    src/render/shading.cpp
    src/render/texture.cpp
)
```

- [ ] **Step 7: Replace local path tracer interpolation helpers**

In `src/backends/cpu/cpu_path_tracer.cpp`, add:

```cpp
#include <yaoray/render/shading.hpp>
```

Remove the local `Barycentric` and `InterpolateUv` functions. In `ResolveHitMaterial`, replace:

```cpp
const Vec3f barycentric = Barycentric(hit_point, triangle);
const Vec2f uv = InterpolateUv(triangle, barycentric);
```

with:

```cpp
const Vec3f barycentric = BarycentricCoordinates(hit_point, triangle);
const Vec2f uv = InterpolateUv(triangle, barycentric);
```

In `TracePath`, replace:

```cpp
const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);
```

with:

```cpp
const Vec3f geometric_normal = FaceForward(Normalize(triangle.normal), -ray.direction);
const Vec3f barycentric = BarycentricCoordinates(hit_point, triangle);
const Vec3f normal = FaceForward(ResolveShadingNormal(triangle, barycentric, geometric_normal), geometric_normal);
```

- [ ] **Step 8: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all tests pass and shading helper tests pass.

- [ ] **Step 9: Commit**

```powershell
git add CMakeLists.txt include/yaoray/render/render_scene.hpp include/yaoray/render/shading.hpp src/render/shading.cpp src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add render shading normals"
```

---

## Task 4: Compile Imported Normals Into RenderScene

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Write failing scene compiler test**

Add to `tests/render_scene_tests.cpp` near OBJ compiler tests:

```cpp
YR_TEST(scene_compiler_preserves_obj_vertex_normals) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/normal_triangle.obj")});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_NEAR(triangle.n0.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.n1.y, 0.70710678, 1e-6);
    YR_EXPECT_NEAR(triangle.n2.x, 0.70710678, 1e-6);
}

YR_TEST(scene_compiler_transforms_obj_vertex_normals) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", FixturePath("assets/normal_triangle.obj")});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 90.0f, 0.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_TRUE(triangle.has_vertex_normals);
    YR_EXPECT_TRUE(triangle.n0.x > 0.99f);
    YR_EXPECT_NEAR(triangle.n0.z, 0.0, 1e-5);
}
```

- [ ] **Step 2: Run focused test and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: tests fail because scene compiler does not copy imported vertex normals.

- [ ] **Step 3: Add normal transform helper**

Add this helper to `src/render/scene_compiler.cpp` after `ApplyTransform`:

```cpp
Vec3f ApplyNormalTransform(Vec3f normal, const TransformDescription& transform) {
    Vec3f value{
        transform.scale.x != 0.0f ? normal.x / transform.scale.x : normal.x,
        transform.scale.y != 0.0f ? normal.y / transform.scale.y : normal.y,
        transform.scale.z != 0.0f ? normal.z / transform.scale.z : normal.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Normalize(value);
}
```

- [ ] **Step 4: Copy imported normals into render triangles**

In `AppendImportedMesh`, compute transformed normals after transformed positions:

```cpp
const Vec3f n0 = ApplyNormalTransform(triangle.n0, transform);
const Vec3f n1 = ApplyNormalTransform(triangle.n1, transform);
const Vec3f n2 = ApplyNormalTransform(triangle.n2, transform);
const bool has_vertex_normals =
    triangle.has_vertex_normals &&
    LengthSquared(n0) > 0.0f &&
    LengthSquared(n1) > 0.0f &&
    LengthSquared(n2) > 0.0f;
```

Replace the render triangle push with an explicit object:

```cpp
RenderTriangle render_triangle;
render_triangle.p0 = world_p0;
render_triangle.p1 = world_p1;
render_triangle.p2 = world_p2;
render_triangle.normal = Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0));
render_triangle.material_index = material_index;
render_triangle.uv0 = triangle.uv0;
render_triangle.uv1 = triangle.uv1;
render_triangle.uv2 = triangle.uv2;
render_triangle.has_uv = triangle.has_uv;
render_triangle.n0 = n0;
render_triangle.n1 = n1;
render_triangle.n2 = n2;
render_triangle.has_vertex_normals = has_vertex_normals;
compiled.triangles.push_back(render_triangle);
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all tests pass, including scene compiler imported normal tests.

- [ ] **Step 6: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile imported vertex normals"
```

---

## Task 5: Vendor tinygltf And Add Loader Skeleton

**Files:**
- Create: `external/tinygltf/tiny_gltf.h`
- Create: `external/tinygltf/json.hpp`
- Create: `include/yaoray/assets/gltf_loader.hpp`
- Create: `src/assets/gltf_loader.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Fetch tinygltf headers**

Download these files from upstream and place them under `external/tinygltf/`:

```text
external/tinygltf/tiny_gltf.h
external/tinygltf/json.hpp
```

Use the current upstream `tinygltf` repository for `tiny_gltf.h` and the matching `json.hpp` expected by that header. Keep the files unmodified except for line endings.

- [ ] **Step 2: Write failing skeleton tests**

Add this include to `tests/assets_tests.cpp`:

```cpp
#include <yaoray/assets/gltf_loader.hpp>
```

Add these tests:

```cpp
YR_TEST(gltf_loader_rejects_non_gltf_extension) {
    const yr::AssetLoadResult result = yr::LoadGltfMesh(FixturePath("assets/not_obj.txt"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, ".gltf or .glb"));
}

YR_TEST(gltf_loader_returns_error_for_missing_file) {
    const yr::AssetLoadResult result = yr::LoadGltfMesh(FixturePath("assets/missing.gltf"));

    YR_EXPECT_TRUE(!result.mesh.has_value());
    YR_EXPECT_TRUE(ErrorContains(result, "glTF file not found"));
}
```

- [ ] **Step 3: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `gltf_loader.hpp` does not exist.

- [ ] **Step 4: Add glTF loader header**

Create `include/yaoray/assets/gltf_loader.hpp`:

```cpp
#pragma once

#include <filesystem>

#include <yaoray/assets/imported_asset.hpp>

namespace yr {

AssetLoadResult LoadGltfMesh(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 5: Add loader skeleton implementation**

Create `src/assets/gltf_loader.cpp`:

```cpp
#include <yaoray/assets/gltf_loader.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <filesystem>

namespace yr {
namespace {

bool HasGltfExtension(const std::filesystem::path& path) {
    const std::filesystem::path extension = path.extension();
    return extension == ".gltf" || extension == ".glb";
}

} // namespace

AssetLoadResult LoadGltfMesh(const std::filesystem::path& path) {
    AssetLoadResult result;

    if (!HasGltfExtension(path)) {
        result.errors.push_back("glTF asset path must use a .gltf or .glb extension: " + path.generic_string());
        return result;
    }

    if (!std::filesystem::exists(path)) {
        result.errors.push_back("glTF file not found: " + path.generic_string());
        return result;
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string error;
    std::string warning;
    const bool ok = path.extension() == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, path.string())
        : loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    if (!warning.empty()) {
        result.warnings.push_back(warning);
    }
    if (!ok) {
        result.errors.push_back(error.empty() ? "failed to parse glTF file: " + path.generic_string() : error);
        return result;
    }

    result.errors.push_back("glTF file contains no supported triangle meshes: " + path.generic_string());
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Add tinygltf and loader source to CMake**

Add this target near the existing external targets:

```cmake
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE external/tinygltf external/stb)
```

Update `yaoray_assets`:

```cmake
add_library(yaoray_assets STATIC
    src/assets/gltf_loader.cpp
    src/assets/obj_loader.cpp
)
target_include_directories(yaoray_assets PUBLIC include)
target_link_libraries(yaoray_assets PUBLIC yaoray_core yaoray_scene PRIVATE tinygltf tinyobjloader)
target_compile_options(yaoray_assets PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
```

- [ ] **Step 7: Run skeleton tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: all tests pass. The skeleton tests pass because they only cover extension and missing-file diagnostics.

- [ ] **Step 8: Commit**

```powershell
git add CMakeLists.txt external/tinygltf include/yaoray/assets/gltf_loader.hpp src/assets/gltf_loader.cpp tests/assets_tests.cpp
git commit -m "feat: add gltf loader skeleton"
```

---

## Task 6: glTF Geometry, Node Transforms, And Material Factors

**Files:**
- Create: `tests/fixtures/assets/gltf/README.md`
- Add small Khronos fixtures under `tests/fixtures/assets/gltf/`
- Modify: `src/assets/gltf_loader.cpp`
- Test: `tests/assets_tests.cpp`

- [ ] **Step 1: Add fixture source note**

Create `tests/fixtures/assets/gltf/README.md`:

```markdown
# glTF Test Fixtures

These fixtures are small compatibility samples from the Khronos glTF Sample Assets or hand-authored minimal assets derived for importer testing.

Source:
- https://github.com/KhronosGroup/glTF-Sample-Assets

Keep this directory limited to tiny fixtures that are required by unit tests. Do not copy the full sample asset repository.
```

- [ ] **Step 2: Add small glTF fixtures**

Add these fixture sets under `tests/fixtures/assets/gltf/`:

```text
Triangle/glTF/Triangle.gltf
Triangle/glTF/Triangle.bin
TriangleWithoutIndices/glTF/TriangleWithoutIndices.gltf
TriangleWithoutIndices/glTF/TriangleWithoutIndices.bin
SimpleTexture/glTF/SimpleTexture.gltf
SimpleTexture/glTF/SimpleTexture.bin
SimpleTexture/glTF/Texture.png
BoxTextured/glTF-Binary/BoxTextured.glb
```

Copy these assets from `https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/<ModelName>/...` when the new sample repository contains them. If one of these exact assets only exists in the archived `KhronosGroup/glTF-Sample-Models` repository, use that source for this v1 fixture and record it in `tests/fixtures/assets/gltf/README.md`. Keep the exact original directory names so future updates are traceable.

- [ ] **Step 3: Write failing geometry and material tests**

Add to `tests/assets_tests.cpp`:

```cpp
YR_TEST(gltf_loader_loads_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/Triangle/glTF/Triangle.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.mesh->triangles[0].normal.z, 1.0, 1e-6);
}

YR_TEST(gltf_loader_loads_non_indexed_triangle) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/TriangleWithoutIndices/glTF/TriangleWithoutIndices.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.mesh->triangles.size(), std::size_t{1});
}

YR_TEST(gltf_loader_loads_base_color_texture_material) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(!result.mesh->materials.empty());
    YR_EXPECT_TRUE(result.mesh->materials[0].has_diffuse_texture);
    YR_EXPECT_TRUE(result.mesh->materials[0].diffuse_texture_path.generic_string().find("Texture.png") != std::string::npos);
}

YR_TEST(gltf_loader_loads_binary_glb) {
    const yr::AssetLoadResult result =
        yr::LoadGltfMesh(FixturePath("assets/gltf/BoxTextured/glTF-Binary/BoxTextured.glb"));

    YR_EXPECT_TRUE(result.mesh.has_value());
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.mesh->triangles.size() >= std::size_t{12});
}
```

- [ ] **Step 4: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: tests fail with "glTF file contains no supported triangle meshes".

- [ ] **Step 5: Implement glTF accessor decoding**

In `src/assets/gltf_loader.cpp`, add local helpers for accessors:

```cpp
template <typename T>
const T* BufferData(const tinygltf::Model& model, const tinygltf::Accessor& accessor) {
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const std::size_t offset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    return reinterpret_cast<const T*>(buffer.data.data() + offset);
}

bool AccessorHasBufferView(const tinygltf::Accessor& accessor) {
    return accessor.bufferView >= 0;
}

std::optional<Point3f> ReadVec3AccessorValue(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::size_t index) {
    if (!AccessorHasBufferView(accessor) || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC3) {
        return std::nullopt;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::size_t stride = accessor.ByteStride(view) != -1 ? static_cast<std::size_t>(accessor.ByteStride(view)) : sizeof(float) * 3;
    const std::byte* bytes = reinterpret_cast<const std::byte*>(model.buffers[static_cast<std::size_t>(view.buffer)].data.data());
    const std::size_t offset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset) + index * stride;
    const float* values = reinterpret_cast<const float*>(bytes + offset);
    return Point3f{values[0], values[1], values[2]};
}

std::optional<Vec2f> ReadVec2AccessorValue(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::size_t index) {
    if (!AccessorHasBufferView(accessor) || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.type != TINYGLTF_TYPE_VEC2) {
        return std::nullopt;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::size_t stride = accessor.ByteStride(view) != -1 ? static_cast<std::size_t>(accessor.ByteStride(view)) : sizeof(float) * 2;
    const std::byte* bytes = reinterpret_cast<const std::byte*>(model.buffers[static_cast<std::size_t>(view.buffer)].data.data());
    const std::size_t offset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset) + index * stride;
    const float* values = reinterpret_cast<const float*>(bytes + offset);
    return Vec2f{values[0], values[1]};
}

std::optional<std::uint32_t> ReadIndexAccessorValue(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::size_t index) {
    if (!AccessorHasBufferView(accessor) || accessor.type != TINYGLTF_TYPE_SCALAR) {
        return std::nullopt;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    const std::byte* bytes = reinterpret_cast<const std::byte*>(model.buffers[static_cast<std::size_t>(view.buffer)].data.data());
    const int byte_stride = accessor.ByteStride(view);
    const std::size_t component_size =
        accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ? sizeof(std::uint16_t) :
        accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ? sizeof(std::uint32_t) :
        accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? sizeof(std::uint8_t) :
        0;
    if (component_size == 0) {
        return std::nullopt;
    }
    const std::size_t stride = byte_stride != -1 ? static_cast<std::size_t>(byte_stride) : component_size;
    const std::size_t offset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset) + index * stride;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        return *reinterpret_cast<const std::uint16_t*>(bytes + offset);
    }
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        return *reinterpret_cast<const std::uint32_t*>(bytes + offset);
    }
    return *reinterpret_cast<const std::uint8_t*>(bytes + offset);
}
```

- [ ] **Step 6: Implement minimal material conversion**

Add helper:

```cpp
ImportedMaterial ConvertMaterial(const tinygltf::Model& model, const tinygltf::Material& material, const std::filesystem::path& asset_dir) {
    ImportedMaterial imported;
    imported.name = material.name;
    const auto& pbr = material.pbrMetallicRoughness;
    imported.diffuse = Color3f{
        static_cast<float>(pbr.baseColorFactor[0]),
        static_cast<float>(pbr.baseColorFactor[1]),
        static_cast<float>(pbr.baseColorFactor[2])
    };
    imported.roughness = static_cast<float>(pbr.roughnessFactor);
    const float metallic = static_cast<float>(pbr.metallicFactor);
    if (metallic >= 0.5f) {
        imported.type = MaterialKind::Metal;
    } else if (imported.roughness < 0.35f) {
        imported.type = MaterialKind::Plastic;
        imported.specular = 0.04f;
    } else {
        imported.type = MaterialKind::Diffuse;
    }
    if (pbr.baseColorTexture.index >= 0) {
        const tinygltf::Texture& texture = model.textures[static_cast<std::size_t>(pbr.baseColorTexture.index)];
        if (texture.source >= 0) {
            const tinygltf::Image& image = model.images[static_cast<std::size_t>(texture.source)];
            if (!image.uri.empty()) {
                imported.diffuse_texture_path = asset_dir / image.uri;
                imported.has_diffuse_texture = true;
            }
        }
    }
    return imported;
}
```

- [ ] **Step 7: Implement triangle primitive loading**

Replace the skeleton error in `LoadGltfMesh` with traversal that:

1. Converts all model materials with `ConvertMaterial`.
2. Uses `model.defaultScene >= 0 ? model.defaultScene : 0`.
3. Recursively visits scene nodes.
4. For each node with `mesh >= 0`, loads mesh primitives with `mode == TINYGLTF_MODE_TRIANGLES`.
5. Requires `POSITION`.
6. Reads optional `NORMAL` and `TEXCOORD_0`.
7. Expands indexed and non-indexed triangles into `ImportedTriangle`.

Use this triangle fill pattern:

```cpp
ImportedTriangle triangle;
triangle.p0 = p0;
triangle.p1 = p1;
triangle.p2 = p2;
triangle.normal = Normalize(Cross(p1 - p0, p2 - p0));
triangle.uv0 = uv0.value_or(Vec2f{});
triangle.uv1 = uv1.value_or(Vec2f{});
triangle.uv2 = uv2.value_or(Vec2f{});
triangle.has_uv = uv0.has_value() && uv1.has_value() && uv2.has_value();
triangle.n0 = Normalize(n0.value_or(Vec3f{}));
triangle.n1 = Normalize(n1.value_or(Vec3f{}));
triangle.n2 = Normalize(n2.value_or(Vec3f{}));
triangle.has_vertex_normals =
    n0.has_value() && n1.has_value() && n2.has_value() &&
    LengthSquared(triangle.n0) > 0.0f &&
    LengthSquared(triangle.n1) > 0.0f &&
    LengthSquared(triangle.n2) > 0.0f;
triangle.material_index = primitive.material;
mesh.triangles.push_back(triangle);
```

For this task, support TRS transforms and identity matrix nodes. If `node.matrix` is non-empty, apply the 4x4 matrix to positions and the normalized upper-left 3x3 to normals. If a matrix cannot be interpreted as 16 numbers, add an error.

- [ ] **Step 8: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: glTF loader tests pass and existing asset tests remain passing.

- [ ] **Step 9: Commit**

```powershell
git add src/assets/gltf_loader.cpp tests/assets_tests.cpp tests/fixtures/assets/gltf
git commit -m "feat: load static gltf meshes"
```

---

## Task 7: Compile glTF Assets Into RenderScene

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Write failing glTF scene compiler tests**

Add these tests to `tests/render_scene_tests.cpp` near OBJ compiler tests:

```cpp
YR_TEST(scene_compiler_expands_gltf_asset) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "triangle",
        FixturePath("assets/gltf/Triangle/glTF/Triangle.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
}

YR_TEST(scene_compiler_imports_gltf_texture_and_uvs) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.instances.push_back(yr::InstanceDescription{"textured", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_TRUE(!compiled.triangles.empty());
    YR_EXPECT_TRUE(compiled.triangles[0].has_uv);
    YR_EXPECT_TRUE(compiled.triangles[0].has_vertex_normals || !compiled.triangles.empty());
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.materials[0].albedo_texture, 0);
}

YR_TEST(scene_compiler_scene_material_overrides_imported_gltf_material) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{
        "textured",
        FixturePath("assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf")
    });
    scene.materials.push_back(yr::MaterialDescription{
        "override",
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.9f, 0.1f, 0.2f},
        yr::Color3f{}
    });
    yr::InstanceDescription instance;
    instance.asset = "textured";
    instance.material = "override";
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
    YR_EXPECT_TRUE(compiled.textures.empty());
    YR_EXPECT_EQ(compiled.triangles[0].material_index, 0);
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: first test fails because scene compiler still rejects `.gltf` assets.

- [ ] **Step 3: Add glTF extension dispatch**

In `src/render/scene_compiler.cpp`, add:

```cpp
#include <yaoray/assets/gltf_loader.hpp>
```

Add extension helper near `HasObjExtension`:

```cpp
bool HasGltfExtension(const std::filesystem::path& path) {
    return path.extension() == ".gltf" || path.extension() == ".glb";
}
```

- [ ] **Step 4: Preserve imported material type and scalars**

In `CompileImportedMaterials`, replace the hard-coded diffuse assignment with:

```cpp
RenderMaterial render_material;
render_material.type = material.type;
render_material.albedo = material.diffuse;
render_material.emission = material.emission;
render_material.roughness = material.roughness;
render_material.specular = material.specular;
```

- [ ] **Step 5: Generalize asset append function**

Rename `AppendObjAsset` to `AppendExternalAsset`, add a loader function argument, or create a sibling `AppendGltfAsset` with the same body and `LoadGltfMesh`. The resulting glTF branch in `CompileScene` should be:

```cpp
} else if (HasGltfExtension(asset_path)) {
    AppendGltfAsset(
        scene,
        compiled,
        asset_path,
        instance.transform,
        material_index,
        mesh_cache,
        texture_cache,
        result.diagnostics
    );
} else {
```

`AppendGltfAsset` must mirror OBJ behavior:

- cache by asset path,
- forward loader warnings and errors as `assets.path` diagnostics,
- compile imported materials unless a scene-authored material override exists,
- call `AppendImportedMesh`.

- [ ] **Step 6: Run tests**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray_tests.exe
```

Expected: glTF scene compiler tests pass and existing OBJ compiler tests still pass.

- [ ] **Step 7: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile gltf assets into render scene"
```

---

## Task 8: glTF Example Scene And CLI Coverage

**Files:**
- Create: `scenes/examples/gltf_textured_asset.toml`
- Modify: `CMakeLists.txt`
- Test: CTest CLI render target

- [ ] **Step 1: Add example scene**

Create `scenes/examples/gltf_textured_asset.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 256
height = 256
spp = 16
max_depth = 5
seed = 7
threads = 0
light_samples = 2
sampler = "stratified"

[film]
output = "out/gltf_textured_asset.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0.0, 0.6, 3.0]
target = [0.0, 0.35, 0.0]
fov_y = 45.0

[[assets]]
name = "sample"
path = "assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf"

[[instances]]
asset = "sample"
translate = [0.0, 0.0, 0.0]
scale = [1.0, 1.0, 1.0]

[[lights]]
type = "area"
position = [0.0, 3.0, 1.5]
size = [2.0, 2.0]
radiance = [6.0, 6.0, 6.0]

[environment]
type = "constant"
radiance = [0.02, 0.02, 0.025]
strength = 1.0
```

Copy the same three files used by the test fixture into `scenes/examples/assets/gltf/SimpleTexture/glTF/`:

```text
scenes/examples/assets/gltf/SimpleTexture/glTF/SimpleTexture.gltf
scenes/examples/assets/gltf/SimpleTexture/glTF/SimpleTexture.bin
scenes/examples/assets/gltf/SimpleTexture/glTF/Texture.png
```

- [ ] **Step 2: Add CLI CTest coverage**

Add this test to `CMakeLists.txt` inside `if(BUILD_TESTING)`:

```cmake
    add_test(NAME yaoray_cli_render_gltf
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/gltf_textured_asset.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/gltf_textured_asset.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Integrator: path') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 3: Run CLI coverage**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: the new `yaoray_cli_render_gltf` test passes and writes a PNG.

- [ ] **Step 4: Commit**

```powershell
git add CMakeLists.txt scenes/examples/gltf_textured_asset.toml scenes/examples/assets/gltf
git commit -m "test: add gltf render example"
```

---

## Task 9: Documentation And Manual Verification

**Files:**
- Create: `docs/assets/khronos-sample-assets.md`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Document sample asset sources**

Create `docs/assets/khronos-sample-assets.md`:

```markdown
# Khronos glTF Sample Assets Used By YaoRay

YaoRay uses a small subset of Khronos glTF sample assets for importer compatibility tests and manual smoke scenes.

Source repository:
- https://github.com/KhronosGroup/glTF-Sample-Assets

Fixture policy:
- Keep only small assets required by tests or examples.
- Preserve original model directory names when practical.
- Do not vendor the full sample asset repository.
- Re-check each model's license metadata before adding larger or branded assets.

Current intended fixtures:
- Triangle: indexed triangle loading.
- Triangle Without Indices: non-indexed primitive loading.
- Simple Texture: base color texture loading.
- Box Textured: GLB binary loading smoke coverage.
```

- [ ] **Step 2: Update README current status and run instructions**

In `README.md`, add glTF/GLB support to the status list:

```markdown
- OBJ and glTF/GLB static mesh import with UVs, vertex normals, basic material factors, and base color textures
```

Add the new run command:

```powershell
build\Debug\yaoray.exe render scenes\examples\gltf_textured_asset.toml --backend cpu
```

Update the path tracer/importer paragraph with:

```markdown
glTF/GLB import supports static triangle meshes, node transforms, positions, normals, UVs, base color factors, base color textures, and a conservative metallic/roughness mapping into YaoRay's current diffuse/metal/plastic material kinds. It does not yet support animation, skinning, morph targets, normal maps, alpha modes, compressed meshes, sparse accessors, or full glTF PBR semantics.
```

- [ ] **Step 3: Update architecture overview**

In `docs/architecture/overview.md`, add a current implemented slice:

```markdown
- modern static glTF/GLB asset import with positions, normals, UVs, base color textures, and conservative material mapping
```

Update the asset importer paragraph to mention:

```markdown
The asset layer now has OBJ and glTF loaders that feed shared imported mesh/material data into the scene compiler. The render layer still receives flat triangles; a later large-scene milestone should introduce mesh resources and instancing when memory layout and reuse become measurable problems.
```

- [ ] **Step 4: Run full verification**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
cmake --build build-release --config Release
.\build-release\Release\yaoray.exe render .\scenes\examples\gltf_textured_asset.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

Expected:

- Debug build succeeds.
- CTest passes.
- Release build succeeds.
- glTF example writes `scenes/examples/out/gltf_textured_asset.png`.
- Existing textured OBJ example still writes `scenes/examples/out/textured_quad.png`.
- Material showcase still renders.

- [ ] **Step 5: Commit**

```powershell
git add README.md docs/architecture/overview.md docs/assets/khronos-sample-assets.md
git commit -m "docs: document modern asset import"
```

---

## Task 10: Final Integration Check

**Files:**
- Review all changed files.

- [ ] **Step 1: Inspect git status**

Run:

```powershell
git status --short --branch
```

Expected: branch contains only intended changes, with no unrelated local edits staged.

- [ ] **Step 2: Inspect recent commits**

Run:

```powershell
git log --oneline -10
```

Expected: commits include the planned feature slices in order:

```text
docs: document modern asset import
test: add gltf render example
feat: compile gltf assets into render scene
feat: load static gltf meshes
feat: add gltf loader skeleton
feat: compile imported vertex normals
feat: add render shading normals
feat: import obj vertex normals
refactor: share imported asset types
```

- [ ] **Step 3: Run final verification**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
cmake --build build-release --config Release
.\build-release\Release\yaoray.exe render .\scenes\examples\gltf_textured_asset.toml --backend cpu
```

Expected:

- Debug build exits with code 0.
- CTest reports all tests passed.
- Release build exits with code 0.
- Manual render exits with code 0 and reports `Rendered image: scenes/examples/out/gltf_textured_asset.png`.

- [ ] **Step 4: Summarize implementation**

Prepare a concise summary listing:

- OBJ vertex normals and smooth shading support.
- tinygltf dependency and static glTF/GLB loader.
- supported glTF data: positions, normals, UVs, indices, node transforms, base color texture, base color/metallic/roughness factors.
- new Khronos fixtures and manual glTF scene.
- remaining non-goals: animation, skinning, normal maps, alpha modes, compressed meshes, full PBR, CUDA parity.

Do not claim completion unless Step 3 produced the expected evidence in the current session.
