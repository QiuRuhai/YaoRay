# YaoRay Material v1 Showcase Design

YaoRay currently has named materials with `albedo` and `emission`, but the CPU
path tracer treats every non-black material as diffuse. That is enough for the
Cornell Box baseline, yet it does not demonstrate a real material system. This
slice adds the first explicit scattering model boundary and a small showcase
scene that makes the new behavior visible.

The goal is not to build a final physically based material system. The goal is
to introduce a conservative material-kind field, preserve all existing scenes,
and add one high-signal perfect-mirror material that proves path scattering is
no longer hard-coded to diffuse.

## Goals

- Add a scene-authored material kind with default `"diffuse"`.
- Support `"diffuse"` and `"mirror"` material kinds.
- Preserve existing material files that only specify `name`, `albedo`, and
  `emission`.
- Keep `emission` as an additive property that can exist on any material kind.
- Make CPU `path` choose scattering behavior from the compiled material kind.
- Keep CPU `debug_direct` simple; it may continue using `albedo` and `emission`
  without simulating mirror reflection.
- Add a Cornell-style material showcase scene with at least one mirror object
  and one diffuse object.
- Keep fixed-seed output deterministic and thread-count independent.

## Non-Goals

- No glass or dielectric refraction.
- No rough metals, roughness, GGX, or microfacet models.
- No texture, UV, normal map, or imported material support.
- No sphere primitive.
- No OBJ material or `.mtl` parsing.
- No MIS.
- No spectral rendering.
- No CUDA material implementation.
- No large material folder/module refactor in this slice.

## User-Facing Material Syntax

Existing material syntax remains valid:

```toml
[[materials]]
name = "cornell_white"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]
```

It is equivalent to:

```toml
[[materials]]
name = "cornell_white"
type = "diffuse"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]
```

Mirror materials use the same `albedo` field as their reflection tint:

```toml
[[materials]]
name = "mirror_block"
type = "mirror"
albedo = [0.95, 0.95, 0.95]
emission = [0, 0, 0]
```

Semantics:

- `type = "diffuse"` means the current diffuse BRDF behavior.
- `type = "mirror"` means perfect specular reflection.
- Missing `type` defaults to `"diffuse"`.
- Unknown material types produce a `materials.type` diagnostic.
- Non-string material types produce a `materials.type` diagnostic.
- `emission` remains additive radiance on hit for both diffuse and mirror
  materials.

The field is named `type` rather than `shader` because it describes a scene
material category, not a programmable shader.

## Data Model And Parsing

Add a material kind enum near the existing scene enums:

```cpp
enum class MaterialKind {
    Diffuse,
    Mirror,
};
```

Extend semantic materials:

```cpp
struct MaterialDescription {
    std::string name;
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

Extend compiled materials:

```cpp
struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

Parser changes:

- Add `"type"` to the allowed `[[materials]]` fields.
- Accept `"diffuse"` and `"mirror"`.
- Reject unknown strings with `materials.type` and `unknown material type`.
- Reject non-string values with `materials.type` and `must be a string`.
- Keep existing defaults for `albedo` and `emission`.

Scene helper changes:

- Add `MaterialKindName(MaterialKind)`.
- Add `ParseMaterialKindName(std::string_view)`.
- Return `"unknown"` for invalid enum values, matching other enum helpers.

Compiler changes:

- Copy `MaterialDescription::type` into `RenderMaterial::type`.
- Default materials created for unbound instances remain diffuse.

## CPU Path Integrator Behavior

The current path loop always does:

```text
add material emission
estimate diffuse direct lighting
sample cosine hemisphere
throughput *= material albedo
continue
```

After this slice, the path loop branches by material type.

Diffuse behavior:

```text
add material emission
estimate diffuse direct lighting
if max depth reached or albedo black: stop
sample cosine hemisphere
throughput *= material albedo
continue
```

Mirror behavior:

```text
add material emission
do not estimate diffuse direct lighting
if max depth reached or albedo black: stop
reflect the incoming ray direction around the oriented surface normal
offset origin by the surface bias
throughput *= material albedo
continue
```

The perfect mirror reflection direction is:

```cpp
reflected = ray.direction - normal * (2.0f * Dot(ray.direction, normal));
```

where `normal` is already face-forwarded against the incoming ray. The reflected
ray should be normalized before use, consistent with other ray directions.

This is a delta material. It does not receive area-light direct lighting through
`EstimateDirectLight` because that estimator is diffuse-only and has no MIS or
delta-light handling.

## CPU Debug Renderer Behavior

`debug_direct` remains a fast reference renderer. It can keep its existing
behavior:

- `albedo` still controls visible base color and direct diffuse debug shading.
- `emission` still adds hit radiance.
- `MaterialKind::Mirror` does not produce recursive reflection in
  `debug_direct`.

This keeps debug output predictable and avoids turning the debug renderer into a
second path tracer.

## Showcase Scene

Add a new manual scene:

```text
scenes/examples/material_showcase.toml
```

The scene should be Cornell-style rather than replacing the existing measured
Cornell Box scenes. It should use existing inline quad assets so no new sphere
primitive is required.

Recommended composition:

- Cornell-style red and green side walls.
- White floor, ceiling, back wall, and split ceiling area-light panel.
- One diffuse white block.
- One perfect mirror block, using `type = "mirror"` and high albedo.
- `integrator = "path"`.
- `sampler = "stratified"`.
- `light_samples = 4`.
- `max_depth >= 6`, so mirror rays can reflect room content.
- Output path: `out/material_showcase.png`.

The mirror object may be a cube or rectangular block built from inline quads.
This is less visually elegant than a mirror sphere, but it keeps the slice
focused and immediately reuses current geometry support.

## Documentation

Update README and architecture overview to state:

- Materials now have a default `type = "diffuse"`.
- `type = "mirror"` enables perfect specular reflection in the CPU path
  integrator.
- `emission` remains an additive material property.
- `debug_direct` does not recursively reflect mirror materials.
- Glass, roughness, texture import, and CUDA materials remain future work.
- `scenes/examples/material_showcase.toml` is the first material showcase scene.

## Testing

Parser tests:

- `MaterialDescription::type` defaults to `Diffuse`.
- `[[materials]] type = "diffuse"` parses successfully.
- `[[materials]] type = "mirror"` parses successfully.
- Unknown material types fail with `materials.type` and `unknown material type`.
- Non-string material types fail with `materials.type` and `must be a string`.
- Existing material tests for default albedo/emission and duplicate names still
  pass.

Compiler tests:

- `RenderMaterial::type` defaults to `Diffuse`.
- `CompileScene` copies `MaterialDescription::type` into `RenderMaterial::type`.
- The default material for an unbound instance is diffuse.

Path tracer tests:

- A mirror material reflects a constant environment when max depth allows one
  reflection bounce.
- A mirror material does not receive diffuse direct-light contribution from an
  area light at the first hit.
- A mirror material with black albedo stops the path after emission.
- Existing diffuse path tests still pass unchanged.
- Thread-count determinism still passes when at least one material is mirror.

CLI/manual tests:

- Full Debug CTest passes.
- Release build succeeds.
- Rendering `scenes/examples/material_showcase.toml` succeeds and writes a PNG.

## Acceptance Criteria

- Existing scenes without `materials.type` still parse and render.
- Scenes can author `type = "diffuse"` and `type = "mirror"`.
- Invalid material types produce clear diagnostics.
- `CompileScene` propagates material kind to `RenderMaterial`.
- CPU path tracing branches between diffuse and mirror scattering.
- Mirror materials skip diffuse direct lighting and reflect rays perfectly.
- `debug_direct` remains non-recursive and stable.
- `material_showcase.toml` demonstrates mirror and diffuse materials in a
  Cornell-style scene.
- Full Debug CTest passes.
- Release build succeeds.
- Manual material showcase render succeeds.
