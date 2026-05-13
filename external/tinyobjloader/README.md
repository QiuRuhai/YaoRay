# tinyobjloader

Vendored header for geometry-only Wavefront OBJ loading.

- Source repository: https://github.com/tinyobjloader/tinyobjloader
- Header URL: https://raw.githubusercontent.com/tinyobjloader/tinyobjloader/release/tiny_obj_loader.h
- Retrieved: 2026-05-13
- License: MIT, as stated in `tiny_obj_loader.h`

YaoRay uses tinyobjloader only in `src/assets/obj_loader.cpp` for OBJ position and face loading. Materials, textures, UVs, imported normals, and smoothing groups are intentionally ignored in the first OBJ importer slice.
