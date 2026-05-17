# YaoRay Configurable Area Light Samples Design

YaoRay now has a deterministic CPU path tracer with diffuse bounces, emissive
hits, random area-light surface sampling, BVH shadow rays, tile threading, and
Cornell Box examples. The current image-quality limitation is variance: the
soft shadow from the Cornell ceiling light is physically better than the old
center-sampled shadow, but it is noisy because each path hit samples each area
light only once.

This slice adds a small, explicit quality control: `render.light_samples`.
It lowers direct-lighting and soft-shadow noise by averaging multiple random
surface samples per area light at each path hit.

## Goals

- Add a scene-authored `render.light_samples` integer setting.
- Default `light_samples` to `1` so existing scenes keep current behavior.
- Use `light_samples` only in `integrator = "path"`.
- Keep `debug_direct` behavior unchanged as a fast reference renderer.
- Keep path output deterministic for a fixed scene, seed, spp, max depth, and
  thread count.
- Preserve bitwise-identical path output across requested worker counts.
- Improve the manual Cornell Box path preview by using more than one light
  sample per hit.

## Non-Goals

- No MIS.
- No denoiser.
- No Russian roulette.
- No adaptive sampling.
- No low-discrepancy or stratified sampler in this slice.
- No arbitrary area-light orientation.
- No material model changes.
- No CUDA backend work.

## User-Facing Setting

The new setting lives in the existing `[render]` table:

```toml
[render]
integrator = "path"
width = 512
height = 512
spp = 16
max_depth = 5
seed = 123
threads = 0
light_samples = 4
```

Semantics:

- `light_samples = 1` means the current behavior: one random point per area
  light per valid path hit.
- `light_samples = N` means the path integrator samples each area light `N`
  times at each valid path hit and averages the direct-light contribution.
- The setting is a positive integer.
- Missing `light_samples` defaults to `1`.
- `0`, negative values, floats, and strings produce a `render.light_samples`
  scene diagnostic.

The setting is parsed and carried through the render data model even when
`integrator = "debug_direct"`, but `debug_direct` does not consume it.

## Data Model And Parsing

Add `light_samples` beside the existing render quality and scheduling fields:

```cpp
struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
};
```

`RenderScene` mirrors the field:

```cpp
struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    ...
};
```

Parser changes:

- Add `"light_samples"` to the allowed `[render]` field list.
- Read it with the same integer path used by `spp`, `max_depth`, and `threads`.
- Reject values less than or equal to zero with `must be positive`.
- Existing unknown-field diagnostics remain unchanged for misspellings.

Compiler changes:

- Copy `scene.render.light_samples` to `compiled.light_samples`.

## Path Integrator Behavior

The current path direct-light estimator samples one point per area light:

```text
for each area light:
  sample one point on the light
  evaluate geometry, visibility, diffuse BRDF, and area PDF
  accumulate contribution
```

Change it to:

```text
light_sample_count = max(1, scene.light_samples)

for each area light:
  area_light_sum = 0
  for sample_index in 0 .. light_sample_count - 1:
    sample one point on the light using the path RNG
    evaluate geometry, visibility, diffuse BRDF, and area PDF
    accumulate contribution into area_light_sum
  add area_light_sum / light_sample_count to direct radiance
```

This keeps the estimator unbiased for direct area-light sampling. More samples
reduce variance in soft shadows and direct diffuse lighting, while increasing
shadow rays and BVH traversal work roughly in proportion to the number of valid
light samples.

RNG behavior:

- Light samples are consumed from the existing per-path RNG.
- The RNG seed remains derived from scene seed, pixel, sample index, and path
  state exactly as it is today.
- Work partitioning does not affect RNG order for a pixel/sample path, so tile
  threading remains deterministic.

Stats behavior:

- `shadow_rays` counts each actual visibility ray.
- `occluded_shadow_rays` counts each occluded visibility ray.
- `rays_traced` remains path rays, not shadow rays, matching current semantics.
- CLI throughput stats need no schema changes.

## Example Scenes

Update the manual Cornell path preview to demonstrate the setting:

```toml
light_samples = 4
```

Recommended targets:

- `scenes/examples/cornell_box_path.toml`
- `scenes/examples/cornell_box_path_threaded.toml`

These are manual visual-review scenes, not heavyweight required CTest renders.
At the current sample counts, they should show cleaner soft-shadow regions than
the one-light-sample version, with higher render cost.

## Documentation

Update README and architecture overview to state:

- `render.light_samples` controls direct area-light samples for `path`.
- Default is `1`.
- Higher values trade speed for lower soft-shadow/direct-light noise.
- `debug_direct` ignores the setting.
- MIS, denoising, Russian roulette, stratified sampling, and adaptive sampling
  remain future work.

## Testing

Parser tests:

- `SceneDescription::render.light_samples` defaults to `1`.
- `[render] light_samples = 4` parses successfully.
- `light_samples = 0` fails with `render.light_samples` and `must be positive`.
- `light_samples = -1` fails with `render.light_samples` and `must be positive`.
- `light_samples = 1.5` fails with `render.light_samples` and `must be an integer`.
- `light_samples = "many"` fails with `render.light_samples` and `must be an integer`.

Compiler tests:

- `CompileScene` copies `render.light_samples` into `RenderScene::light_samples`.
- `RenderScene` default value is `1`.

Path tracer tests:

- A small direct-light floor scene with `light_samples = 4` produces more
  `shadow_rays` than the same scene with `light_samples = 1`.
- For a fixed scene and seed, two renders with the same `light_samples` are
  bitwise identical.
- Existing thread determinism still passes for `light_samples > 1`.
- Invalid or absent area lights still produce zero shadow rays where existing
  tests expect no direct-light contribution.

CLI smoke tests:

- Existing CLI smoke tests continue to pass.
- No exact pixel assertion is required for Cornell preview scenes.

## Acceptance Criteria

- Scenes can author `render.light_samples`.
- Missing `light_samples` keeps existing output behavior.
- Invalid `light_samples` reports clear scene diagnostics.
- `CompileScene` propagates the setting to `RenderScene`.
- `path` uses the setting to average multiple direct area-light samples.
- `debug_direct` output is unchanged.
- Full Debug CTest passes.
- Release build succeeds.
- A manual Cornell path render with `light_samples = 4` succeeds and reports
  increased shadow-ray work compared with `light_samples = 1`.
