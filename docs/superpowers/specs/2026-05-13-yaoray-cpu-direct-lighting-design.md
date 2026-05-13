# YaoRay CPU Direct Lighting Design

Date: 2026-05-13

## Purpose

YaoRay can now parse TOML scenes, import OBJ geometry, compile render data, build a BVH, render through the CPU backend, and write PNG output. The current CPU renderer still shades hits with a debug normal-facing term. That makes the pipeline testable, but the image is not yet driven by scene lights.

This slice makes the CPU renderer use the existing `RenderAreaLight` and `RenderMaterial` data for a first direct-lighting pass. The goal is not full path tracing; it is the first visibly meaningful renderer step: surfaces respond to lights, materials, emission, shadows, and environment misses.

## Goals

- Replace the CPU hit shading debug normal term with deterministic direct lighting.
- Use `RenderMaterial::albedo` as a Lambert diffuse color.
- Add `RenderMaterial::emission` directly on hit surfaces.
- Use every `RenderAreaLight` as a center-sampled direct light source.
- Cast BVH shadow rays from hit points toward area-light centers.
- Keep environment color for camera misses.
- Accumulate BVH node and triangle test statistics for primary and shadow rays.
- Add explicit shadow-ray statistics to backend stats and CLI output.
- Update example scenes so rendered PNGs include actual area lights.
- Keep the implementation deterministic and easy to test.

## Non-Goals

- No path tracing or secondary diffuse bounces.
- No random sampling.
- No soft shadows.
- No multiple importance sampling.
- No BSDF abstraction.
- No material TOML syntax.
- No texture import.
- No OBJ `.mtl` material import.
- No light orientation schema.
- No physically exact rectangular area-light integration.
- No Integrator abstraction in this slice.
- No CUDA rendering changes beyond backend stats shape staying compatible.

## Approved Decisions

Keep the work inside the current CPU debug renderer instead of introducing an `Integrator` abstraction now. There is still only one real CPU algorithm in the codebase. A direct-lighting helper split inside `src/backends/cpu/cpu_debug_renderer.cpp` is enough for this step, and a future path tracer can justify the real integrator boundary when there are two algorithms to share code between.

Use a deterministic center sample for each `RenderAreaLight`. This gives stable tests and visible shadows without adding random number generation, sample sequences, or convergence concerns.

Treat the current area light as an un-oriented emitter approximation. `RenderAreaLight` has position, width, height, and radiance, but no orientation. The first lighting term uses light center, light area, inverse-square falloff, and surface cosine. Light-side cosine is deferred until the scene schema has an orientation for area lights.

## Architecture

Current CPU render flow:

```text
camera ray
  -> IntersectBvh()
  -> debug ShadeHit() or environment
  -> Film
```

New CPU render flow:

```text
camera ray
  -> IntersectBvh()
  -> if miss: environment
  -> if hit:
       hit point
       face-forward normal
       material emission
       for each area light:
         center sample
         shadow ray through IntersectBvh()
         Lambert direct contribution if visible
  -> Film
```

This stays in `yaoray_backends`. It consumes the renderer-facing `RenderScene`, `RenderMaterial`, `RenderTriangle`, `RenderAreaLight`, and BVH traversal API. It does not add dependencies from `render` to `backends`.

## Direct Lighting Model

For a primary hit:

```cpp
Point3f p = ray.origin + ray.direction * hit.t;
Vec3f n = FaceForward(Normalize(triangle.normal), -ray.direction);
Color3f radiance = material.emission;
```

For each area light:

```cpp
Vec3f to_light = light.position - p;
float distance_squared = LengthSquared(to_light);
float distance = std::sqrt(distance_squared);
Vec3f wi = to_light / distance;
float n_dot_l = std::max(0.0f, Dot(n, wi));
float area = light.width * light.height;
```

If `distance > epsilon`, `n_dot_l > 0`, `area > 0`, and the shadow ray is not occluded:

```cpp
radiance += material.albedo * light.radiance * (area * n_dot_l / distance_squared);
```

This is intentionally simple. It is closer to direct lighting than the current debug normal term, but it is not a final physically exact area-light estimator.

## Shadows

Shadow rays use the same BVH traversal helper:

```cpp
Ray3f shadow_ray{p + n * ShadowEpsilon, wi};
BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_stats);
```

A light is occluded if:

```cpp
shadow_hit.hit && shadow_hit.t < distance - ShadowEpsilon
```

Shadow traversal stats are accumulated into the same backend totals:

- `bvh_node_tests`
- `triangle_tests`

Add explicit stats:

```cpp
std::uint64_t shadow_rays = 0;
std::uint64_t occluded_shadow_rays = 0;
```

The CLI prints:

```text
Shadow rays: N
Occluded shadow rays: N
```

`rays_traced` remains primary camera rays. This keeps the old meaning stable while making shadow rays visible through their own fields.

## Normal Handling

Use a face-forward shading normal for camera-visible surfaces:

```cpp
Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}
```

This keeps one-sided OBJ winding issues from making visible triangles completely dark in the first lighting slice. It does not introduce imported smoothing normals or normal maps.

## Error And Edge Behavior

- Invalid material index keeps the magenta fallback color and skips lighting.
- No area lights means non-emissive hits are black.
- Emissive materials render even without lights.
- Environment still appears only on camera misses.
- Area lights with non-positive width or height contribute nothing.
- Degenerate light distance contributes nothing.
- Shadow rays are deterministic and use the same BVH as primary rays.

## Example Scenes

Update renderable examples to include at least one area light:

```toml
[[lights]]
type = "area"
position = [0, 3, 3]
size = [2, 2]
radiance = [8, 8, 8]
```

The exact values can be tuned per scene, but examples must not rely on environment color to light hit surfaces.

## Testing Strategy

Renderer unit tests:

- Camera misses still return environment color.
- A hit with material emission returns emission even with no lights.
- A front-facing diffuse hit receives positive direct light from an unoccluded area light.
- A hit receives no direct light when the light is behind the surface.
- A blocking triangle between hit point and light causes an occluded shadow ray and lower radiance.
- Invalid material index still returns magenta fallback.
- `rays_traced` remains width times height.
- `shadow_rays` increases for lit hit points.
- `occluded_shadow_rays` increases in the blocker test.

CLI tests:

- Existing render CLI tests still pass.
- CLI output includes `Shadow rays:` and `Occluded shadow rays:`.
- PNG output remains valid.

Scene compiler tests:

- Existing light copy tests remain valid.
- Example and fixture scenes with area lights compile.

No tests should assert exact elapsed time or broad visual quality. Tests should assert deterministic pixel-level behavior for tiny scenes and statistics shape.

## Documentation

Update `README.md` and `docs/architecture/overview.md` to state:

- CPU rendering now includes deterministic direct lighting from area lights.
- The renderer still is not a full path tracer.
- Direct lighting is center-sampled and shadowed through BVH traversal.
- Material TOML, texture import, soft shadows, and path tracing remain future work.

## Completion Criteria

- CPU hit shading uses material emission plus direct light instead of the debug normal term.
- Shadow rays use BVH traversal and affect lighting.
- Primary and shadow traversal tests pass.
- Backend and CLI stats include shadow-ray counts.
- Example scenes include area lights and render to valid PNG files.
- Full Debug build and CTest pass.

## Future Work

Likely follow-up slices:

1. Add material TOML syntax and instance material binding.
2. Add a real CPU path tracer with multi-bounce sampling.
3. Introduce an Integrator abstraction once debug/direct/path algorithms need a shared boundary.
4. Add random or stratified area-light sampling for soft shadows.
5. Add light orientation to the scene schema.
6. Add imported OBJ/glTF materials and textures.
7. Add CUDA implementation of the same direct-lighting baseline.
