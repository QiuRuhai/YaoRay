# YaoRay AssetResource Decoupling v1 Design

## Goal

Separate imported asset material semantics from the current scene/render
material enum without changing rendered behavior.

`AssetResource` should describe what an asset file contains. The render compiler
should decide how those imported fields are lowered into the current
`RenderMaterial` and `MaterialKind` compatibility model.

## Background

The renderer now has clearer boundaries:

```text
SceneDescription
  -> AssetResource
  -> RenderSceneIR
  -> backend-owned PreparedScene
```

`RenderSceneIR` has table-shaped geometry, and backend preparation owns runtime
data. The next boundary leak is material ownership: `AssetMaterial` currently
contains `approximate_type = MaterialKind::Diffuse`, which forces
`asset_resource.hpp` to include `yaoray/scene/scene.hpp`. glTF and OBJ loaders
therefore decide a render material kind while importing asset files.

That decision belongs in the render compiler. Asset import should preserve
asset fields such as base color, metallic factor, roughness factor, texture
indices, alpha mode, and double-sided metadata. Rendering can then lower those
fields into the current CPU-compatible material model.

## Scope

This slice removes the asset-layer dependency on scene/render material enums.

In scope:

- Remove `AssetMaterial::approximate_type`.
- Remove `#include <yaoray/scene/scene.hpp>` from
  `include/yaoray/assets/asset_resource.hpp`.
- Keep `AssetMaterial` as the source-material record for OBJ and glTF imports.
- Move the current glTF/OBJ material-kind approximation into
  `src/render/scene_compiler.cpp`.
- Preserve existing render behavior for OBJ, glTF, FlightHelmet, and textured
  asset scenes.
- Add tests that make the boundary explicit.

Out of scope:

- No new material model.
- No BSDF rewrite.
- No closure graph or full glTF PBR implementation.
- No CUDA material packing.
- No scene TOML material syntax changes.
- No imported material extension support.

## Design

### Asset Layer

`AssetMaterial` remains a simple imported-material record:

```text
AssetMaterial
  name
  base_color
  base_color_alpha
  emission
  metallic
  roughness
  specular
  base_color_texture
  metallic_roughness_texture
  normal_texture
  occlusion_texture
  emissive_texture
  normal_scale
  occlusion_strength
  alpha_mode
  alpha_cutoff
  double_sided
```

It will no longer contain `MaterialKind approximate_type`.

glTF import will continue to preserve PBR metallic-roughness fields and texture
indices. OBJ import will continue to preserve diffuse MTL color and diffuse
texture data. Neither loader chooses `Diffuse`, `Metal`, or `Plastic`.

### Render Compiler Lowering

The render compiler owns source-material-to-render-material lowering.

Suggested helper shape:

```text
MaterialKind LowerAssetMaterialKind(const AssetMaterial& material)
```

The helper preserves today's compatibility behavior:

- `material.metallic >= 0.5f` lowers to `MaterialKind::Metal`.
- Otherwise, `material.roughness < 0.35f` lowers to `MaterialKind::Plastic`.
- Otherwise, it lowers to `MaterialKind::Diffuse`.

OBJ materials should keep diffuse-compatible defaults. Because current OBJ MTL
support only preserves `Kd` and diffuse texture data, the loader should not
author a low roughness value that would trigger the glTF plastic approximation.
Setting imported OBJ roughness to the diffuse-compatible default keeps the
current render behavior without adding an asset source enum.

All other material fields are copied as they are today:

- color and alpha factors
- emission factor
- scalar roughness, metallic, specular
- texture handles after texture compilation
- normal and occlusion controls
- alpha mode, alpha cutoff, and double-sided flags

### Boundary Rule

After this slice:

- `yaoray_assets` can include core math and texture sampler types.
- `yaoray_assets` must not include scene parser or render material headers.
- Render compiler code may include both asset and render headers because it is
  the lowering boundary.

## Error Handling

No new user-facing diagnostics are required.

Existing diagnostics stay in place:

- asset loader errors remain asset-file parsing and validation errors
- scene compiler errors remain render lowering and texture-loading errors
- unsupported glTF `alphaMode = BLEND` still emits the current compatibility
  warning during render compilation

## Testing

Add or adjust tests around three boundaries:

1. Asset defaults and imports

   - `AssetMaterial` default tests no longer mention a render material kind.
   - glTF PBR loader tests still prove base color, alpha, metallic, roughness,
     texture slots, alpha mode, alpha cutoff, and double-sided metadata are
     preserved.
   - OBJ MTL loader tests still prove diffuse color and texture preservation.

2. Render lowering

   - Scene compiler tests continue proving a metallic glTF material lowers to
     `MaterialKind::Metal`.
   - Add focused tests for rough non-metallic imported material lowering if
     coverage is missing.
   - OBJ textured asset tests continue proving OBJ materials lower to the same
     render behavior as before.

3. Include boundary

   - The project must compile after removing the `scene.hpp` include from
     `asset_resource.hpp`.
   - A search check should confirm `asset_resource.hpp` does not reference
     `MaterialKind` or include scene/render material headers.

Full verification remains:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

## Risks

- Risk: moving the lowering rule changes OBJ material appearance.
  Mitigation: preserve current OBJ render compiler tests and add focused
  lowering assertions if needed.

- Risk: the generic glTF lowering rule remains approximate.
  Mitigation: document this slice as boundary cleanup only. Full glTF PBR or a
  closure-based material system remains future work.

- Risk: adding an asset material model enum now could become premature API.
  Mitigation: avoid introducing a new enum in this slice unless tests show the
  current fields are insufficient to preserve behavior.

## Acceptance Criteria

- `include/yaoray/assets/asset_resource.hpp` no longer includes
  `yaoray/scene/scene.hpp`.
- `AssetMaterial` no longer contains `MaterialKind` or an equivalent render
  material enum.
- glTF and OBJ loaders no longer assign render material kinds.
- The scene compiler owns imported material lowering into `RenderMaterial`.
- Existing OBJ, glTF, FlightHelmet, and material rendering tests pass.
- Architecture documentation names this as an asset/render boundary cleanup
  without claiming a new material system.

## Follow-Up Work

After this boundary is clean, the next architecture-hardening slice should be
the CUDA prepared scene and host-side packer prototype. A larger material system
redesign should wait until there is a clear target, such as fuller glTF PBR
support, CUDA material packing, or a closure-based BSDF representation.
