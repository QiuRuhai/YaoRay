# YaoRay Modern Asset Import v1 Design

## Context

YaoRay now has a CPU path tracer with BVH traversal, tile threading, direct-light
MIS, Russian roulette, diffuse/mirror/metal/plastic materials, PNG output, OBJ
UV import, basic OBJ MTL parsing, diffuse texture loading, and a Cornell-style
manual test scene. The current asset path can prove geometry, UV, material, and
texture plumbing, but it is still closer to a debug importer than a modern asset
pipeline.

OBJ remains useful because it is small, readable, and easy to test. It should not
be the long-term primary asset format. Modern public asset libraries and DCC
tools commonly provide glTF or GLB, and glTF 2.0 includes scene hierarchy,
binary buffers, images, and a core metallic-roughness PBR material model. This
slice moves YaoRay toward modern asset ingestion without forcing a full mesh
resource-system rewrite yet.

Relevant external references:

- Khronos glTF overview: https://www.khronos.org/gltf
- Khronos glTF PBR material overview: https://www.khronos.org/gltf/pbr
- glTF 2.0 specification registry: https://registry.khronos.org/glTF/
- Khronos glTF Sample Assets: https://github.com/KhronosGroup/glTF-Sample-Assets
- tinygltf header-only C++ loader: https://github.com/syoyo/tinygltf

## Goals

- Import OBJ vertex normals and preserve them through scene compilation.
- Add render-triangle vertex normal storage for smooth shading.
- Use interpolated shading normals in the CPU path tracer while retaining
  geometric normals for intersection, visibility, and ray bias decisions.
- Add a focused glTF/GLB importer for static mesh assets.
- Support glTF node hierarchy transforms.
- Support glTF mesh primitives with positions, normals, UVs, and indices.
- Support non-indexed triangle primitives.
- Support glTF `pbrMetallicRoughness` material factors.
- Support glTF base color textures through the existing render texture path.
- Add official small Khronos sample assets to importer compatibility tests.
- Add one small real glTF example scene for manual render verification.
- Keep `RenderScene` flat-triangle output as the backend contract for this
  milestone.

## Non-Goals

- No animation, skinning, or morph targets.
- No camera or light import from glTF.
- No multiple glTF scene selection UI beyond default scene or first scene.
- No normal maps, tangent generation, or tangent-space shading.
- No occlusion, emissive, metallic, or roughness texture maps.
- No alpha blend or alpha mask semantics.
- No exact `doubleSided` material behavior.
- No Draco or meshopt decompression.
- No sparse accessor support in v1.
- No glTF extension material models such as transmission, clearcoat, sheen,
  volume, specular, or iridescence.
- No complete PBR BSDF rewrite.
- No CUDA importer or CUDA material evaluation.
- No large-scene resource-system refactor.

## Target User Story

A user can reference a small `.gltf` or `.glb` asset from a TOML scene, render it
with the CPU path tracer, and see geometry with smooth normals, base color
material factors, and base color textures. The same renderer should still handle
existing OBJ, Cornell, and material showcase scenes.

## Architecture

The importer should be structured around a shared imported-asset representation,
not two independent OBJ and glTF scene-compiler code paths.

### Asset Loader Layer

Keep the existing OBJ loader and add a new glTF loader. Both should produce an
intermediate asset structure that represents the information YaoRay can consume:

- triangle or indexed mesh geometry,
- positions,
- optional vertex normals,
- optional UVs,
- per-primitive or per-triangle material references,
- imported material descriptions,
- imported texture references,
- source diagnostics.

The exact C++ type names can follow existing `yaoray_assets` conventions, but
the intent is that file-format details remain inside the loader layer.

### Scene Compiler Layer

The scene compiler converts imported assets into the current render layer:

- `RenderMaterial`
- `RenderTexture`
- flat `RenderTriangle` entries
- BVH build input

This milestone intentionally keeps the current flat-triangle `RenderScene`
contract. glTF should not force a premature mesh/resource architecture. A later
large-scene milestone can introduce mesh resources, instancing, memory layout
changes, and GPU-friendly arrays when those costs become measurable.

### CPU Shading Layer

`RenderTriangle` should store both geometric and optional vertex-normal data:

```cpp
struct RenderTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal;
    int material_index = 0;
    Vec2f uv0{};
    Vec2f uv1{};
    Vec2f uv2{};
    bool has_uv = false;
    Vec3f n0{};
    Vec3f n1{};
    Vec3f n2{};
    bool has_vertex_normals = false;
};
```

At a surface hit, the path tracer computes barycentric coordinates, interpolates
UVs and vertex normals when available, and evaluates BSDFs with the shading
normal. The geometric normal remains authoritative for:

- ray-triangle intersection,
- BVH visibility,
- shadow and bounce ray bias,
- front/back classification,
- correcting a shading normal that points into the wrong hemisphere.

This keeps smooth shading from changing the actual geometric surface used for
visibility.

## glTF v1 Scope

The glTF importer should use a focused, well-tested subset:

- `.gltf` with external `.bin` and image files,
- `.glb` binary container files,
- default scene, falling back to first scene when no default is specified,
- node hierarchy traversal,
- node transforms from matrix or TRS,
- mesh primitives with mode `TRIANGLES`,
- `POSITION` accessor,
- optional `NORMAL` accessor,
- optional `TEXCOORD_0` accessor,
- optional index accessor with valid glTF integer component types,
- non-indexed triangle primitives,
- image textures resolvable by tinygltf,
- `pbrMetallicRoughness.baseColorFactor`,
- `pbrMetallicRoughness.baseColorTexture`,
- `pbrMetallicRoughness.metallicFactor`,
- `pbrMetallicRoughness.roughnessFactor`,
- optional `emissiveFactor` as a material factor if it fits the current material
  model cleanly.

Unsupported primitive modes, missing required attributes, unsupported compressed
buffers, and unsupported sparse accessors should produce clear diagnostics.

## Material Mapping

YaoRay does not yet have a full glTF metallic-roughness BSDF. The v1 importer
should map glTF materials conservatively into current material kinds:

- `metallicFactor >= 0.5`
  - `MaterialKind::Metal`
  - `albedo = baseColorFactor.rgb`
  - `roughness = roughnessFactor`
- `metallicFactor < 0.5` and `roughnessFactor < 0.35`
  - `MaterialKind::Plastic`
  - `albedo = baseColorFactor.rgb`
  - `roughness = roughnessFactor`
  - `specular = 0.04`
- otherwise
  - `MaterialKind::Diffuse`
  - `albedo = baseColorFactor.rgb`

If `baseColorTexture` exists, it should map to the existing `albedo_texture`
field and use the existing texture sampling path. This approximation is not
physically complete, but it keeps v1 useful and makes later PBR work easier to
motivate and verify.

## Dependency Strategy

Use `tinygltf` for glTF parsing rather than implementing glTF JSON, GLB, buffer,
accessor, and image handling from scratch. It is header-only and fits the
project's current dependency style.

Integration rules:

- Vendor the dependency under `external/` or use the project's established
  external dependency pattern.
- Keep tinygltf usage inside `yaoray_assets`.
- Do not expose tinygltf types through public render or scene headers.
- Continue using existing image-loading infrastructure where practical.
- Preserve deterministic tests by keeping fixture assets small.

## Diagnostics

Diagnostics should be explicit and asset-author friendly:

- missing glTF file,
- invalid glTF or GLB parse failure,
- missing external buffer,
- missing image file,
- unsupported primitive mode,
- missing `POSITION`,
- invalid accessor component type,
- unsupported sparse accessor,
- unsupported compressed extension,
- primitive vertex-count not divisible by three for non-indexed triangles,
- invalid material or texture reference,
- transform values that cannot be interpreted.

Unknown glTF extensions should not fail the import unless the asset declares
them as required through `extensionsRequired` and YaoRay does not support them.

## Khronos Sample Assets

This milestone should use real small glTF assets, not only hand-authored
fixtures. Use a small subset from the Khronos sample asset repositories and
record source and license information near the fixtures.

Recommended compatibility fixtures:

- `Triangle`
  - indexed triangle,
  - minimal scene path.
- `Triangle Without Indices`
  - non-indexed primitive path.
- `Simple Meshes`
  - positions, normals, UVs, and repeated mesh use.
- `Simple Texture`
  - base color texture path.
- `Box` or `Box Textured`
  - simple closed model and material behavior.

Recommended manual visual asset:

- `Avocado`, `Duck`, or `Boom Box`
  - small enough for repository or documented manual download,
  - more representative than a cube,
  - useful for checking transforms, normals, UVs, material factors, and texture
    orientation.

Do not import the whole Khronos sample repository. Keep repository assets small
and intentional. If a model's license or trademark terms are not clean enough,
use a different model.

## Example Scenes

Add one deterministic example scene for the official small test asset:

```text
scenes/examples/gltf_textured_asset.toml
```

The scene should:

- use `render.integrator = "path"`,
- use a modest resolution and SPP suitable for manual verification,
- reference a small `.gltf` or `.glb` asset,
- write to `scenes/examples/out/gltf_textured_asset.png`,
- use an existing area light or simple authored light setup,
- keep camera and scale settings easy to inspect.

The existing Cornell and material showcase scenes remain the primary path
tracing sanity scenes. The glTF example proves asset compatibility rather than
final lighting quality.

## Testing Strategy

### OBJ Loader Tests

- OBJ `vn` records are parsed.
- OBJ `f v//vn` is parsed.
- OBJ `f v/vt/vn` preserves both UVs and normals.
- Quad triangulation preserves matching UV and normal corners.
- Position-only OBJ tests still pass.

### glTF Loader Tests

- `.gltf` with external `.bin` loads.
- `.glb` loads.
- Indexed triangle primitive loads.
- Non-indexed triangle primitive loads.
- `POSITION`, `NORMAL`, and `TEXCOORD_0` accessors are decoded.
- Node transform hierarchy is applied.
- `baseColorFactor`, `metallicFactor`, and `roughnessFactor` are decoded.
- `baseColorTexture` resolves to image data.
- Unsupported primitive modes produce diagnostics.
- Missing required `POSITION` produces diagnostics.

### Scene Compiler Tests

- Imported glTF assets compile to the expected render triangle count.
- Compiled glTF triangles preserve UVs.
- Compiled glTF triangles preserve vertex normals.
- Imported glTF materials map to diffuse, metal, or plastic as specified.
- Imported base color textures load into `RenderScene.textures`.
- Scene-authored material overrides still work for imported assets.
- Existing OBJ texture scenes still compile and render.

### Path Tracer Tests

- A hit on a triangle with vertex normals uses an interpolated shading normal.
- A hit on a triangle without vertex normals falls back to the geometric normal.
- Shading normals are corrected to remain in a valid hemisphere relative to the
  geometric normal.
- Textured glTF primitives render visibly different texel regions.
- Existing untextured, textured OBJ, Cornell, and material showcase tests keep
  passing.

### Manual Verification

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
cmake --build build-release --config Release
.\build-release\Release\yaoray.exe render .\scenes\examples\gltf_textured_asset.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

The glTF example should show the expected model silhouette, texture colors,
smooth normals where the source asset provides them, and no obvious coordinate
or texture-orientation inversion.

## Acceptance Criteria

- OBJ vertex normals import and compile into render triangles.
- Smooth shading uses interpolated vertex normals in the CPU path tracer.
- Geometric normals still control ray bias and visibility decisions.
- `.gltf` and `.glb` static meshes can be imported.
- glTF node transforms are applied.
- glTF positions, normals, UVs, indices, and non-indexed triangles work.
- glTF base color factors and textures map into YaoRay materials.
- glTF metallic and roughness factors map into current material kinds.
- Small Khronos sample assets are used for compatibility coverage.
- A small glTF example scene renders through the CPU backend.
- Existing OBJ, texture, Cornell, and material showcase behavior remains intact.
- README and architecture docs describe the new importer scope and limits.

## Follow-Up Work

- Full metallic-roughness BSDF.
- Normal maps and tangent generation.
- Roughness and metallic texture maps.
- Alpha mask and alpha blend support.
- Emissive texture support.
- glTF camera and light import.
- Environment lighting and environment MIS.
- Mesh/resource/instance architecture for large scenes.
- Sparse accessor support.
- Draco and meshopt support.
- Larger Sponza, Bistro, and San Miguel asset milestones.
- CUDA material and texture parity.
