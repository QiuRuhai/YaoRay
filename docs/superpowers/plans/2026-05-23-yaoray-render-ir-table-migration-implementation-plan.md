# YaoRay Render IR Table Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add table-shaped geometry fields to `RenderSceneIR` and make the scene compiler populate them while preserving the existing CPU triangle compatibility path.

**Architecture:** This is Architecture Hardening v1 Slice 3. `RenderSceneIR` will gain `vertices`, `indices`, and `primitives` as backend-neutral geometry tables. The compiler will dual-write table geometry and existing `triangles`; CPU BVH/rendering continues to read `triangles` in this slice so rendered output and current backend behavior remain unchanged.

**Tech Stack:** C++20, CMake, custom `yr_test` framework, existing `yaoray_render` scene compiler, existing CPU backend compatibility path.

---

## Scope

This plan implements the first table-shaped Render IR migration only:

- Add render vertex/index/primitive tables.
- Populate those tables for builtin triangle, inline quad, OBJ, and glTF compilation.
- Keep `RenderSceneIR::triangles` as the compatibility path.
- Keep CPU prepare, BVH, debug renderer, and path tracer reading `triangles`.
- Document the table layout and compatibility path.

This plan does not:

- Remove `RenderSceneIR::triangles`.
- De-duplicate imported vertices across triangles.
- Move CPU BVH building to table geometry.
- Add CUDA packing.
- Decouple `AssetResource` from scene/render enums.

The first table writer intentionally emits one three-index primitive per render triangle. That mirrors current `RenderTriangle` semantics exactly and gives CUDA packing a stable table contract before deeper vertex sharing work.

## File Structure

- Modify `include/yaoray/render/render_scene.hpp`
  - Adds `RenderVertex`, `RenderPrimitive`, and table vectors to `RenderSceneIR`.

- Modify `src/render/scene_compiler.cpp`
  - Adds helper functions that append both table geometry and compatibility triangles.
  - Updates builtin triangle, inline quad, OBJ, and glTF asset compilation paths to use those helpers.

- Modify `src/render/render_scene_hash.cpp`
  - Adds table geometry counts to the render settings hash so checkpoint metadata changes if render-relevant table counts change.

- Modify `tests/render_scene_tests.cpp`
  - Adds default table assertions.
  - Adds compiler table count assertions for builtin triangle, inline quad, OBJ quad, glTF triangle, and FlightHelmet compatibility.
  - Adds a hash-count test for table fields.

- Modify `docs/architecture/overview.md`
  - Documents the table geometry fields and the temporary `triangles` compatibility path.

## Task 1: Write Failing Render IR Table Tests

**Files:**
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add default table assertions**

In `YR_TEST(render_scene_ir_defaults_are_backend_friendly)`, after:

```cpp
    YR_EXPECT_TRUE(scene.triangles.empty());
```

add:

```cpp
    YR_EXPECT_TRUE(scene.vertices.empty());
    YR_EXPECT_TRUE(scene.indices.empty());
    YR_EXPECT_TRUE(scene.primitives.empty());
```

- [ ] **Step 2: Add builtin table assertions**

In `YR_TEST(scene_compiler_outputs_backend_neutral_triangles_for_builtin_triangle)`, after:

```cpp
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
```

add:

```cpp
    YR_EXPECT_EQ(result.scene.value().vertices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().indices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().indices[0], std::uint32_t{0});
    YR_EXPECT_EQ(result.scene.value().indices[1], std::uint32_t{1});
    YR_EXPECT_EQ(result.scene.value().indices[2], std::uint32_t{2});
    YR_EXPECT_EQ(result.scene.value().primitives[0].first_index, std::uint32_t{0});
    YR_EXPECT_EQ(result.scene.value().primitives[0].index_count, std::uint32_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives[0].material_index, 0);
```

- [ ] **Step 3: Add OBJ table assertions**

In `YR_TEST(scene_compiler_outputs_backend_neutral_triangles_for_obj_quad)`, after:

```cpp
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
```

add:

```cpp
    YR_EXPECT_EQ(result.scene.value().vertices.size(), std::size_t{6});
    YR_EXPECT_EQ(result.scene.value().indices.size(), std::size_t{6});
    YR_EXPECT_EQ(result.scene.value().primitives.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().primitives[0].first_index, std::uint32_t{0});
    YR_EXPECT_EQ(result.scene.value().primitives[1].first_index, std::uint32_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives[0].index_count, std::uint32_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives[1].index_count, std::uint32_t{3});
```

- [ ] **Step 4: Add inline quad table assertions**

In `YR_TEST(scene_compiler_expands_inline_quad_asset)`, after:

```cpp
    YR_EXPECT_EQ(compiled.triangles.size(), std::size_t{2});
```

add:

```cpp
    YR_EXPECT_EQ(compiled.vertices.size(), std::size_t{6});
    YR_EXPECT_EQ(compiled.indices.size(), std::size_t{6});
    YR_EXPECT_EQ(compiled.primitives.size(), std::size_t{2});
    YR_EXPECT_EQ(compiled.primitives[0].material_index, 0);
    YR_EXPECT_EQ(compiled.primitives[1].material_index, 0);
```

- [ ] **Step 5: Add glTF table assertions**

In `YR_TEST(scene_compiler_expands_gltf_asset)`, after:

```cpp
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
```

add:

```cpp
    YR_EXPECT_EQ(result.scene.value().vertices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().indices.size(), std::size_t{3});
    YR_EXPECT_EQ(result.scene.value().primitives.size(), std::size_t{1});
```

- [ ] **Step 6: Add FlightHelmet table compatibility assertions**

In `YR_TEST(scene_compiler_compiles_flight_helmet_pbr_fields)`, after:

```cpp
    YR_EXPECT_TRUE(!compiled.triangles.empty());
```

add:

```cpp
    YR_EXPECT_EQ(compiled.vertices.size(), compiled.triangles.size() * std::size_t{3});
    YR_EXPECT_EQ(compiled.indices.size(), compiled.triangles.size() * std::size_t{3});
    YR_EXPECT_EQ(compiled.primitives.size(), compiled.triangles.size());
```

- [ ] **Step 7: Add hash table-count sensitivity test**

Add this test after `YR_TEST(render_settings_hash_changes_for_camera_and_resource_counts)`:

```cpp
YR_TEST(render_settings_hash_changes_for_geometry_table_counts) {
    yr::SceneCompileResult base_result = yr::CompileScene(MakeBaseScene());
    YR_EXPECT_TRUE(base_result.scene.has_value());

    yr::RenderSceneIR changed = base_result.scene.value();
    changed.vertices.push_back(yr::RenderVertex{});

    YR_EXPECT_TRUE(yr::ComputeRenderSettingsHash(base_result.scene.value()) != yr::ComputeRenderSettingsHash(changed));
}
```

- [ ] **Step 8: Run tests and confirm expected compile failure**

Run:

```powershell
cmake --build build
```

Expected: build fails because `RenderSceneIR::vertices`, `RenderSceneIR::indices`, `RenderSceneIR::primitives`, `RenderVertex`, and `RenderPrimitive` do not exist yet.

## Task 2: Add Render IR Table Types

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add table record types**

In `include/yaoray/render/render_scene.hpp`, after `struct RenderTriangle`, add:

```cpp
struct RenderVertex {
    Point3f position;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    Vec2f uv;
    Vec3f tangent;
    float tangent_handedness = 1.0f;
    bool has_uv = false;
    bool has_normal = false;
    bool has_tangent = false;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = 0;
};
```

- [ ] **Step 2: Add table vectors to `RenderSceneIR`**

In `RenderSceneIR`, replace:

```cpp
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
```

with:

```cpp
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
```

Keep `triangles` after the table fields because it is now the compatibility view derived from the table writer.

- [ ] **Step 3: Run tests and confirm compiler table assertions still fail**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: build succeeds. Tests that only check default empty table fields pass. Compiler table-count tests fail because the compiler does not populate the new table fields yet.

## Task 3: Populate Table Geometry In The Scene Compiler

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add table append helper functions**

In `src/render/scene_compiler.cpp`, before `bool AppendTriangle(...)`, add:

```cpp
RenderVertex VertexFromTriangleCorner(
    const RenderTriangle& triangle,
    Point3f position,
    Vec2f uv,
    Vec3f normal,
    Vec3f tangent,
    float tangent_handedness
) {
    RenderVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.uv = uv;
    vertex.tangent = tangent;
    vertex.tangent_handedness = tangent_handedness;
    vertex.has_uv = triangle.has_uv;
    vertex.has_normal = triangle.has_vertex_normals;
    vertex.has_tangent = triangle.has_tangents;
    return vertex;
}

void AppendRenderTriangle(RenderSceneIR& compiled, RenderTriangle triangle) {
    const std::uint32_t first_vertex = static_cast<std::uint32_t>(compiled.vertices.size());
    const std::uint32_t first_index = static_cast<std::uint32_t>(compiled.indices.size());

    const Vec3f n0 = triangle.has_vertex_normals ? triangle.n0 : triangle.normal;
    const Vec3f n1 = triangle.has_vertex_normals ? triangle.n1 : triangle.normal;
    const Vec3f n2 = triangle.has_vertex_normals ? triangle.n2 : triangle.normal;

    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p0,
        triangle.uv0,
        n0,
        triangle.t0,
        triangle.tangent_handedness0
    ));
    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p1,
        triangle.uv1,
        n1,
        triangle.t1,
        triangle.tangent_handedness1
    ));
    compiled.vertices.push_back(VertexFromTriangleCorner(
        triangle,
        triangle.p2,
        triangle.uv2,
        n2,
        triangle.t2,
        triangle.tangent_handedness2
    ));

    compiled.indices.push_back(first_vertex + 0);
    compiled.indices.push_back(first_vertex + 1);
    compiled.indices.push_back(first_vertex + 2);
    compiled.primitives.push_back(RenderPrimitive{first_index, 3, triangle.material_index});
    compiled.triangles.push_back(triangle);
}
```

- [ ] **Step 2: Update author-authored triangle append path**

In `AppendTriangle(...)`, replace:

```cpp
    compiled.triangles.push_back(RenderTriangle{
        p0,
        p1,
        p2,
        normal,
        material_index
    });
```

with:

```cpp
    AppendRenderTriangle(compiled, RenderTriangle{
        p0,
        p1,
        p2,
        normal,
        material_index
    });
```

- [ ] **Step 3: Update builtin triangle append path**

In `AppendBuiltinTriangle(...)`, replace:

```cpp
    compiled.triangles.push_back(RenderTriangle{
        world_p0,
        world_p1,
        world_p2,
        Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
        material_index
    });
```

with:

```cpp
    AppendRenderTriangle(compiled, RenderTriangle{
        world_p0,
        world_p1,
        world_p2,
        Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
        material_index
    });
```

- [ ] **Step 4: Update imported asset append path**

In `AppendAssetPrimitive(...)`, replace:

```cpp
        compiled.triangles.push_back(render_triangle);
```

with:

```cpp
        AppendRenderTriangle(compiled, render_triangle);
```

- [ ] **Step 5: Run compiler tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: table-count compiler tests pass. The hash table-count test still fails until Task 4.

## Task 4: Include Table Counts In Render Settings Hash

**Files:**
- Modify: `src/render/render_scene_hash.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add table counts to the settings hash**

In `src/render/render_scene_hash.cpp`, replace:

```cpp
    HashValue(hash, scene.triangles.size());
    HashValue(hash, scene.materials.size());
```

with:

```cpp
    HashValue(hash, scene.vertices.size());
    HashValue(hash, scene.indices.size());
    HashValue(hash, scene.primitives.size());
    HashValue(hash, scene.triangles.size());
    HashValue(hash, scene.materials.size());
```

- [ ] **Step 2: Run unit tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all `yaoray_tests` pass.

- [ ] **Step 3: Run CLI regression tests**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all CTest tests pass. Offline checkpoint/resume tests may produce new checkpoint hashes because the hash version now includes table counts; they should still pass because tests regenerate local checkpoint files.

- [ ] **Step 4: Commit table IR implementation**

Run:

```powershell
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp src/render/render_scene_hash.cpp tests/render_scene_tests.cpp
git commit -m "refactor: add render ir geometry tables"
```

Expected: commit succeeds and includes only table IR code and tests.

## Task 5: Document The Table IR Compatibility Path

**Files:**
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update the render layer paragraph**

In `docs/architecture/overview.md`, replace:

```markdown
The render layer compiles that semantic scene into backend-neutral input. The current `yaoray_render` slice provides a minimal `RenderSceneIR` with render settings, camera data, environment data, area lights, materials, textures, and flat world-space triangles. Rendering is dispatched through a two-stage backend interface: each backend first prepares backend-owned runtime data from a `RenderSceneIR` value, then renders from that prepared scene without app-layer knowledge of CPU BVHs, CUDA buffers, or future OptiX handles. Prepared scenes own the render input they need, so rendering does not depend on caller-owned `RenderSceneIR` lifetime after preparation.
```

with:

```markdown
The render layer compiles that semantic scene into backend-neutral input. The current `yaoray_render` slice provides `RenderSceneIR` with render settings, camera data, environment data, area lights, materials, textures, table-shaped geometry (`vertices`, `indices`, and `primitives`), and a temporary flat-triangle compatibility view. Rendering is dispatched through a two-stage backend interface: each backend first prepares backend-owned runtime data from a `RenderSceneIR` value, then renders from that prepared scene without app-layer knowledge of CPU BVHs, CUDA buffers, or future OptiX handles. Prepared scenes own the render input they need, so rendering does not depend on caller-owned `RenderSceneIR` lifetime after preparation.
```

- [ ] **Step 2: Update the render compiler paragraph**

In the paragraph beginning with:

```markdown
The render compiler consumes `AssetResource` from the default asset scene.
```

replace the sentence:

```markdown
The render compiler consumes `AssetResource` from the default asset scene. It recursively traverses from the default asset scene's root nodes, composes node and instance transforms, maps asset materials and textures to render materials and render-owned RGBA textures, and expands mesh primitives into flat world-space triangles in `RenderSceneIR`.
```

with:

```markdown
The render compiler consumes `AssetResource` from the default asset scene. It recursively traverses from the default asset scene's root nodes, composes node and instance transforms, maps asset materials and textures to render materials and render-owned RGBA textures, and expands mesh primitives into table-shaped geometry plus the current flat world-space triangle compatibility view in `RenderSceneIR`.
```

Leave the rest of the paragraph unchanged.

- [ ] **Step 3: Add current limitation note**

After the updated render compiler paragraph, add:

```markdown
The initial table geometry writer mirrors the compatibility triangle stream with one three-index primitive per render triangle. It establishes a GPU-packable contract without changing CPU traversal yet; vertex sharing and CPU table-native BVH preparation remain follow-up work.
```

- [ ] **Step 4: Run documentation search**

Run:

```powershell
rg -n "table-shaped geometry|vertices|indices|primitives|compatibility view" docs\architecture\overview.md include\yaoray\render src\render tests\render_scene_tests.cpp
```

Expected: output shows the new overview wording, `RenderSceneIR` table fields, compiler append helper, and render scene tests.

- [ ] **Step 5: Run full verification**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Expected: all commands pass.

- [ ] **Step 6: Commit documentation update**

Run:

```powershell
git add docs/architecture/overview.md
git commit -m "docs: document render ir table geometry"
```

Expected: commit succeeds and includes only `docs/architecture/overview.md`.

## Final Verification

- [ ] **Step 1: Confirm branch state**

Run:

```powershell
git status --short --branch
```

Expected: the branch may still show pre-existing unrelated local changes such as Duck assets or the dielectric spec edit, but no code or documentation changes from this plan remain uncommitted.

- [ ] **Step 2: Confirm recent commits**

Run:

```powershell
git log --oneline --decorate -5
```

Expected: the latest commits include:

```text
docs: document render ir table geometry
refactor: add render ir geometry tables
```

- [ ] **Step 3: Record verification commands in the final response**

Report these commands and pass/fail status:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

## Self-Review Notes

Spec coverage:

- Slice 3 table geometry fields are covered by Tasks 1 and 2.
- Compiler population for builtin, inline quad, OBJ, and glTF paths is covered by Tasks 1 and 3.
- CPU compatibility is preserved by keeping `RenderSceneIR::triangles` and leaving CPU prepare/render code unchanged.
- Hash awareness is covered by Task 4.
- Documentation is covered by Task 5.

Type consistency:

- The plan consistently uses `RenderVertex`, `RenderPrimitive`, `RenderSceneIR::vertices`, `RenderSceneIR::indices`, and `RenderSceneIR::primitives`.
- Existing `RenderTriangle` and `RenderSceneIR::triangles` remain the compatibility view.
- New table indices use `std::uint32_t`, matching the existing C++20 and `<cstdint>` availability in `render_scene.hpp`.
