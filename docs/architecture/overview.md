# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings. The current implementation parses this semantic layer from TOML scene files into `SceneDescription`.

The render layer will compile that semantic scene into backend-friendly data: flat arrays of triangles, BVH nodes, materials, textures, lights, camera data, and environment data. CPU, CUDA, and future OptiX backends will consume this compiled representation.

Current implemented slices:

- project structure, tests, core math primitives, Film accumulation, and CLI shell
- TOML scene parsing, validation diagnostics, and `yaoray render` command shell

Rendering-specific modules will be added in focused implementation plans.
