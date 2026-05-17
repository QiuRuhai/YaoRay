# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings. The current implementation parses this semantic layer from TOML scene files into `SceneDescription`.

The render layer compiles that semantic scene into backend-friendly data. The current `yaoray_render` slice provides a minimal `RenderScene` with render settings, camera data, environment data, area lights, materials, flat world-space triangles, and a BVH over those triangles. Rendering is dispatched through a common backend interface so CPU, CUDA, and future OptiX backends can consume this compiled representation without app-layer special cases.

Current implemented slices:

- project structure, tests, core math primitives, Film accumulation, and CLI shell
- TOML scene parsing, validation diagnostics, and `yaoray render` command shell
- initial `RenderScene` compilation with temporary `builtin:triangle` asset support
- CPU rendering with deterministic area-light direct lighting and BVH shadow rays
- PNG output for renderable scenes, with PPM still available for debug/test output
- common render backend interface with CPU debug and CUDA not-implemented backends
- geometry-only OBJ asset import through the `yaoray_assets` module
- BVH acceleration over compiled render triangles
- TOML named diffuse/emissive materials with instance-level material binding
- scene-authored inline quad assets and a Cornell Box example based on Cornell measured geometry
- render integrator selection with a deterministic CPU path tracer v0
- deterministic CPU path tracer tile threading with actual thread and throughput reporting

The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output; it remains single-threaded for reference debugging. `path` is the CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, random XZ-rectangle area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, and row-major tile threading controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). It is still a v0 integrator without MIS, Russian roulette, spectral rendering, random environment sampling, configurable light sample counts, arbitrary oriented area lights, or final-quality material models.

The OBJ importer converts small Wavefront OBJ meshes into flat world-space triangles during scene compilation. It ignores OBJ `.mtl` files, textures, UVs, imported normals, and smoothing data in this slice; scene-authored named materials can still bind to a whole imported instance.

Inline quad assets let TOML scenes define small measured or hand-authored quad meshes directly. The Cornell Box example uses this path so the official measured vertices stay visible in the scene file. Its materials are current RGB diffuse/emissive approximations; spectral matching remains future work.

The path-traced Cornell example is separate from the debug Cornell scene. The debug scene remains a fast geometry and pipeline smoke test; the path scene is for manual visual review of indirect diffuse lighting.

Spectral rendering, texture import, imported material files, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, more complete CPU path tracing, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
