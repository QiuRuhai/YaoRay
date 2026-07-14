# YaoRay Repository Guide

This file applies to the entire repository.

## Project Direction

YaoRay is a C++20 physically based offline renderer with a PBRT v4 frontend and a multi-threaded CPU path tracer.

The current priority is to keep the CPU renderer correct, understandable, testable, and easy to extend. CUDA/GPU work is deferred. Do not add GPU abstractions, CUDA implementations, device-specific data layouts, or GPU dependencies unless the user explicitly requests them. Existing CUDA stubs may remain, but repository cleanup should not expand them.

## Architecture Boundaries

- `core`: dependency-light math, rays, bounds, transforms, RNG, and version utilities.
- `io`: external asset data plus format-specific readers and decoders. It translates bytes without deciding scene fallback, rendering policy, or final image output.
- `frontend/pbrt`: PBRT parsing, source-format representation, and PBRT-to-render-scene compilation.
- `scene`: format-neutral render-scene contracts, handles, settings, hashes, and diagnostics.
- `shading`: textures, material evaluation, BSDF/BSSRDF models, measured material data, and shading distributions.
- `geometry`: primitive intersection and surface-hit construction.
- `accel`: acceleration-structure construction and traversal.
- `sampling`: sample-sequence generation, deterministic seeding, sample dimensions, and sampling strategies.
- `lighting`: light and environment representation, selection, evaluation, and importance sampling.
- `integrators`: light-transport algorithms, path state, direct-lighting policy, and MIS composition.
- `runtime`: render requests/results, backend lifecycle, capabilities, and frozen CUDA stubs.
- `backends/cpu`: CPU scene preparation, tile/thread scheduling, and execution of integrators.
- `film`: accumulation, checkpoints, tone mapping, and image output.
- `app`: CLI composition root. Keep command-line concerns out of lower layers.

Prefer dependencies flowing from the application and frontend toward stable rendering primitives. Do not make `core` depend on higher-level modules. Keep implementation-only helpers in the relevant `.cpp` anonymous namespace instead of adding them to public headers.

## Repository Hygiene

- Do not commit generated files from `/build/`, `/tmp/`, or `/output/`.
- Keep project documentation limited to root `README.md` and `AGENTS.md`.
- Keep showcase images under `media/showcase/`; do not recreate `docs/`, historical plans/specs, or per-scene README files unless explicitly requested.
- Large downloaded scenes and assets must remain outside version control according to `.gitignore`.
- Preserve user changes and avoid unrelated formatting or mechanical rewrites.

## Build and Test

Use an out-of-source Release build for normal validation:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

When changing a focused subsystem, run its relevant tests first, then run the complete CTest suite before handing off. Rendering and sampling tests must use deterministic seeds where possible.

Do not weaken numerical tolerances, delete failing tests, or change expected images merely to make the suite pass. If an existing failure is unrelated to the requested change, report it clearly.

## Code Guidelines

- Match the existing C++20 style and naming in the surrounding module.
- Prefer small value types and explicit ownership.
- Use indices or typed handles for stable identities; avoid introducing new long-lived raw-pointer relationships.
- Private helper structs are appropriate in `.cpp` anonymous namespaces when their fields belong to one operation or lifecycle.
- Do not create generic dumping grounds such as `utils.hpp`, `common.hpp`, or a global `types.hpp`.
- Split a file when it has multiple independent reasons to change, not merely because it has many lines.
- Keep hot rendering paths allocation-conscious, but measure before introducing complex optimizations.
- Add tests for observable behavior and numerical properties, not private implementation details.

## Documentation

When behavior, supported PBRT surface, architecture boundaries, build steps, or test counts change, update `README.md` in the same change. `AGENTS.md` owns contributor rules; `README.md` owns user-facing and architecture facts. Do not create temporary implementation plans as maintained documentation.
