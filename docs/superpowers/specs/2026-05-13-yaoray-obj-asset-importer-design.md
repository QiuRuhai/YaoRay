# YaoRay OBJ Asset Importer Design

Date: 2026-05-13

## Purpose

YaoRay can now parse TOML scenes, compile a minimal `RenderScene`, dispatch rendering through a backend interface, and write CPU debug PPM images. The only renderable geometry source is still the temporary `builtin:triangle` asset. That keeps tests simple, but it prevents realistic geometry tests and makes BVH work hard to evaluate.

This slice adds the first external mesh asset path: Wavefront OBJ. The goal is to turn small `.obj` files into `RenderScene::triangles` so the existing CPU debug backend can render more than one built-in triangle.

## Goals

- Add a focused `yaoray_assets` module.
- Vendor tinyobjloader as a third-party OBJ parser.
- Add an OBJ loader that converts `.obj` files into a simple imported triangle mesh.
- Support OBJ positions and faces.
- Use tinyobjloader triangulation for polygon faces.
- Support multiple OBJ shapes by appending their triangles in file order.
- Ignore OBJ materials, texture coordinates, imported normals, smoothing groups, and vertex colors in this slice.
- Update `CompileScene()` so non-`builtin:` `.obj` assets load through the asset module.
- Preserve existing `builtin:triangle` behavior.
- Apply existing instance transforms to imported OBJ triangles.
- Assign a default material to each OBJ instance.
- Cache imported meshes within one `CompileScene()` call so repeated instances do not reload the same file.
- Keep existing CLI output useful by reporting `Compiled triangles: N`.
- Add loader, compiler, and CLI tests for OBJ assets.

## Non-Goals

- No BVH or acceleration structure.
- No path tracing changes.
- No CUDA asset upload.
- No glTF, GLB, FBX, STL, or other importer formats.
- No material import from `.mtl`.
- No texture import or sampling.
- No UV use.
- No imported normal or smoothing group use.
- No mesh instancing in `RenderScene`; triangles are still expanded into world space.
- No persistent asset cache across commands.
- No large showcase model in this slice.

## Approved Decisions

The next implementation slice should be OBJ first, not BVH first.

Reasoning:

- BVH is valuable once there are enough triangles to accelerate.
- The current scene has one built-in triangle, so BVH would mostly prove plumbing.
- OBJ import immediately gives real triangle counts and visual feedback.
- BVH can follow with useful performance tests against imported meshes.

The first OBJ importer should use tinyobjloader instead of a hand-written OBJ parser. tinyobjloader is a single-file C++ OBJ loader with STL-only requirements and built-in triangulation support, which fits YaoRay's current dependency style.

## Third-Party Dependency

Add tinyobjloader under:

```text
external/tinyobjloader/tiny_obj_loader.h
external/tinyobjloader/README.md
```

The README should record:

- source repository: `https://github.com/tinyobjloader/tinyobjloader`
- license: MIT
- copied header name and retrieval date or commit when available
- note that YaoRay uses the header only for OBJ geometry loading in this slice

CMake should expose it as an interface target:

```cmake
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE external/tinyobjloader)
```

The tinyobjloader implementation macro must be defined in exactly one YaoRay translation unit, most likely `src/assets/obj_loader.cpp`:

```cpp
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
```

## Architecture

Add a new asset module:

```text
include/yaoray/assets/obj_loader.hpp
src/assets/obj_loader.cpp
```

Add a CMake library:

```text
yaoray_assets
```

Recommended dependency direction:

```text
scene -> core
assets -> core + tinyobjloader
render -> scene + core + assets
film -> core
backends -> render + film + core
app -> scene + render + film + backends
```

The asset module should not depend on `scene`, `render`, `film`, `backends`, or `app`. It should expose simple loaded geometry and plain error/warning strings. `render` remains responsible for converting asset load failures into `SceneDiagnostic`.

## Asset Loader API

Add this public API:

```cpp
struct ImportedTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
};

struct ImportedMesh {
    std::vector<ImportedTriangle> triangles;
};

struct AssetLoadResult {
    std::optional<ImportedMesh> mesh;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

AssetLoadResult LoadObjMesh(const std::filesystem::path& path);
```

`LoadObjMesh()` should:

- require a `.obj` extension
- fail with an error when the file does not exist
- call tinyobjloader with triangulation enabled
- read vertex positions from `attrib.vertices`
- iterate all shapes and all triangulated faces
- build one `ImportedTriangle` per face
- compute a flat geometric normal from triangle vertices
- skip degenerate triangles and record a warning
- fail if no non-degenerate triangles are produced
- return tinyobjloader warnings as warnings
- return tinyobjloader errors as errors

The loader should not resolve scene-relative paths. The scene parser already normalizes ordinary asset paths relative to the scene file. `LoadObjMesh()` receives the path after that stage.

## OBJ Parsing Rules

Use tinyobjloader's object-oriented API:

```cpp
tinyobj::ObjReaderConfig config;
config.triangulate = true;
config.vertex_color = false;

tinyobj::ObjReader reader;
reader.ParseFromFile(path.string(), config);
```

If tinyobjloader produces warnings, preserve them. Warnings should not fail compilation unless the resulting mesh has no triangles.

If tinyobjloader fails to parse the file, return `mesh = std::nullopt` and include a useful error string. If tinyobjloader does not provide a specific error, return a fallback such as:

```text
failed to parse OBJ file: path/to/model.obj
```

Only position indices are used. Texture coordinate indices, normal indices, material ids, and smoothing groups are ignored.

## Scene Compiler Integration

The current compiler behavior is:

```text
asset path == builtin:triangle -> append built-in triangle
otherwise -> asset import not implemented yet
```

Change it to:

```text
asset path == builtin:triangle -> append built-in triangle
asset extension == .obj -> load OBJ mesh and append imported triangles
otherwise -> unsupported asset diagnostic
```

For each OBJ instance:

1. Load the mesh from the compile-local cache or from disk.
2. If loading failed, add one `SceneDiagnostic` error per loader error using field `assets.path`.
3. Add loader warnings as `SceneDiagnostic` warnings using field `assets.path`.
4. If loading succeeded, add one default `RenderMaterial` for the instance.
5. Apply the instance transform to every imported triangle vertex.
6. Recompute the world-space normal from transformed vertices.
7. Append each world-space triangle to `RenderScene::triangles` with the instance material index.

The compile-local cache should be keyed by the asset path generic string:

```text
std::unordered_map<std::string, ImportedMesh>
```

The cache exists only during one `CompileScene()` call. It prevents repeated disk reads when multiple instances use the same OBJ asset, but it does not introduce global cache lifetime yet.

## Transform Behavior

OBJ vertices should use the same transform order as the built-in triangle:

```text
scale -> rotate X -> rotate Y -> rotate Z -> translate
```

World-space normals for imported triangles should be computed after transform from:

```text
Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0))
```

This is sufficient for flat debug rendering and avoids importing or transforming OBJ normals in this slice.

## CLI Behavior

`yaoray render <scene.toml> --backend cpu` should continue to:

- parse the scene
- compile it into `RenderScene`
- print `Compiled triangles: N`
- render through the selected backend
- write the configured PPM output path

For an OBJ scene, `Compiled triangles: N` is the primary user-visible proof that importer expansion happened.

Failure examples:

Missing OBJ file:

```text
Scene error in scenes/examples/obj.toml:
  [assets.path] OBJ file not found: scenes/examples/assets/missing.obj
```

Unsupported asset extension:

```text
Scene error in scenes/examples/model.toml:
  [assets.path] asset import not implemented yet: scenes/examples/assets/model.glb
```

The `.glb` unsupported message should remain compatible with existing tests.

## Test Assets

Add small text OBJ fixtures under tests, for example:

```text
tests/fixtures/assets/triangle.obj
tests/fixtures/assets/quad.obj
```

`triangle.obj`:

```text
v -0.5 0.0 0.0
v 0.5 0.0 0.0
v 0.0 1.0 0.0
f 1 2 3
```

`quad.obj`:

```text
v -0.5 -0.5 0.0
v 0.5 -0.5 0.0
v 0.5 0.5 0.0
v -0.5 0.5 0.0
f 1 2 3 4
```

Add at least one scene fixture:

```text
tests/fixtures/scene/obj_quad.toml
```

It should reference `../assets/quad.obj` or another path that exercises scene-relative asset path normalization.

Optionally add a human-facing small example under:

```text
scenes/examples/assets/pyramid.obj
scenes/examples/obj_pyramid.toml
```

The example must remain small enough to render without BVH.

## Tests

Required tests:

- `LoadObjMesh()` loads a single triangle OBJ.
- `LoadObjMesh()` triangulates a quad OBJ into two triangles.
- `LoadObjMesh()` rejects non-`.obj` paths.
- `LoadObjMesh()` returns an error for a missing file.
- `LoadObjMesh()` returns an error when an OBJ has no triangles.
- `CompileScene()` compiles an OBJ asset into `RenderScene::triangles`.
- `CompileScene()` applies instance transforms to imported OBJ vertices.
- `CompileScene()` can expand two instances of the same OBJ into twice as many triangles.
- `CompileScene()` preserves existing `builtin:triangle` behavior.
- `CompileScene()` still rejects unsupported `.glb` assets with the existing not-implemented diagnostic.
- CLI rendering an OBJ fixture succeeds and reports the expected compiled triangle count.
- CLI rendering an OBJ fixture writes a valid PPM file.

The tests do not need to assert exact rasterized pixels. The importer slice should assert geometry count, transformed vertex positions, diagnostics, and PPM file existence/header.

## Documentation

Update `README.md` and `docs/architecture/overview.md` to state:

- YaoRay can import small OBJ mesh assets.
- OBJ support is geometry-only in this slice.
- Materials, textures, glTF, BVH, and final-quality rendering are future work.
- OBJ rendering is still through the CPU debug backend unless another backend is added later.

## Completion Criteria

- `yaoray_assets` builds and links into `yaoray_render`.
- tinyobjloader is vendored under `external/tinyobjloader/` with source and license notes.
- `LoadObjMesh()` loads triangle and quad OBJ fixtures.
- `CompileScene()` turns OBJ asset instances into world-space `RenderScene::triangles`.
- Existing `builtin:triangle` scenes still compile and render.
- Unsupported `.glb` assets still fail with a clear diagnostic.
- `yaoray render` can render a test OBJ scene to PPM.
- `ctest --test-dir build --output-on-failure -C Debug` passes.
- Docs describe geometry-only OBJ import and the lack of BVH.

## Future Work

Likely next slices after OBJ import:

1. BVH construction over imported `RenderScene::triangles`.
2. A small curated showcase scene once BVH exists.
3. Imported material and `.mtl` handling.
4. Imported normals and smoothing behavior.
5. Texture coordinates and texture loading.
6. glTF/GLB import once material and asset boundaries are more mature.
7. CUDA asset upload and GPU-side acceleration structures.
