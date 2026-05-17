# YaoRay Material v2 Pack Design

YaoRay now has a render-level BSDF API and two material kinds: `diffuse` and
`mirror`. That boundary is ready for a larger material slice. This design adds
a focused pack of non-refractive materials so the renderer can show a broader
range of surfaces without pulling in the extra complexity of glass.

## Goals

- Add `metal` and `plastic` material kinds.
- Express rough metal through `type = "metal"` plus `roughness > 0`, not a
  separate `rough_metal` enum.
- Add scene-authored scalar material fields:
  - `roughness`
  - `specular`
- Keep existing scene files valid.
- Keep material dispatch data-driven through `MaterialKind`.
- Extend the BSDF API implementation, not the CPU path tracer control flow.
- Add a material v2 showcase scene with diffuse, mirror, metal, rough metal,
  plastic, and emissive surfaces.

## Non-Goals

- No glass or dielectric refraction.
- No nested/layered material graph.
- No texture, UV, normal map, or imported material support.
- No complex conductor IOR tables.
- No MIS.
- No Russian roulette.
- No sphere primitive.
- No CUDA backend implementation.

## Scene Schema

Existing material syntax remains valid:

```toml
[[materials]]
name = "white"
type = "diffuse"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]
```

New metal material syntax:

```toml
[[materials]]
name = "brushed_gold"
type = "metal"
albedo = [1.0, 0.72, 0.32]
roughness = 0.35
emission = [0, 0, 0]
```

New plastic material syntax:

```toml
[[materials]]
name = "red_plastic"
type = "plastic"
albedo = [0.8, 0.05, 0.03]
roughness = 0.25
specular = 0.04
emission = [0, 0, 0]
```

Fields:

- `type`
  - Existing: `"diffuse"`, `"mirror"`.
  - New: `"metal"`, `"plastic"`.
- `roughness`
  - Optional scalar in `[0, 1]`.
  - Used by `metal` and `plastic`.
  - Missing `roughness` defaults by material kind:
    - `diffuse`: `0.0`, ignored in this slice.
    - `mirror`: `0.0`, ignored in this slice.
    - `metal`: `0.0`, producing polished delta metal.
    - `plastic`: `0.25`, producing a finite glossy lobe.
- `specular`
  - Optional scalar in `[0, 1]`.
  - Used by `plastic`.
  - Missing `specular` defaults to `0.04`.
  - Ignored by `diffuse`, `mirror`, and `metal` in this slice.

The parser should accept `roughness` and `specular` on any material entry so
scene authors get a stable schema. The BSDF layer decides which fields are
used by each material type.

## Data Model

Extend semantic and render materials:

```cpp
enum class MaterialKind {
    Diffuse,
    Mirror,
    Metal,
    Plastic,
};

struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
};

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float roughness = 0.0f;
    float specular = 0.04f;
};
```

Because `roughness` and `specular` are appended after existing fields, old
aggregate initializers need only update where tests construct materials
explicitly and inspect all fields.

## Parser And Compiler Behavior

Parser:

- Add `"metal"` and `"plastic"` to `ParseMaterialKindName`.
- Add stable names to `MaterialKindName`.
- Allow `roughness` and `specular` in `[[materials]]`.
- Reject non-numeric `roughness` and `specular`.
- Reject non-finite values.
- Reject values outside `[0, 1]`.
- Track whether `roughness` was authored so `plastic` can default to `0.25`
  while `metal` can default to `0.0`.

Compiler:

- Copy `type`, `albedo`, `emission`, `roughness`, and `specular` into
  `RenderMaterial`.
- Default materials created for unbound instances remain diffuse with
  `roughness = 0.0` and `specular = 0.04`.

## BSDF Behavior

### Diffuse

Unchanged. Lambertian reflection:

```text
f = albedo / pi
sample weight = albedo
```

`roughness` and `specular` are ignored.

### Mirror

Unchanged. Perfect specular reflection:

```text
wi = reflect(-wo, normal)
sample weight = albedo
```

`roughness` and `specular` are ignored.

### Metal

`metal` uses `albedo` as the conductor reflection tint.

If `roughness <= 0.001`:

- Treat as a delta specular material.
- `IsDeltaBsdf()` returns true.
- `SampleBsdf()` returns perfect reflection with `weight = albedo`.
- `EvaluateBsdf()` returns black for finite direction queries.
- `PdfBsdf()` returns zero for finite direction queries.

If `roughness > 0.001`:

- Treat as a non-delta GGX microfacet reflection material.
- Convert user roughness to microfacet alpha with:

```text
alpha = max(roughness * roughness, 0.001)
```

- Use GGX/Trowbridge-Reitz normal distribution.
- Use a Smith-style masking-shadowing approximation.
- Use Schlick Fresnel with `F0 = albedo`.
- `EvaluateBsdf()` returns the finite microfacet BRDF.
- `PdfBsdf()` returns the half-vector sampling pdf converted to reflected
  direction pdf.
- `SampleBsdf()` samples a GGX half-vector, reflects `-wo`, and returns:

```text
weight = EvaluateBsdf(material, wo, wi, normal) * abs(dot(normal, wi)) / pdf
```

### Plastic

`plastic` approximates a dielectric-like surface with a diffuse base and a
white glossy specular lobe.

- Diffuse base: `albedo`.
- Specular F0: grayscale `specular`.
- Roughness: user `roughness`, with an effective minimum of `0.05` so plastic
  stays a finite glossy lobe in this slice.
- `IsDeltaBsdf()` always returns false for plastic.
- `EvaluateBsdf()` returns:

```text
diffuse = albedo * (1 - specular) / pi
specular_lobe = GGX using F0 = [specular, specular, specular]
result = diffuse + specular_lobe
```

- `PdfBsdf()` uses a mixture pdf of diffuse cosine sampling and GGX specular
  half-vector sampling.
- `SampleBsdf()` chooses one lobe from the provided `Vec2f` sample:
  - one half of the sample domain selects diffuse,
  - the other half selects specular,
  - the selected lobe remaps the sample into `[0, 1]` and returns an unbiased
    `weight = f * cos / pdf`.

This is not a final physically exact plastic model. It is a controlled v2
material that provides visible non-metallic highlights while keeping the
integrator and schema simple.

## CPU Path Tracer Impact

The CPU path tracer should not add new material-kind branches for this slice.
It should continue to call:

- `IsDeltaBsdf()`
- `EvaluateBsdf()`
- `PdfBsdf()`
- `SampleBsdf()`

Direct lighting naturally includes non-delta `metal` and `plastic` through
`EvaluateBsdf()`. Delta `mirror` and polished `metal` skip direct-light
estimation and contribute through recursive reflected paths.

## Showcase Scene

Create or update a material v2 showcase scene. Prefer creating a new scene:

```text
scenes/examples/material_v2_showcase.toml
```

The scene should include:

- diffuse Cornell-style room surfaces,
- emissive light panel,
- existing mirror material,
- polished metal material (`metal`, `roughness = 0.0`),
- rough metal material (`metal`, `roughness` around `0.35`),
- plastic material (`plastic`, `roughness` around `0.25`, `specular = 0.04`).

Because sphere primitives are out of scope, use inline quads/boxes or existing
block-style geometry. The goal is to verify the material pipeline and provide a
manual preview, not to create the final beauty demo.

## Testing Strategy

Parser and scene tests:

- `MaterialKindName()` returns stable names for `metal` and `plastic`.
- `ParseMaterialKindName()` accepts `metal` and `plastic`.
- `[[materials]] roughness` parses and defaults correctly.
- `[[materials]] specular` parses and defaults correctly.
- Non-numeric `roughness` and `specular` are diagnostics.
- Out-of-range `roughness` and `specular` are diagnostics.

Compiler tests:

- `CompileScene()` propagates `roughness` and `specular`.
- Default unbound materials keep `type = Diffuse`, `roughness = 0.0`, and
  `specular = 0.04`.

BSDF tests:

- Polished `metal` samples like tinted delta reflection.
- Rough `metal` has finite evaluate/pdf/sample results.
- Rough `metal` is not delta.
- `plastic` has finite evaluate/pdf/sample results.
- `plastic` is not delta.
- Existing diffuse and mirror tests continue to pass.

Path/render tests:

- Existing CPU path tracer tests continue to pass.
- Manual rendering of `material_v2_showcase.toml` succeeds.

## Documentation

Update README and architecture overview to state that YaoRay supports:

- diffuse,
- emissive surfaces through `emission`,
- perfect mirror,
- metal with roughness,
- simple plastic.

Keep the limitations explicit: glass, textures, complex conductor IOR,
normal maps, MIS, spectral rendering, and CUDA material evaluation remain
future work.

## Acceptance Criteria

- Scene files can author `type = "metal"` and `type = "plastic"`.
- Scene files can author `roughness` and `specular` scalar fields.
- Existing scene files without those fields remain valid.
- `RenderMaterial` carries `roughness` and `specular`.
- BSDF dispatch remains switch-based through `MaterialKind`.
- CPU path tracer control flow stays material-generic.
- `metal` supports polished and rough behavior through `roughness`.
- `plastic` supports diffuse plus finite glossy highlights.
- `material_v2_showcase.toml` renders successfully.
- Full Debug tests pass.
