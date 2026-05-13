# YaoRay Render Backend Interface Design

Date: 2026-05-13

## Purpose

YaoRay now has a working CPU debug image loop:

```text
scene.toml -> SceneDescription -> RenderScene -> RenderCpuDebug -> Film -> PPM
```

The command-line app currently dispatches directly to `RenderCpuDebug()` and contains a special `cuda` not-implemented branch. That is enough for the first image, but it is the wrong boundary for the next renderer slices. BVH construction, a CPU path tracer, CUDA work, and future OptiX work should plug into a stable backend entry point without teaching the CLI about every renderer implementation.

This slice introduces a thin render backend interface. It moves backend dispatch out of the app layer while keeping image writing, tone mapping, TOML parsing, and scene compilation outside the backend.

## Goals

- Add a common backend interface under `include/yaoray/backends/`.
- Add a backend factory that creates a backend from `RenderBackendKind`.
- Wrap the current CPU debug renderer in a `CpuDebugBackend`.
- Represent CUDA as a backend that fails with a clear not-implemented error.
- Return a unified render result with a `Film`, stats, and error text.
- Update `yaoray render` to call the backend interface instead of `RenderCpuDebug()` directly.
- Preserve current CLI behavior for CPU success and CUDA failure.
- Keep `RenderCpuDebug()` available as a focused lower-level function for unit tests.
- Add backend-level tests before changing future rendering internals.

## Non-Goals

- No Integrator abstraction in this slice.
- No BVH construction or traversal changes.
- No path tracing, direct lighting, sampling changes, or material changes.
- No real CUDA implementation.
- No OptiX implementation.
- No image output ownership inside backends.
- No PNG, EXR, or output format changes.
- No progress callbacks, cancellation, multithreading, or tile scheduler.

## Approved Decisions

The first backend layer is intentionally thin:

```text
CLI
  -> LoadSceneFile
  -> CompileScene
  -> CreateRenderBackend(kind)
  -> backend.Render(render_scene, request)
  -> WritePpm(result.film, tone_map, output_path)
```

Backends render a compiled `RenderScene` into a `Film`. They do not parse TOML, compile scenes, choose tone mapping, or write image files.

The Integrator abstraction is deferred. It becomes useful when YaoRay has at least two CPU-side algorithms, such as a debug integrator and a path tracer. Adding it now would create an interface that has only one real user and would likely change during BVH and path tracing work.

## Architecture

The dependency direction should become:

```text
scene -> core
render -> scene + core
film -> core
backends -> render + film + core
app -> scene + render + film + backends
```

The app layer may include the common backend interface. It should not include CPU-specific renderer headers.

Recommended files:

```text
include/yaoray/backends/backend.hpp
src/backends/backend.cpp
include/yaoray/backends/cpu/cpu_debug_backend.hpp
src/backends/cpu/cpu_debug_backend.cpp
```

The existing CPU debug renderer remains in:

```text
include/yaoray/backends/cpu/cpu_debug_renderer.hpp
src/backends/cpu/cpu_debug_renderer.cpp
```

The CMake target can be renamed or folded into a single `yaoray_backends` library that contains the backend factory, CPU debug backend wrapper, CUDA not-implemented backend, and existing CPU debug renderer. Tests and the app should link that backend library instead of depending directly on CPU renderer implementation details.

## Public API

Add a common backend interface:

```cpp
struct RenderRequest {
};

struct RenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct RenderResult {
    bool ok = false;
    std::string error;
    std::optional<Film> film;
    RenderStats stats;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    virtual RenderBackendKind Kind() const = 0;
    virtual RenderResult Render(const RenderScene& scene, const RenderRequest& request) = 0;
};

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);
```

`RenderRequest` is empty in this slice. It exists as the future home for render-time controls that are not part of the compiled scene, such as cancellation, progress reporting, tile scheduling, or debug options. It should not include output paths or tone mapping settings.

`RenderResult::film` is optional because failed backends, such as the CUDA stub, should not need to manufacture an empty `Film`. A successful render must set `ok = true`, leave `error` empty, and provide a film.

## CPU Debug Backend

`CpuDebugBackend` should adapt the existing renderer:

```text
CpuDebugBackend::Render(scene, request)
  -> RenderCpuDebug(scene)
  -> map CpuDebugRenderResult to RenderResult
```

Stats should be copied into `RenderStats` without changing current meanings:

- `rays_traced`
- `triangle_tests`
- `hits`
- `misses`
- `elapsed_seconds`

This preserves current CLI output and keeps existing CPU debug renderer tests meaningful.

## CUDA Not-Implemented Backend

`CreateRenderBackend(RenderBackendKind::Cuda)` should return a backend object rather than making the CLI special-case CUDA. Its render result should be:

```text
ok = false
error = "CUDA backend not implemented yet."
film = empty
```

The CLI prints this error and exits non-zero. Later CUDA work can replace the stub without changing CLI dispatch.

## CLI Behavior

`yaoray render <scene.toml> [--backend cpu|cuda]` keeps the same user-facing behavior:

1. Parse and validate the scene file.
2. Apply the backend override.
3. Compile the scene into `RenderScene`.
4. Create the backend from `render_scene.backend`.
5. Render through the backend interface.
6. If rendering fails, print `RenderResult::error` and return non-zero.
7. If rendering succeeds, write `scene.film.output` using the PPM writer.
8. Print output path and render stats.

After this change, `src/app/main.cpp` should no longer include `yaoray/backends/cpu/cpu_debug_renderer.hpp`.

## Error Handling

Backend failures are ordinary user-facing render failures, not exceptions. They should return `RenderResult{.ok = false, .error = "..."}`
with no film.

The CLI is responsible for converting backend failure into a non-zero process exit. Image write failures remain separate and continue to use `ImageWriteResult`.

Backend implementations may still use assertions or internal checks for programmer errors, but predictable runtime states such as unsupported CUDA should be structured render failures.

## Tests

Required tests:

- `CreateRenderBackend(RenderBackendKind::Cpu)` returns a non-null backend with kind `Cpu`.
- CPU backend render returns `ok = true`.
- CPU backend render returns a populated `Film` with the requested dimensions.
- CPU backend render maps current debug stats into `RenderStats`.
- `CreateRenderBackend(RenderBackendKind::Cuda)` returns a non-null backend with kind `Cuda`.
- CUDA backend render returns `ok = false`.
- CUDA backend render error contains `CUDA backend not implemented yet`.
- CLI CPU render still writes a valid PPM file.
- CLI CUDA render still exits non-zero and prints the CUDA not-implemented message.

Existing lower-level CPU debug renderer tests should remain in place. They verify renderer math and shading behavior below the backend interface.

## Completion Criteria

- The app calls `CreateRenderBackend()` and no longer branches directly on CPU versus CUDA rendering.
- `src/app/main.cpp` no longer includes CPU-specific backend headers.
- CPU rendering still writes `scenes/examples/out/minimal.ppm`.
- CUDA rendering still fails clearly with `CUDA backend not implemented yet.`
- Backend unit tests cover CPU success and CUDA not-implemented failure.
- `ctest --test-dir build --output-on-failure -C Debug` passes.
- README or architecture docs mention that rendering now goes through a backend interface.

## Future Work

The next slices after this interface should be:

1. BVH construction and nearest-hit traversal inside the CPU backend path.
2. Asset importer interfaces and first real mesh loading.
3. CPU path tracer work that can later motivate a real Integrator abstraction.
4. CUDA backend subset that consumes the same `RenderScene` and backend result contract.
