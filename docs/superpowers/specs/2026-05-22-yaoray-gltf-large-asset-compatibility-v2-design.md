# YaoRay glTF Large Asset Compatibility v2 Design

## Summary

This slice makes YaoRay a more useful CPU offline renderer for real glTF assets
before CUDA work begins. The driving in-repository validation model is Khronos
`FlightHelmet`, which is small enough to keep in Git and broad enough to stress
core glTF PBR material plumbing. Amazon Lumberyard Bistro Interior remains a
local benchmark target and is not committed to Git in this slice.

The goal is not exact production glTF PBR parity. The goal is a robust data path
from glTF materials and textures into CPU path tracing, covering the core
features needed by realistic models: RGBA texture storage, explicit texture
color spaces, metallic-roughness textures, normal maps with generic tangent
generation, emissive textures, alpha mask visibility, and double-sided material
metadata.

## Goals

- Add `FlightHelmet` as an in-repository example asset and documentation-backed
  compatibility target.
- Keep Bistro Interior as a documented local benchmark target, with no original
  ORCA zip or converted Bistro asset committed to Git.
- Preserve glTF material data in `AssetResource` instead of collapsing it too
  early into diffuse/metal/plastic approximations.
- Extend render textures so CPU rendering can distinguish sRGB color textures
  from linear/data textures and can preserve alpha.
- Support glTF metallic-roughness textures in CPU material resolution.
- Support glTF normal maps with tangent-space shading normals.
- Generate tangents generically when imported assets have positions, normals,
  and UVs but no tangent attribute.
- Support alpha mask visibility for camera, indirect, and shadow rays.
- Keep `BLEND` alpha mode as a controlled warning or opaque fallback in this
  slice; do not implement sorted transparency or volumetric transmission.
- Keep default tests fast. Default CTest should load and compile Flight Helmet,
  but full Flight Helmet path rendering remains an explicit manual example
  command. Bistro remains manual-only.

## Non-Goals

- No CUDA implementation.
- No OptiX implementation.
- No glTF animation, skinning, morph targets, cameras, or lights.
- No Draco, meshopt, KTX, KTX2, Basis Universal, or sparse accessor support.
- No glTF material extensions such as clearcoat, transmission, volume, sheen,
  specular, iridescence, or anisotropy.
- No exact glTF BRDF conformance target.
- No alpha blend transparency, transparent sorting, or colored glass import from
  glTF extensions.
- No mipmaps, anisotropic filtering, texture LOD selection, or GPU texture
  parity.
- No default CTest benchmark that renders Bistro.
- No large Bistro binary asset committed to Git.

## Asset Targets

### Flight Helmet

`FlightHelmet` is the required in-repository compatibility model.

Source:

```text
KhronosGroup/glTF-Sample-Assets
Models/FlightHelmet/glTF/
```

Expected repository location:

```text
scenes/examples/assets/gltf/FlightHelmet/glTF/
```

Rationale:

- It is a real static glTF model with multiple materials and texture maps.
- It is CC0 in the Khronos sample assets repository.
- The full model directory is about 48 MB, which is large but still acceptable
  for this repository without Git LFS.
- It exercises base color, normal, occlusion/roughness/metallic style textures,
  UVs, vertex normals, and multi-material mesh data.

Flight Helmet becomes the visual validation target for this slice. The example
scene should use reduced resolution and sample count so manual renders are
practical on CPU.

### Bistro Interior

Bistro Interior is a local benchmark target only.

Source:

```text
NVIDIA ORCA Amazon Lumberyard Bistro
```

Policy:

- Do not commit the 853 MiB original NVIDIA download zip.
- Do not commit converted Bistro assets in this slice.
- Document a local expected location such as:

```text
external/assets/bistro/
```

- Document the license and conversion notes when a local conversion is used.
- Document the exact TOML scene fields needed to render a locally converted
  Bistro asset.

Bistro exists to guide architectural decisions: large material counts, large
texture sets, BVH build cost, scene stats, and long-running CPU path tracing. It
must not slow normal test runs.

## Asset Layer Design

`AssetResource` should store glTF material and texture intent, not only current
renderer approximations.

Add material metadata:

```cpp
enum class AssetAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct AssetMaterial {
    std::string name;
    MaterialKind approximate_type = MaterialKind::Diffuse;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    float base_color_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int occlusion_texture = -1;
    int emissive_texture = -1;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    AssetAlphaMode alpha_mode = AssetAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
};
```

Keep `AssetTexture` as image plus sampler identity. Texture color space is not a
global property of the glTF texture object because the same image can be used
from different material slots. The compiler derives texture usage from the
material slot:

- `baseColorTexture`: color/sRGB.
- `emissiveTexture`: color/sRGB.
- `metallicRoughnessTexture`: data/linear.
- `normalTexture`: data/linear.
- `occlusionTexture`: data/linear.

For shared glTF texture indices used in multiple slots, the asset layer keeps
the original glTF texture entry. The compiler cache key includes image
identity, sampler state, and material-slot-derived texture usage so
the same PNG can be loaded as sRGB in one slot and linear data in another slot.

## Texture Representation

`RenderTexture` needs RGBA storage and explicit color interpretation at load
time.

Target render texture shape:

```cpp
struct Color4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

enum class TextureColorSpace {
    Srgb,
    Linear,
};

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureColorSpace color_space = TextureColorSpace::Linear;
};
```

Sampling helpers should preserve existing RGB callers while adding alpha access:

```cpp
Color4f SampleTexture4(const RenderTexture& texture, Vec2f uv);
Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
float SampleTextureAlpha(const RenderTexture& texture, Vec2f uv);
```

PNG loading should take a color-space parameter:

```cpp
TextureLoadResult LoadPngTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space
);
```

Behavior:

- `TextureColorSpace::Srgb`: RGB channels convert from sRGB to linear; alpha is
  normalized but not gamma converted.
- `TextureColorSpace::Linear`: RGB and alpha channels are normalized without
  sRGB conversion.
- Existing environment HDR loading remains linear.
- Existing OBJ diffuse texture behavior remains sRGB.
- Existing glTF base color texture behavior remains sRGB.

## Render Material Design

Extend `RenderMaterial` enough for core glTF PBR approximation:

```cpp
enum class RenderAlphaMode {
    Opaque,
    Mask,
    Blend,
};

struct RenderMaterial {
    MaterialKind type = MaterialKind::Diffuse;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    float albedo_alpha = 1.0f;
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 0.0f;
    float specular = 0.04f;
    int albedo_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int emissive_texture = -1;
    int occlusion_texture = -1;
    float normal_scale = 1.0f;
    float occlusion_strength = 1.0f;
    RenderAlphaMode alpha_mode = RenderAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

The existing `MaterialKind` remains. The slice improves current
approximation rather than introduce a full glTF BRDF:

- Metallic materials route to current metal behavior.
- Non-metallic rough materials route to diffuse or plastic behavior based on
  roughness/specular.
- Per-hit metallic/roughness sampling updates the resolved material before BSDF
  evaluation.
- Emissive texture multiplies or modulates `emission`.
- Occlusion texture is stored in the material data but does not affect CPU
  shading in this slice. The documentation must call this out as a stored
  field with no current shading effect.

## Geometry And Tangent Design

Add tangent support to the render triangle:

```cpp
struct RenderTriangle {
    // existing fields...
    Vec3f t0;
    Vec3f t1;
    Vec3f t2;
    float tangent_handedness0 = 1.0f;
    float tangent_handedness1 = 1.0f;
    float tangent_handedness2 = 1.0f;
    bool has_tangents = false;
};
```

Asset primitives should preserve imported tangents when present. Since the core
math layer currently has no four-component vector type, use a focused asset
tangent type:

```cpp
struct AssetTangent {
    Vec3f direction;
    float handedness = 1.0f;
};

std::vector<AssetTangent> tangents;
```

If no tangents are imported, generate tangents in the render compiler when the
primitive has positions, UVs, and normals. Tangent generation should:

- Work for glTF and OBJ-derived primitives.
- Accumulate tangent vectors per vertex.
- Use triangle UV derivatives.
- Skip degenerate UV triangles.
- Orthogonalize tangent against vertex normal.
- Preserve a handedness value so normal maps can construct bitangents.
- Fall back to vertex normals or face normals when tangent generation is not
  possible.

Normal map shading should:

1. Resolve geometric and vertex shading normal as it does today.
2. Interpolate tangent and handedness.
3. Sample normal texture as linear data.
4. Convert texture value from `[0, 1]` to tangent-space `[-1, 1]`.
5. Apply `normal_scale` to tangent-space x/y.
6. Transform through TBN to world space.
7. Face-forward against the geometric normal or incoming ray consistently with
   current shading.

## Alpha Mask Design

Alpha mask must affect visibility, not only final color.

Supported behavior:

- `OPAQUE`: all geometric hits are visible.
- `MASK`: sample alpha at hit UV; if alpha is below `alpha_cutoff`, skip the hit
  and continue tracing behind it.
- `BLEND`: warn during import or compile and treat as `OPAQUE` in this slice.

Alpha source:

- Base color factor alpha should be preserved.
- Base color texture alpha should be sampled when present.
- Final alpha is `base_color_factor_alpha * base_color_texture_alpha`.

The current `BvhHit` only reports the closest geometric hit. Alpha mask needs a
higher-level CPU visibility query that can skip masked hits without embedding
material rules into BVH construction.

Recommended API shape:

```cpp
struct CpuSurfaceHit {
    bool hit = false;
    BvhHit geometry_hit;
    Vec3f barycentric;
    Vec2f uv;
    RenderMaterial material;
};

CpuSurfaceHit TraceVisibleSurface(
    const CpuPreparedScene& prepared_scene,
    Ray3f ray,
    float t_min,
    float t_max,
    BvhTraceStats& stats
);
```

Implementation behavior:

```text
repeat:
  IntersectBvh(scene, bvh, ray, t_min, t_max)
  if miss: return miss
  resolve uv/material alpha
  if material is visible: return hit
  advance ray origin or t_min just past hit
```

The BVH remains geometry-only. CPU debug rendering, CPU path tracing, and
shadow visibility should call the visibility helper instead of raw
`IntersectBvh()` when a ray needs material-visible surfaces.

Stats should continue counting BVH node and triangle tests for skipped alpha
hits. Shadow stats should count a shadow ray once even if the helper internally
skips multiple masked hits.

## Render Compiler Flow

The compiler should convert asset material fields into render material fields
without losing semantic data.

Texture loading needs cache keys that include:

- Resolved image path or image identity.
- Wrap mode.
- Filter mode.
- Texture color space or usage.

This prevents a base-color texture and a data texture from incorrectly sharing
the same loaded `RenderTexture` when they require different RGB decoding.

This v2 implementation targets Flight Helmet's external PNG texture layout.
Embedded images and data URI images are not required in this slice. The texture
cache should still use an image identity abstraction rather than assuming every
future texture source is a filesystem path.

## CPU Path Tracer Flow

Replace the current `ResolveHitMaterial()` with a broader hit material resolver:

```cpp
struct ResolvedMaterialSample {
    RenderMaterial material;
    Vec3f shading_normal;
    Vec2f uv;
    float alpha = 1.0f;
};
```

At each visible hit:

1. Compute barycentric coordinates and UV.
2. Resolve base color factor and base color texture.
3. Resolve metallic/roughness factors and texture channels.
4. Resolve emissive factor and emissive texture.
5. Resolve normal map and shading normal.
6. Return alpha for visibility filtering.

The BSDF functions keep taking `RenderMaterial` for this slice. They should
receive a resolved material with sampled albedo, roughness, metallic-derived
type, and emission.

## CPU Debug Renderer Flow

The debug renderer should respect alpha mask for primary and shadow rays,
so test images do not show cutout geometry as solid surfaces.

It continues using a simpler lighting model. It uses the shared alpha and base
material resolver, but normal map shading is required only for the CPU path
tracer in this slice.

## Error Handling

- Invalid glTF texture indices remain load errors.
- Unsupported texture slots should not crash; required core slots should either
  load or produce a clear diagnostic.
- `BLEND` alpha mode should emit a warning and fall back to opaque rendering.
- Missing normal texture image is an error if the material references it.
- Missing tangents are not an error when UVs and normals are present; the
  compiler generates tangents.
- Degenerate UVs during tangent generation emit at most a warning and fall back
  for affected triangles or vertices.
- Missing Bistro local files should not fail normal tests.

## Testing Strategy

Asset tests:

- Flight Helmet loads as `AssetResource`.
- glTF material fields preserve base color, metallic, roughness, normal texture,
  metallic-roughness texture, emissive texture, alpha mode, alpha cutoff, and
  double-sided data when present.
- Compiler material-slot usage classification marks base color/emissive as
  color and metallic-roughness/normal/occlusion as data.
- Invalid texture references remain errors.

Texture tests:

- sRGB PNG loading converts RGB and preserves alpha.
- linear/data PNG loading does not apply sRGB conversion.
- alpha sampling works for nearest and bilinear filtering.
- existing albedo texture tests continue to pass.

Compiler tests:

- Asset material textures compile into distinct render texture slots when color
  space differs.
- Render materials preserve alpha mode, alpha cutoff, normal texture,
  metallic-roughness texture, emissive texture, and double-sided metadata.
- Tangents are imported when present.
- Tangents are generated when missing and valid UVs/normals exist.
- Degenerate UV tangent generation falls back cleanly.

Renderer tests:

- Alpha mask primary rays skip cutout texels and hit geometry behind them.
- Alpha mask shadow rays do not treat cutout texels as blockers.
- Metallic-roughness texture changes resolved roughness/metallic values.
- Normal map changes shading normals in a deterministic test scene.
- Emissive texture contributes to hit emission in path tracing.

CLI/example tests:

- A low-resolution Flight Helmet scene renders successfully on CPU.
- The default CTest suite remains bounded in runtime.
- Bistro local benchmark scene is documented but not part of default CTest.

## Documentation

Update:

- `docs/assets/khronos-sample-assets.md` with Flight Helmet source, license, and
  purpose.
- `docs/architecture/overview.md` with glTF compatibility v2 scope.
- `README.md` with Flight Helmet example command and note that Bistro is a local
  benchmark target.
- Example scene files under `scenes/examples/` for Flight Helmet.
- Optional local benchmark notes for Bistro under `docs/assets/` or
  `scenes/examples/README.md`.

## Risks And Mitigations

- **Scope growth:** Keep glTF extensions and alpha blend out of this slice.
- **Large asset churn:** Commit Flight Helmet only; keep Bistro local.
- **Texture memory growth:** Flight Helmet is acceptable. Bistro benchmark notes
  should warn about memory and render time.
- **BVH/material coupling:** Keep BVH geometric and implement alpha visibility
  as a CPU backend helper.
- **Normal map correctness:** Use deterministic tangent tests and accept
  approximate visual parity instead of exact DCC parity.
- **Runtime regression:** Keep Flight Helmet smoke render low resolution and
  avoid adding Bistro to default tests.

## Success Criteria

- Flight Helmet is committed with source/license documentation.
- Flight Helmet can be rendered from a YaoRay example scene with CPU path
  tracing.
- Base color, emissive, metallic-roughness, normal, and alpha mask material data
  travel from glTF to `RenderSceneIR`.
- CPU path tracing resolves glTF material samples per hit.
- Alpha mask affects primary, indirect, and shadow visibility.
- Tangents are imported or generated for normal mapped assets.
- Existing OBJ and smaller glTF examples still render.
- Full CTest passes on macOS.
- Bistro is documented as a local benchmark target and is not committed to Git.
