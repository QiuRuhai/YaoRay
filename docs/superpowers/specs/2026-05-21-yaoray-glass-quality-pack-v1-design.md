# YaoRay Glass Quality Pack v1 Design

## Summary

Glass Quality Pack v1 improves the current dielectric renderer without turning
it into a full medium system. The slice adds Beer-Lambert absorption for thick
dielectric materials, a conservative render-level radiance clamp for firefly
control, and a revised glass showcase scene that makes clear, tinted, rough,
and thin glass visually distinct.

The design intentionally stays small: it adds one lightweight path medium state
to the CPU path tracer and keeps all material dispatch data-oriented. It does
not add nested medium stacks, caustic algorithms, denoising, spectral rendering,
or CUDA parity.

## Context

The renderer currently supports smooth, rough, and thin dielectric glass through
one render material kind. The BSDF can reflect and transmit rays, but the path
tracer treats transmission as surface throughput only. A thick glass object
therefore has no color variation with distance, so it reads like a transparent
shell instead of a volume.

Recent glass showcase debugging also showed that high-energy samples can create
obvious fireflies around dielectric paths. That is a sampling and variance
problem, not the same as absorption, but a small configurable clamp is useful
for showcase-quality previews while better sampling and denoising remain future
work.

## Goals

- Add authorable absorption controls to dielectric materials.
- Apply Beer-Lambert attenuation to thick dielectric paths according to travel
  distance inside the glass.
- Keep thin glass as a surface pane approximation with no thickness absorption.
- Add an optional radiance clamp that can reduce fireflies in showcase renders.
- Update the glass showcase to demonstrate clear glass, tinted glass, rough
  glass, and thin glass.
- Preserve the current data-oriented material dispatch so future CUDA work does
  not inherit virtual material objects.

## Non-Goals

- No nested medium stack or arbitrary overlapping volumes.
- No air/liquid/glass IOR stack tracking.
- No caustic-specific rendering such as photon mapping, bidirectional path
  tracing, or MLT.
- No denoiser, adaptive sampling, Sobol, CMJ, or blue-noise sampler work.
- No glTF transmission, volume, IOR, or specular extension import.
- No CUDA dielectric parity in this slice.
- No spectral absorption or wavelength-dependent rendering.

## User-Facing Scene Format

Dielectric materials gain two optional fields:

```toml
[[materials]]
name = "blue_glass"
type = "glass"
ior = 1.5
absorption_color = [0.55, 0.75, 1.0]
absorption_distance = 1.0
```

`absorption_color` is the RGB transmittance after light travels
`absorption_distance` scene units inside the material. `[1, 1, 1]` means no
absorption. Lower values absorb more of that channel.

`absorption_distance` must be a finite positive float. The default is `1.0`.
The default `absorption_color` is `[1.0, 1.0, 1.0]`, preserving current output
for scenes that do not opt in.

The authoring fields are meaningful for `dielectric`, `glass`, and
`rough_glass`. They are ignored for `thin_glass` because thin glass is a
zero-thickness pane model.

Render settings gain one optional field:

```toml
[render]
radiance_clamp = 20.0
```

`radiance_clamp <= 0` disables clamping. The default is disabled. When enabled,
the CPU path tracer clamps unusually large sample radiance before adding it to
the film. This is a preview stability control, not a physically exact feature.

## Data Model

`MaterialDescription` and `RenderMaterial` gain:

```cpp
Color3f absorption_color{1.0f, 1.0f, 1.0f};
float absorption_distance = 1.0f;
```

`RenderSettings` and `RenderScene` gain:

```cpp
float radiance_clamp = 0.0f;
```

The scene parser validates:

- `absorption_color` is a vec3 with finite components in `[0, 1]`.
- `absorption_distance` is finite and greater than zero.
- `render.radiance_clamp` is finite and non-negative.

The scene compiler copies the new fields directly into render data.

Imported OBJ and glTF materials keep default absorption values for now. Scene
material overrides can still bind an imported mesh to an authored absorbing
glass material.

## Path Tracing Design

The CPU path tracer gains a lightweight medium state:

```cpp
struct PathMediumState {
    bool active = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

At the start of each path segment, if `PathMediumState::active` is true and the
ray hits geometry at distance `hit.t`, the tracer attenuates throughput before
surface shading:

```cpp
throughput *= Transmittance(absorption_color, absorption_distance, hit.t);
```

The helper converts authoring transmittance into Beer-Lambert coefficients:

```cpp
sigma_a = -log(clamp(absorption_color, epsilon, 1.0)) / absorption_distance;
transmittance = exp(-sigma_a * distance);
```

For a miss while inside a medium, the current v1 behavior does not apply
infinite-distance attenuation. It simply evaluates the environment with the
current throughput and exits the path. This is a limitation of the single-state
model and avoids inventing unbounded medium distances.

After sampling a BSDF event, the tracer updates medium state only for thick
dielectric transmission:

- If the material is dielectric, not thin, and the sampled direction crosses to
  the opposite side of the shading normal, the event is transmission.
- If the ray was outside the medium, transmission activates the material's
  absorption state.
- If the ray was inside the medium, transmission clears the state.
- Reflection events keep the current state unchanged.
- Non-dielectric and thin-glass events keep or clear no medium state by
  themselves.

This does not support nested glass or overlapping media. If a path enters one
absorbing dielectric and then intersects another before exiting the first, v1
will behave as a single active medium model.

## Radiance Clamp Design

The clamp is applied to the final per-sample path radiance before accumulation
into the film:

```cpp
if (scene.radiance_clamp > 0) {
    sample_radiance = ClampMaxComponent(sample_radiance, scene.radiance_clamp);
}
```

Clamping by max component preserves color ratios for bright samples better than
clamping each channel independently. A sample with max component below the
threshold is unchanged.

The default remains disabled so existing physically based tests and example
renders are not silently altered.

## Showcase Update

`scenes/examples/glass_showcase.toml` will become a clearer visual comparison:

- Clear thick glass sphere.
- Tinted thick glass sphere with visible distance-based color.
- Rough tinted glass sphere or block.
- Thin glass pane that remains mostly color-neutral.
- High-contrast backdrop cards and stable soft lighting.
- Optional `render.radiance_clamp` enabled only for the showcase if needed.

The scene remains a small manual preview. It is not a final caustics benchmark.

## Testing

Parser tests:

- Default material settings include neutral absorption and disabled clamp.
- `absorption_color` and `absorption_distance` parse for dielectric materials.
- Invalid absorption color components and non-positive distances fail.
- `render.radiance_clamp` parses and rejects negative or non-finite values.

Compiler tests:

- Scene-authored absorption fields copy to `RenderMaterial`.
- Imported materials retain neutral absorption by default.
- `radiance_clamp` copies to `RenderScene`.

Render/path tests:

- A thick dielectric path attenuates transmitted environment/background radiance
  according to distance.
- Thin glass does not apply thickness absorption.
- Reflection-only dielectric paths do not accidentally toggle medium state.
- Radiance clamp limits a synthetic high-radiance sample and stays disabled by
  default.

CLI/showcase tests:

- Existing glass showcase CLI rendering still writes a PNG.
- Visual sanity continues to reject overexposed or contrast-free output.
- The showcase render remains deterministic for a fixed seed.

## Risks And Mitigations

- Medium state can be wrong for nested or overlapping dielectric objects.
  Mitigation: document v1 as a single active dielectric model and keep tests to
  single-object paths.
- Absorption can make scenes unexpectedly dark if users set very small
  `absorption_distance`. Mitigation: validate positive finite distance and
  explain that `absorption_color` is transmittance after that distance.
- Radiance clamp is biased. Mitigation: default it to disabled and document it
  as preview firefly control.
- Rough dielectric variance may still produce noise. Mitigation: clamp only
  limits extreme samples; better sampling and denoising remain future work.

## Success Criteria

- Existing scenes render the same by default unless they opt into absorption or
  clamp settings.
- Authored tinted glass visibly darkens with path length.
- Thin glass remains a pane approximation.
- All unit, parser, compiler, CLI, and visual sanity tests pass.
- README and architecture docs describe absorption, clamp behavior, and the
  remaining lack of nested media, caustic algorithms, denoising, and CUDA parity.

## Future Work

- Full medium stack with nested dielectric boundaries.
- Beer-Lambert absorption imported from glTF volume/transmission extensions.
- Better rough dielectric sampling, firefly reduction, and denoising.
- Caustic-specific algorithms.
- Spectral absorption.
- CUDA path tracer parity for dielectric absorption.
