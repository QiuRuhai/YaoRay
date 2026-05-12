# YaoRay Scene Foundation Design

Date: 2026-05-12

## Purpose

The scene foundation slice defines the first real user-facing input contract for YaoRay. It adds a semantic scene layer, a TOML parser, validation diagnostics, and a `yaoray render` CLI command that can read and validate scene files before rendering backends exist.

This slice should make the next stages safer: asset import, scene compilation, CPU rendering, and CUDA rendering will all build around a tested `SceneDescription` contract instead of inventing their own input assumptions.

## Goals

- Add a `scene` module for user-authored semantic scene data.
- Parse YaoRay scene files from TOML using `toml++`.
- Keep `toml++` hidden inside implementation files; public YaoRay headers expose only YaoRay types.
- Validate required fields, field types, numeric ranges, array lengths, unknown fields, and asset references.
- Add `yaoray render <scene.toml> --backend cpu|cuda` as a CLI command shell.
- Provide clear user-facing diagnostics for missing files, invalid TOML, invalid fields, and unsupported backends.
- Add a long-lived `scenes/examples/minimal.toml` fixture.
- Update docs to state that YaoRay currently supports scene parsing, not rendering.

## Non-Goals

- No path tracing or image generation.
- No glTF, GLB, OBJ, texture, HDRI, or material file loading.
- No BVH construction.
- No full `RenderScene` implementation.
- No CPU, CUDA, or OptiX backend implementation.
- No PNG, EXR, or checkpoint image writing.
- No support for imported cameras, lights, animation, skinning, or arbitrary glTF scene semantics.

## Dependency Choice

YaoRay will use `toml++` as the TOML parser for this slice.

Integration starts as a vendored single-header dependency at this project-local path:

```text
external/tomlplusplus/toml.hpp
```

The public API rule is fixed: `toml++` may only be included by implementation files such as `src/scene/scene_parser.cpp`. Headers under `include/yaoray/scene/` must not expose `toml++` types.

If `toml++` integration proves unexpectedly painful on the supported MSVC toolchain, `toml11` is the fallback parser. Switching parser libraries should not affect public scene APIs.

## Architecture

The second-stage data flow is:

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
ValidateSceneDescription()
   |
   v
CLI render command
```

The `scene` module owns semantic scene descriptions: the information a person writes and debugs. It does not own backend-friendly triangle arrays, BVH nodes, GPU upload buffers, or renderer execution.

The `render` module may receive a placeholder boundary in this slice, but full scene compilation is deferred:

```cpp
SceneCompileResult CompileScene(const SceneDescription& scene);
```

This boundary exists to keep the next plan obvious. It should not trigger asset loading, BVH building, or backend implementation in this slice.

## Scene Module

The scene module should define focused data structures:

- `SceneDescription`: top-level scene object.
- `RenderSettings`: backend, dimensions, sample count, max depth, and seed.
- `FilmSettings`: output path, tone mapper, exposure, checkpoint interval, and checkpoint path.
- `CameraDescription`: first-stage perspective camera parameters.
- `AssetDescription`: named asset path references.
- `InstanceDescription`: asset references plus translation, rotation, and scale.
- `LightDescription`: first-stage area light description.
- `EnvironmentDescription`: none, constant, or HDRI environment settings.
- `SceneDiagnostic`: structured parser and validation diagnostics.

The scene module depends on `core` for vector-like values where useful. It should avoid depending on `film`, `assets`, `render`, or `backends`.

## Scene Schema

The first TOML schema should be small but representative:

```toml
[render]
backend = "cpu"
width = 1280
height = 720
spp = 64
max_depth = 8
seed = 42

[film]
output = "out/example.png"
tone_mapper = "aces"
exposure = 0.0
checkpoint_interval_s = 0
checkpoint_path = ""

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
aperture = 0.0
focus_distance = 4.0

[[assets]]
name = "model"
path = "assets/models/model.glb"

[[instances]]
asset = "model"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[[lights]]
type = "area"
position = [0, 4, 2]
size = [2, 2]
radiance = [8, 7, 6]

[environment]
type = "constant"
radiance = [0.02, 0.025, 0.03]
strength = 1.0
```

Defaults:

- `render.backend = "cpu"`
- `render.spp = 1`
- `render.max_depth = 5`
- `film.tone_mapper = "aces"`
- `film.exposure = 0.0`
- `film.checkpoint_interval_s = 0`
- `film.checkpoint_path = ""`
- `instance.translate = [0, 0, 0]`
- `instance.rotate_degrees = [0, 0, 0]`
- `instance.scale = [1, 1, 1]`
- `environment.type = "none"` when `[environment]` is absent

Required fields:

- `[render]`
- `render.width`
- `render.height`
- `[camera]`
- `camera.type`
- `camera.position`
- `camera.target`
- `camera.fov_y`
- `[film]`
- `film.output`

At least one renderable contributor should exist: an asset instance, an explicit light, or a non-none environment. This can be enforced conservatively in validation so an empty scene fails with a clear message.

File paths are parsed and normalized relative to the scene file directory. This slice does not require asset, texture, HDRI, or output parent paths to exist; existence checks belong to asset import and output writing stages.

## Validation

Validation should be strict in this early stage:

- Unknown fields are errors, not warnings.
- Missing required fields are errors.
- Wrong TOML value types are errors.
- Three-component vectors must contain exactly three numeric values.
- Two-component values such as area light size must contain exactly two numeric values.
- Width, height, spp, and max depth must be positive.
- Field-of-view, aperture, focus distance, exposure, and environment strength must be finite.
- Asset names must be non-empty and unique.
- Instances must reference an existing asset name.
- Backend names must be `cpu` or `cuda`.
- Tone mapper names must be `none`, `reinhard`, or `aces`.
- Camera type must be `perspective`.
- Light type must be `area`.
- Environment type must be `none`, `constant`, or `hdri`.

Validation returns structured diagnostics instead of throwing for ordinary user input problems. Internal programming errors can still assert or throw where appropriate.

## Diagnostics

Diagnostics should carry enough structure for both tests and CLI formatting:

```cpp
enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct SceneDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::filesystem::path file;
    std::string field;
    std::string message;
};
```

CLI output should be concise and field-oriented:

```text
Scene error in scenes/examples/minimal.toml:
  [camera] missing required field: position
```

Bad TOML syntax should preserve useful parser location information from `toml++` where possible. The CLI does not need to expose raw C++ exception names.

## CLI Behavior

The CLI should support:

```powershell
yaoray render scenes/examples/minimal.toml --backend cpu
yaoray render scenes/examples/minimal.toml --backend cuda
```

Behavior:

- `yaoray --help` and `yaoray --version` continue to work.
- `yaoray render --help` prints render command usage.
- `yaoray render <scene.toml>` uses the scene file backend default or `render.backend`.
- `--backend cpu|cuda` overrides the file's backend setting.
- Missing scene path exits non-zero with usage.
- Missing file exits non-zero with a clear file error.
- Bad TOML exits non-zero with parser diagnostics.
- Validation errors exit non-zero with scene diagnostics.
- Successful parse exits zero and prints a clear message that rendering is not implemented yet.
- `--backend cuda` is accepted as a requested backend. In this slice, it exits zero after successful parsing and prints the requested backend plus the same rendering-not-implemented message as CPU.
- Unknown backend names such as `metal` are validation errors and exit non-zero.

The command should not pretend to render. It should make the current capability explicit.

## Tests

The second-stage tests should cover:

- Parsing a complete `minimal.toml`.
- Applying default values.
- Rejecting a missing `[render]`.
- Rejecting a missing `[camera]`.
- Rejecting `width <= 0`.
- Rejecting a vector with the wrong length, such as `position = [0, 1]`.
- Rejecting wrong field types.
- Rejecting unknown fields, such as `[render] widht = 1280`.
- Rejecting duplicate asset names.
- Rejecting an instance that references a missing asset.
- Rejecting an empty scene with no renderable contributors.
- CLI render command success for a valid scene.
- CLI render command success for a valid scene with `--backend cuda`, while reporting that rendering is not implemented yet.
- CLI diagnostics for a missing file.
- CLI diagnostics for bad TOML.
- CLI diagnostics for validation errors.

Tests should use small fixture files under `tests/fixtures/scene/` and keep `scenes/examples/minimal.toml` as a human-facing example.

## Documentation

Documentation updates should include:

- README current status: scene parsing exists, rendering does not.
- README command examples for `yaoray render`.
- Architecture overview update showing the semantic scene layer.
- `scenes/examples/minimal.toml` as a stable example scene.

## Completion Criteria

- `yaoray render scenes/examples/minimal.toml --backend cpu` parses and validates successfully, then clearly reports that rendering is not implemented yet.
- `yaoray render scenes/examples/minimal.toml --backend cuda` parses and validates successfully, then clearly reports that rendering is not implemented yet.
- `SceneDescription` covers the inputs needed by later asset import and CPU backend plans.
- All parser and validation error categories listed in this spec have tests.
- `toml++` is isolated to scene parser implementation files.
- Public scene headers expose no third-party TOML types.
- README and architecture docs describe the scene parsing stage.
- Baseline CTest passes from a clean build directory.

## Future Work

The next implementation plans after this slice should cover:

1. `SceneCompiler`, `RenderScene`, basic transforms, and asset-independent geometry fixtures.
2. Asset importer interfaces plus first glTF and OBJ import.
3. CPU path tracer, BVH, lights, materials, and Film output.
4. CUDA backend subset and CPU/CUDA comparison tests.
