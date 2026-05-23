# YaoRay PBRT SceneWorld Frontend v1 Design

## Goal

Add a direct PBRT scene frontend without making TOML the long-term canonical
scene entry point.

The first user-facing target is:

```text
yaoray render scene.pbrt --backend cpu
```

The frontend should parse a practical PBRT subset into a new high-level
`SceneWorld` layer, then compile that world into `RenderSceneIR` for CPU
preparation and rendering.

## Background

YaoRay currently has this main scene flow:

```text
TOML SceneDescription -> RenderSceneIR -> backend-owned PreparedScene
```

This worked for small authored scenes, material showcases, glTF importer smoke
tests, and local-only benchmark notes. It is not the right long-term boundary
for renderer-scale scene formats:

- `SceneDescription` is shaped around TOML authoring.
- `RenderSceneIR` is backend-neutral render input and should stay low-level,
  table-oriented, and future GPU-packable.
- PBRT is a renderer scene description format, not an asset file format.

Treating PBRT as `PBRT -> TOML -> SceneDescription -> RenderSceneIR` would force
TOML to carry PBRT semantics it was not designed to represent. Instead, TOML and
PBRT should become peer frontends.

Target architecture:

```text
TOML frontend   \
PBRT frontend    -> SceneWorld -> RenderSceneIR -> CpuPreparedScene
future frontend /
```

## Asset Target: Breakfast

The first large PBRT target should be **The Breakfast Room** from Benedikt
Bitterli's Rendering Resources.

Source:

- Rendering Resources: https://benedikt-bitterli.me/resources/
- PBRT v4 package: https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip
- PBRT v3 scene note: https://www.pbrt.org/scenes-v3

Rationale:

- It is visually useful: an indoor dining scene with chairs, tableware, glass,
  indirect lighting, and variants that stress difficult light transport.
- It is a better first PBRT target than a huge outdoor scene because it exercises
  materials, mesh loading, camera/lights, and indirect illumination without
  immediately turning the work into a memory-capacity project.
- It is listed by the PBRT v3 scene page as `breakfast`, with attribution to
  Wig42 and a CC-BY license note.
- Bitterli's page provides explicit scene downloads and PBRT v4 conversions;
  it also notes that PBRT output can deviate from the original Tungsten renders
  because conversion is approximate.

The downloaded scene files must remain local-only:

```text
external/assets/pbrt/breakfast/
```

`external/assets/` is already ignored by Git. The repository may include
documentation and local benchmark scene references, but not the downloaded zip,
expanded meshes, or textures.

## Scope

In scope for the first PBRT frontend slice:

- Introduce `SceneWorld` as the shared high-level scene semantic layer.
- Add an adapter from current TOML `SceneDescription` to `SceneWorld`.
- Move render compilation to consume `SceneWorld`.
- Add a PBRT parser/frontend that produces `SceneWorld`.
- Allow `yaoray render` to dispatch by file extension:
  - `.toml` uses the TOML frontend.
  - `.pbrt` uses the PBRT frontend.
- Support enough PBRT syntax to start loading Breakfast and small PBRT fixtures.
- Add a local Breakfast asset setup document and keep all downloaded assets out
  of source control.

Out of scope for v1:

- No CUDA PBRT path.
- No complete PBRT compatibility.
- No TOML generation.
- No full PBRT material, texture, spectral, medium, or light transport parity.
- No EXR/DDS texture decoding unless Breakfast smoke loading proves it is a
  hard blocker and the implementation plan explicitly scopes it.
- No external asset files committed to Git.

## SceneWorld

`SceneWorld` is an internal semantic layer, not a new file format. It should be
small at first and should only model concepts the compiler needs from multiple
frontends.

Initial contents:

- source path and source root
- render, film, and offline settings
- camera
- named materials with source-independent material intent
- mesh assets or inline mesh data
- instances with transforms and material bindings
- area lights and environment lighting
- diagnostics and warnings from frontend normalization

`SceneWorld` should preserve high-level semantics until the render compiler
lowers them into `RenderSceneIR`. It should not contain backend-prepared data,
BVHs, CUDA buffer handles, or PBRT parser internals.

## TOML Frontend

The current `SceneDescription` can remain as the TOML parser output, but it
should no longer be the project-wide scene model.

Flow:

```text
ParseTomlScene(file) -> SceneDescription -> BuildSceneWorld(SceneDescription)
```

The adapter should preserve existing behavior exactly. TOML tests and CLI
render tests should continue to pass.

## PBRT Frontend

The PBRT frontend should be isolated in its own module so parser complexity does
not spread into render compilation.

Suggested module shape:

```text
include/yaoray/pbrt/pbrt_scene.hpp
src/pbrt/pbrt_scene.cpp
```

The parser should read PBRT files, handle included files relative to the current
file, preserve diagnostics with file paths, and produce `SceneWorld`.

Minimum PBRT syntax targets:

- `Include`
- `WorldBegin` / `WorldEnd`
- `AttributeBegin` / `AttributeEnd`
- `TransformBegin` / `TransformEnd`
- transform operations needed by Breakfast:
  - `Identity`
  - `Translate`
  - `Scale`
  - `Rotate`
  - `LookAt`
  - `Transform`
  - `ConcatTransform`
- `Camera`, initially perspective-compatible
- `Film`, initially image resolution and output path where available
- `Integrator` and `Sampler` parsing as warnings or best-effort defaults
- `MakeNamedMaterial`
- `NamedMaterial`
- `Material`
- `Shape "trianglemesh"`
- `Shape "plymesh"`
- `AreaLightSource` for area lights that can map to existing CPU area-light
  behavior
- `LightSource` / environment-related declarations as best-effort warnings
  unless they map cleanly to current environment support

Unsupported directives should produce clear warnings when they can be ignored
and errors when the scene cannot be loaded safely.

## PLY Mesh Loading

PBRT scenes commonly use `Shape "plymesh"` with external PLY files. Add PLY
loading as an asset capability rather than baking PLY parsing into the PBRT
parser.

Suggested module shape:

```text
include/yaoray/assets/ply_loader.hpp
src/assets/ply_loader.cpp
```

Initial PLY support:

- ASCII PLY and binary little-endian PLY if Breakfast requires both; otherwise
  scope to the required variant first.
- vertex positions
- optional normals
- optional UVs
- triangular faces
- quad faces triangulated if present
- diagnostics for unsupported elements or malformed faces

The loader should output `AssetResource` or a mesh record compatible with
`SceneWorld` asset storage.

## Material Lowering

PBRT material definitions should first become source-independent material intent
in `SceneWorld`, then lower through the render compiler.

Initial mapping:

- PBRT `matte` -> diffuse
- PBRT `plastic` -> plastic
- PBRT `metal` -> metal
- PBRT `glass` -> dielectric
- unsupported materials -> warning plus diffuse fallback when parameters make a
  fallback reasonable, otherwise a load error

Texture parameters should preserve paths and scalar/color intent. v1 should
support image textures that map to existing PNG/JPG/HDR texture loading. Other
texture types can warn and fall back to constants unless they are required for
Breakfast's first smoke render.

## CLI Behavior

`yaoray render` should load a scene by extension:

```text
yaoray render scenes/examples/minimal.toml
yaoray render external/assets/pbrt/breakfast/scene.pbrt
```

For PBRT files, command-line `--backend cpu|cuda` can continue to override the
frontend's backend setting. CPU remains the only runnable backend in this slice.

Diagnostics should name the frontend:

```text
PBRT scene load failed: ...
```

The app should continue printing compiled geometry, material, texture, memory,
prepare, and render statistics after successful compilation.

## Breakfast Local Workflow

Repository files may include:

```text
docs/assets/pbrt-breakfast-local-benchmark.md
```

The doc should cover:

- source URL and license/attribution notes
- recommended local root
- expected zip file name
- extraction layout
- first supported `.pbrt` entrypoint
- smoke render command
- benchmark render command once CPU loading is reliable

The implementation should not add default CTest cases that require Breakfast.
Instead, tests should use small checked-in PBRT/PLY fixtures.

## Error Handling

PBRT frontend diagnostics should distinguish:

- lexical or parse errors
- missing include files
- include recursion limits
- missing external meshes or textures
- unsupported shape types
- unsupported material types
- unsupported texture types
- unsupported light declarations
- invalid transform stack state

Warnings are appropriate for ignored metadata or unsupported features with a
defined fallback. Errors are appropriate when geometry, camera, or required
resources are unavailable.

## Testing

Add focused tests rather than relying on Breakfast in CI.

SceneWorld tests:

- TOML minimal scene adapts to `SceneWorld` with the same camera, render
  settings, material, asset, instance, light, and environment semantics.
- TOML existing fixture compiles through `SceneWorld` to the same
  `RenderSceneIR` counts as before.

PBRT parser tests:

- minimal PBRT camera/world/triangle scene loads.
- `Include` resolves relative paths.
- transform stack composes nested transforms.
- named material assignment affects following shapes.
- unsupported directive emits a warning or error according to the v1 rule.

PLY tests:

- triangle PLY loads.
- quad PLY triangulates if supported.
- malformed PLY reports diagnostics.

Compiler and CLI tests:

- `.toml` CLI render behavior remains unchanged.
- `.pbrt` CLI smoke fixture renders through CPU.
- unsupported `.pbrt` feature paths fail with useful diagnostics.

Manual verification:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Optional local Breakfast verification after the asset is downloaded:

```powershell
build\yaoray.exe render external\assets\pbrt\breakfast\<entrypoint>.pbrt --backend cpu
```

## Risks

- Risk: `SceneWorld` becomes a second overgrown scene format.
  Mitigation: keep it internal, minimal, and compiler-oriented. Do not add
  syntax-driven PBRT details unless multiple frontends need them.

- Risk: PBRT support expands beyond the v1 subset.
  Mitigation: load small fixtures first, then Breakfast, and log unsupported
  features explicitly instead of silently pretending full parity.

- Risk: PLY support grows into a broad mesh-processing project.
  Mitigation: implement only the PLY variants needed by PBRT fixtures and
  Breakfast, then add formats incrementally as tests require them.

- Risk: Breakfast requires texture or material features YaoRay does not support.
  Mitigation: support geometry-first loading, constant fallbacks, and clear
  warnings; improve materials in later CPU slices.

- Risk: local assets accidentally enter Git.
  Mitigation: keep assets under `external/assets/`, which is already ignored,
  and add docs that state Breakfast files must not be committed.

## Acceptance Criteria

- `yaoray render *.toml` still works through the TOML frontend.
- `yaoray render *.pbrt` is accepted and routed to the PBRT frontend.
- `SceneWorld` exists as the shared frontend output.
- `RenderSceneIR` remains backend input and does not gain PBRT parser state.
- Basic PBRT fixtures can parse, compile, and CPU render.
- PLY mesh fixtures can load through the asset layer.
- Breakfast is documented as the first local PBRT benchmark target.
- No Breakfast zip, expanded mesh, or texture files are committed.

## Follow-Up Work

After v1, likely follow-up slices are:

- CPU table-native BVH preparation if not already completed before PBRT work.
- More complete PBRT texture support after Breakfast smoke loading identifies
  exact missing features.
- Better imported material intent and material lowering for PBRT and glTF.
- Scene statistics and compile-only mode for large assets.
- Local Breakfast benchmark presets for smoke, preview, and final renders.
- Future CUDA preparation and GPU benchmark comparison using the same PBRT
  scene frontend.
