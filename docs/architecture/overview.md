# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings. The current implementation parses this semantic layer from TOML scene files into `SceneDescription`.

The render layer compiles that semantic scene into backend-friendly data. The current `yaoray_render` slice provides a minimal `RenderScene` with render settings, camera data, environment data, area lights, materials, and flat world-space triangles. Rendering is dispatched through a common backend interface so CPU, CUDA, and future OptiX backends can consume this compiled representation without app-layer special cases.

Current implemented slices:

- project structure, tests, core math primitives, Film accumulation, and CLI shell
- TOML scene parsing, validation diagnostics, and `yaoray render` command shell
- initial `RenderScene` compilation with temporary `builtin:triangle` asset support
- CPU debug rendering to PPM for the first image-output loop
- common render backend interface with CPU debug and CUDA not-implemented backends

The CPU debug renderer is a simple reference path through camera rays, triangle intersection, Film accumulation, tone mapping, and PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.

Asset import, BVH construction, PNG output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
