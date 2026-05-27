# YaoRay

YaoRay is a learning-oriented, engineering-grade offline path tracer focused on physically based rendering, clean architecture, and future CUDA acceleration.

This repository is a rewrite of the previous ToyRender experiment. The old code is preserved on `archive/toyrender-before-yaoray`; the new project starts from a clean architecture.

## Current Status

The foundation slices provide:

- CMake project structure
- small CTest-based test harness
- core vector, ray, and bounds primitives
- Film accumulation and tone mapping basics
- CLI help/version shell
- TOML scene parsing and validation through `yaoray render`
- initial backend-neutral `RenderSceneIR` compilation through the `yaoray_render` module
- temporary `builtin:triangle` scenes for compiler and CLI verification
- CPU rendering with deterministic area-light direct lighting and BVH shadow rays
- PNG output for renderable scenes, with PPM still available for debug/test output
- render backend dispatch through a common backend interface
- shared `AssetResource` import for OBJ and static glTF/GLB assets through `yaoray_assets`
- RGBA PNG texture loading with per-slot color-space policy, bilinear filtering, and repeat/clamp/mirrored wrap support
- glTF PBR compatibility coverage for base color, metallic-roughness, normal, occlusion, emissive, alpha mask metadata, double-sided metadata, imported tangents, and generated tangents
- CPU material sampling for glTF base-color alpha, metallic-roughness, emissive, and tangent-space normal maps
- CPU alpha-mask visibility for camera, indirect, and shadow rays
- CPU backend BVH preparation over compiled render triangles
- TOML named diffuse, emissive, mirror, metal, plastic, and dielectric/glass materials with instance material binding
- scene-authored inline quad assets and a Cornell Box geometry smoke scene
- selectable render integrators with a first deterministic CPU path tracer
- deterministic tile-threaded CPU path tracing with CLI throughput stats
- Radiance `.hdr` environment maps with equirectangular lookup and importance-sampled CPU path lighting

Final path tracing quality, spectral rendering, nested media, exact glTF PBR BRDF parity, advanced glTF extensions, advanced BVH split methods, HDR output, and real CUDA/OptiX backend support are planned as separate implementation slices.

## Build

macOS/Linux:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

## Run

macOS/Linux:

```bash
./build/yaoray --help
./build/yaoray --version
./build/yaoray render scenes/pbrt/hello_emissive/hello_emissive.pbrt --backend cpu
./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu
```

Windows:

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\pbrt\hello_emissive\hello_emissive.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\cornell_box_pbrt\cornell_box_pbrt.pbrt --backend cpu
```

The `render` command currently parses, compiles backend-neutral render input, lets the selected backend prepare runtime data, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests.

`render.integrator = "path"` selects the CPU path tracer with diffuse bounce, perfect mirror reflection, smooth/rough/thin dielectric glass, Beer-Lambert absorption for thick dielectric paths, transparent colored direct shadows through dielectric glass, deterministic sampling, random or stratified XZ-rectangle area-light surface sampling, area-light MIS, MIS-weighted BSDF-sampled emissive hits, HDRI environment lookup, HDRI direct environment sampling through a luminance-weighted equirectangular distribution, BSDF-to-environment MIS, fixed-policy Russian roulette path termination with `render.max_depth` as a hard limit, diffuse texture sampling, optional sample radiance clamping through `render.radiance_clamp`, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). `render.sampler` controls path sample placement: `"independent"` is the default baseline, while `"stratified"` stratifies pixel jitter and direct-light UV samples. `render.light_samples` controls how many direct area-light or environment samples the path integrator averages per hit; the default is `1`, and higher values trade more shadow rays for lower soft-shadow and direct-light noise. The CLI reports actual threads plus samples/sec and rays/sec.

The shared `AssetResource` asset layer imports OBJ and static glTF/GLB assets through `yaoray_assets`. OBJ assets can carry vertex normals, UV coordinates, and basic MTL diffuse textures through `map_Kd`; glTF/GLB assets can carry static mesh primitives, node transforms, vertex normals, UVs, tangents, base-color RGBA factors and textures, metallic/roughness factors and textures, normal texture scale, occlusion texture strength, emissive factors and textures, alpha mode/cutoff, double-sided metadata, and sampler wrap modes. PNG texture storage is RGBA; base-color and emissive slots are loaded as sRGB, while normal, occlusion, and metallic-roughness slots are loaded as linear data. The CPU backend resolves base-color alpha, metallic-roughness maps, emissive maps, tangent-space normal maps, and alpha-mask visibility for primary, indirect, and shadow rays. Texture v1 still does not implement mipmaps, anisotropic filtering, user-facing filter controls, alpha blending, texture transform extensions, occlusion darkening, exact glTF PBR BRDF parity, or CUDA texture parity. HDRI environment maps are supported for CPU path tracing, but there are no environment mipmaps, portal lights, sun/sky model, EXR support, or CUDA environment sampling yet.

Materials default to `type = "diffuse"`; `type = "mirror"` enables perfect specular reflection, `type = "metal"` supports polished and rough tinted reflection through `roughness`, and `type = "plastic"` adds a simple diffuse plus glossy model through `roughness` and `specular`, while `emission` remains an additive material property. Dielectric materials support smooth, rough, and thin variants; thick dielectric variants can optionally use Beer-Lambert absorption through `absorption_color` and `absorption_distance`. `glass`, `rough_glass`, and `thin_glass` are authoring aliases for the same render material kind.

`scenes/examples/textured_quad.toml` demonstrates the OBJ UV + MTL texture pipeline; `scenes/examples/gltf_textured_asset.toml` demonstrates the small glTF base-color texture pipeline; `scenes/examples/gltf_flight_helmet.toml` demonstrates the committed FlightHelmet glTF PBR compatibility asset; `scenes/examples/material_showcase.toml` demonstrates diffuse, emissive, and mirror materials in a Cornell-style scene; `scenes/examples/material_v2_showcase.toml` adds polished metal, rough metal, and plastic preview objects; `scenes/examples/hdri_lighting_showcase.toml` demonstrates HDRI environment lighting on diffuse and mirror geometry; `scenes/examples/glass_showcase.toml` demonstrates absorbing blue glass, rough amber glass, and thin glass against high-contrast backdrop cards so refraction remains readable. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render. Bistro is documented as a local-only large-scene benchmark in `docs/assets/bistro-local-benchmark.md`; its model files are intentionally not committed.

The path integrator still does not implement user-configurable roulette parameters, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, mipmaps, alpha blending, texture transform extensions, occlusion texture darkening, CUDA materials, or other advanced material models. Dielectric materials do not yet include nested medium stacks, caustic-specific sampling, glTF glass extension import, or CUDA parity. `render.radiance_clamp` is an optional biased preview control for firefly reduction and is disabled by default.
