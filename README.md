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
- initial `RenderScene` compilation through the `yaoray_render` module
- temporary `builtin:triangle` scenes for compiler and CLI verification
- CPU rendering with deterministic area-light direct lighting and BVH shadow rays
- PNG output for renderable scenes, with PPM still available for debug/test output
- render backend dispatch through a common backend interface
- basic textured OBJ import through vertex normals, `vt`, `mtllib`, `usemtl`, `Kd`, and PNG `map_Kd`
- static glTF/GLB mesh import through tinygltf, including node transforms, positions, normals, UVs, indices, base-color factors, and external PNG base-color textures
- PNG albedo/base-color texture loading with sRGB-to-linear conversion, bilinear filtering, and repeat/clamp/mirrored wrap support
- BVH acceleration over compiled render triangles for the CPU debug renderer
- TOML named diffuse, emissive, mirror, metal, plastic, and dielectric/glass materials with instance material binding
- scene-authored inline quad assets and a Cornell Box geometry smoke scene
- selectable render integrators with a first deterministic CPU path tracer
- deterministic tile-threaded CPU path tracing with CLI throughput stats
- Radiance `.hdr` environment maps with equirectangular lookup and importance-sampled CPU path lighting

Final path tracing quality, spectral rendering, Beer-Lambert absorption, nested media, full glTF PBR material import, advanced texture/material import, advanced BVH split methods, HDR output, and real CUDA backend support are planned as separate implementation slices.

## Build

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

## Run

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\obj_pyramid.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\cornell_box.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\cornell_box_path.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\textured_quad.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\gltf_textured_asset.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\material_showcase.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\material_v2_showcase.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\hdri_lighting_showcase.toml --backend cpu
build\Debug\yaoray.exe render scenes\examples\glass_showcase.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\hdri_lighting_showcase.toml --backend cpu
```

The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests.

`render.integrator = "path"` selects the CPU path tracer with diffuse bounce, perfect mirror reflection, smooth/rough/thin dielectric glass, deterministic sampling, random or stratified XZ-rectangle area-light surface sampling, area-light MIS, MIS-weighted BSDF-sampled emissive hits, HDRI environment lookup, HDRI direct environment sampling through a luminance-weighted equirectangular distribution, BSDF-to-environment MIS, fixed-policy Russian roulette path termination with `render.max_depth` as a hard limit, diffuse texture sampling, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). `render.sampler` controls path sample placement: `"independent"` is the default baseline, while `"stratified"` stratifies pixel jitter and direct-light UV samples. `render.light_samples` controls how many direct area-light or environment samples the path integrator averages per hit; the default is `1`, and higher values trade more shadow rays for lower soft-shadow and direct-light noise. The CLI reports actual threads plus samples/sec and rays/sec.

OBJ assets can carry vertex normals, UV coordinates, and basic MTL diffuse textures through `map_Kd`; glTF/GLB assets can carry static mesh primitives, node transforms, vertex normals, UVs, base color factors, metallic/roughness factors approximated onto current material kinds, emissive factors, external PNG base-color textures, and base-color sampler wrap modes. PNG albedo/base-color textures are stored in linear RGB, sampled with bilinear filtering by default, and support repeat, clamp-to-edge, and mirrored-repeat wrapping. Texture v1 still does not implement mipmaps, anisotropic filtering, normal maps, alpha masks, bilinear user controls, imported roughness/metallic textures, or CUDA texture parity. HDRI environment maps are supported for CPU path tracing, but there are no environment mipmaps, portal lights, sun/sky model, EXR support, or CUDA environment sampling yet.

Materials default to `type = "diffuse"`; `type = "mirror"` enables perfect specular reflection, `type = "metal"` supports polished and rough tinted reflection through `roughness`, and `type = "plastic"` adds a simple diffuse plus glossy model through `roughness` and `specular`, while `emission` remains an additive material property. `type = "dielectric"` adds glass-style reflection and transmission through `ior`, `roughness`, and `thin`; `glass`, `rough_glass`, and `thin_glass` are authoring aliases for the same render material kind.

`scenes/examples/textured_quad.toml` demonstrates the OBJ UV + MTL texture pipeline; `scenes/examples/gltf_textured_asset.toml` demonstrates the glTF base-color texture pipeline; `scenes/examples/material_showcase.toml` demonstrates diffuse, emissive, and mirror materials in a Cornell-style scene; `scenes/examples/material_v2_showcase.toml` adds polished metal, rough metal, and plastic preview objects; `scenes/examples/hdri_lighting_showcase.toml` demonstrates HDRI environment lighting on diffuse and mirror geometry; `scenes/examples/glass_showcase.toml` demonstrates clear glass, rough glass, and thin glass over an HDRI-lit floor. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render.

The path integrator still does not implement user-configurable roulette parameters, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, normal maps, alpha masks, mipmaps, imported roughness/metallic textures, CUDA materials, or other advanced material models. Dielectric materials do not yet include Beer-Lambert absorption, medium stacks, caustic-specific sampling, glTF glass extension import, or CUDA parity.
