# YaoRay HDRI Environment Importance Sampling v1 Design

## Context

YaoRay can now render static OBJ and glTF/GLB assets with CPU path tracing,
BVH traversal, threaded tiles, area-light MIS, Russian roulette, diffuse and
glossy materials, PNG texture loading, bilinear texture sampling, and basic
glTF texture sampler state. The current environment model is still minimal:
semantic scene files can describe `none`, `constant`, or `hdri`, but scene
compilation rejects HDRI environments, and the CPU path tracer only evaluates a
constant environment color on miss rays.

This slice turns HDRI environment lighting into a real offline-rendering
feature. It is not only a background-image feature: the environment should
illuminate diffuse and glossy objects, reflect in delta materials, and use
importance sampling plus MIS so bright HDRI regions are sampled predictably at
low sample counts.

## Goals

- Load Radiance `.hdr` environment maps as linear floating-point RGB data.
- Preserve PNG albedo/base-color behavior: PNG textures still decode from sRGB
  to linear RGB, while HDRI data stays linear.
- Extend scene syntax for HDRI environment path, strength, and horizontal
  rotation.
- Compile HDRI environment data into `RenderScene`.
- Evaluate equirectangular HDRI radiance for arbitrary world directions.
- Build an importance distribution weighted by texel luminance and
  equirectangular texel solid angle.
- Sample HDRI directions through the render sampler and return a solid-angle
  PDF for MIS.
- Add direct environment lighting for non-delta BSDFs, including BVH shadow
  visibility.
- Apply MIS when BSDF-sampled paths escape to the HDRI environment.
- Preserve constant environment behavior and existing area-light behavior.
- Add focused tests for parsing, loading, mapping, PDF behavior, scene
  compilation, and CPU path tracing.

## Non-Goals

- No EXR input or output.
- No PNG/JPG/LDR environment maps in this slice.
- No environment mipmaps or filtered rough-specular environment lookup.
- No sun/sky physical atmosphere model.
- No portal lights.
- No multiple environment maps or blended environments.
- No glTF camera, light, or environment import.
- No CUDA parity.
- No broad integrator API refactor beyond the local interfaces needed for
  environment evaluation, sampling, and PDF queries.

## User Story

A user can author:

```toml
[environment]
type = "hdri"
path = "assets/env/studio.hdr"
strength = 1.0
rotation_degrees = 0.0
```

and render a scene where the HDRI is visible on camera misses, reflected by
mirror and near-perfect metal materials, and used as an actual light source for
diffuse, plastic, and rough metal surfaces. Bright HDRI areas should contribute
with less noise than pure BSDF sampling because the environment has its own
importance distribution and MIS integration.

## Architecture

Environment lighting should live in render-level helpers rather than being
embedded directly into the CPU path tracer. This keeps the path tracer as the
orchestrator of light transport while making environment lookup, sampling, and
PDF math reusable by future CPU, CUDA, and integrator work.

Extend semantic environment data with horizontal rotation:

```cpp
struct EnvironmentDescription {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    std::filesystem::path path;
    float strength = 1.0f;
    float rotation_degrees = 0.0f;
};
```

Extend compiled environment data with a texture reference and sampling state:

```cpp
struct RenderEnvironment {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};
```

The exact storage can be adjusted during planning, but the boundary should stay
the same: `RenderScene` owns the loaded HDR texels and the compiled importance
distribution. The CPU path tracer should call render-level functions:

```cpp
Color3f EvaluateEnvironment(const RenderScene& scene, Vec3f direction);

struct EnvironmentSample {
    Vec3f direction;
    Color3f radiance;
    float pdf_solid_angle = 0.0f;
    bool valid = false;
};

EnvironmentSample SampleEnvironment(const RenderScene& scene, Vec2f sample);
float PdfEnvironment(const RenderScene& scene, Vec3f direction);
bool HasSampleableEnvironment(const RenderScene& scene);
```

Constant environments remain supported by `EvaluateEnvironment()`. Constant
environments do not need explicit direct-light sampling in this slice; keeping
them miss/BSDF-evaluated preserves existing behavior. HDRI environments use
their luminance-weighted distribution and are the only sampleable environment
type for v1.

## HDR Loading

`LoadPngTexture()` currently uses `stb_image` with PNG-only decoding and stores
linear RGB after sRGB conversion. HDRI loading should add a separate function:

```cpp
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path);
```

`LoadHdrTexture()` accepts only `.hdr`, uses floating-point stb loading, and
stores texels directly as linear `Color3f`. It should reject missing files,
wrong extensions, invalid dimensions, empty data, and non-finite values.

The implementation must keep the single `STB_IMAGE_IMPLEMENTATION` translation
unit model. If `STBI_ONLY_PNG` blocks `.hdr`, replace it with the smallest
supported stb configuration that can load both PNG and HDR in the same
translation unit. PNG tests must prove albedo texture decoding still works.

## Equirectangular Mapping

Use Y-up world directions, matching the current scene convention:

- longitude is measured around the Y axis.
- latitude/elevation maps to texture V.
- horizontal rotation offsets longitude before lookup.

The exact helper names can be chosen during planning, but the math should be
centralized in render-level environment code:

```cpp
Vec2f DirectionToEnvironmentUv(Vec3f direction, float rotation_radians);
Vec3f EnvironmentUvToDirection(Vec2f uv, float rotation_radians);
```

`EvaluateEnvironment()` samples the HDR texture with the existing
`SampleTexture()` entry point so bilinear filtering and wrap behavior remain
centralized. Environment maps should repeat horizontally and clamp vertically.

## Importance Distribution

For an equirectangular environment map, each texel covers a different solid
angle. Texels near the poles cover less sphere area than texels near the
equator. The distribution weight should therefore be:

```text
weight(x, y) = luminance(texel[x, y]) * sin(theta_y)
```

where `theta_y` is the polar angle for the texel row center. Luminance should
use the same simple RGB weighting convention used elsewhere in tests, for
example Rec. 709-style coefficients.

The compiled distribution should support:

- sampling a texel from a 2D random sample.
- jittering within the chosen texel.
- returning a solid-angle PDF.
- evaluating the same solid-angle PDF for an arbitrary direction.

The solid-angle PDF is the discrete probability of selecting a texel divided by
that texel's solid angle. For a width `W` and height `H`, a texel row centered
at polar angle `theta` has approximate solid angle:

```text
texel_solid_angle = (2 * pi / W) * (pi / H) * sin(theta)
```

If the HDRI is all black, has zero total weight, or strength is zero, sampling
must not divide by zero. Use a safe fallback:

- `EvaluateEnvironment()` returns black when strength is zero.
- `SampleEnvironment()` may return invalid for zero-strength environments.
- all-black HDRI sampling falls back to uniform sphere for PDF stability, but
  contributes black radiance.

## Path Tracer Integration

The CPU path tracer changes in three places.

First, miss rays should call:

```cpp
EvaluateEnvironment(scene, ray.direction)
```

instead of a constant-only helper.

Second, direct lighting should include environment samples for non-delta
materials. At a surface point:

1. Sample an environment direction and PDF.
2. Reject directions below the shading hemisphere.
3. Offset the shadow ray with the existing surface-bias policy.
4. Intersect the BVH along that direction.
5. If the shadow ray is unoccluded, add:

```text
BSDF(wo, wi) * Le(wi) * cos(theta) * MIS / pdf_env
```

The environment sample count should use the existing `render.light_samples`
setting at first. This keeps user-facing controls compact: higher
`light_samples` reduces both area-light and environment direct-light noise.

Third, when a BSDF-sampled path misses geometry and reaches the environment,
non-delta previous bounces should apply MIS against `PdfEnvironment()`. Delta
previous bounces, such as mirror reflection and perfect metal, should get full
environment contribution because there was no competing explicit environment
sample for that deterministic direction.

Area-light MIS should remain unchanged. This slice should add environment MIS
without rewriting the existing area-light helper contract.

## Scene Parsing And Compilation

Parser changes:

- allow `rotation_degrees` in `[environment]`.
- reject non-finite `rotation_degrees`.
- reject negative `strength`.
- require `path` for `type = "hdri"` either in parser validation or compiler
  diagnostics. Compiler validation is acceptable if it matches existing scene
  error style.

Compiler changes:

- `none` and `constant` environments keep current behavior.
- `hdri` environments require a non-empty `.hdr` path.
- `.hdr` is loaded through `LoadHdrTexture()`.
- compiled environment stores strength, rotation, texture reference, and
  distribution reference.
- compile diagnostics include clear path and loader failure information.

Scene syntax remains `type = "hdri"` because the enum already exists. An alias
such as `type = "image"` can be considered later if environment types broaden.

## Error Handling

- `type = "hdri"` with no `path` produces an error.
- `path` with a non-`.hdr` extension produces an error.
- missing HDRI files produce an error with the requested path.
- stb loading failures include `stbi_failure_reason()` when available.
- invalid dimensions, empty texels, and non-finite texels produce errors.
- negative `strength` produces an error.
- `strength = 0` is allowed and contributes black environment light.
- non-finite `rotation_degrees` produces an error.
- all-black HDRI data is allowed and must not cause NaN, Inf, or division by
  zero in PDF code.

## Tests

Add or update tests in these layers:

- `tests/scene_parser_tests.cpp`
  - parses `environment.rotation_degrees`.
  - rejects unknown environment fields.
  - rejects negative environment strength.
- `tests/texture_tests.cpp`
  - loads a tiny `.hdr` fixture as linear floating-point RGB.
  - rejects wrong extension for HDR loading.
  - confirms PNG sRGB conversion behavior still works.
- `tests/environment_tests.cpp` or equivalent render tests
  - direction-to-UV and UV-to-direction mapping round-trip for cardinal
    directions.
  - rotation changes horizontal lookup predictably.
  - bright texels receive higher sampling probability than dark texels.
  - PDF is positive for sampleable HDRI directions.
  - all-black HDRI distribution remains finite.
- `tests/render_scene_tests.cpp`
  - compiler accepts a valid HDRI environment.
  - compiler rejects missing HDRI path.
  - compiler rejects non-HDR extension.
  - compiled `RenderScene` carries environment texture and rotation state.
- `tests/cpu_path_tracer_tests.cpp`
  - camera miss sees HDRI radiance.
  - mirror reflects HDRI radiance.
  - diffuse surface receives direct environment lighting.
  - occluder blocks direct environment shadow rays.
  - BSDF-sampled environment hit applies environment MIS for non-delta previous
    bounces.

Use a tiny committed HDR fixture for deterministic tests. Large or visually
pleasing HDRIs should stay as local manual assets unless their licenses are
explicitly accepted and documented.

## Manual Verification

Add a compact scene such as:

```text
scenes/examples/hdri_lighting_showcase.toml
```

The scene should render with the CPU path integrator and show that HDRI light
affects visible geometry, not only the background. A small checked-in HDR
fixture is sufficient for the automated showcase. A real studio HDRI can be
used locally for prettier manual renders after license review.

Expected command shape:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\hdri_lighting_showcase.toml --backend cpu
```

## Success Criteria

- `type = "hdri"` no longer fails compilation for valid `.hdr` files.
- HDRI miss rays, mirror reflection, and diffuse environment lighting work in
  the CPU path integrator.
- Environment direct sampling uses the existing render sampler and
  `render.light_samples`.
- HDRI importance sampling prefers bright texels and uses solid-angle PDFs.
- BSDF-sampled environment hits use MIS where there is a competing explicit
  environment sample.
- Constant environment behavior remains compatible with existing tests.
- Area-light sampling and MIS continue to pass existing tests.
- No NaN or Inf is produced for zero-strength or all-black environments.
- README and architecture docs describe HDRI environment importance sampling and
  remaining limitations.

## Future Work

- Alias table or hierarchical distributions if the first distribution structure
  is too slow for large HDRIs.
- Environment mipmaps and rough-specular filtered lookup.
- Multiple importance sampling across area lights, mesh lights, and
  environments through a unified light interface.
- Portal lights for indoor HDRI-lit scenes.
- Sun/sky analytic environment models.
- LDR environment map support with explicit color-space rules.
- EXR input and output.
- CUDA environment sampling parity.

## Implementation Status

Implemented in HDRI Environment Importance Sampling v1:

- Radiance `.hdr` loading through stb floating-point decode.
- Equirectangular HDRI evaluation with horizontal rotation.
- Luminance- and solid-angle-weighted environment importance distribution.
- CPU path tracer miss, direct environment lighting, shadow visibility, and
  BSDF-to-environment MIS integration.
- Parser, compiler, texture loader, environment module, path tracer, and CLI
  smoke tests.
