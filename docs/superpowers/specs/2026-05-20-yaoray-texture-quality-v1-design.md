# YaoRay Texture Quality v1 Design

## Context

YaoRay now renders textured OBJ and static glTF/GLB assets through the CPU path
tracer. The current texture path is intentionally minimal: PNG RGB values are
stored directly as floats, sampling is nearest-filtered, UVs always repeat, and
glTF sampler state is ignored. This is enough to prove the asset path, but it is
not enough for believable imported models. Even simple assets such as Duck and
future PBR samples need correct color-space conversion and smoother texture
reconstruction before material or lighting improvements are easy to evaluate.

This slice improves texture correctness while staying focused on offline
rendering foundations. It does not add realtime-engine features; it makes asset
textures enter the path tracer in a more physically coherent way.

## Goals

- Add bilinear texture filtering.
- Keep nearest filtering as an explicit debug/test path.
- Convert PNG albedo/base-color RGB from sRGB to linear when loading.
- Add texture wrap modes: repeat, clamp-to-edge, and mirrored repeat.
- Import glTF `sampler.wrapS` and `sampler.wrapT` for base-color textures.
- Use one render-level texture sampling entry point from the CPU path tracer.
- Preserve existing OBJ texture behavior except for improved filtering and
  color-space conversion.
- Add focused tests for filtering, wrapping, sRGB conversion, glTF sampler
  import, scene compilation, and CPU path tracer texture usage.
- Keep Duck as a local manual verification asset unless we later decide to
  accept and document its license for repository inclusion.

## Non-Goals

- No mipmaps.
- No anisotropic filtering.
- No normal maps.
- No roughness, metallic, occlusion, or emissive texture maps.
- No HDRI or environment lighting.
- No environment MIS.
- No CUDA texture parity.
- No user-facing TOML syntax for choosing texture filter mode in this slice.
- No broad texture resource-system refactor.

## User Story

A user can render existing textured OBJ scenes, the checked-in glTF texture
example, and a local Duck glTF scene with smoother texture sampling and more
correct base-color energy. The renderer should continue to use linear color for
shading and still write display-ready PNG output through the existing Film and
tone-mapping path.

## Architecture

Texture quality belongs in `yaoray_render`, not in individual integrator logic.
The CPU path tracer should ask for a sampled material albedo and should not need
to know whether the texture is nearest, bilinear, repeat, clamp, or mirrored.

Extend `RenderTexture` with sampler state:

```cpp
enum class TextureFilter {
    Nearest,
    Bilinear
};

enum class TextureWrap {
    Repeat,
    ClampToEdge,
    MirroredRepeat
};

struct RenderTexture {
    int width = 0;
    int height = 0;
    std::vector<Color3f> texels;
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};
```

Keep `SampleTextureNearest()` for explicit tests and debugging, add
`SampleTextureBilinear()`, and route production sampling through:

```cpp
Color3f SampleTexture(const RenderTexture& texture, Vec2f uv);
```

`SampleTexture()` dispatches by `texture.filter`. The CPU path tracer replaces
direct calls to `SampleTextureNearest()` with `SampleTexture()`.

## Sampling Semantics

Sampling should treat UV coordinates as normalized texture coordinates. The
current repeat behavior remains available:

- `Repeat`: fractional repeat for positive and negative coordinates.
- `ClampToEdge`: clamp to `[0, 1]` edge texels.
- `MirroredRepeat`: mirror every integer interval.

Nearest sampling should remain compatible with the existing tests for repeat
coordinates. Bilinear sampling should interpolate four neighboring texels in
linear color space. Single-pixel textures should return their only texel for all
filter and wrap modes.

The default filter for loaded textures is `Bilinear`. There is no TOML syntax for
overriding this yet; a later texture-resource milestone can add user-facing
controls if needed.

## Color Space

PNG albedo/base-color textures should be decoded from sRGB into linear RGB at
load time:

```text
srgb <= 0.04045 -> linear = srgb / 12.92
srgb >  0.04045 -> linear = ((srgb + 0.055) / 1.055) ^ 2.4
```

`RenderTexture::texels` stores linear values. BSDF evaluation, light transport,
and Film accumulation therefore stay in linear space. The existing output path
continues to handle tone mapping and display conversion.

Alpha stays ignored in this slice, matching current material behavior.

## Imported Asset Data Flow

`ImportedMaterial` currently carries one diffuse/base-color texture path. Extend
it minimally with sampler state instead of introducing a full imported texture
resource graph:

```cpp
TextureWrap diffuse_texture_wrap_s = TextureWrap::Repeat;
TextureWrap diffuse_texture_wrap_t = TextureWrap::Repeat;
```

This keeps the change aligned with the current flat imported-material model.
When the glTF loader sees a `baseColorTexture`, it should inspect the referenced
glTF texture's sampler:

- `wrapS = 10497` maps to `Repeat`.
- `wrapS = 33071` maps to `ClampToEdge`.
- `wrapS = 33648` maps to `MirroredRepeat`.
- missing sampler or missing wrap value defaults to `Repeat`.
- unsupported wrap constants emit a warning and default to `Repeat`.

Apply the same mapping for `wrapT`. OBJ MTL import continues to default to
repeat wrapping.

During scene compilation, `LoadTextureIndex()` should receive the imported
sampler state and apply it to the loaded `RenderTexture`. Texture caching must
include sampler state, not only file path, because the same image path sampled
with different wrap modes is a different render texture.

## Error Handling

- Empty or invalid textures still sample black.
- Invalid texture indices still fall back to material albedo at call sites.
- Unsupported glTF wrap values should be warnings, not hard errors.
- Non-PNG texture files remain unsupported and produce the existing clear error.
- Existing missing-file diagnostics remain unchanged.

## Tests

Add or update tests in these layers:

- `tests/texture_tests.cpp`
  - nearest repeat behavior remains intact.
  - bilinear blends a 2x2 texture at the center.
  - clamp-to-edge clamps negative and greater-than-one UVs.
  - mirrored repeat maps adjacent intervals correctly.
  - sRGB conversion maps a known encoded byte value to expected linear output.
  - single-pixel textures sample consistently.
- `tests/assets_tests.cpp`
  - glTF loader preserves base-color texture wrap modes from `SimpleTexture`
    (`MIRRORED_REPEAT` in both axes).
  - unsupported wrap constants warn and default to repeat through a small fixture
    if practical.
- `tests/render_scene_tests.cpp`
  - scene compiler propagates imported wrap modes into `RenderTexture`.
  - texture cache distinguishes same path with different wrap modes if a compact
    fixture can express that without bloating assets.
- `tests/cpu_path_tracer_tests.cpp`
  - textured material sampling uses unified `SampleTexture()` behavior.
  - existing textured path tracer behavior remains deterministic.

Manual verification:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\gltf_textured_asset.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\textured_quad.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

If local Duck files are present, also run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\duck_gltf.toml --backend cpu
```

Duck remains a local manual asset unless its license is intentionally accepted
and documented in a separate commit.

## Success Criteria

- Loaded PNG albedo/base-color textures are stored in linear RGB.
- CPU path tracing samples textures through the unified render-level API.
- Bilinear filtering is the default loaded-texture behavior.
- Repeat, clamp-to-edge, and mirrored repeat are implemented and tested.
- glTF `wrapS` and `wrapT` affect base-color texture sampling in compiled
  `RenderScene` data.
- Existing OBJ, glTF, Cornell, material showcase, and CLI tests continue to pass.
- README and architecture docs describe texture quality v1 and remaining limits.

## Future Work

- Mipmaps and texture LOD.
- Anisotropic filtering.
- User-authored texture sampler settings in TOML.
- Normal maps and tangent-space shading.
- Roughness, metallic, occlusion, and emissive textures.
- HDRI loading and environment sampling.
- Environment MIS.
- CUDA texture sampling parity.
