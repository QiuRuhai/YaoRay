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
- geometry-only OBJ asset import for small mesh scenes
- BVH acceleration over compiled render triangles for the CPU debug renderer
- TOML named diffuse/emissive materials with instance material binding
- scene-authored inline quad assets and a Cornell Box geometry smoke scene
- selectable render integrators with a first deterministic CPU path tracer
- deterministic tile-threaded CPU path tracing with CLI throughput stats

Final path tracing quality, spectral rendering, texture import, imported asset materials, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.

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
```

The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the CPU path tracer with diffuse bounce, deterministic sampling, random or stratified area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, explicit emissive hits, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). `render.sampler` controls path sample placement: `"independent"` is the default baseline, while `"stratified"` stratifies pixel jitter and direct area-light UV samples. `render.light_samples` controls how many direct area-light samples the path integrator averages per light per hit; the default is `1`, and higher values trade more shadow rays for lower soft-shadow and direct-light noise. The CLI reports actual threads plus samples/sec and rays/sec. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render. The path integrator still does not implement MIS, Russian roulette, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, or advanced material models.
