# YaoRay BVH Acceleration Design

Date: 2026-05-13

## Purpose

YaoRay can now parse TOML scenes, import geometry-only OBJ meshes, compile them into `RenderScene::triangles`, and render CPU debug PPM images. The CPU debug renderer still intersects every ray against every triangle. That is correct for tiny scenes, but it scales poorly once OBJ import produces real triangle counts.

This slice adds the first acceleration structure: a bounding volume hierarchy over compiled render triangles. The goal is to make BVH part of the renderer-facing scene data, use it by default in the CPU debug renderer, and expose enough statistics to compare future split methods and backends.

## Goals

- Add a `RenderBvh` data structure owned by the `yaoray_render` module.
- Store the BVH inside `RenderScene` next to `triangles`.
- Build the BVH during `CompileScene()` after all world-space triangles have been appended.
- Use BVH traversal by default in the CPU debug renderer.
- Preserve `RenderScene::triangles` as the source triangle array for debugging, tests, shading, and future backend upload.
- Use a simple `LongestAxisMedian` split method in the first implementation.
- Keep the builder interface ready for future split methods such as midpoint split, binned SAH, full SAH, and GPU-oriented LBVH.
- Add BVH statistics to render results and CLI output.
- Treat BVH build invariant failures as scene compilation errors.
- Preserve valid empty scenes: zero triangles produce an empty, valid BVH and render as misses.
- Add builder, traversal, renderer, and CLI tests.

## Non-Goals

- No SAH implementation in this slice.
- No user-facing TOML option for split method selection yet.
- No linear traversal runtime fallback.
- No GPU BVH upload or CUDA traversal.
- No persistent acceleration cache.
- No multithreaded BVH build.
- No refit support for animated geometry.
- No material, lighting, sampling, integrator, or path tracing changes.
- No large showcase scene or benchmark scene in this slice.

## Approved Decisions

BVH is the next implementation slice after OBJ import. A separate medium OBJ smoke scene is not needed first; OBJ import already produces enough triangles to validate the acceleration path, and a larger showcase can wait until BVH exists.

The CPU debug renderer must use BVH by default. Linear traversal must not be exposed as a user option. Tests may keep reference linear traversal helpers so BVH hits and misses can be compared against a simpler algorithm.

BVH must live in `RenderScene`, not inside the CPU backend. `RenderScene` is the compiled renderer-facing data package produced by `CompileScene()`: camera, environment, materials, flat world-space triangles, and now acceleration data. Keeping BVH in `RenderScene` makes it a shared backend input instead of a CPU-only optimization.

BVH build failures must produce diagnostics instead of silently falling back to linear traversal. Since BVH is the default render path, silent fallback would hide structural bugs such as invalid primitive ranges, bad split output, missing bounds, or inconsistent node data.

## Architecture

Current high-level data flow:

```text
TOML scene file
  -> SceneDescription
  -> CompileScene()
  -> RenderScene
  -> RenderBackend
```

This slice keeps that flow and expands `RenderScene`:

```cpp
struct RenderScene {
    RenderBackendKind backend;
    int width;
    int height;
    int spp;
    int max_depth;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
    RenderBvh bvh;
};
```

Recommended module ownership:

```text
core   -> math, rays, bounds
scene  -> semantic scene description and TOML parsing
assets -> OBJ loading into imported geometry
render -> RenderScene, scene compilation, BVH data, BVH build and traversal helpers
film   -> film accumulation and output color data
backends -> CPU/CUDA backend dispatch and execution
app    -> CLI orchestration, diagnostics, image writing
```

The BVH builder belongs to `yaoray_render` because it consumes `RenderTriangle` and produces renderer-facing data. It must not depend on `film`, `backends`, `assets`, or `app`.

## Public Render Data

Add a BVH header under the render module:

```text
include/yaoray/render/bvh.hpp
src/render/bvh.cpp
```

Use these public types unless implementation details require only minor naming adjustments:

```cpp
enum class BvhSplitMethod {
    LongestAxisMedian,
};

struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::LongestAxisMedian;
    int max_leaf_triangles = 4;
};

struct RenderBvhNode {
    Bounds3f bounds;
    int left_child = -1;
    int right_child = -1;
    int first_triangle = 0;
    int triangle_count = 0;
};

struct RenderBvh {
    std::vector<RenderBvhNode> nodes;
    std::vector<int> triangle_indices;
    int max_depth = 0;
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(
    const std::vector<RenderTriangle>& triangles,
    const BvhBuildOptions& options = {}
);
```

`RenderBvhNode` must represent either an interior node or a leaf:

- Interior node: `left_child >= 0`, `right_child >= 0`, `triangle_count == 0`.
- Leaf node: `left_child == -1`, `right_child == -1`, `triangle_count > 0`.
- Empty BVH: `nodes.empty()`, `triangle_indices.empty()`, `max_depth == 0`.

The BVH does not copy triangles. Leaf nodes reference `RenderScene::triangles` through `triangle_indices`.

## Build Algorithm

The first implementation uses `LongestAxisMedian`.

Builder inputs:

- `std::vector<RenderTriangle>`
- `BvhBuildOptions`

Builder steps:

1. If `triangles` is empty, return an empty valid BVH.
2. Build a temporary primitive array with:
   - triangle index
   - triangle bounds
   - centroid
3. Recursively build nodes over primitive ranges.
4. Node bounds are the union of primitive bounds in the range.
5. If range size is `<= max_leaf_triangles`, emit a leaf and append primitive triangle indices to `RenderBvh::triangle_indices`.
6. Otherwise compute centroid bounds, choose the longest centroid axis, and partition by median along that axis.
7. Emit an interior node with child indices.
8. Track `max_depth`.

Degenerate centroid ranges must still produce valid output. If all centroids are equal on the chosen axis, median partition still creates non-empty left and right ranges as long as the range has more than one primitive.

`max_leaf_triangles` less than 1 is invalid. The first implementation must return a build error for invalid options rather than clamping or guessing.

## Build Error Handling

Legal cases:

- Zero triangles: empty valid BVH, no errors.
- One to `max_leaf_triangles` triangles: one leaf node.
- Degenerate but finite triangle bounds: valid BVH if bounds can be computed.

Build errors are reserved for internal invariant failures or invalid build options:

- `max_leaf_triangles < 1`
- non-finite bounds or centroids
- partition produces an empty child range
- node child indices are invalid
- leaf range would reference triangles outside `triangles`

`CompileScene()` must convert `BvhBuildResult::errors` into `SceneDiagnostic` errors using field `render.bvh`. If BVH build errors exist, scene compilation fails and the CLI exits non-zero.

There must be no runtime fallback from failed BVH build to linear triangle traversal.

## Scene Compiler Integration

`CompileScene()` currently builds a `RenderScene` by appending materials, triangles, camera data, environment data, and lights. At the end of successful geometry compilation, it must call:

```cpp
BvhBuildResult bvh_result = BuildBvh(compiled.triangles);
```

Then:

- append BVH build errors to diagnostics
- fail compilation if errors exist
- otherwise assign `compiled.bvh = std::move(bvh_result.bvh)`

`builtin:triangle` and OBJ asset behavior must not change except that compiled scenes now also contain BVH data.

## Traversal

The CPU debug renderer must not call a scene-wide linear `FindNearestHit()` over all triangles. Instead it must traverse `scene.bvh`.

Use a traversal helper with this shape:

```cpp
struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
};

struct TriangleHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    const RenderTriangle* triangle = nullptr;
};

TriangleHit IntersectBvh(
    const RenderScene& scene,
    const Ray3f& ray,
    BvhTraceStats& stats
);
```

Implementation notes:

- Empty BVH returns miss without node or triangle tests.
- Each visited node increments `node_tests`.
- Bounds tests must use the current nearest hit distance as `t_max` so farther nodes can be skipped after a closer hit.
- Leaf nodes iterate `triangle_indices[first_triangle ... first_triangle + triangle_count)`.
- Each triangle intersection increments `triangle_tests`.
- Traversal must use an explicit stack instead of recursion to stay backend-friendly.
- Child visit order is simple and unsorted in the first slice. Near-first sorting is future work.

The triangle intersection function remains the existing Moller-Trumbore implementation used by the CPU debug renderer.

## Render Statistics And CLI Output

Extend CPU/backend statistics with:

```cpp
std::uint64_t bvh_node_tests = 0;
int bvh_nodes = 0;
int bvh_max_depth = 0;
```

Existing `triangle_tests` remains, but its meaning changes from:

```text
rays * scene.triangles.size()
```

to:

```text
actual triangle intersection tests performed after BVH culling
```

The CLI must print:

```text
Compiled triangles: N
BVH nodes: N
BVH max depth: N
Rendered image: ...
Rays traced: ...
BVH node tests: ...
Triangle tests: ...
Hits: ...
Misses: ...
Elapsed seconds: ...
```

For zero-triangle scenes, `BVH nodes` is `0` and `BVH max depth` is `0`.

## Testing Strategy

Builder unit tests:

- Empty triangle vector returns an empty valid BVH.
- One triangle produces one leaf node.
- Four triangles with default options produce one leaf node.
- Five separated triangles produce an interior root and at least two leaves.
- `max_leaf_triangles = 1` produces one primitive per leaf.
- `max_leaf_triangles < 1` returns an error.
- `max_depth` is tracked.
- Leaf triangle indices reference valid triangle positions.

Traversal tests:

- BVH miss matches a linear reference miss.
- BVH nearest hit matches a linear reference hit for two triangles at different depths.
- BVH traversal tests fewer triangles than linear traversal for a ray that intersects a sparse scene.
- Empty BVH traversal returns miss and zero triangle tests.

Scene compiler tests:

- Compiling `builtin:triangle` creates a non-empty BVH with one triangle reference.
- Compiling an OBJ quad creates a BVH over two triangles.
- A compiler-level empty-triangle scene fixture remains legal and produces an empty BVH.

CPU renderer tests:

- Existing hit, miss, material fallback, and film size behavior remain unchanged.
- `rays_traced` remains one ray per pixel for the debug renderer.
- `bvh_node_tests` is greater than zero for non-empty geometry.
- `triangle_tests` is less than `rays * triangles.size()` for a small sparse multi-triangle scene.

CLI tests:

- Existing CPU render tests continue to pass.
- OBJ CLI render test still reports the expected compiled triangle count.
- CPU render CLI output includes `BVH nodes`, `BVH max depth`, and `BVH node tests`.
- CUDA not-implemented behavior remains unchanged.
- Unsupported `.glb` asset behavior remains unchanged.

The tests must not assert exact elapsed time or broad speedup ratios. They must assert structural correctness, traversal equivalence, and useful statistics.

## Documentation

Update `README.md` and `docs/architecture/overview.md` to state:

- YaoRay now builds a BVH over compiled render triangles.
- The CPU debug renderer uses BVH by default.
- CLI render output includes BVH statistics.
- The first split method is a simple longest-axis median builder.
- More advanced split methods and GPU acceleration structures are future work.

## Completion Criteria

- `RenderScene` owns `RenderBvh`.
- `BuildBvh()` is implemented and tested.
- `CompileScene()` builds BVH after triangle compilation.
- BVH build errors become scene diagnostics and fail compilation.
- CPU debug rendering uses BVH traversal by default.
- Linear traversal is not exposed as a runtime option.
- Render stats and CLI output include BVH node count, max depth, and node test count.
- Existing built-in triangle and OBJ scenes still render to valid PPM files.
- CUDA stub and unsupported asset diagnostics still behave as before.
- `ctest --test-dir build --output-on-failure -C Debug` passes.

## Future Work

Likely follow-up slices:

1. Add `BvhSplitMethod::LongestAxisMidpoint` for comparison.
2. Add binned SAH and CLI or TOML-controlled debug selection.
3. Add near-first child ordering during traversal.
4. Add compact GPU-friendly BVH upload data.
5. Add CUDA traversal over the same conceptual BVH.
6. Add a medium or large OBJ showcase scene for quality and performance comparison.
7. Add build-time and render-time benchmark reporting.
