# YaoRay Large glTF Sponza v1 Design

## Summary

This slice makes YaoRay attempt a larger native glTF scene without building an
FBX/DDS conversion pipeline first. The target is Khronos `Sponza`, downloaded
locally under `external/assets/large-gltf/sponza/` and excluded from Git.

The goal is not final image quality. The goal is to prove the current glTF asset
layer, render compiler, CPU BVH preparation, and CPU path tracer can handle a
scene that is meaningfully larger than FlightHelmet, while adding the smallest
missing compatibility feature needed by Sponza: JPEG texture loading.

## Goals

- Download Khronos `Models/Sponza/glTF/` into a local ignored asset directory.
- Keep the Sponza model and textures out of Git.
- Support `.jpg` and `.jpeg` image files in the same texture loader path as PNG.
- Preserve the existing per-slot color-space policy: base-color and emissive
  JPEG/PNG images load as sRGB; normal, occlusion, and metallic-roughness images
  load as linear data.
- Add a local Sponza TOML example or documented template that can be run after
  the asset is downloaded.
- Add scene statistics that make large-scene runs debuggable:
  triangles, materials, textures, estimated texture memory, BVH build time, and
  render time.
- Run at least one CPU smoke render of Sponza at low resolution/sample count.
- Keep default CTest fast and deterministic; Sponza must remain manual-only.

## Non-Goals

- No Bistro FBX conversion in this slice.
- No FBX loader.
- No DDS/TGA loader.
- No KTX/KTX2/Basis/AVIF support.
- No embedded-image GLB work.
- No material extension implementation.
- No exact glTF PBR conformance.
- No mipmaps, anisotropic filtering, or texture LOD.
- No default test that downloads or renders Sponza.
- No Sponza model files committed to Git.

## Asset Target

Primary target:

```text
KhronosGroup/glTF-Sample-Assets
Models/Sponza/glTF/Sponza.gltf
```

Expected local location:

```text
external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf
```

The source model is appropriate for this slice because it is a real architectural
scene, is already in native glTF form, includes many external JPG/PNG texture
files, and avoids the FBX/DDS conversion problem in the official Bistro package.
Its README states that tangents have been computed with MikkTSpace and that
images were repacked into glTF material layouts.

References:

- `https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza`
- `https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza/glTF`

## Download Workflow

Use a sparse clone or partial clone into a local ignored directory:

```bash
external/assets/large-gltf/sponza/
```

The workflow should fetch only:

```text
Models/Sponza/glTF/
Models/Sponza/LICENSE.md
Models/Sponza/README.md
Models/Sponza/metadata.json
```

The repository already ignores `external/assets/`, so the downloaded model stays
local. The committed repo should contain only documentation, scripts, and scene
templates needed to reproduce the local setup.

## JPEG Texture Loading

The current image loader is named around PNG. This slice should generalize it to
an LDR texture loader while preserving existing behavior.

Recommended API:

```cpp
TextureLoadResult LoadLdrTexture(
    const std::filesystem::path& path,
    TextureColorSpace color_space
);
```

Behavior:

- Accept `.png`, `.jpg`, and `.jpeg`.
- Always request RGBA output from `stb_image`.
- Preserve alpha when present; JPEG alpha defaults to `1.0`.
- Apply sRGB transfer only to RGB channels when `TextureColorSpace::Srgb`.
- Leave RGB and alpha normalized directly when `TextureColorSpace::Linear`.
- Keep `LoadPngTexture()` as a compatibility wrapper if that avoids broad churn,
  or replace call sites when the diff stays contained.

Tests should cover:

- Existing PNG behavior still passes.
- JPEG fixture loads successfully.
- JPEG sRGB and linear loads differ in at least one RGB channel.
- JPEG alpha is `1.0` for all texels.
- The scene compiler accepts glTF texture images with `.jpg`/`.jpeg` paths.

## Scene Statistics

Large-scene work needs observability before deeper optimization. The CLI should
print enough compile and render stats to tell whether failure is in loading,
compilation, BVH build, or rendering.

Add or expose:

- Compiled triangle count.
- Material count.
- Texture count.
- Estimated texture memory in MiB from decoded `RenderTexture` texels.
- BVH build time from CPU prepare.
- Render elapsed time from the backend result.

The current CLI already prints triangles, render elapsed time, BVH nodes, BVH
depth, ray counts, and triangle tests. This slice should add the missing
material/texture/memory/prepare-time pieces without changing the backend split:
CPU prepare owns BVH timing; render IR owns decoded texture data.

## Sponza Scene Template

Add a committed TOML template that references the expected local Sponza path.
Because the model is ignored, this scene is a manual example rather than a CTest
fixture.

Recommended path:

```text
scenes/examples/local_sponza.toml
```

The scene should use conservative defaults for a first CPU smoke render:

- `backend = "cpu"`
- `integrator = "path"`
- low resolution, such as `320x180` or `400x225`
- low `spp`, such as `1` to `4`
- `max_depth` around `4`
- constant or HDRI environment
- at least one area light, because Sponza itself does not provide punctual
  lights to YaoRay in this slice

The example should be documented as requiring the local download first.

## Error Handling

Large assets must fail with actionable diagnostics.

Expected diagnostics:

- Missing Sponza asset path: point to the expected local download directory.
- Unsupported image extension: include the texture path and accepted extensions.
- Failed image decode: include the texture path and `stb_image` reason when
  available.
- Unsupported glTF extension: warn only when the feature can be ignored safely.
- Missing texture file: report the exact resolved path.

Do not turn Sponza-specific quirks into hidden special cases. Any fix should be
general enough for another external-file glTF scene.

## Testing Strategy

Automated tests stay small:

- Unit tests for JPEG texture loading.
- Scene compiler fixture using a tiny glTF with a JPEG base-color texture.
- Existing PNG/glTF/FlightHelmet tests remain passing.
- No test downloads Sponza.
- No default CTest renders Sponza.

Manual verification:

```bash
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
```

The manual run should report scene stats and produce a non-empty PNG. Visual
quality can be rough; the success criterion is loading and rendering a large
native glTF scene without crashing.

## Risks

- Sponza may expose loader assumptions around JPG extension casing, URI
  escaping, or sampler state.
- CPU memory usage may be high because all textures decode to RGBA float.
- Rendering may be noisy or poorly lit until lighting and camera defaults are
  tuned.
- Without mipmaps, distant textured surfaces may alias.
- Texture memory stats may make the need for a later compact texture format
  refactor obvious.

## Success Criteria

- Sponza is downloaded locally and remains ignored by Git.
- JPEG/JPG textures load through the renderer texture pipeline.
- A local Sponza scene compiles and renders on CPU.
- CLI output shows enough stats to reason about large-scene cost.
- Full default CTest remains passing.
- The implementation does not add Bistro, Sponza, or other large model files to
  the repository.
