# YaoRay

YaoRay is a learning-oriented, engineering-grade offline path tracer focused on physically based rendering, clean architecture, and future CUDA acceleration.

This repository is a rewrite of the previous ToyRender experiment. The old code is preserved on `archive/toyrender-before-yaoray`; the new project starts from a clean architecture.

## Current Status

The foundation slice provides:

- CMake project structure
- small CTest-based test harness
- core vector, ray, and bounds primitives
- Film accumulation and tone mapping basics
- CLI help/version shell

Scene files, asset import, path tracing, and CUDA backend support are planned as separate implementation slices.

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
```
