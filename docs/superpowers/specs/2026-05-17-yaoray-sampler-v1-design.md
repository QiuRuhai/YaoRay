# YaoRay Sampler v1 Design

YaoRay's CPU path tracer currently owns its random number generation directly
inside `cpu_path_tracer.cpp`. That is still simple and deterministic, but the
sample placement is purely independent random. At low and medium sample counts,
independent samples cluster unevenly, which makes Cornell Box edges, soft
shadows, and direct area-light noise converge slower than they need to.

This slice introduces a small sampler layer and a scene-authored sampler mode.
The goal is to improve sample placement for the existing CPU path tracer without
changing the rendering equation, material model, light model, or CUDA backend.

## Goals

- Add a scene-authored `render.sampler` setting.
- Support `"independent"` as the default sampler, preserving current behavior as
  the baseline/debug mode.
- Support `"stratified"` for deterministic stratified pixel jitter and
  deterministic stratified area-light UV sampling.
- Move path tracer random sample requests behind a small sampler object instead
  of calling the RNG directly at every sampling site.
- Keep fixed-seed output deterministic.
- Preserve output determinism across `render.threads` values.
- Keep the design small enough that future BSDF, MIS, lens, time, CMJ, Sobol,
  blue-noise, and GPU samplers can be added later without committing to a large
  hierarchy now.

## Non-Goals

- No Sobol, Halton, Hammersley, CMJ, or blue-noise sampler.
- No GPU sampler or CUDA backend work.
- No MIS.
- No BSDF abstraction.
- No diffuse-bounce stratification in this slice.
- No adaptive sampling.
- No denoiser.
- No Russian roulette.
- No lens, time, or motion-blur sampling.
- No image regression harness.

## User-Facing Setting

The new setting lives in the existing `[render]` table:

```toml
[render]
integrator = "path"
width = 512
height = 512
spp = 64
max_depth = 5
seed = 1
threads = 0
light_samples = 4
sampler = "stratified"
```

Semantics:

- Missing `sampler` defaults to `"independent"`.
- `"independent"` keeps the current random sampling behavior as closely as
  possible.
- `"stratified"` stratifies pixel jitter over `spp` and area-light UV sampling
  over `light_samples`.
- The setting is parsed and carried through the render data model even when the
  selected integrator does not consume it.
- Unknown sampler names produce a `render.sampler` diagnostic.

`debug_direct` ignores `render.sampler`, matching its role as a simple reference
renderer.

## Data Model And Parsing

Add a sampler kind enum near the existing render setting enums:

```cpp
enum class RenderSamplerKind {
    Independent,
    Stratified,
};
```

Add the setting beside the current render quality controls:

```cpp
struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
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
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
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

- Add `"sampler"` to the allowed `[render]` field list.
- Accept `"independent"` and `"stratified"`.
- Reject non-string values with a clear `render.sampler` diagnostic.
- Reject unknown strings with a clear `render.sampler` diagnostic.
- Keep existing unknown-field diagnostics unchanged for misspellings.

Compiler changes:

- Copy `scene.render.sampler` to `compiled.sampler`.

## Sampler Object

Add a small CPU path tracer sampler object rather than a virtual sampler
hierarchy. The first implementation can live in the CPU path tracer module or a
small CPU render helper file if the implementation becomes too large.

The intended shape is:

```cpp
class PixelSampler {
public:
    PixelSampler(
        RenderSamplerKind kind,
        std::uint64_t seed,
        int x,
        int y,
        int sample_index,
        int samples_per_pixel,
        int light_samples);

    Vec2f NextPixel2D();
    Vec2f NextLight2D(int light_sample_index);
    float Next1D();
    Vec2f Next2D();

private:
    Rng rng_;
    RenderSamplerKind kind_;
    int sample_index_ = 0;
    int samples_per_pixel_ = 1;
    int light_samples_ = 1;
};
```

The concrete API may differ slightly during implementation, but the boundary is
the important part: sampling sites ask the sampler for pixel, light, or generic
sample values and do not know how those values are distributed.

## Independent Behavior

For `"independent"`:

- `NextPixel2D()` returns the current behavior:
  - `spp == 1`: `(0.5, 0.5)`
  - `spp > 1`: two RNG floats
- `NextLight2D()` returns two RNG floats.
- `Next1D()` and `Next2D()` return RNG floats.

This preserves the current baseline mode and keeps debugging simple.

## Stratified Behavior

For `"stratified"`:

- `NextPixel2D()` places each pixel sample in a deterministic jittered cell.
- `NextLight2D(light_sample_index)` places each direct-light sample in a
  deterministic jittered cell on the area light's unit square.
- `Next1D()` and `Next2D()` remain independent RNG draws for generic use.
- Diffuse bounce sampling continues to use generic independent draws in this
  slice.

Grid shape:

- Use an approximately square grid for a requested count.
- `columns = ceil(sqrt(count))`
- `rows = ceil(count / columns)`
- The linear sample index maps to `cell_x = index % columns` and
  `cell_y = index / columns`.
- The returned point is the cell origin plus random jitter inside that cell.
- Clamp or guard the final value so it remains in `[0, 1)`.

This handles non-square counts such as `spp = 10` or `light_samples = 6` without
requiring user-facing constraints.

## Path Integrator Data Flow

Current flow:

```text
for each pixel:
  for each sample:
    create Rng from scene seed, pixel, sample index
    draw pixel jitter directly from Rng
    trace path, passing Rng through direct-light and bounce sampling
```

New flow:

```text
for each pixel:
  for each sample:
    create PixelSampler from scene sampler kind, seed, pixel, sample index, spp,
    and light_samples
    draw pixel jitter from PixelSampler
    trace path, passing PixelSampler through direct-light and bounce sampling
```

Direct area-light sampling changes from raw RNG UV draws to
`NextLight2D(light_sample_index)`. The existing `render.light_samples` averaging
logic remains the same.

The path tracer still seeds each pixel/sample independently. Because each
pixel/sample owns its sampler state, tile scheduling and worker count do not
change sample streams.

## Example Scenes

Update the manual Cornell path examples to demonstrate the new sampler:

```toml
sampler = "stratified"
```

Recommended targets:

- `scenes/examples/cornell_box_path.toml`
- `scenes/examples/cornell_box_path_threaded.toml`

The debug Cornell scene should stay simple unless a later plan needs it.

## Documentation

Update README and architecture overview to state:

- `render.sampler` selects CPU path tracer sample placement.
- Default is `"independent"`.
- `"stratified"` improves pixel and area-light sample distribution.
- `debug_direct` ignores the setting.
- More advanced samplers and GPU sampling remain future work.

## Testing

Parser tests:

- `SceneDescription::render.sampler` defaults to `Independent`.
- `[render] sampler = "independent"` parses successfully.
- `[render] sampler = "stratified"` parses successfully.
- Unknown strings fail with `render.sampler`.
- Non-string values fail with `render.sampler`.

Compiler tests:

- `CompileScene` copies `render.sampler` into `RenderScene::sampler`.
- `RenderScene` default value is `Independent`.

Sampler/path tracer tests:

- Independent mode keeps existing deterministic behavior for fixed seeds.
- Stratified mode is deterministic for fixed seeds.
- Stratified mode changes at least one stochastic render result compared with
  independent mode in a small scene where pixel jitter or light sampling affects
  the image.
- A threaded path scene with `sampler = "stratified"` produces the same image as
  the same scene with one worker.
- Existing `light_samples` shadow-ray accounting remains valid.

CLI/manual tests:

- Full Debug CTest passes.
- Release build succeeds.
- Manual Cornell path render succeeds with `sampler = "stratified"` and
  `light_samples > 1`.

## Acceptance Criteria

- Scenes can author `render.sampler`.
- Missing `sampler` preserves current behavior through the independent sampler.
- Invalid sampler settings produce clear diagnostics.
- `CompileScene` propagates the sampler kind to `RenderScene`.
- CPU path tracer consumes sampler-generated pixel and area-light samples.
- `independent` remains the baseline/debug mode.
- `stratified` affects pixel jitter and area-light UV sampling.
- Fixed-seed output is deterministic.
- Thread count does not change output.
- Cornell path example renders successfully with `sampler = "stratified"`.
