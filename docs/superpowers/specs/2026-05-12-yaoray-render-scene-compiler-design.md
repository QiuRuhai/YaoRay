# YaoRay RenderScene Compiler Design

Date: 2026-05-12

## Purpose

This slice creates the first bridge between YaoRay's human-authored scene layer and the renderer-facing data layer. The existing `scene` module parses TOML into `SceneDescription`. This design adds a focused `render` module that compiles a validated `SceneDescription` into a minimal `RenderScene` that later CPU, CUDA, asset importer, BVH, and integrator work can consume.

The goal is not to render pixels yet. The goal is to make the next rendering work depend on a tested intermediate representation instead of reaching back into TOML or semantic scene structures.

## Goals

- Add a `render` module with public headers under `include/yaoray/render/`.
- Add a `yaoray_render` CMake target.
- Define a minimal `RenderScene` data model with render settings, camera data, environment data, area lights, materials, and flat triangles.
- Add `SceneCompileResult CompileScene(const SceneDescription& scene)`.
- Preserve `builtin:` asset paths in the scene parser so built-in assets are not normalized as filesystem paths.
- Support `asset.path = "builtin:triangle"` as a temporary built-in asset protocol.
- Apply instance translation, Euler rotation in degrees, and scale to built-in triangle geometry.
- Report unsupported external model assets with structured diagnostics.
- Report unsupported HDRI environments with structured diagnostics.
- Wire `yaoray render` through parse plus compile while still making it clear that image rendering is not available in this slice.
- Add unit and CLI tests that prove the parse-to-compile boundary works.

## Non-Goals

- No path tracing, ray traversal, or image generation.
- No BVH construction.
- No glTF, GLB, OBJ, texture, HDRI, or material file import.
- No PNG, EXR, checkpoint, or final image writing.
- No CPU, CUDA, OptiX, or backend interface implementation.
- No physically based material system.
- No mesh light extraction.
- No acceleration structure upload or GPU memory layout work.

## Approved Decisions

The next stage uses:

- Module path: `include/yaoray/render/` and `src/render/`
- CMake library: `yaoray_render`
- Compiler API: `CompileScene(const SceneDescription&)`
- Temporary built-in asset path: `builtin:triangle`

The `builtin:` protocol is intentionally small. It exists to let parser, compiler, CLI, and tests exercise a complete scene-to-render-scene path before real asset import exists. It is not a substitute for the future asset importer.

## Architecture

The data flow becomes:

```text
scene.toml
   |
   v
SceneParser
   |
   v
SceneDescription
   |
   v
SceneCompiler
   |
   v
RenderScene
   |
   v
future CPU / CUDA backend
```

The `scene` module remains the semantic authoring layer. The `render` module owns renderer-facing structures and compilation rules. `render` may depend on `scene` and `core`; `scene` must not depend on `render`.

## Parser Adjustment

The current parser normalizes asset paths relative to the scene file directory. That is correct for filesystem paths, but wrong for built-in asset identifiers.

The parser should treat any asset path string that starts with `builtin:` as a non-filesystem identifier and preserve it exactly:

```toml
[[assets]]
name = "triangle"
path = "builtin:triangle"
```

After parsing, `scene.assets[0].path.generic_string()` must be exactly `builtin:triangle`.

This rule applies to asset paths only. Film output, checkpoint paths, and HDRI paths remain filesystem paths.

## RenderScene Data Model

`RenderScene` should stay plain and easy to inspect:

- render settings: backend, width, height, spp, max depth, seed
- camera: origin, forward, right, up, vertical field of view in radians, aperture, focus distance
- environment: none or constant color plus strength
- materials: minimal albedo and emission colors
- triangles: world-space vertices, geometric normal, material index
- area lights: position, size, radiance copied from the semantic scene

This slice intentionally stores triangles in world space. That avoids introducing instance handles, meshes, or a transform hierarchy before there is an asset importer or backend.

## SceneCompiler Behavior

`CompileScene()` should:

1. Copy render settings from `SceneDescription`.
2. Build a perspective camera basis from `camera.position` and `camera.target`.
3. Convert `camera.fov_y` from degrees to radians.
4. Copy `none` and `constant` environments.
5. Copy area lights.
6. Expand every instance whose asset path is `builtin:triangle`.
7. Apply instance transforms to the built-in triangle vertices.
8. Return diagnostics for unsupported assets and environments.

The built-in triangle object-space vertices are:

```text
p0 = [-0.5, 0.0, 0.0]
p1 = [ 0.5, 0.0, 0.0]
p2 = [ 0.0, 1.0, 0.0]
```

The transform order is fixed:

```text
scale -> rotate X -> rotate Y -> rotate Z -> translate
```

Euler angles are read from `TransformDescription::rotate_degrees`.

## Diagnostics

Scene compilation uses the existing `SceneDiagnostic` type. Ordinary user-facing compilation failures should not throw.

Unsupported external assets produce an error:

```text
Scene error in scenes/examples/minimal.toml:
  [assets.path] asset import not implemented yet: assets/models/model.glb
```

Unsupported HDRI environment produces an error:

```text
Scene error in scenes/examples/minimal.toml:
  [environment.path] HDRI environment import not implemented yet
```

If compilation has errors, `SceneCompileResult::scene` should be empty.

## CLI Behavior

`yaoray render <scene.toml> [--backend cpu|cuda]` should:

1. Parse and validate the scene file.
2. Apply the backend override if present.
3. Compile the scene into `RenderScene`.
4. Print diagnostics and exit non-zero when compilation fails.
5. Print a concise success summary when compilation succeeds.

Successful output should include:

```text
Scene parsed successfully: scenes/examples/minimal.toml
Scene compiled successfully.
Requested backend: cpu
Compiled triangles: 1
Rendering is not implemented yet.
```

The command still does not write an image.

## Tests

Required tests:

- Parser preserves `builtin:triangle` asset paths exactly.
- `RenderScene` defaults are stable and backend-friendly.
- `CompileScene()` copies render settings.
- `CompileScene()` builds camera basis vectors.
- `CompileScene()` copies constant environment data.
- `CompileScene()` copies area lights.
- `CompileScene()` expands one `builtin:triangle` instance into one triangle.
- `CompileScene()` applies scale, rotation, and translation to built-in triangle vertices.
- `CompileScene()` rejects ordinary external model paths with an asset-import diagnostic.
- `CompileScene()` rejects HDRI environments with an HDRI diagnostic.
- CLI succeeds for a built-in triangle scene.
- CLI fails for an external asset scene and prints the asset-import diagnostic.

## Documentation

Documentation updates should state:

- The implemented flow is now parse plus compile, not parse only.
- The render layer has a minimal `RenderScene` but no backend.
- `builtin:triangle` is a temporary built-in asset for tests and examples.
- External asset import and HDRI loading are intentionally deferred.

## Completion Criteria

- `yaoray_render` builds and links into tests and the CLI.
- `ctest --test-dir build --output-on-failure -C Debug` passes.
- `yaoray render scenes/examples/minimal.toml --backend cpu` succeeds and reports one compiled triangle.
- `yaoray render scenes/examples/minimal.toml --backend cuda` succeeds and reports one compiled triangle.
- A scene with a `.glb` asset fails at compile time with `asset import not implemented yet`.
- A scene with `environment.type = "hdri"` fails at compile time with `HDRI environment import not implemented yet`.
- No public `scene` header depends on the `render` module.
- `README.md` and `docs/architecture/overview.md` describe the new parse-to-compile status.

## Future Work

The next focused slices after this design are:

1. Asset importer interfaces and first glTF/OBJ loading.
2. BVH construction over `RenderScene::triangles`.
3. CPU path tracer that consumes `RenderScene`.
4. Film output writing.
5. CUDA backend subset that consumes the same compiled scene model.
