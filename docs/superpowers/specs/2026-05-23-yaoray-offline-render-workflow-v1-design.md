# YaoRay Offline Render Workflow v1 Design

## Summary

This slice turns CPU path tracing from a fire-and-forget render command into a
usable offline workflow for larger scenes such as Sponza. It adds progress/ETA
reporting, periodic preview PNG checkpoints, and resumable render state
checkpoints for CPU path rendering.

The goal is to make long CPU renders practical on macOS before CUDA/OptiX work
begins. The design keeps checkpointing above the backend-neutral scene IR and
below the CLI orchestration boundary: the CPU renderer exposes progress and
resume hooks; the CLI decides when to print, save preview PNGs, and persist
checkpoint state.

## Goals

- Print progress for long CPU path renders:
  - completed samples per pixel
  - percent complete
  - elapsed seconds
  - estimated remaining seconds
  - samples/sec
  - rays/sec
- Write periodic preview PNG files from the current partial film.
- Write periodic resumable checkpoint state files.
- Resume CPU path renders from a compatible checkpoint file.
- Keep checkpoint reads/writes deterministic and data-race free.
- Support Sponza-style manual offline renders without making default CTest slow.
- Keep CUDA as a stub: no CUDA checkpoint implementation in this slice.

## Non-Goals

- No distributed rendering.
- No tile cache format.
- No live UI or web dashboard.
- No progressive display window.
- No EXR/HDR checkpoint output.
- No adaptive sampling.
- No checkpoint compression.
- No checkpoint compatibility guarantees across future binary versions.
- No resume support for debug-direct rendering.
- No CUDA/OptiX implementation.

## User Interface

Add an optional `[offline]` scene table:

```toml
[offline]
progress = true
progress_interval_seconds = 5
checkpoint_png = "out/local_sponza.checkpoint.png"
checkpoint_png_interval_seconds = 60
checkpoint_state = "out/local_sponza.yrcheckpoint"
checkpoint_state_interval_seconds = 60
resume = true
```

Defaults:

- `progress = false`
- `progress_interval_seconds = 5`
- `checkpoint_png = ""`
- `checkpoint_png_interval_seconds = 60`
- `checkpoint_state = ""`
- `checkpoint_state_interval_seconds = 60`
- `resume = false`

Validation:

- Intervals must be positive when their corresponding feature is enabled.
- `checkpoint_png` must be `.png` when non-empty.
- `checkpoint_state` must be `.yrcheckpoint` when non-empty.
- `resume = true` requires `checkpoint_state` to be non-empty.
- Offline checkpoint/resume is valid only for `backend = "cpu"` and
  `integrator = "path"`.

Existing `film.checkpoint_interval_s` and `film.checkpoint_path` must not become
a second long-term checkpoint API. In this slice they are deprecated
compatibility aliases for preview PNG checkpointing only:

- If `[offline]` is absent and `film.checkpoint_path` is set, map it to
  `offline.checkpoint_png`.
- If `[offline]` is absent and `film.checkpoint_interval_s` is positive, map it
  to `offline.checkpoint_png_interval_seconds`.
- If `[offline]` is present, `[offline]` wins and any `film.checkpoint_*` values
  produce a warning diagnostic.
- `film.checkpoint_*` never controls `.yrcheckpoint` state or resume behavior.

## Rendering Model

The current CPU path tracer renders all samples for a pixel inside tile workers.
For checkpointing, this must be refactored into sample passes:

```text
for sample_index in completed_spp .. target_spp:
  render one sample for every pixel using tile workers
  merge worker stats for that pass
  emit progress event at the sample-pass boundary
  allow the caller to checkpoint the stable film
```

The sample-pass boundary is important:

- Every pixel has the same completed sample count.
- `Film` is stable when progress/checkpoint code reads it.
- There is no need for concurrent read/write locking around checkpoint PNG or
  state serialization.
- Resume can continue at `completed_spp` using the existing deterministic
  `SeedForPixelSample(scene.seed, x, y, sample_index)` behavior.

Progress/checkpoint intervals are time based, but events are emitted only at
sample-pass boundaries. If one sample pass takes longer than the interval, the
next boundary emits the pending progress/checkpoint.

## Component Design

### Scene Layer

Add:

```cpp
struct OfflineSettings {
    bool progress = false;
    int progress_interval_seconds = 5;
    std::filesystem::path checkpoint_png;
    int checkpoint_png_interval_seconds = 60;
    std::filesystem::path checkpoint_state;
    int checkpoint_state_interval_seconds = 60;
    bool resume = false;
};
```

Add `OfflineSettings offline;` to `SceneDescription`.

The scene parser should normalize `checkpoint_png` and `checkpoint_state`
relative to the scene file directory, matching existing `film.output` behavior.

### Film Layer

`Film` remains the accumulation container. It should gain minimal APIs needed by
checkpointing:

```cpp
const std::vector<FilmPixel>& Pixels() const;
void SetPixelForCheckpoint(int x, int y, FilmPixel pixel);
```

The setter exists only so checkpoint loading can reconstruct a film without
making serialization code a friend of `Film`. It must validate bounds by using
the same indexing path as `AddSample`.

### Checkpoint State Layer

Create a focused module such as:

```text
include/yaoray/film/film_checkpoint.hpp
src/film/film_checkpoint.cpp
```

Responsibilities:

- Write and read `.yrcheckpoint`.
- Validate magic/version.
- Validate dimensions.
- Store completed samples per pixel.
- Store render settings hash.
- Store film pixel sums and sample counts.
- Return structured errors instead of throwing.

Recommended binary layout:

```text
magic: "YRCHECK1" (8 bytes)
version: uint32 = 1
width: uint32
height: uint32
target_spp: uint32
completed_spp: uint32
settings_hash: uint64
pixel_count: uint64
pixels:
  sum.x: float32
  sum.y: float32
  sum.z: float32
  samples: uint32
```

V1 can use host-endian binary because files are local workflow artifacts, not
portable interchange assets. The reader must reject mismatched magic, version,
dimensions, pixel count, target spp, and settings hash.

The settings hash should include render-affecting values:

- `render.width`
- `render.height`
- `render.spp`
- `render.max_depth`
- `render.seed`
- `render.sampler`
- `render.light_samples`
- `render.radiance_clamp`
- compiled triangle/material/texture counts
- camera basis and environment settings

The hash does not need cryptographic strength. A stable FNV-1a style hash over
explicit numeric/string fields is enough for v1. The purpose is to prevent
obvious accidental resume from an incompatible scene.

### Backend Layer

Extend `RenderRequest` to carry optional offline hooks:

```cpp
struct RenderProgress {
    int completed_spp = 0;
    int target_spp = 0;
    std::uint64_t completed_samples = 0;
    std::uint64_t target_samples = 0;
    std::uint64_t rays_traced = 0;
    double elapsed_seconds = 0.0;
};

struct RenderProgressDecision {
    bool cancel = false;
    std::string error;
};

using RenderProgressCallback = std::function<RenderProgressDecision(const RenderProgress&, const Film&)>;

struct RenderRequest {
    const Film* resume_film = nullptr;
    int resume_completed_spp = 0;
    RenderProgressCallback progress_callback;
};
```

Only the CPU path tracer consumes these fields in v1. Debug-direct ignores them.
CUDA prepare/render remains not implemented.

The callback receives a stable `Film` reference only at sample-pass boundaries.
The callback must run on the render caller thread after worker tile jobs for the
pass have completed, not inside a worker thread. If the callback returns
`cancel = true`, rendering stops and `RenderResult` reports `ok = false` with
the callback error.

### CPU Path Tracer

`RenderCpuPathTrace()` should accept a request/options object or the backend
`RenderRequest` data. It should:

- Initialize film from `resume_film` when provided.
- Start at `resume_completed_spp`.
- Render one sample per pixel per sample pass.
- Merge per-pass worker stats into cumulative stats.
- Call progress callback after each completed sample pass.
- Preserve deterministic output for non-resumed renders with the same scene and
  seed.

Resume behavior:

- `resume_completed_spp <= target_spp`.
- If `resume_completed_spp == target_spp`, render can skip tracing and return
  the loaded film with zero new rays.
- If any pixel sample count in the loaded film differs from
  `resume_completed_spp`, reject the checkpoint.

### CLI Layer

`src/app/main.cpp` should orchestrate offline workflow:

1. Parse scene.
2. Compile scene.
3. Create backend and prepare scene.
4. If `offline.resume`, load checkpoint state before render.
5. Build a `RenderRequest` with:
   - resume film pointer/completed spp
   - progress callback
6. In callback:
   - print progress when interval elapsed
   - write checkpoint PNG when interval elapsed
   - write `.yrcheckpoint` when interval elapsed
7. Render.
8. Write final output image.
9. Write final checkpoint state if `checkpoint_state` is configured and the
   render did not already write the final state at target spp.

CLI progress output should be line-oriented and stable enough for tests:

```text
Progress: 4/64 spp (6.25%) elapsed=12.3s eta=184.5s samples/sec=12000 rays/sec=85000
Checkpoint image: scenes/examples/out/local_sponza.checkpoint.png
Checkpoint state: scenes/examples/out/local_sponza.yrcheckpoint
```

Error handling:

- If checkpoint load fails with `resume = true`, exit non-zero and print the
  checkpoint error.
- If checkpoint PNG write fails during render, exit non-zero after the callback
  returns an error. The callback mechanism should allow cancellation.
- If checkpoint state write fails, exit non-zero.
- If offline settings are enabled for unsupported backend/integrator, fail
  before rendering.

## Testing Strategy

Automated tests stay small and deterministic.

Scene parser tests:

- Defaults for `[offline]`.
- Valid offline table with normalized paths.
- Reject non-positive intervals when enabled.
- Reject `resume = true` without `checkpoint_state`.
- Reject unsupported checkpoint file extensions.

Film checkpoint tests:

- Write and read a tiny `Film`.
- Reject bad magic/version.
- Reject dimension mismatch.
- Reject settings hash mismatch.
- Reject non-uniform pixel sample counts when resuming.

CPU path tracer tests:

- Non-resumed render remains deterministic.
- Rendering `spp = 4` in one run matches rendering `spp = 2`, checkpointing,
  then resuming to `spp = 4`.
- Progress callback is called once per completed sample pass.

CLI tests:

- A tiny CPU path scene with `[offline] progress = true` prints `Progress:`.
- A tiny CPU path scene writes checkpoint PNG and `.yrcheckpoint`.
- A second CLI run with `resume = true` consumes the checkpoint and reaches the
  target output.
- Unsupported `backend = "cuda"` with offline resume/checkpoint fails with a
  clear diagnostic.

Manual verification:

```bash
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.checkpoint.png
test -s scenes/examples/out/local_sponza.yrcheckpoint
```

The committed `local_sponza.toml` can enable conservative offline defaults once
the workflow exists.

## Risks

- Refactoring the path tracer to sample-pass scheduling could affect
  determinism or performance. Tests must compare resumed and non-resumed output.
- Writing checkpoint PNG/state too frequently can dominate short renders.
  Defaults should be conservative.
- The settings hash may miss a render-affecting field. The hash should be
  explicit and covered by tests for at least width, height, spp, seed, and
  integrator/backend compatibility.
- Existing `film.checkpoint_*` fields may confuse users. The implementation
  should document and test the selected compatibility behavior.

## Success Criteria

- CPU path renders can print progress during long runs.
- CPU path renders can emit periodic preview PNG files.
- CPU path renders can emit and resume from `.yrcheckpoint` state.
- A resumed render reaches the same film result as a single uninterrupted render
  for deterministic test scenes.
- Unsupported backend/integrator combinations fail before rendering.
- Default CTest remains fast.
- Sponza can be rendered with progress and checkpoints on CPU.
