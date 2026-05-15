# YaoRay CPU Path Tracer v0 Design

## Context

YaoRay currently has a complete debug-rendering pipeline: TOML scene parsing, scene compilation, named diffuse/emissive materials, inline quad and OBJ assets, BVH traversal, deterministic CPU direct lighting, PNG output, and a Cornell Box geometry smoke scene.

The Cornell Box now exposes the renderer's main quality limitation. Geometry, material binding, BVH traversal, and shadow acne handling are good enough for pipeline validation, but the CPU renderer only computes direct lighting. Dark sides remain black, red/green wall color does not bleed into nearby white surfaces, and the result looks like a debug renderer rather than a path-traced image.

This slice introduces the first CPU path tracer while preserving the current debug renderer.

## Goal

Add a deterministic CPU path-tracing integrator that produces visible diffuse indirect lighting in Cornell-style scenes.

The first version should be good enough to:

- show indirect diffuse bounce in simple scenes
- make Cornell Box shadows less black than the debug renderer
- let red and green walls influence nearby diffuse surfaces
- use existing `spp`, `max_depth`, and `seed` render settings
- remain stable enough for automated tests and repeatable manual image review

## Non-Goals

This slice does not implement:

- multiple importance sampling
- Russian roulette path termination
- random area-light surface sampling
- glossy, metallic, dielectric, or microfacet materials
- textures or imported material files
- spectral rendering or spectral-to-RGB conversion
- physically exact Cornell Box matching
- multithreading
- CUDA, OptiX, or GPU path tracing
- BVH build or traversal optimization
- denoising

## Architecture Boundary

`backend` remains the execution device or runtime target. `integrator` becomes the render algorithm.

Examples:

```toml
[render]
backend = "cpu"
integrator = "debug_direct"
```

```toml
[render]
backend = "cpu"
integrator = "path"
```

This keeps naming clear:

- `backend = "cpu"` means the CPU backend executes the render.
- `integrator = "debug_direct"` means the existing direct-light debug renderer is used.
- `integrator = "path"` means the new CPU path tracer is used.

Avoid names such as `cpu_path` or `cuda_path`; they mix execution target and rendering algorithm.

## Scene Schema

Add a render integrator enum to the semantic scene layer:

```cpp
enum class RenderIntegratorKind {
    DebugDirect,
    Path,
};
```

Add it to render settings:

```cpp
struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 512;
    int height = 512;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
};
```

The TOML field is:

```toml
[render]
integrator = "path"
```

If omitted, `integrator` defaults to `debug_direct` so existing scenes keep their current behavior.

Parser diagnostics:

- `render.integrator`: `unknown integrator`
- unknown fields inside `[render]` still report through the existing unknown-field path

## Render Scene Contract

Copy the integrator setting from `SceneDescription` to `RenderScene` during scene compilation.

```cpp
struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    ...
};
```

The backend dispatch remains based on `RenderBackendKind`. The CPU backend chooses the concrete CPU renderer based on `scene.integrator`.

Unsupported backend/integrator combinations are future work. In this slice:

- CPU + `debug_direct` works.
- CPU + `path` works.
- CUDA remains a not-implemented backend regardless of integrator.

## CPU Path Tracer v0

The path tracer reuses:

- `RenderScene`
- `RenderCamera`
- `RenderMaterial`
- `RenderTriangle`
- `RenderAreaLight`
- `RenderBvh`
- `IntersectBvh`
- `Film`

The implementation should live alongside, not inside, the CPU debug renderer. A suggested file split:

```text
include/yaoray/backends/cpu/cpu_path_tracer.hpp
src/backends/cpu/cpu_path_tracer.cpp
tests/cpu_path_tracer_tests.cpp
```

The algorithm for each pixel:

1. For each sample in `scene.spp`, generate a deterministic camera jitter.
2. Trace a camera ray.
3. For each depth from `0` to `scene.max_depth - 1`:
   - If the ray misses, add environment radiance multiplied by current throughput and stop.
   - If the ray hits invalid material data, add the existing magenta debug fallback color for consistency with the debug renderer.
   - Add material emission multiplied by current throughput.
   - Estimate direct area light contribution from the hit point.
   - Sample one cosine-weighted hemisphere direction around the hit normal.
   - Multiply throughput by diffuse albedo and continue.
4. Average samples through the existing `Film` accumulation.

The v0 diffuse model is Lambertian RGB. It intentionally uses the current `RenderMaterial::albedo` and `RenderMaterial::emission` only.

## Direct Light Handling

Path Tracer v0 keeps explicit direct light estimation.

At each diffuse hit, it should:

- iterate over existing `RenderAreaLight` entries
- target each light's center point
- use a shadow ray to reject occluded direct light
- use the same scale shape as the current debug renderer: `area * n_dot_l / distance_squared`
- multiply by material albedo and light radiance

This is not final physically correct light sampling. It is an intentional v0 choice because a pure random path tracer would rarely hit the small Cornell ceiling light at low sample counts.

Do not add MIS in this slice. Do not randomly sample positions over the area light in this slice.

## Randomness And Determinism

Rendering must be deterministic for a given scene and seed.

Use a small local RNG owned by the path tracer. It should not depend on global state or thread scheduling.

Recommended design:

- derive a per-sample RNG seed from `scene.seed`, pixel coordinates, sample index, and bounce depth
- use deterministic hash mixing, then a small PRNG such as PCG-style or xorshift
- use random values for camera jitter and cosine-weighted hemisphere sampling

This slice remains single-threaded. Multithreading is future work so test output stays stable.

## Sampling

Camera sampling:

- `spp = 1` still traces one centered or jittered sample per pixel.
- `spp > 1` traces multiple jittered samples per pixel.
- `Film::SampleCount(x, y)` must equal `spp` after rendering.

Bounce sampling:

- sample cosine-weighted hemisphere directions around the face-forward surface normal
- continue only if the sampled direction is above the surface
- use `max_depth` as the only path termination mechanism

No Russian roulette is used in v0.

## Stats

Existing public render stats should continue to be filled:

- rays traced
- shadow rays
- occluded shadow rays
- BVH node tests
- triangle tests
- hits
- misses
- BVH node count
- BVH max depth
- elapsed seconds

For v0, `rays_traced` counts primary camera rays plus indirect bounce rays that call `IntersectBvh`. Explicit direct-light shadow rays are tracked separately in `shadow_rays`.

CLI output should include the selected integrator:

```text
Integrator: path
```

## Example Scenes

Keep the existing Cornell debug scene:

```text
scenes/examples/cornell_box.toml
```

Add a separate path-traced Cornell scene:

```text
scenes/examples/cornell_box_path.toml
```

Suggested settings:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 512
height = 512
spp = 64
max_depth = 5
seed = 1
```

The path Cornell scene is for manual visual review, not a mandatory heavy CTest.

## Tests

Parser tests:

- render defaults include `integrator = DebugDirect`
- `integrator = "debug_direct"` parses
- `integrator = "path"` parses
- unknown integrator fails with `render.integrator`

Compiler tests:

- `SceneDescription::render.integrator` is copied to `RenderScene::integrator`

Backend tests:

- CPU backend dispatches `debug_direct` to the existing CPU debug renderer
- CPU backend dispatches `path` to the new CPU path tracer
- CUDA remains not implemented

Path tracer tests:

- traces one sample per pixel when `spp = 1`
- accumulates `spp` samples per pixel when `spp > 1`
- sees emissive surfaces
- same seed produces identical output for a small scene
- different seed changes at least one sampled pixel in a stochastic scene
- `max_depth = 1` produces no indirect bounce contribution
- `max_depth > 1` can produce indirect diffuse contribution in a controlled bounce scene

CLI smoke test:

- add a small low-resolution path tracer fixture
- render through `yaoray render`
- assert output mentions `Integrator: path`
- assert PNG output exists and has the PNG signature

Do not run `cornell_box_path.toml` as a required CTest in this slice.

## Manual Review

Manual review should compare:

- `scenes/examples/cornell_box.toml` with `integrator = debug_direct`
- `scenes/examples/cornell_box_path.toml` with `integrator = path`

Expected visual changes:

- large black regions become less black
- diffuse surfaces receive indirect light
- red and green walls subtly tint nearby white surfaces
- direct-light shadows still exist
- noise is expected at low sample counts

The image still will not be physically matched to Cornell's reference photographs because spectral data, BRDF import, MIS, and final light sampling are out of scope.

## Acceptance Criteria

- Existing scenes continue to render with debug direct lighting by default.
- `render.integrator = "path"` selects the CPU path tracer.
- `spp`, `max_depth`, and `seed` affect the path tracer deterministically.
- Automated tests cover parser, compiler, backend dispatch, path tracer behavior, and CLI smoke output.
- `scenes/examples/cornell_box_path.toml` exists for manual review.
- Full CTest passes.
- The path-traced Cornell image shows visible indirect lighting improvement over debug direct lighting.
