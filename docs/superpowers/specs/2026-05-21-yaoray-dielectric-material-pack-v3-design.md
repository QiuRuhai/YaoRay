# YaoRay Dielectric Material Pack v3 Design

## Context

YaoRay now has a CPU path tracer with BVH traversal, tile threading, Russian
roulette, area-light MIS, HDRI environment importance sampling, textured
diffuse albedo, and a small render-level BSDF API. Current material coverage is
good for opaque surfaces: diffuse, mirror, polished/rough metal, and simple
plastic. The renderer still cannot express transparent dielectric interfaces,
so classic offline-rendering scenes with glass objects, windows, and rough
frosted surfaces are missing an important visual class.

This slice adds a focused dielectric material pack. It should make glass
surfaces real BSDF participants in the existing path tracer without expanding
the project into a full medium system.

## Goals

- Add one core dielectric material kind that supports smooth glass, rough
  glass, and thin glass behavior.
- Use physically based surface-interface Fresnel reflection, Snell refraction,
  and total internal reflection for smooth dielectric materials.
- Implement rough dielectric reflection and transmission through a classic GGX
  microfacet dielectric model.
- Keep `EvaluateBsdf()`, `PdfBsdf()`, and `SampleBsdf()` consistent for rough
  dielectric materials so they can participate in existing direct-light and
  environment MIS paths.
- Add ergonomic TOML material aliases for `glass`, `rough_glass`, and
  `thin_glass` while compiling them to the same render material kind.
- Add focused parser, compiler, BSDF, path tracer, and CLI smoke tests.
- Add a glass showcase scene that uses mesh assets rather than adding analytic
  sphere primitives in this slice.

## Non-Goals

- No Beer-Lambert absorption or path-length-dependent colored glass.
- No medium stack, nested dielectrics, or participating media.
- No caustics-specific sampling such as photon mapping, bidirectional path
  tracing, or manifold next-event estimation.
- No visible-normal GGX sampling in this slice; classic NDF half-vector
  sampling is sufficient for v1.
- No CUDA material parity.
- No glTF glass or material extension import.
- No analytic sphere/cylinder primitive system.
- No broad integrator API refactor beyond the local BSDF and material fields
  needed for this material pack.

## User Story

A user can author clear, rough, and thin glass materials:

```toml
[[materials]]
name = "clear_glass"
type = "dielectric"
ior = 1.5
roughness = 0.0
thin = false

[[materials]]
name = "frosted_glass"
type = "rough_glass"
ior = 1.5
roughness = 0.35

[[materials]]
name = "window"
type = "thin_glass"
ior = 1.45
roughness = 0.0
```

and render a path-traced scene where smooth glass reflects and refracts the
environment, rough glass produces blurred reflection and transmission, and thin
glass behaves like a window pane rather than a thick solid medium.

## Scene Syntax

The canonical material type is:

```toml
type = "dielectric"
```

Convenience aliases are accepted by the scene parser:

- `type = "glass"`: dielectric, `roughness = 0.0`, `thin = false` unless
  explicitly overridden by authored fields.
- `type = "rough_glass"`: dielectric, `thin = false`, default roughness
  `0.25` when `roughness` is omitted.
- `type = "thin_glass"`: dielectric, `thin = true`, default roughness `0.0`
  when `roughness` is omitted.

New material fields:

```toml
ior = 1.5
thin = false
```

Field semantics:

- `ior` is the relative index of refraction for the dielectric surface. It
  defaults to `1.5` and is validated in `[1.0, 3.0]`.
- `thin` is a boolean. It defaults to `false`.
- `roughness` is the existing `[0, 1]` scalar. For dielectric materials,
  `roughness <= DeltaRoughness` means smooth delta glass; larger values mean
  rough microfacet dielectric.
- `albedo` remains available as a simple surface tint with default white. It is
  not Beer-Lambert absorption and does not depend on traveled distance.
- `emission` remains an additive material property as in current materials,
  though emissive glass is not a special target for this slice.
- `specular` remains accepted for compatibility but is ignored by dielectric
  materials.

Internally, aliases should compile to a single `MaterialKind::Dielectric`.
`MaterialKindName(MaterialKind::Dielectric)` should return `"dielectric"`;
the alias strings are parser inputs, not distinct render kinds.

## Render Data Model

Extend semantic and render material structs:

```cpp
struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{1.0f, 1.0f, 1.0f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    float ior = 1.5f;
    bool thin = false;
};

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{1.0f, 1.0f, 1.0f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
    float ior = 1.5f;
    bool thin = false;
};
```

The appended field order preserves existing aggregate initialization as much as
possible. Tests should cover defaults so future field additions remain visible.

## BSDF Design

The existing BSDF API remains the public render-level boundary:

```cpp
Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);
float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);
BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample);
bool IsDeltaBsdf(const RenderMaterial& material);
```

No new integrator-facing virtual interface is required in this slice.

### Smooth Dielectric

When `roughness <= DeltaRoughness` and `thin == false`, dielectric sampling is a
delta interface:

- Compute exact dielectric Fresnel from incident cosine and relative IOR.
- Sample reflection with probability `F`.
- Sample refraction with probability `1 - F`.
- If refraction is impossible, total internal reflection returns reflection
  with probability `1`.
- `EvaluateBsdf()` returns black because the smooth interface is delta.
- `PdfBsdf()` returns zero for the same reason.
- `SampleBsdf()` returns `specular = true`.
- `IsDeltaBsdf()` returns true.

The path tracer already handles delta materials by skipping direct lighting and
continuing through recursive BSDF-sampled paths, which is the desired behavior.

### Rough Dielectric

When `roughness > DeltaRoughness` and `thin == false`, dielectric sampling is a
non-delta microfacet BSDF:

- Use classic GGX NDF half-vector sampling, matching the current metal/plastic
  microfacet style.
- Reflection applies when `wo` and `wi` are on the same side of the geometric
  surface.
- Transmission applies when `wo` and `wi` are on opposite sides.
- Fresnel selects how much energy belongs to the reflective versus
  transmissive lobe.
- `EvaluateBsdf()` supports both reflection and transmission.
- `PdfBsdf()` returns the mixture PDF matching `SampleBsdf()`.
- `SampleBsdf()` returns `specular = false`.
- `IsDeltaBsdf()` returns false.

The implementation should use the standard Walter-style microfacet dielectric
reflection/transmission equations with the correct half-vector Jacobians for
reflection and transmission. It is acceptable for this slice to use classic
NDF sampling rather than VNDF sampling; correctness and testability matter more
than final variance.

### Thin Glass

Thin glass represents a surface such as a window pane, where the renderer
should not simulate a ray traveling through a thick object and exiting through a
second interface.

For `thin == true`:

- Smooth thin glass samples Fresnel reflection or forward transmission.
- Forward transmission uses a straight-through direction on the other side of
  the surface instead of bending as though entering a thick solid.
- Rough thin glass uses rough reflection plus rough forward transmission.
- No medium stack is pushed or popped.
- No path-length absorption is applied.

Thin glass remains a surface BSDF. It is intentionally a practical window/pane
model rather than a physically complete solid dielectric.

## Path Tracer Interaction

The current CPU path tracer can keep its main control flow:

- Delta dielectric samples continue through recursive paths.
- Smooth glass does not receive direct-light samples at the hit point.
- Rough dielectric is non-delta and can participate in existing area-light and
  HDRI direct-light MIS.
- Environment and emissive-hit MIS use `PdfBsdf()` for rough dielectric just as
  they already do for rough metal and plastic.

This slice should not add special glass branches to the integrator beyond using
the existing `IsDeltaBsdf()`, `EvaluateBsdf()`, `PdfBsdf()`, and `SampleBsdf()`
results.

## Showcase Scene

Add a focused manual and smoke-test scene:

```text
scenes/examples/glass_showcase.toml
```

Recommended contents:

- A clear glass mesh sphere.
- A rough glass mesh sphere.
- A thin glass panel.
- Diffuse reference surfaces.
- Either HDRI environment lighting or a Cornell-style area light plus visible
  background features so reflection and refraction are easy to inspect.

Generate mesh assets instead of adding analytic primitives:

```text
scenes/examples/assets/glass_sphere.obj
```

The mesh should include vertex normals. UVs are optional for this slice.

The CLI smoke test should render:

```text
scenes/examples/out/glass_showcase.png
```

and verify the path integrator, PNG output, and basic statistics.

## Testing Strategy

Parser tests:

- `dielectric`, `glass`, `rough_glass`, and `thin_glass` parse successfully.
- `ior` defaults to `1.5`.
- `ior` rejects non-numeric values and values outside `[1.0, 3.0]`.
- `thin` rejects non-boolean values.
- `rough_glass` defaults roughness to `0.25` when omitted.
- `thin_glass` defaults `thin = true`.
- Unknown material names still produce diagnostics.

Compiler tests:

- Semantic dielectric material compiles to `RenderMaterial` with
  `MaterialKind::Dielectric`.
- `ior`, `roughness`, `thin`, `albedo`, and `emission` are preserved.
- Alias-authored materials compile to the same render kind with the expected
  defaults.

BSDF tests:

- Smooth glass can sample refraction from air into glass.
- Smooth glass returns reflection under total internal reflection.
- Smooth glass reflection probability changes with incident angle.
- Smooth glass reports delta behavior.
- Rough dielectric `EvaluateBsdf()` returns finite non-black reflection for a
  valid same-side direction.
- Rough dielectric `EvaluateBsdf()` returns finite non-black transmission for a
  valid opposite-side direction.
- Rough dielectric `PdfBsdf()` is finite and positive for valid sampled
  directions.
- Rough dielectric samples are valid and normalized.
- Thin glass smooth transmission continues forward instead of bending like a
  thick dielectric.
- Unknown or invalid material enum values still fail closed.

CPU path tracer tests:

- A smooth glass surface can reveal non-black environment/background radiance
  through a BSDF-sampled path.
- A rough glass surface produces deterministic non-black output and reports
  non-delta BSDF behavior through existing sampling logic.
- A thin glass panel does not black out light passing through it.
- Existing mirror, metal, plastic, MIS, HDRI, and texture tests remain
  unchanged.

CLI test:

- `yaoray_cli_render_glass_showcase` renders the showcase scene with the CPU
  path integrator and writes a valid PNG.

## Risks And Mitigations

- Rough dielectric transmission PDF is easy to get subtly wrong. Mitigate with
  focused BSDF tests for sampled directions, finite values, positive PDFs, and
  deterministic path tracing behavior.
- Smooth glass without caustic-specific sampling will not render sharp caustics
  efficiently. Keep the showcase focused on visible reflection/refraction and
  rough glass, not caustic validation.
- Thin glass is an approximation. Document it as a surface pane model and avoid
  path-length absorption semantics.
- This slice adds several material aliases. Compile them to one material kind
  so future CUDA and glTF work sees one dielectric path.

## Success Criteria

- Scene files can author dielectric materials through `dielectric`, `glass`,
  `rough_glass`, and `thin_glass`.
- Smooth glass reflects, refracts, and handles total internal reflection.
- Rough glass has consistent non-delta `Evaluate/Pdf/Sample` behavior.
- Thin glass behaves as a pane model rather than a thick medium.
- Existing materials and scenes continue to pass tests.
- `glass_showcase.toml` renders through the CPU path tracer and writes PNG.
- README and architecture docs describe dielectric materials and the remaining
  lack of medium absorption/CUDA parity.

## Future Work

- Beer-Lambert absorption and a real medium stack.
- Nested dielectrics and explicit interior/exterior IOR tracking.
- Visible-normal GGX sampling for lower rough-glass variance.
- Caustic-focused algorithms such as photon mapping or bidirectional path
  tracing.
- glTF material extension import for transmission, IOR, volume, and specular.
- CUDA dielectric material parity.
- Analytic primitives such as spheres and cylinders.

## Implementation Status

Implemented in Dielectric Material Pack v3:

- Scene parser support for `dielectric`, `glass`, `rough_glass`, and
  `thin_glass`.
- Material `ior` and `thin` fields in semantic and render material data.
- Smooth dielectric Fresnel reflection, Snell refraction, and total internal
  reflection.
- Classic GGX rough dielectric reflection and transmission.
- Thin glass pane behavior for smooth and rough materials.
- CPU path tracer transmitted-ray biasing.
- Parser, compiler, BSDF, path tracer, and CLI showcase tests.
