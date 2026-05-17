# YaoRay Random Area Light Direct Lighting Design

## Context

YaoRay currently has a CPU path integrator with diffuse bounces, emissive hits,
explicit direct area-light contribution, BVH shadow rays, and deterministic tile
threading. The current direct-light estimate samples each area light at its
center only. This is useful for early validation, but it makes shadows too hard
and keeps the path tracer looking like a debug renderer.

This slice starts the image-quality phase by replacing center-sampled direct
lighting in `integrator = "path"` with random area-light surface sampling and a
physically weighted diffuse direct-light estimator.

## Goals

- Improve Cornell Box image quality with soft area-light shadows.
- Keep `debug_direct` unchanged as a stable geometry and pipeline reference.
- Keep path rendering deterministic for a fixed scene, seed, and thread count.
- Preserve thread-count independence: `threads = 1`, `2`, and `4` should still
  produce bitwise-identical path output for deterministic test scenes.
- Make direct lighting use explicit diffuse BRDF and area-light PDF terms.
- Shape the function boundaries so MIS and configurable light samples can be
  added later without rewriting this estimator.

## Non-Goals

- No `debug_direct` behavior changes.
- No scene-authored `light_samples` setting yet.
- No MIS in this slice.
- No Russian roulette.
- No new material types.
- No texture, HDRI, or environment light sampling.
- No arbitrary oriented area lights.
- No CUDA backend work.

## Area Light Geometry

The current `RenderAreaLight` contains only:

```cpp
Point3f position;
float width;
float height;
Color3f radiance;
```

For this slice, the path integrator interprets each area light as a rectangular
emitter centered at `position`, parallel to the XZ plane, with normal
`Vec3f{0.0f, -1.0f, 0.0f}`. This matches the current Cornell Box ceiling light.

Sampling a light uses two random numbers:

```text
sample_x = (u0 - 0.5) * width
sample_z = (u1 - 0.5) * height
sample_point = position + Vec3f{sample_x, 0, sample_z}
pdf_area = 1 / (width * height)
```

If `width <= 0`, `height <= 0`, or the computed area is not positive, the light
contributes nothing.

Future arbitrary light orientation can extend `RenderAreaLight` with tangent
axes or a transform. This slice does not introduce that representation yet.

## Direct Lighting Estimator

At each valid path hit, the path integrator estimates direct light by sampling
one point on every area light.

For a sampled light point:

```text
to_light = light_point - hit_point
distance_squared = dot(to_light, to_light)
wi = normalize(to_light)
cos_surface = max(0, dot(surface_normal, wi))
cos_light = max(0, dot(light_normal, -wi))
diffuse_brdf = albedo / pi
contribution = Li * diffuse_brdf * cos_surface * cos_light / distance_squared / pdf_area
```

With uniform area sampling, `1 / pdf_area` is the light area, so the contribution
can be implemented as:

```text
Li * (albedo / pi) * cos_surface * cos_light * area / distance_squared
```

The estimator returns zero when:

- the light has non-positive area
- the sampled point is too close to the hit point
- `cos_surface <= 0`
- `cos_light <= 0`
- the shadow ray is occluded before reaching the sampled point

## Shadow Rays And Bias

The current shadow bias strategy remains in use. The shadow ray starts at:

```text
hit_point + surface_normal * shadow_bias
```

The sampled light point is used as the shadow target. A hit only counts as an
occluder if:

```text
shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias
```

This allows a scene to include both a `RenderAreaLight` and matching emissive
geometry for visible light panels without treating the sampled light surface as
a blocker at the target distance.

## Randomness And Determinism

The implementation continues to use the existing per-pixel/per-sample RNG seed:

```text
SeedFor(scene, x, y, sample)
```

Camera jitter, direct-light sampling, and diffuse bounce sampling consume values
from the same path RNG. This keeps output reproducible for the same scene and
seed. Because the RNG is local to each pixel sample and no shared random state is
introduced, tile threading must not change pixel values.

The changed direct-light estimator will alter existing path output. Tests should
assert deterministic relationships rather than exact legacy brightness values
except for tightly scoped cases that are analytically stable.

## Function Boundaries

The path tracer should be split into small helpers inside
`src/backends/cpu/cpu_path_tracer.cpp`:

```cpp
struct AreaLightSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float area;
    float pdf_area;
};

std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Rng& rng);
Color3f EvaluateDiffuseBrdf(Color3f albedo);
Color3f EstimateDirectLight(..., Rng& rng, CpuPathTraceStats& stats);
```

The exact names can vary, but the responsibilities should remain separate:

- sampling light geometry
- evaluating diffuse BRDF
- tracing visibility
- applying the Monte Carlo direct-light weight

This keeps the estimator ready for a later MIS slice, where light sampling and
BSDF sampling will need separate PDFs.

## Tests

Add or update focused CPU path tracer tests:

- A sampled area light still illuminates a simple diffuse scene.
- A back-facing or geometrically invalid light contributes no direct light.
- Moving the light sample over an area changes output for different seeds.
- The same scene and seed render deterministically.
- `threads = 1`, `2`, and `4` remain bitwise-identical for a deterministic
  threaded scene.
- An inserted blocker increases `occluded_shadow_rays` and dims the lit pixel.
- Existing debug renderer tests remain unchanged.

CLI smoke tests do not need exact pixel assertions, but the threaded Cornell Box
example should continue rendering successfully in Release and Debug builds.

## Documentation

Update README and architecture notes to state that the path integrator now uses
random area-light surface sampling and diffuse BRDF/PDF weighting. Keep the
limitations explicit: no MIS, no configurable `light_samples`, and no arbitrary
area-light orientation yet.

## Acceptance Criteria

- `debug_direct` implementation and expected behavior remain unchanged.
- `path` direct lighting samples area-light surfaces rather than only centers.
- Direct light uses `albedo / pi`, surface cosine, light cosine, inverse-square
  distance, and area-PDF weighting.
- Fixed seed renders are deterministic.
- Threaded path tests still prove bitwise equality across requested worker
  counts.
- Full CTest suite passes.
- The threaded Cornell Box example renders and shows softer area-light shadows
  at sufficiently high `spp`.
