# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings.

The render layer compiles that semantic scene into backend-friendly data: flat arrays of triangles, BVH nodes, materials, textures, lights, camera data, and environment data. CPU, CUDA, and future OptiX backends consume this compiled representation.

The foundation slice in this branch establishes the project structure, tests, core math primitives, Film accumulation, and CLI shell. Rendering-specific modules will be added in focused implementation plans.
