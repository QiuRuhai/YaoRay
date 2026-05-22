# YaoRay Backend Layer v1 Refactor Design

## Summary

This refactor makes the backend layer explicit without implementing CUDA
rendering. The current architecture already has a backend-neutral
`RenderSceneIR` and a CPU-specific `CpuPreparedScene`, but the public backend
interface still exposes a single `Render(scene, request)` entry point. That
keeps scene preparation hidden inside the CPU backend and leaves the CUDA path
as a small anonymous not-implemented renderer.

Backend Layer v1 changes the contract to a clear two-stage lifecycle:

```text
RenderSceneIR -> backend-specific prepared scene -> rendered Film
```

CPU remains the only real backend. CUDA gets a named interface and prepared
scene placeholder, but it still returns a clear not-implemented result. No CUDA
runtime dependency, kernel code, device allocation, or OptiX work is included.

## Goals

- Make backend preparation an explicit part of the backend abstraction.
- Keep `RenderSceneIR` backend-neutral and free of CPU BVH, CUDA pointers, or
  OptiX handles.
- Keep CPU behavior and rendered output unchanged.
- Preserve the existing CLI flow: parse TOML, compile `RenderSceneIR`, select a
  backend, render, write the film, print stats.
- Move the CUDA placeholder from an anonymous local class into a named backend
  slice with future extension points.
- Define a small `CudaPreparedScene` placeholder so future CUDA work has an
  obvious target without pulling CUDA into this slice.
- Add tests that lock the CPU prepare/render boundary and the CUDA
  not-implemented behavior.

## Non-Goals

- No CUDA kernel implementation.
- No CUDA runtime, driver API, CMake CUDA language, or device-memory dependency.
- No OptiX implementation.
- No backend registry or full capability-query system.
- No render compiler changes beyond adapting to the backend interface.
- No `RenderSceneIR` data model changes.
- No material, texture, BVH algorithm, sampler, or integrator behavior changes.
- No CLI syntax change.

## Current State

Current public backend interface:

```cpp
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual RenderResult Render(const RenderSceneIR& scene,
                                const RenderRequest& request) = 0;
};
```

The CPU implementation prepares inside `CpuDebugBackend::Render()`:

```text
CpuDebugBackend::Render()
  PrepareCpuScene(RenderSceneIR)
  if integrator == path: RenderCpuPathTrace(CpuPreparedScene)
  else: RenderCpuDebug(CpuPreparedScene)
```

That works, but it hides a boundary that matters for CUDA. Future GPU backends
will need a preparation stage for compact buffers, image data, acceleration
structures, and potentially backend-specific validation. Keeping this stage as
an implementation detail makes the backend contract less clear.

CUDA is currently an anonymous `CudaNotImplementedBackend` inside
`src/backends/backend.cpp`. It proves that `render.backend = "cuda"` parses and
returns a controlled error, but it is not an architecture placeholder for a real
future CUDA backend.

## Target Architecture

### Backend Interface

Introduce a backend-prepared-scene base type and make the backend lifecycle
explicit:

```cpp
struct BackendPrepareResult {
    bool ok = false;
    std::string error;
    std::unique_ptr<PreparedScene> scene;
};

class PreparedScene {
public:
    virtual ~PreparedScene() = default;
    virtual RenderBackendKind Kind() const = 0;
    virtual const RenderSceneIR& SourceScene() const = 0;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual BackendPrepareResult Prepare(const RenderSceneIR& scene) = 0;
    virtual RenderResult Render(const PreparedScene& scene,
                                const RenderRequest& request) = 0;
};
```

Exact names can be adjusted during implementation, but the boundary should stay
the same: `Prepare()` owns backend-specific preparation and `Render()` consumes
only a prepared scene for the same backend.

The top-level app flow becomes:

```text
compile scene -> backend->Prepare(render_ir) -> backend->Render(prepared)
```

This keeps app and CLI code independent of CPU BVH details and future CUDA
device details.

### CPU Backend

`CpuPreparedScene` becomes the CPU implementation of the prepared-scene
contract:

```cpp
struct CpuPreparedScene final : PreparedScene {
    const RenderSceneIR* render_scene = nullptr;
    RenderBvh bvh;

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSceneIR& Scene() const;
};
```

`PrepareCpuScene()` can remain as a focused helper returning `CpuPrepareResult`,
or it can be adapted internally by `CpuBackend::Prepare()`. The important
boundary is that BVH construction stays CPU-specific and no BVH appears in
`RenderSceneIR`.

The CPU backend render stage should downcast or otherwise validate that the
prepared scene is a CPU prepared scene. If the wrong prepared-scene kind is
passed, it should return a clean error instead of undefined behavior.

The existing CPU debug and CPU path tracer entry points keep consuming
`CpuPreparedScene`.

### CUDA Placeholder

Add a named CUDA backend slice:

```text
include/yaoray/backends/cuda/cuda_backend.hpp
include/yaoray/backends/cuda/cuda_prepared_scene.hpp
src/backends/cuda/cuda_backend.cpp
src/backends/cuda/cuda_prepared_scene.cpp
```

The CUDA prepared scene is a minimal placeholder:

```cpp
struct CudaPreparedScene final : PreparedScene {
    const RenderSceneIR* render_scene = nullptr;

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
};
```

`CudaBackend::Prepare()` should return `ok = false` with a clear message such
as:

```text
CUDA backend preparation is not implemented yet.
```

`CudaBackend::Render()` should also return a controlled not-implemented error if
called. It should not allocate device data, include CUDA headers, or require CUDA
toolchains on macOS.

This preserves current user-facing CUDA behavior while creating a real place
for future CUDA buffer upload and GPU scene preparation.

### Backend Factory

`CreateRenderBackend(RenderBackendKind kind)` remains the public factory.

Implementation moves CUDA out of the anonymous local class and returns a named
`CudaBackend`. No registry is needed in this slice because there are only two
backend kinds and no runtime plugin system.

### Request And Result

`RenderRequest`, `RenderResult`, and `RenderStats` stay as the shared backend
API. CPU still fills the same stats fields. CUDA not-implemented results should
set `ok = false`, leave `film` empty, and provide a clear error string.

No capability fields are added in v1. If future CUDA work needs feature-level
errors such as "CUDA path tracer does not support HDRI yet", that can be added
as a later `BackendCapabilities` slice once multiple real backend capabilities
exist.

## Data Flow

CPU flow:

```text
SceneDescription
  -> CompileScene()
  -> RenderSceneIR
  -> CpuBackend::Prepare()
  -> CpuPreparedScene(RenderSceneIR*, RenderBvh)
  -> CpuBackend::Render()
  -> RenderCpuDebug() or RenderCpuPathTrace()
  -> RenderResult
```

CUDA placeholder flow:

```text
SceneDescription
  -> CompileScene()
  -> RenderSceneIR
  -> CudaBackend::Prepare()
  -> not implemented error
```

If `CudaBackend::Render()` is called directly with any prepared scene, it should
also return a not-implemented error. This prevents tests or future callers from
depending on undefined placeholder behavior.

## Error Handling

- `Prepare()` failure returns `BackendPrepareResult{ok=false, error=...}` and no
  prepared scene.
- `Render()` failure returns `RenderResult{ok=false, error=...}` and no film.
- Passing a prepared scene to the wrong backend returns a clean error.
- CLI should print backend prepare or render errors in the same style as current
  render backend errors.
- CUDA error messages should say not implemented, not "not available", because
  the backend kind exists but has no implementation yet.

## Testing Strategy

Backend tests should cover:

- `CreateRenderBackend(RenderBackendKind::Cpu)` returns a CPU backend.
- `CreateRenderBackend(RenderBackendKind::Cuda)` returns a named CUDA backend.
- CPU `Prepare()` succeeds for a simple triangle scene and produces a prepared
  scene with CPU BVH nodes and max depth.
- CPU `Render()` consumes a CPU prepared scene and preserves current debug/path
  behavior.
- CPU `Render()` rejects a non-CPU prepared scene with a controlled error.
- CUDA `Prepare()` returns a not-implemented error and no prepared scene.
- CUDA `Render()` returns a not-implemented error.
- CLI `--backend cuda` still exits through the expected controlled failure path.

Regression verification:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

No rendered CPU output or stats semantics should change.

## Migration Plan

1. Add `PreparedScene` and `BackendPrepareResult` to the backend API.
2. Adapt CPU prepared scene to implement the prepared-scene contract.
3. Split `CpuDebugBackend::Render(scene, request)` into CPU `Prepare()` and
   `Render(prepared, request)`.
4. Move CUDA placeholder into named CUDA backend files.
5. Update backend factory and app render flow.
6. Update backend tests and CLI CUDA tests.
7. Run full verification.

## Risks And Mitigations

- **Interface churn without behavior change:** Keep the slice small and preserve
  current CPU render outputs.
- **Over-abstracting too early:** Do not add registry, capabilities, async
  preparation, or device abstractions yet.
- **Unsafe downcasts:** Validate prepared-scene kind before casting in backend
  render methods.
- **Mac development friction:** CUDA files must compile as ordinary C++ and must
  not include CUDA headers.
- **Stats regression:** Reuse existing CPU stats conversion and keep CLI output
  stable.

## Success Criteria

- Backend API exposes a two-stage prepare/render lifecycle.
- `RenderSceneIR` remains backend-neutral.
- CPU backend still prepares `CpuPreparedScene` with CPU BVH and renders current
  debug/path scenes.
- CUDA has named backend and prepared-scene placeholders but no implementation.
- CUDA requests fail with clear not-implemented errors.
- Existing CPU tests and CLI render tests pass.
- Full CTest passes on macOS.

## Implementation Status

Backend Layer v1 is implemented as a two-stage backend lifecycle. `RenderBackend::Prepare()` produces backend-owned prepared-scene data, and `RenderBackend::Render()` consumes a `PreparedScene` instead of raw `RenderSceneIR`.

CPU rendering keeps the existing debug and path integrator behavior through `CpuPreparedScene`, including CPU BVH preparation outside `RenderSceneIR`. CUDA is represented by named `CudaBackend` and `CudaPreparedScene` classes that compile as ordinary C++ on macOS and return controlled not-implemented errors without requiring CUDA headers, kernels, or CMake CUDA language support.
