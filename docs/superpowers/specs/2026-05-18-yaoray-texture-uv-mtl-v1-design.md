# YaoRay Texture, UV, and OBJ MTL v1 Design

## Context

YaoRay now has a usable CPU path tracer with BVH traversal, Cornell-style inline
geometry, OBJ import, PNG output, diffuse/mirror/metal/plastic materials, direct
area-light MIS, sampler selection, tile threading, and fixed-policy Russian
roulette. The next goal is to move from hand-authored color blocks toward real
asset scenes.

Large offline-rendering targets such as San Miguel and Bistro require texture
coordinates, image textures, and imported material bindings before their visual
appearance can be evaluated. This slice adds the smallest asset-texture loop that
lets a textured OBJ scene render with visible diffuse texture color.

## Goals

- Parse OBJ texture coordinates (`vt`).
- Preserve UVs through OBJ face parsing and quad triangulation.
- Parse basic OBJ material library data from `.mtl` files.
- Support `.mtl` fields:
  - `newmtl`
  - `Kd`
  - `map_Kd`
- Bind OBJ `usemtl` names to imported material data.
- Load PNG textures referenced by `map_Kd`.
- Store textures in `RenderScene` and reference them from render materials.
- Store per-triangle UVs in render triangles.
- Sample diffuse texture color in the CPU path tracer.
- Add a small textured OBJ example scene and manual render target.

## Non-Goals

- No glTF, GLB, FBX, USD, or PBRT scene import.
- No full mesh/instance/resource-system refactor.
- No normal maps.
- No roughness, metallic, specular, or emission texture maps.
- No alpha mask or cutout opacity.
- No mipmaps.
- No bilinear filtering.
- No anisotropic filtering.
- No complete OBJ/MTL compatibility.
- No per-face material behavior beyond basic `usemtl` material assignment.
- No color-management overhaul.
- No CUDA texture support.

## Target User Story

A user can place a small OBJ file with UV coordinates, an MTL file, and a PNG
diffuse texture under `scenes/examples/assets/`, reference the OBJ from a TOML
scene, render with the CPU path tracer, and see texture colors in the output
image.

## Scope Boundary

This slice is an asset pipeline milestone, not a full resource architecture
milestone. The existing flat-triangle `RenderScene` remains in place. UVs and
texture references are added to the current data model so the renderer can prove
the texture path end to end before a later mesh/instance refactor.

That later refactor should be driven by larger scenes such as San Miguel or
Bistro, where resource reuse, instancing, and memory layout become measurable
problems.

## Asset Parsing

### OBJ Loader

Extend the OBJ loader to understand:

- `vt u v`
- `mtllib path`
- `usemtl name`
- `f v/vt`
- `f v/vt/vn`
- existing position-only faces

OBJ faces still support triangles and quads. Quad triangulation must preserve
the correct UV corner assignments for both generated triangles.

The loader should output enough information for the scene compiler to know:

- triangle vertex positions,
- optional triangle UVs,
- optional material name per triangle,
- referenced material library paths.

The importer may continue ignoring imported normals and smoothing data in this
slice.

### MTL Loader

Add a focused MTL parser for basic diffuse material data:

```text
newmtl material_name
Kd r g b
map_Kd relative/or/absolute/path.png
```

Rules:

- `newmtl` starts a material entry.
- `Kd` defaults to white if omitted.
- `map_Kd` is optional.
- Relative `map_Kd` paths resolve relative to the `.mtl` file directory.
- Unknown MTL statements are ignored in v1.
- Duplicate material names inside the same material library are diagnostics.
- Missing texture files are diagnostics.
- Unsupported image extensions are diagnostics.

If multiple `mtllib` files define the same material name for one OBJ asset, the
compiler should report a clear duplicate-material diagnostic rather than choose
one silently.

## Scene Compiler Design

When compiling an OBJ asset:

1. Load the OBJ geometry.
2. Load each referenced MTL file.
3. Build an OBJ-material lookup table.
4. Convert referenced OBJ materials to render materials.
5. Load each unique `map_Kd` PNG into a texture cache.
6. Assign each compiled render triangle:
   - material index,
   - UV coordinates if available.

The texture cache belongs in the scene compiler for this slice. The same
canonical texture path should load once per compiled scene, and multiple
materials may point to the same `texture_index`.

Scene-authored TOML materials still work. Imported OBJ materials should only be
used when an instance does not explicitly bind a scene-authored material. If an
instance binds `material = "name"` in TOML, that material remains an override for
the whole imported instance, preserving current behavior.

## Render Data Model

Extend render data minimally:

```cpp
struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
};

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission{};
    float roughness = 0.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
};

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
};

struct RenderScene {
    std::vector<RenderTexture> textures;
    ...
};
```

Exact placement can follow the current headers, but the ownership should stay
simple: render scene owns texture texels, materials reference textures by index,
and triangles carry their UVs.

## Texture Sampling

Add a render-level texture sampling helper:

```cpp
Color3f SampleTextureNearest(const RenderTexture& texture, Vec2f uv);
```

Sampling policy for v1:

- filter: nearest
- wrap: repeat
- image format: PNG
- pixel conversion: 8-bit RGB/RGBA to `Color3f` in `0..1`
- alpha channel: ignored
- missing or invalid texture index: fall back to material albedo

The first version intentionally treats PNG values as direct `0..1` color values.
Real sRGB-to-linear conversion should be handled later as part of a broader
color-management and HDR/tone-mapping slice.

## Path Tracer Integration

At a valid surface hit, the CPU path tracer should resolve a shading material for
that hit. For v1 this can be a local copy of `RenderMaterial`:

1. Start from the triangle's bound material.
2. If the material has a valid `albedo_texture` and the triangle has UVs:
   - compute barycentric hit coordinates,
   - interpolate `uv0/uv1/uv2`,
   - sample the texture,
   - replace the material albedo with sampled color.
3. Evaluate direct light and BSDF sampling with this resolved material.

This keeps the existing BSDF API unchanged and avoids pushing texture sampling
into every material branch.

Texture sampling should affect diffuse albedo only in v1. If a metal or plastic
material references a texture through imported MTL data, the compiler can still
store the texture, but only diffuse behavior is required and tested in this
slice.

## Diagnostics

Diagnostics should be explicit enough for asset debugging:

- missing `.mtl` file,
- duplicate imported material name,
- unknown `usemtl` material name,
- missing `map_Kd` texture file,
- unsupported texture extension,
- invalid PNG load.

Existing unsupported-asset diagnostics should keep working.

Unknown MTL statements should not be diagnostics in v1 because real MTL files
commonly contain fields that this slice intentionally ignores.

## Example Scene

Add a small asset set under `scenes/examples/assets/`:

```text
textured_quad.obj
textured_quad.mtl
checker_2x2.png
```

Add a scene:

```text
scenes/examples/textured_quad.toml
```

The example should:

- use `render.integrator = "path"`,
- render a simple textured quad or block,
- output `scenes/examples/out/textured_quad.png`,
- use a small texture with visibly different texel colors.

The sample texture should be tiny and deterministic so it is reasonable to keep
in the repository.

## Testing Strategy

### OBJ Loader Tests

- OBJ loader parses `vt` records.
- OBJ loader parses `f v/vt`.
- OBJ loader parses `f v/vt/vn` while still ignoring normals.
- Quad triangulation preserves UV corner assignments.
- `mtllib` references are preserved.
- `usemtl` names are attached to emitted triangles.
- Existing position-only OBJ tests keep passing.

### MTL Loader Tests

- MTL loader parses `newmtl`.
- MTL loader parses `Kd`.
- MTL loader parses `map_Kd`.
- Relative texture paths resolve from the `.mtl` directory.
- Duplicate material names fail clearly.
- Unknown statements are ignored.

### Texture Tests

- PNG loading returns width, height, and texels.
- Nearest sampling returns expected texels.
- Repeat wrapping handles UVs outside `[0, 1]`.
- Empty or invalid texture samples fall back safely at call sites.

### Scene Compiler Tests

- Imported OBJ material converts to `RenderMaterial`.
- `map_Kd` loads one render texture.
- Duplicate texture paths are cached once.
- Compiled triangles preserve UVs.
- Instance-authored TOML material overrides imported OBJ material.
- Missing texture emits a clear diagnostic.

### Path Tracer Tests

- A textured quad with a 2x2 texture renders different colors in different UV
  regions.
- A diffuse material without a texture preserves current albedo behavior.
- Thread-count determinism still passes for textured scenes.

### Manual Verification

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
cmake --build build-release --config Release
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

The textured quad render should visibly show the texture colors, and the existing
material showcase should continue rendering.

## Acceptance Criteria

- OBJ assets can carry UV coordinates through scene compilation.
- Basic `.mtl` files can define diffuse color and diffuse texture.
- PNG diffuse textures load into `RenderScene`.
- Render materials can reference albedo textures.
- Render triangles can carry UVs.
- CPU path tracing samples diffuse texture color at hit UVs.
- A small textured OBJ example renders successfully.
- Existing untextured scenes and material tests keep passing.
- README and architecture docs state the new texture/MTL support and v1 limits.

## Follow-Up Work

- Bilinear filtering.
- sRGB-to-linear color management.
- Mipmaps.
- Alpha cutouts.
- Normal maps.
- Roughness and metallic texture maps.
- Imported material support beyond basic OBJ MTL.
- Mesh/instance/resource-system refactor.
- Larger Sponza, Bistro, and San Miguel import milestones.
- CUDA texture parity.
