# YaoRay CPU Path Tracer Threading Design

## Context

YaoRay now has two CPU integrators:

- `debug_direct`, the single-sample direct-lighting reference renderer
- `path`, the deterministic CPU path tracer v0

The path tracer closes the rendering loop for Cornell-style indirect lighting, but it is still single-threaded. A 256x256 Cornell path preview at 16 spp is slow enough to make iteration on sampling, materials, and lighting painful.

CUDA remains a future target, but a deterministic CPU multithreaded path tracer is still useful now. It shortens the development loop, provides a stable CPU reference for future GPU work, and gives a performance baseline for later renderer changes.

## Goal

Add deterministic CPU tile-based multithreading for `render.integrator = "path"`.

This slice should:

- keep the path tracer bitwise deterministic for the same scene, seed, spp, and max depth regardless of thread setting
- keep output independent from worker scheduling order
- let scenes request a fixed thread count for benchmark comparisons
- keep `debug_direct` unchanged
- expose lightweight performance stats in CLI output

## Non-Goals

This slice does not implement:

- CUDA, OptiX, or GPU rendering
- multithreading for `debug_direct`
- a full job system or engine-wide scheduler
- a benchmark subcommand
- dynamic tile-size tuning
- SIMD optimization
- path-tracing quality changes such as MIS, random area-light surface sampling, new material models, or denoising

## Scene Schema

Add `threads` to `[render]`:

```toml
[render]
threads = 0
```

Semantic meaning:

- `threads = 0`: auto thread count
- `threads = 1`: force single-threaded path rendering
- `threads = N`: request `N` CPU workers

The default is `0`.

Validation:

- missing `threads` defaults to `0`
- non-integer values are scene errors
- negative values are scene errors
- `0` is accepted only as auto
- positive values are accepted as explicit worker requests

`threads` is copied from `SceneDescription::render` to `RenderScene`, just like `spp`, `max_depth`, and `seed`.

## Auto Thread Count

Auto mode resolves to:

```cpp
max(1, std::thread::hardware_concurrency() - 1)
```

If `hardware_concurrency()` returns `0`, auto resolves to `1`. The implementation should avoid unsigned underflow when computing `hardware_concurrency() - 1`.

Explicit `threads = N` should not reserve a core. It means the scene author wants to benchmark or run with that requested count.

The renderer may cap the actual worker count by tile count to avoid creating idle workers for tiny images. CLI output should report the actual worker count used for the render.

## Tile Scheduler

Add a thin CPU tile helper under the CPU backend area, for example:

- `include/yaoray/backends/cpu/cpu_tile_scheduler.hpp`
- `src/backends/cpu/cpu_tile_scheduler.cpp`

The helper owns only CPU tile scheduling concerns:

- split an image into fixed-size rectangular tiles
- resolve actual worker count from requested threads and tile count
- visit every tile exactly once
- call a renderer-supplied callback for each tile

It must not know about:

- path tracing
- Film
- BVH traversal
- materials
- scene parsing
- CLI output

A fixed tile size of `16x16` is sufficient for this slice. The tile list should be deterministic, ordered by `tile_y` then `tile_x`.

## Path Tracer Integration

`RenderCpuPathTrace(scene)` uses the tile helper for both single-threaded and multithreaded rendering.

Within each tile, the path tracer keeps the existing deterministic order:

1. iterate pixels in row-major order
2. iterate samples from `0` to `scene.spp - 1`
3. seed the RNG from `scene.seed`, pixel coordinate, and sample index
4. write only that pixel's samples

Each pixel belongs to exactly one tile, so no two workers write the same Film pixel.

The existing `debug_direct` renderer remains single-threaded and does not use the tile helper in this slice.

## Determinism

Determinism is a hard requirement.

The implementation must avoid:

- shared RNG state
- unordered floating-point accumulation into the same pixel
- shared stats counters updated from worker threads
- output that depends on tile execution order

Expected property:

```text
render(path, threads = 1) == render(path, threads = 2) == render(path, threads = 4)
```

for every pixel and sample count, except for `elapsed_seconds`.

This works because each sample path is seeded directly from scene and pixel identity, not from worker order.

## Stats

Each worker should maintain local `CpuPathTraceStats`.

After all workers finish, the main thread merges local stats in a stable order by worker index:

- ray, shadow, triangle, BVH node test, hit, and miss counters are summed
- `bvh_nodes` and `bvh_max_depth` come from `scene.bvh`
- `elapsed_seconds` measures wall-clock time for the whole render

Add the actual resolved thread count to the render stats surface so the CLI can report it. Use a concrete integer field named `threads` in both `RenderStats` and `CpuPathTraceStats`.

For `debug_direct`, this value remains `1`. For `path`, it reports the actual worker count after auto resolution and tile-count capping.

## CLI Output

Keep the existing timing and trace counters, and add:

```text
Threads: N
Samples/sec: X
Rays/sec: Y
```

Definitions:

- `Threads`: actual worker count used for the render
- `Samples/sec`: `width * height * spp / elapsed_seconds`
- `Rays/sec`: `rays_traced / elapsed_seconds`

If `elapsed_seconds <= 0`, print `0` for rates rather than dividing by zero.

These are lightweight benchmark aids, not a dedicated benchmark mode.

## Testing

### Parser and Compiler Tests

Cover:

- `threads` defaults to `0`
- `threads = 1` parses
- `threads = 4` parses
- `threads` copies into `RenderScene`
- `threads = -1` is a scene error
- `threads = 1.5` is a scene error
- `threads = "fast"` is a scene error

### Tile Helper Tests

Cover:

- a small image is fully covered by tiles
- edge tiles clamp to image bounds
- every tile is visited exactly once
- `threads = 1` visits all tiles
- `threads = 4` visits all tiles
- resolved worker count is at least `1`

Do not test OS scheduling order.

### Path Tracer Tests

Use a small deterministic path-traced scene.

Render it with:

- `threads = 1`
- `threads = 2`
- `threads = 4`

Assert:

- `Film::SampleCount(x, y)` is identical
- `Film::LinearPixel(x, y)` is exactly identical for every pixel
- core stats are identical, excluding `elapsed_seconds`
- reported thread count matches resolved worker count for the run

### CLI Smoke Tests

Extend the existing path smoke test to check:

- `Threads:`
- `Samples/sec:`
- `Rays/sec:`

## Error Handling

Scene validation reports bad `threads` values as `render.threads`.

Runtime thread resolution should be defensive:

- auto with unavailable hardware concurrency resolves to `1`
- empty tile lists render nothing but still produce a valid Film and stats
- worker exceptions are not introduced in this slice because render callbacks do not throw under normal renderer operation

## Acceptance Criteria

- `render.threads` is documented and parsed.
- `RenderScene` carries the requested thread count.
- CPU path tracer uses tile-based rendering.
- `debug_direct` output and implementation remain unchanged.
- Path tracer output is bitwise identical for `threads = 1`, `2`, and `4` on the deterministic test scene.
- CLI output reports actual threads, samples/sec, and rays/sec.
- Full CTest passes.
