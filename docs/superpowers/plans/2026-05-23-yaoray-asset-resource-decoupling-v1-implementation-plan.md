# YaoRay AssetResource Decoupling v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove render material enum ownership from `AssetResource` while preserving current OBJ/glTF rendering behavior.

**Architecture:** `AssetResource` will keep imported material fields only. The render compiler will lower `AssetMaterial` into the current `RenderMaterial` and `MaterialKind` compatibility model. OBJ and glTF loaders will stop assigning render material kinds.

**Tech Stack:** C++20, CMake/Ninja, custom `yr_test` framework, existing `yaoray_assets`, `yaoray_render`, and CPU backend tests.

---

## Scope

This plan implements the narrow AssetResource decoupling slice from
`docs/superpowers/specs/2026-05-23-yaoray-asset-resource-decoupling-v1-design.md`.

It does:

- Add tests that lock current imported material behavior.
- Move imported-material kind lowering into `src/render/scene_compiler.cpp`.
- Remove `AssetMaterial::approximate_type`.
- Remove `asset_resource.hpp`'s include of `yaoray/scene/scene.hpp`.
- Preserve OBJ, glTF, FlightHelmet, and textured asset behavior.
- Document the resulting boundary.

It does not:

- Add a new material model.
- Change BSDF behavior.
- Implement full glTF PBR.
- Add CUDA material packing.
- Change scene TOML material syntax.

## File Structure

- Modify `tests/assets_tests.cpp`
  - Assert imported OBJ material fields stay diffuse-compatible without render enum checks.

- Modify `tests/render_scene_tests.cpp`
  - Assert OBJ imported materials still lower to `MaterialKind::Diffuse`.
  - Existing glTF PBR compiler tests continue asserting metallic glTF material lowering.

- Modify `src/render/scene_compiler.cpp`
  - Add `LowerAssetMaterialKind(const AssetMaterial&)`.
  - Use the helper in `CompileAssetMaterials()`.

- Modify `src/assets/obj_loader.cpp`
  - Stop assigning a render material kind.
  - Keep OBJ roughness at the `AssetMaterial` default so OBJ remains diffuse-compatible under the compiler lowering rule.

- Modify `src/assets/gltf_loader.cpp`
  - Stop assigning render material kinds in `ConvertMaterial()`.

- Modify `include/yaoray/assets/asset_resource.hpp`
  - Remove the scene header include.
  - Remove `AssetMaterial::approximate_type`.

- Modify `docs/architecture/overview.md`
  - Document that assets preserve imported material fields and the render compiler owns material lowering.

## Task 1: Lock Imported Material Behavior With Tests

**Files:**
- Modify: `tests/assets_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add default imported material scalar assertions**

In `tests/assets_tests.cpp`, in `YR_TEST(asset_material_defaults_include_gltf_pbr_fields)`, after:

```cpp
    const yr::AssetMaterial material;
```

add:

```cpp
    YR_EXPECT_NEAR(material.metallic, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
```

- [ ] **Step 2: Add OBJ diffuse-compatible imported field assertions**

In `tests/assets_tests.cpp`, in `YR_TEST(obj_loader_imports_basic_mtl_material)`, after:

```cpp
    YR_EXPECT_NEAR(material.base_color.z, 0.75, 1e-6);
```

add:

```cpp
    YR_EXPECT_NEAR(material.metallic, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.roughness, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
```

This assertion should initially fail because `ConvertObjMaterial()` currently authors
`roughness = 0.0f`.

- [ ] **Step 3: Add OBJ render lowering assertion**

In `tests/render_scene_tests.cpp`, in `YR_TEST(scene_compiler_imports_obj_material_texture_and_uvs)`, after:

```cpp
    YR_EXPECT_EQ(compiled.materials.size(), std::size_t{1});
```

add:

```cpp
    YR_EXPECT_EQ(compiled.materials[0].type, yr::MaterialKind::Diffuse);
```

This locks the current render behavior before removing `AssetMaterial::approximate_type`.

- [ ] **Step 4: Run tests and confirm the expected failure**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected:

- Build succeeds.
- `asset_material_defaults_include_gltf_pbr_fields` passes.
- `scene_compiler_imports_obj_material_texture_and_uvs` passes.
- `obj_loader_imports_basic_mtl_material` fails on the new roughness expectation.

## Task 2: Move Imported Material Lowering Into The Render Compiler

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `src/assets/obj_loader.cpp`
- Test: `tests/assets_tests.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add the compiler-owned lowering helper**

In `src/render/scene_compiler.cpp`, before `std::vector<int> CompileAssetMaterials(...)`, add:

```cpp
MaterialKind LowerAssetMaterialKind(const AssetMaterial& material) {
    if (material.metallic >= 0.5f) {
        return MaterialKind::Metal;
    }
    if (material.roughness < 0.35f) {
        return MaterialKind::Plastic;
    }
    return MaterialKind::Diffuse;
}
```

- [ ] **Step 2: Use the helper in asset material compilation**

In `CompileAssetMaterials(...)`, replace:

```cpp
        render_material.type = material.approximate_type;
```

with:

```cpp
        render_material.type = LowerAssetMaterialKind(material);
```

- [ ] **Step 3: Keep OBJ material roughness diffuse-compatible**

In `src/assets/obj_loader.cpp`, in `ConvertObjMaterial(...)`, remove this line:

```cpp
    imported.roughness = 0.0f;
```

Do not add a replacement assignment. `AssetMaterial::roughness` already defaults
to `1.0f`, which lowers to `MaterialKind::Diffuse`.

- [ ] **Step 4: Run focused tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all `yaoray_tests` pass. The existing glTF PBR compiler test still
expects `MaterialKind::Metal`, and the new OBJ test expects diffuse-compatible
roughness plus diffuse render lowering.

- [ ] **Step 5: Commit the behavior-preserving lowering move**

Run:

```powershell
git add src/render/scene_compiler.cpp src/assets/obj_loader.cpp tests/assets_tests.cpp tests/render_scene_tests.cpp
git commit -m "refactor: move asset material lowering to compiler"
```

Expected: commit succeeds and contains only the helper, OBJ roughness default
preservation, and behavior tests.

## Task 3: Remove Render Material Enum Ownership From AssetResource

**Files:**
- Modify: `include/yaoray/assets/asset_resource.hpp`
- Modify: `src/assets/gltf_loader.cpp`
- Modify: `src/assets/obj_loader.cpp`
- Test: `tests/assets_tests.cpp`
- Test: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Remove the scene header include from AssetResource**

In `include/yaoray/assets/asset_resource.hpp`, remove:

```cpp
#include <yaoray/scene/scene.hpp>
```

- [ ] **Step 2: Remove the render enum field from AssetMaterial**

In `include/yaoray/assets/asset_resource.hpp`, in `struct AssetMaterial`, remove:

```cpp
    MaterialKind approximate_type = MaterialKind::Diffuse;
```

The resulting `AssetMaterial` starts with:

```cpp
struct AssetMaterial {
    std::string name;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    float base_color_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
```

- [ ] **Step 3: Remove glTF loader render-kind assignments**

In `src/assets/gltf_loader.cpp`, in `ConvertMaterial(...)`, remove this block:

```cpp
    if (imported.metallic >= 0.5f) {
        imported.approximate_type = MaterialKind::Metal;
    } else if (imported.roughness < 0.35f) {
        imported.approximate_type = MaterialKind::Plastic;
        imported.specular = 0.04f;
    } else {
        imported.approximate_type = MaterialKind::Diffuse;
    }
```

Do not add a replacement in the loader. The render compiler now owns this
decision.

- [ ] **Step 4: Remove OBJ loader render-kind assignment**

In `src/assets/obj_loader.cpp`, in `ConvertObjMaterial(...)`, remove this line if
it is still present:

```cpp
    imported.approximate_type = MaterialKind::Diffuse;
```

After Task 2 and this step, `ConvertObjMaterial(...)` should begin like this:

```cpp
    AssetMaterial imported;
    imported.name = material.name;
    imported.base_color = Color3f{material.diffuse[0], material.diffuse[1], material.diffuse[2]};
    imported.specular = 0.04f;
```

- [ ] **Step 5: Run compile and tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: build succeeds and all `yaoray_tests` pass. If the build reports
`approximate_type` or `MaterialKind` in assets code, remove the remaining
asset-layer reference and rerun.

- [ ] **Step 6: Verify the include boundary by search**

Run:

```powershell
$matches = rg -n "scene\.hpp|MaterialKind|approximate_type" include\yaoray\assets src\assets tests\assets_tests.cpp; if ($LASTEXITCODE -eq 0) { $matches; exit 1 } elseif ($LASTEXITCODE -eq 1) { exit 0 } else { exit $LASTEXITCODE }
```

Expected: command exits 0 because no matches remain in asset headers, asset
loaders, or asset tests.

- [ ] **Step 7: Commit the AssetResource boundary cleanup**

Run:

```powershell
git add include/yaoray/assets/asset_resource.hpp src/assets/gltf_loader.cpp src/assets/obj_loader.cpp
git commit -m "refactor: decouple asset materials from render enums"
```

Expected: commit succeeds and contains only asset boundary cleanup.

## Task 4: Document The Asset/Render Material Boundary

**Files:**
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update the asset layer paragraph**

In `docs/architecture/overview.md`, find the paragraph beginning:

```markdown
The asset layer imports OBJ and static glTF/GLB files into `AssetResource`.
```

Replace the sentence that starts with that text with:

```markdown
The asset layer imports OBJ and static glTF/GLB files into `AssetResource`, preserving source asset geometry, textures, and material fields without owning render material kinds. OBJ import preserves vertex normals, UV coordinates, and basic MTL diffuse data (`Kd` and PNG `map_Kd`). glTF/GLB import preserves default or first scenes, node hierarchy transforms, `TRIANGLES` primitives, positions, optional normals, optional UVs, imported tangents, indexed and non-indexed geometry, base-color RGBA factors and textures, metallic/roughness factors and textures, normal texture scale, occlusion texture strength, emissive factors and textures, alpha mode/cutoff metadata, double-sided metadata, and sampler wrap modes. Embedded image buffers and `data:` image URIs are intentionally rejected for now.
```

- [ ] **Step 2: Update the render compiler paragraph**

In the next paragraph beginning:

```markdown
The render compiler consumes `AssetResource` from the default asset scene.
```

Replace the first sentence with:

```markdown
The render compiler consumes `AssetResource` from the default asset scene. It recursively traverses from the default asset scene's root nodes, composes node and instance transforms, lowers imported material fields into current diffuse/metal/plastic render material compatibility behavior, maps textures to render-owned RGBA textures, and expands mesh primitives into table-shaped geometry plus the current flat world-space triangle compatibility view in `RenderSceneIR`.
```

Leave the rest of the paragraph unchanged.

- [ ] **Step 3: Run documentation and boundary search**

Run:

```powershell
rg -n "render material kinds|lowers imported material fields|AssetResource|approximate_type|MaterialKind" docs\architecture\overview.md docs\superpowers\specs\2026-05-23-yaoray-asset-resource-decoupling-v1-design.md include\yaoray\assets src\assets src\render\scene_compiler.cpp
```

Expected:

- `docs/architecture/overview.md` mentions source asset fields and compiler lowering.
- The spec mentions the removed historical field.
- `src/render/scene_compiler.cpp` contains `LowerAssetMaterialKind`.
- No `MaterialKind` or `approximate_type` matches appear under `include\yaoray\assets` or `src\assets`.

- [ ] **Step 4: Run full verification**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all `yaoray_tests` pass, and all CTest tests pass.

- [ ] **Step 5: Commit the documentation update**

Run:

```powershell
git add docs/architecture/overview.md
git commit -m "docs: document asset material lowering boundary"
```

Expected: commit succeeds and contains only `docs/architecture/overview.md`.

## Final Verification

- [ ] **Step 1: Confirm branch state**

Run:

```powershell
git status --short --branch
```

Expected: the branch may still show pre-existing unrelated local changes such as
Duck assets or the dielectric spec edit, but no code or documentation changes
from this plan remain uncommitted.

- [ ] **Step 2: Confirm recent commits**

Run:

```powershell
git log --oneline --decorate -6
```

Expected: the latest commits include:

```text
docs: document asset material lowering boundary
refactor: decouple asset materials from render enums
refactor: move asset material lowering to compiler
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

- AssetResource removal of render enum ownership is covered by Task 3.
- Loader behavior preservation is covered by Tasks 1 and 2.
- Compiler-owned lowering is covered by Task 2.
- Boundary search is covered by Task 3.
- Architecture documentation is covered by Task 4.

Type consistency:

- The plan consistently uses existing `AssetMaterial`, `RenderMaterial`, and
  `MaterialKind`.
- The new helper is local to `src/render/scene_compiler.cpp`.
- `AssetResource` remains dependent on core math and texture sampler types only.
