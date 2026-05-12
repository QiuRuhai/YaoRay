# YaoRay CPU Debug Renderer Design

Date: 2026-05-12

## Purpose

This slice turns the current parse-plus-compile pipeline into the first complete image-output loop:

```text
scene.toml
   -> SceneDescription
   -> RenderScene
   -> CpuDebugRenderer
   -> Film
   -> PPM image
```

The CPU Debug Renderer is a correctness and smoke-test renderer. It is not the final CPU path tracer and should not be treated as the final image-quality or performance target. Its job is to make scene compilation, camera ray generation, triangle intersection, Film accumulation, tone mapping, file output, and CLI behavior observable with a real image file.

## Goals

- Add a small CPU debug renderer that consumes `RenderScene`.
- Generate one primary camera ray per pixel through the pixel center.
- Intersect rays against `RenderScene::triangles` using a direct triangle loop.
- Shade hits with a deterministic debug material color.
- Shade misses with the constant environment color when present.
- Accumulate results into `Film`.
- Add a PPM writer that writes tone-mapped display colors to `.ppm`.
- Update `yaoray render` so `--backend cpu` writes an image file.
- Update `yaoray render --backend cuda` to fail clearly until a CUDA backend exists.
- Report simple render statistics such as rays, triangle tests, hits, misses, and elapsed time.
- Keep the slice dependency-light and avoid image output dependencies.

## Non-Goals

- No PNG, EXR, or HDR image writing.
- No path tracing, shadow rays, direct-light sampling, global illumination, MIS, or Russian roulette.
- No BVH or acceleration structure.
- No multithreading or tile scheduler.
- No random sampling or spp loop beyond a single deterministic sample per pixel.
- No real material system, BSDF, texture sampling, or PBR shading.
- No CUDA backend implementation.
- No asset importer work.
- No final-quality benchmark claims.

## Approved Decisions

- First image format: ASCII PPM (`P3`).
- First renderer type: CPU Debug Renderer.
- First supported backend in `yaoray render`: `cpu`.
- `cuda` remains accepted by the scene schema, but rendering with it returns a clear not-implemented diagnostic in this slice.

PPM is intentionally temporary. It exists to prove the pipeline and make test output easy to inspect. Later slices should add PNG output through a small writer dependency and preserve PPM only as a debug-friendly format if useful.

## Architecture

The new code should live in focused modules:

- `backends/cpu`: CPU debug rendering and triangle intersection.
- `film`: PPM file writing using existing `Film` and tone mapping APIs.
- `app`: CLI orchestration only.

The `render` module should continue to own `RenderScene` and scene compilation. The CPU debug renderer consumes `RenderScene`; it should not reach back into `SceneDescription` or TOML parser types.

Recommended dependency direction:

```text
scene -> core
render -> scene + core
film -> core
backends/cpu -> render + film + core
app -> scene + render + film + backends/cpu
```

No module should depend on `app`.

## CPU Debug Renderer

The public renderer API should be small and testable:

```cpp
struct CpuDebugRenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct CpuDebugRenderResult {
    Film film;
    CpuDebugRenderStats stats;
};

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene);
```

`RenderCpuDebug()` creates a `Film` using `scene.width` and `scene.height`, renders one sample per pixel, and returns the filled film plus stats.

### Camera Rays

For each pixel center:

```text
aspect = width / height
screen_x = (2 * (x + 0.5) / width - 1) * aspect * tan(fov_y / 2)
screen_y = (1 - 2 * (y + 0.5) / height) * tan(fov_y / 2)
direction = normalize(camera.forward + screen_x * camera.right + screen_y * camera.up)
```

The origin is `scene.camera.origin`.

This is intentionally deterministic. `scene.spp` is accepted as render settings metadata but does not trigger stochastic sampling in this slice.

### Triangle Intersection

Triangle hits should use a straightforward Moller-Trumbore test:

- Ignore intersections with `t <= 1e-5`.
- Treat triangles as two-sided for debug visibility.
- Choose the nearest hit.
- Count every ray/triangle pair in `triangle_tests`.

This direct loop is expected to be slow on large scenes. That is useful for now because it gives a simple reference before BVH work.

### Debug Shading

Hit color should be deterministic and easy to see:

```text
normal_lighting = max(0.15, abs(dot(triangle.normal, -ray.direction)))
color = material.albedo * normal_lighting + material.emission
```

If `material_index` is out of range, use a visible fallback color such as magenta. That indicates a compiler or importer bug without crashing the debug renderer.

Miss color:

- `EnvironmentKind::Constant`: `environment.radiance * environment.strength`
- `EnvironmentKind::None`: black

HDRI environments are still rejected by `SceneCompiler`, so the renderer does not need HDRI sampling.

## PPM Writer

Add a simple PPM writer to the `film` module:

```cpp
struct ImageWriteResult {
    bool ok = false;
    std::string error;
};

ImageWriteResult WritePpm(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);
```

The writer should:

- Require a `.ppm` extension.
- Create the parent output directory when it exists in the path.
- Write ASCII `P3`.
- Use `ToDisplayColor()` to convert each linear pixel to display color.
- Clamp output channels to `[0, 255]`.
- Return structured failure text for invalid extension, directory creation failure, or file open failure.

ASCII PPM is chosen over binary PPM because tests can read and assert the output with ordinary text checks.

## CLI Behavior

`yaoray render <scene.toml> [--backend cpu|cuda]` should:

1. Parse and validate the scene.
2. Apply the backend override.
3. Compile the scene into `RenderScene`.
4. If backend is `cpu`, render with `RenderCpuDebug()`.
5. Write `scene.film.output` as PPM.
6. Print output path and debug stats.
7. Exit zero on success.

Successful CPU output should include:

```text
Scene parsed successfully: scenes/examples/minimal.toml
Scene compiled successfully.
Requested backend: cpu
Compiled triangles: 1
Rendered image: out/minimal.ppm
Rays traced: 230400
Triangle tests: 230400
Hits: <non-zero>
Misses: <non-zero>
```

When `--backend cuda` is requested, the command should fail non-zero with:

```text
CUDA backend not implemented yet
```

This is more honest than pretending CUDA rendering succeeded.

## Example Scene

Update `scenes/examples/minimal.toml` to write:

```toml
[film]
output = "out/minimal.ppm"
```

The existing `builtin:triangle` scene should remain the primary example for this slice.

## Tests

Required tests:

- PPM writer rejects non-`.ppm` output paths.
- PPM writer writes a valid `P3` header.
- PPM writer writes tone-mapped 0-255 RGB values.
- CPU debug renderer traces one ray per pixel.
- CPU debug renderer records triangle-test counts.
- CPU debug renderer produces hit pixels for the built-in triangle scene.
- CPU debug renderer produces environment-colored miss pixels.
- CPU debug renderer uses fallback color for invalid material indices.
- CLI `yaoray render scenes/examples/minimal.toml --backend cpu` writes `out/minimal.ppm`.
- CLI CPU success output includes output path and render stats.
- CLI `--backend cuda` fails with `CUDA backend not implemented yet`.

The tests should keep generated output under a test-local or build-local output path where possible. Human-facing `scenes/examples/minimal.toml` may continue to write under `out/`.

## Completion Criteria

- `yaoray render scenes/examples/minimal.toml --backend cpu` writes `out/minimal.ppm`.
- The generated PPM has a valid `P3` header and non-empty image data.
- The image contains visible triangle-hit pixels and background pixels.
- `yaoray render scenes/examples/minimal.toml --backend cuda` fails with a clear CUDA-not-implemented message.
- Debug CTest passes from the main build directory.
- README and architecture docs state that YaoRay can render a CPU debug PPM image, while final rendering quality is future work.

## Future Work

Next slices after this one should focus on:

1. PNG output through a small dependency such as `stb_image_write`.
2. BVH construction over `RenderScene::triangles`.
3. Asset importer interfaces plus first glTF/OBJ loading.
4. A real CPU path tracer that consumes `RenderScene`.
5. CUDA backend subset and CPU/CUDA comparison tests.
