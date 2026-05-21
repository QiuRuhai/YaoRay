# YaoRay Glass Shadows v1 Design

## Summary

Glass Shadows v1 makes direct lighting behave more plausibly around glass.
Today the CPU path tracer treats any shadow-ray hit as blocked, so glass can
cast black direct-light shadows even after the main path supports dielectric
transmission and Beer-Lambert absorption. This slice changes direct-light
visibility so shadow rays can pass through dielectric materials and return a
colored transmittance instead of a binary visible/occluded result.

The scope remains deliberately narrow: shadow rays still travel in straight
lines. They can be tinted by glass, but they do not refract toward or away from
lights and therefore do not create true caustics.

## Goals

- Let CPU path-tracer direct-light shadow rays pass through dielectric glass.
- Apply Beer-Lambert attenuation to thick dielectric shadow segments using the
  existing `absorption_color` and `absorption_distance` material fields.
- Give thin glass a simple surface transmittance approximation instead of
  thickness absorption.
- Preserve opaque occlusion for diffuse, emissive, mirror, metal, and plastic
  materials.
- Share the same visibility helper between area-light direct lighting and HDRI
  direct environment lighting.
- Add focused tests for opaque shadows, clear glass shadows, colored glass
  shadows, thin glass shadows, and mixed transparent/opaque occluders.
- Update the glass showcase only if the new behavior needs a clearer visual
  cue.

## Non-Goals

- No caustic transport, photon mapping, bidirectional path tracing, MLT, or path
  guiding.
- No refracted or bent shadow rays. Visibility is a straight segment query.
- No nested medium stack or overlapping medium correctness.
- No new scene-file fields.
- No glTF glass extension import.
- No CUDA implementation in this slice.
- No change to the main camera-path dielectric BSDF except for any small helper
  reuse needed to avoid duplicate absorption math.

## Current Behavior

`EstimateDirectLight()` traces one shadow ray from the shaded point toward each
sampled area-light point. If any BVH hit is found before the light, it counts
the sample as occluded and contributes no direct light.

`EstimateDirectEnvironmentLight()` traces one shadow ray from the shaded point
toward the sampled environment direction. If any BVH hit exists anywhere along
that ray, it also treats the sample as fully occluded.

This binary test is correct for opaque geometry, but too harsh for dielectric
glass. It also duplicates visibility logic in two direct-light paths.

## Proposed Architecture

Add a CPU path tracer local helper that traces direct-light visibility and
returns a color:

```cpp
struct ShadowVisibility {
    bool visible = true;
    Color3f transmittance{1.0f, 1.0f, 1.0f};
};

ShadowVisibility TraceShadowVisibility(
    const RenderScene& scene,
    Ray3f ray,
    float max_distance,
    CpuPathTraceStats& stats
);
```

`max_distance` is finite for area-light samples and infinite for environment
samples. The helper owns all BVH tracing and triangle-test stat accumulation
for shadow rays. Callers still increment `shadow_rays` once per direct-light
visibility query.

The helper loops through intersections along the shadow segment:

1. Find the nearest BVH hit.
2. Before processing a hit, attenuate the traveled segment if a thick
   dielectric medium is currently active.
3. If no hit exists before `max_distance`, attenuate the remaining finite
   segment if needed, then return visible with the accumulated
   transmittance.
4. Resolve the hit material.
5. If the material is not a dielectric, return blocked.
6. If the material is thin dielectric, multiply accumulated transmittance by
   its surface transmittance and continue the ray after a surface bias.
7. If the material is thick dielectric, toggle the active-medium state and
   continue the ray after a surface bias.
8. If accumulated transmittance becomes black, return blocked.
9. Stop after a fixed transparent-hit guard to prevent infinite loops in broken
   or self-intersecting geometry.

The initial fixed guard should be small and explicit, for example
`MaxTransparentShadowHits = 16`. Hitting the guard returns blocked, which is a
safe failure mode.

## Material Rules

Opaque material kinds remain binary occluders:

- `Diffuse`
- `Mirror`
- `Metal`
- `Plastic`

Dielectric materials transmit shadow visibility:

- `thin == true`: apply a surface approximation. The first version should use
  `material.albedo` as the transmittance multiplier and ignore
  `absorption_color` and `absorption_distance`. This keeps thin glass as a pane
  model and avoids fake path-length darkening.
- `thin == false`: apply Beer-Lambert attenuation across the distance traveled
  between the current shadow-ray origin and the dielectric hit exit segment.

For v1, the thick-glass rule uses the same single-active-medium approximation
as the camera path, but inside the shadow helper:

- A dielectric transmission boundary toggles an internal active-medium state.
- While active, each segment length is attenuated with
  `BeerLambertTransmittance(absorption_color, absorption_distance, distance)`.
- The first hit on a thick dielectric enters the material; the second
  transmission boundary exits it.
- Reflection is ignored for shadow visibility. Direct-light visibility uses a
  deterministic transparent approximation, not stochastic Fresnel sampling.

This is intentionally biased but stable. It avoids glass becoming black while
waiting for a future caustic-capable transport algorithm.

## Direct Light Integration

Area-light direct lighting changes from:

```cpp
if (shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias) {
    continue;
}
```

to:

```cpp
const ShadowVisibility visibility =
    TraceShadowVisibility(scene, shadow_ray, shadow_distance - shadow_bias, stats);
if (!visibility.visible) {
    continue;
}
light_radiance += Multiply(visibility.transmittance, contribution);
```

Environment direct lighting changes similarly, using an infinite max distance.
The sampled environment radiance is multiplied by `visibility.transmittance`
before applying the BSDF, cosine, MIS, and PDF terms.

`occluded_shadow_rays` should continue to mean visibility queries that ended
fully blocked. Transparent glass hits should not increment it unless the ray
eventually becomes blocked or exceeds the transparent-hit guard.

## Testing

Unit/path tests should be added in `tests/cpu_path_tracer_tests.cpp`:

- Opaque occluder still blocks direct area light and increments
  `occluded_shadow_rays`.
- Clear dielectric pane between diffuse surface and area light no longer makes
  the surface black.
- Thick absorbing dielectric slab between diffuse surface and area light tints
  direct light according to `absorption_color`.
- Thin glass pane transmits direct light without thickness absorption.
- Environment direct-light sampling also passes through clear dielectric glass.
- A transparent glass pane followed by an opaque blocker is still blocked.

Prefer small 1x1 or 3x3 deterministic scenes with `threads = 1`,
`light_samples` high enough for stable direct-light assertions, and no reliance
on visual screenshots for the core behavior.

CLI visual sanity can remain focused on `glass_showcase.toml`. If the new
transparent shadow behavior materially changes the showcase, update the scene
or visual sanity thresholds in the implementation plan, not in this spec.

## Risks And Mitigations

- **Bias:** Straight transparent shadow rays are not physically exact for
  refractive objects. Mitigation: document this as a direct-light visibility
  approximation and keep caustics as future work.
- **Self-intersection:** Transparent continuation can re-hit the same surface.
  Mitigation: advance the origin using the existing surface bias in the shadow
  direction and cap transparent hits.
- **Nested glass:** Single active medium can be wrong for nested or overlapping
  volumes. Mitigation: tests use simple panes/slabs and docs keep nested media
  out of scope.
- **Stats semantics:** Existing tests may expect occluded shadow counts.
  Mitigation: define `occluded_shadow_rays` as fully blocked queries, not
  transparent-hit counts, and update tests accordingly.

## Success Criteria

- Direct area-light and environment-light shadow rays pass through dielectric
  glass.
- Thick absorbing glass casts tinted direct-light shadows.
- Thin glass does not apply fake thickness darkening.
- Opaque occluders still block direct light.
- The shared helper removes duplicated binary shadow logic from the two direct
  light estimators.
- All unit, CLI, and visual sanity tests pass.

## Future Work

- Refracted shadow rays and caustic-specific transport.
- Nested medium stack and overlapping medium correctness.
- Fresnel-aware deterministic shadow opacity controls.
- glTF transmission and volume extension import.
- CUDA parity for transparent shadow visibility.
