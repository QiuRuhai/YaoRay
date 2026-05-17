# YaoRay BSDF v0 Abstraction Design

YaoRay currently supports two material kinds in the CPU path tracer:
`diffuse` and `mirror`. The implementation works, but the material scattering
logic is embedded directly in `cpu_path_tracer.cpp`. That is acceptable for two
cases, but it will become brittle when adding glass, rough metals, Fresnel,
MIS, or a CUDA backend.

This slice introduces a small, data-driven BSDF abstraction. It preserves the
current rendered behavior while moving material evaluation and scattering
decisions behind a focused renderer-level API.

## Goals

- Centralize BSDF behavior for existing `MaterialKind::Diffuse` and
  `MaterialKind::Mirror`.
- Keep the abstraction data-driven and switch-based, with no virtual classes,
  heap allocation, or backend-specific ownership.
- Make the API usable by CPU code now and structurally compatible with future
  CUDA code.
- Move Lambertian and perfect mirror formulas out of the path tracer.
- Keep the CPU path tracer responsible for path control, emission
  accumulation, shadow-ray visibility, environment misses, depth limits, and
  statistics.
- Preserve current scene syntax and rendered behavior.

## Non-Goals

- No new material kind.
- No glass, dielectric refraction, rough metal, GGX, Fresnel, plastic, or
  layered material.
- No texture, UV, normal map, or imported material support.
- No MIS.
- No Russian roulette.
- No CUDA backend implementation.
- No broad material module refactor.
- No scene schema changes.

## Terminology

`BxDF` is the general term for a concrete scattering component. A BRDF reflects
light on the same side of a surface, a BTDF transmits light through the surface,
and a BSDF is the umbrella interface that can cover both reflection and
transmission.

In this v0 slice:

- `MaterialKind::Diffuse` maps to a Lambertian BRDF.
- `MaterialKind::Mirror` maps to a perfect specular reflection BRDF.

Future material kinds can add more BxDF behavior behind the same switch-based
entry points.

## Module Boundary

Add the BSDF API to the `yaoray_render` module:

- `include/yaoray/render/bsdf.hpp`
- `src/render/bsdf.cpp`

This is intentionally not a CPU backend file. BSDF evaluation is renderer math
over compiled render data, not a CPU backend policy. To keep the render module
independent from CPU sampler code, the sampling API accepts a raw `Vec2f`
sample. The CPU path tracer remains responsible for asking `CpuSampler` for
that sample.

## Public API

Add these renderer-level types and functions:

```cpp
struct BsdfSample {
    Vec3f wi;
    Color3f weight;
    float pdf = 0.0f;
    bool valid = false;
    bool specular = false;
};

Color3f EvaluateBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f wi,
    Vec3f normal
);

float PdfBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f wi,
    Vec3f normal
);

BsdfSample SampleBsdf(
    const RenderMaterial& material,
    Vec3f wo,
    Vec3f normal,
    Vec2f sample
);

bool IsDeltaBsdf(const RenderMaterial& material);
```

Conventions:

- `normal` is the face-forward shading normal.
- `wo` points away from the surface toward the previous path vertex or camera.
- `wi` points away from the surface toward the next path vertex or light.
- `BsdfSample::weight` is the path throughput multiplier for the sampled
  direction, already including the BSDF, cosine term, and sampling pdf
  convention used by that material.
- `BsdfSample::pdf` is the sampling pdf for non-delta materials. For the v0
  perfect mirror branch, it can be `1.0f` as a delta-event bookkeeping value,
  while `EvaluateBsdf()` and `PdfBsdf()` return black and zero for mirror under
  finite-direction queries.
- `BsdfSample::valid = false` means the integrator should terminate the path.

## Diffuse Behavior

Diffuse remains Lambertian:

```text
f(wo, wi) = albedo / pi
pdf(wi) = cos(theta) / pi
sample weight = f * cos(theta) / pdf = albedo
```

`EvaluateBsdf()` returns black when either `wo` or `wi` is below the surface.
`PdfBsdf()` returns zero for below-surface directions.
`SampleBsdf()` uses cosine-weighted hemisphere sampling around `normal`.

This preserves the current path throughput behavior, where a diffuse bounce
multiplies throughput by `material.albedo`.

## Mirror Behavior

Mirror remains a perfect delta reflection:

```text
wi = reflect(-wo, normal)
sample weight = albedo
```

`SampleBsdf()` returns a valid specular sample with `specular = true`.
`IsDeltaBsdf()` returns true for mirror.
`EvaluateBsdf()` returns black for mirror because a delta distribution cannot be
queried as a normal finite BRDF value by direct area-light sampling.
`PdfBsdf()` returns zero for mirror for the same reason.

This preserves the current behavior where mirror materials do not receive
diffuse direct-light estimates and only contribute through recursive reflected
paths.

## CPU Path Tracer Integration

`cpu_path_tracer.cpp` should become thinner:

1. Intersect ray and fetch `RenderMaterial`.
2. Add `throughput * material.emission`.
3. If `!IsDeltaBsdf(material)`, estimate direct light using
   `EvaluateBsdf()` for the light direction.
4. If the next depth would exceed `max_depth`, terminate.
5. Ask `CpuSampler` for one generic 2D sample.
6. Call `SampleBsdf(material, wo, normal, sample)`.
7. If the sample is invalid or near-black, terminate.
8. Multiply throughput by `sample.weight`.
9. Spawn the next ray along `sample.wi`.

The path tracer should not directly switch on `MaterialKind` for scattering
after this slice. It may still inspect material emission, depth, and validity
because those are integrator responsibilities.

`EstimateDirectLight()` should take the material and outgoing direction, then
call `EvaluateBsdf()` instead of the current local diffuse-only helper. Delta
materials are skipped before direct-light estimation.

## CUDA Compatibility

The chosen shape is intentionally CUDA-friendly:

- Material dispatch is an enum switch, not virtual dispatch.
- Inputs and outputs are plain data structures.
- The BSDF API receives raw sample values instead of a CPU sampler object.
- The formulas are stateless and do not allocate memory.
- Future GPU code can mirror the same functions as `__host__ __device__`
  helpers or port the same switch structure into CUDA kernels.

This slice does not require annotating functions for CUDA yet. It only avoids
design choices that would make CUDA migration harder.

## Error Handling

The BSDF layer does not produce scene diagnostics. Invalid material names and
schema errors remain parser/compiler responsibilities.

For unknown enum values at runtime, BSDF functions should fail closed:

- `EvaluateBsdf()` returns black.
- `PdfBsdf()` returns zero.
- `SampleBsdf()` returns `valid = false`.
- `IsDeltaBsdf()` returns false.

This keeps debug behavior predictable without adding exceptions to the render
loop.

## Tests

Add focused renderer-level BSDF tests:

- Diffuse `EvaluateBsdf()` returns `albedo / pi` for valid upper-hemisphere
  directions.
- Diffuse `EvaluateBsdf()` returns black for below-surface directions.
- Diffuse `PdfBsdf()` returns `cos(theta) / pi`.
- Diffuse `SampleBsdf()` returns a valid non-specular upper-hemisphere sample
  with positive pdf and `weight == albedo`.
- Mirror `SampleBsdf()` returns the reflected direction, `weight == albedo`,
  `valid = true`, and `specular = true`.
- Mirror `EvaluateBsdf()` returns black and `PdfBsdf()` returns zero for finite
  direction queries.
- Unknown material enum values return invalid or zero results.

Keep existing CPU path tracer tests and update them only where they assert
implementation details that move into the BSDF layer. Existing diffuse,
mirror, direct-light, threaded determinism, and material showcase behavior
should remain unchanged.

## Documentation

Update architecture documentation to say that material scattering is now routed
through a small render-level BSDF API. Keep the limitations explicit: only
Lambertian diffuse and perfect mirror are implemented, while glass, roughness,
GGX, textures, MIS, and CUDA material evaluation remain future work.

README can mention the BSDF abstraction only briefly if useful; it should not
over-explain internal architecture to users.

## Acceptance Criteria

- `yaoray_render` exposes a BSDF API for diffuse and mirror materials.
- `cpu_path_tracer.cpp` no longer owns Lambertian and mirror scattering
  formulas directly.
- Direct lighting evaluates non-delta materials through `EvaluateBsdf()`.
- Mirror materials remain excluded from diffuse direct-light estimation.
- Existing scene files and material syntax continue to parse and render.
- Existing tests pass.
- New BSDF tests cover diffuse, mirror, and unknown enum behavior.
- Manual material showcase rendering still succeeds.
