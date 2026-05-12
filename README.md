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
- CPU debug rendering of compiled triangle scenes to ASCII PPM

Final path tracing quality, asset import, BVH construction, PNG output, and CUDA backend support are planned as separate implementation slices.

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
```

The `render` command currently parses, compiles, and renders CPU debug images to ASCII PPM. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
