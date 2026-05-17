# YaoRay MIS Architecture v1 Design

YaoRay now has a render-level BSDF API, explicit area-light sampling, rough
metal, and plastic. The next quality and architecture bottleneck is direct
lighting: the CPU path tracer samples area lights explicitly, then also follows
BSDF-sampled paths that can hit emissive surfaces. These two estimators need a
shared PDF and weighting boundary before the renderer grows more materials,
light types, environment sampling, or CUDA path tracing.

This design adds a focused MIS architecture slice. The goal is to establish
render-level sampling and weighting helpers that the CPU path tracer can use
now and that a future integrator refactor can keep.

## Goals

- Add a render-level MIS helper for power heuristic weighting.
- Add a render-level area-light sampling helper independent of CPU sampler
  state.
- Express area-light PDFs in solid angle at the shading point.
- Allow the CPU path tracer to weight explicit light samples against BSDF PDFs.
- Allow BSDF-sampled emissive hits to be weighted against area-light PDFs.
- Preserve the existing `render.light_samples` meaning: samples per area light
  per non-delta hit.
- Keep `cpu_path_tracer.cpp` as the owner of the path tracing loop in this
  slice.
- Leave a clear path toward a later formal Integrator API refactor.

## Non-Goals

- No full Integrator API refactor.
- No new scene schema.
- No new material kinds.
- No Russian roulette.
- No environment light sampling or environment MIS.
- No one-sample light selection distribution.
- No arbitrary oriented area lights.
- No mesh light extraction from emissive triangles.
- No CUDA backend implementation.
- No denoising, adaptive sampling, or sampler sequence changes.

## Current State

The CPU path tracer currently has these responsibilities in one file:

- camera ray generation,
- path loop,
- direct area-light sampling,
- shadow visibility,
- BSDF sampling,
- emissive hit accumulation,
- tile/thread statistics.

The direct-light estimator samples every `RenderAreaLight` and averages
`render.light_samples` samples for each light. For each sample it computes:

```text
L_direct = bsdf * light_radiance * cos_surface * cos_light / distance^2 / pdf_area
```

Because `pdf_area = 1 / area`, this is equivalent to:

```text
L_direct = bsdf * light_radiance * cos_surface / pdf_light_solid_angle
```

The path loop also samples the BSDF for the next bounce. If that ray reaches
an emissive surface, emission is accumulated on the next iteration. Without
MIS, explicit light sampling and BSDF-sampled light hits can contribute to the
same direct-light transport path without a shared weighting rule.

## Architecture

Add two small render-level helper modules:

```text
include/yaoray/render/mis.hpp
src/render/mis.cpp

include/yaoray/render/light_sampling.hpp
src/render/light_sampling.cpp
```

The CPU path tracer continues to own the path loop and backend stats. It calls
these helpers instead of keeping area-light sampling math private.

```text
cpu_path_tracer.cpp
  - obtains random samples from CpuSampler
  - calls SampleAreaLight(light, uv)
  - calls PdfAreaLightSampleSolidAngle(...)
  - calls PdfAreaLightsForPointSolidAngle(...)
  - calls PowerHeuristic(...)
  - calls BSDF API for f / pdf / samples
```

The new helpers live in `yaoray_render` because they are pure render math over
`RenderScene`, `RenderAreaLight`, vectors, and PDFs. They should not depend on
CPU backend classes, CPU stats, threads, or CLI code.

## MIS Helper

Add:

```cpp
float PowerHeuristic(int sample_count_a, float pdf_a, int sample_count_b, float pdf_b);
```

Behavior:

- If `sample_count_a <= 0` or `pdf_a <= 0`, return `0`.
- If `sample_count_b <= 0` or `pdf_b <= 0`, return `1` for a valid A
  estimator.
- Use the power heuristic:

```text
a = sample_count_a * pdf_a
b = sample_count_b * pdf_b
weight = (a * a) / (a * a + b * b)
```

- Guard against non-finite intermediate values by returning `0` when the A
  estimator is invalid and `1` when A is valid but B cannot compete.

The helper is intentionally generic. It does not know about lights, BSDFs, or
path tracing.

## Area-Light Sampling Helper

Move the area-light sample record out of `cpu_path_tracer.cpp` and make it a
render-level concept:

```cpp
struct AreaLightSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float area = 0.0f;
    float pdf_area = 0.0f;
};
```

Add:

```cpp
std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Vec2f uv);
```

Behavior:

- Return `std::nullopt` if `width <= 0` or `height <= 0`.
- Preserve current geometry:
  - rectangle centered at `light.position`,
  - width along X,
  - height along Z,
  - normal `{0, -1, 0}`.
- Clamp `uv.x` and `uv.y` to `[0, 1]` before mapping them to the light
  rectangle.
- Return `pdf_area = 1 / area`.

Add:

```cpp
float PdfAreaLightSampleSolidAngle(
    const RenderAreaLight& light,
    Point3f shading_point,
    Point3f light_point
);
```

Behavior:

- Return `0` for invalid light area.
- Return `0` when the point is on the wrong side of the light.
- Return `0` when `distance_squared <= 1.0e-12f`.
- Convert area PDF to solid-angle PDF:

```text
pdf_omega = distance^2 / (cos_light * area)
```

where:

```text
cos_light = max(0, dot(light_normal, -wi))
wi = normalize(light_point - shading_point)
```

Add:

```cpp
float PdfAreaLightsForPointSolidAngle(
    const RenderScene& scene,
    Point3f shading_point,
    Point3f light_point
);
```

Behavior:

- Return the sum of solid-angle PDFs for all `RenderAreaLight` records whose
  rectangle contains `light_point`.
- Use current area-light geometry only: XZ rectangle centered at
  `light.position` with normal `{0, -1, 0}`.
- Use `1.0e-3f` scene units as the positional tolerance for point-on-light
  tests.
- Return `0` when no current `RenderAreaLight` could have sampled that point.

This function is used when a BSDF-sampled ray hits an emissive surface. If the
hit point corresponds to an explicit area light, the competing light-sampling
PDF is available for MIS. If it does not, BSDF-sampled emission has no explicit
light-sampling competitor in this slice.

## CPU Path Tracer Behavior

### Light-Sampled Direct Lighting

For each non-delta surface hit, continue sampling every area light and averaging
`render.light_samples` samples per light.

For each valid unoccluded light sample:

1. Compute `wi`, `cos_surface`, and `pdf_light_solid_angle`.
2. Query `pdf_bsdf = PdfBsdf(material, wo, wi, normal)`.
3. Compute:

```text
mis_weight = PowerHeuristic(light_sample_count, pdf_light_solid_angle, 1, pdf_bsdf)
contribution = bsdf * light_radiance * cos_surface / pdf_light_solid_angle * mis_weight
```

4. Average by `light_sample_count` as today.

Delta materials still skip direct-light estimation. Polished mirror and
polished metal should contribute to lights only through recursive paths.

### BSDF-Sampled Emissive Hits

Track the previous bounce enough to weight emissive hits:

- whether the previous sampled BSDF event was delta,
- previous surface point,
- previous outgoing direction,
- previous BSDF PDF,
- previous direct-light sample count.

When the current ray hits an emissive surface:

- If this is the camera ray, add emission with weight `1`.
- If the previous event was delta, add emission with weight `1`.
- If the previous event was non-delta:
  - compute `pdf_light_solid_angle` using
    `PdfAreaLightsForPointSolidAngle(scene, previous_surface_point, hit_point)`,
  - compute:

```text
mis_weight = PowerHeuristic(1, previous_bsdf_pdf, previous_light_sample_count, pdf_light_solid_angle)
```

  - add `throughput * emission * mis_weight`.

This makes BSDF-sampled light hits compete with explicit light sampling for the
same direct-light transport path. Emissive surfaces that are not represented by
`RenderAreaLight` keep full BSDF-sampled emission because the competing
light-sampling PDF is `0`.

### Indirect Emission

This slice does not introduce path-depth classification beyond the previous
event state described above. A ray that reaches an emissive surface after a
non-delta BSDF sample is treated as a BSDF-sampled emitter hit and receives the
MIS weight. A ray that reaches an emissive surface after a delta event receives
full emission.

This is the standard practical boundary for the current renderer because
explicit light sampling happens at every non-delta hit.

## Render Stats And CLI

No new CLI fields are required for v1.

Existing stats should remain valid:

- `shadow_rays` still counts visibility rays for explicit light samples.
- `rays_traced` still counts path rays.
- `Samples/sec` and `Rays/sec` remain comparable.

Tests may inspect existing counters to ensure MIS does not remove light-sample
visibility checks, but the user-facing CLI output does not need new counters in
this slice.

## Testing Strategy

MIS helper tests:

- `PowerHeuristic(1, 1, 1, 1)` returns `0.5`.
- Larger PDF or sample count receives larger weight.
- Invalid A estimator returns `0`.
- Invalid B estimator returns `1` when A is valid.

Area-light helper tests:

- `SampleAreaLight()` preserves current XZ rectangle placement and normal.
- Invalid area returns `std::nullopt`.
- `PdfAreaLightSampleSolidAngle()` matches the analytic value for a point below
  the center of a light.
- Back-facing or degenerate cases return `0`.
- `PdfAreaLightsForPointSolidAngle()` returns a positive PDF for a point on the
  current light rectangle and `0` for a non-light point.

CPU path tracer tests:

- Direct-light sampling still traces shadow rays for non-delta materials.
- Delta materials still skip direct-light estimation.
- A BSDF-sampled emissive hit after a non-delta event is MIS-weighted rather
  than added at full strength when the hit point is on an explicit area light.
- A BSDF-sampled emissive hit after a delta event remains full strength.
- Existing deterministic tests remain deterministic for fixed seeds and thread
  counts.

Manual render checks:

- `scenes/examples/cornell_box_path.toml` still renders.
- `scenes/examples/material_v2_showcase.toml` still renders.
- The material v2 showcase should remain visually plausible; dramatic visual
  improvement is not an acceptance requirement for this architecture slice.

## Documentation

Update README and architecture overview to state:

- the CPU path tracer uses direct-light MIS for non-delta surfaces,
- light samples are weighted against BSDF PDFs,
- BSDF-sampled emissive hits are weighted against area-light PDFs,
- environment MIS and full integrator refactoring remain future work.

## Future Refactor Trigger

Do the full Integrator API refactor when at least two of these are true:

- MIS v1 is implemented and tests cover both light-sampled and BSDF-sampled
  emitter paths.
- Russian roulette is added.
- Environment light sampling or environment MIS is added.
- More than one explicit light shape or a real light-selection distribution is
  added.
- CUDA path tracing work starts to duplicate CPU path-loop structure.

At that point, split the current path tracer into formal units:

```text
PathIntegrator
DirectLightingEstimator
LightSampler
BSDF layer
Sampler
Backend tile/thread scheduler
```

The render-level `mis` and `light_sampling` helpers from this slice should
survive that refactor and become dependencies of `DirectLightingEstimator` or
`LightSampler`.

## Acceptance Criteria

- `PowerHeuristic()` is available from the render layer and covered by tests.
- Area-light sampling math is available from the render layer and covered by
  tests.
- `cpu_path_tracer.cpp` no longer owns area-light sampling geometry or
  solid-angle PDF math.
- Explicit light samples are MIS-weighted against `PdfBsdf()`.
- BSDF-sampled emissive hits are MIS-weighted against area-light PDFs when the
  hit point belongs to an explicit area light.
- Delta paths keep full emissive contribution.
- Existing scene schema remains unchanged.
- Existing deterministic CPU path tracing tests pass.
- Full Debug tests pass.
- `material_v2_showcase.toml` renders successfully after the change.
