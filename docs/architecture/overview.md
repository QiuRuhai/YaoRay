# YaoRay Architecture Overview

YaoRay is a physically-based offline path tracer that consumes PBRT v4
scene files and produces HDR images via a multi-threaded CPU backend.
A CUDA backend is planned for M2+.

## Two-Layer Pipeline

```text
PBRT v4 scene  ──CompilePbrtScene──▶  RenderSceneIR  ──Backend.Prepare──▶  Renderable
   (.pbrt)                              (flat tables)                       (BVH + buffers)
```

The PBRT layer parses a `.pbrt` file into `PbrtScene` — a faithful
representation of the directives and parameters as written. The render
layer consumes `PbrtScene` and emits `RenderSceneIR`, a flat,
GPU-friendly layout: indexed vertices, primitives, materials,
textures, and lights. The CPU backend's `Prepare` step turns
`RenderSceneIR` into a `CpuPreparedScene` (BVH + texture cache + light
distributions); `Render` then ray-traces against that prepared scene.

This is the two-layer design introduced by the M0 architecture reset,
which dropped the older three-layer `TOML + SceneWorld + frontends`
pipeline along with OBJ and glTF loaders.

## Supported PBRT v4 Surface (M1)

**Geometry:** `trianglemesh` (P / N / uv / S), `plymesh`, `sphere`.

**Materials:** `diffuse` / `matte`, `conductor` / `metal`, `dielectric` /
`glass`, `thindielectric`, `coateddiffuse`, `coatedconductor`,
`diffusetransmission`. Texture binding via `"texture <param>" ["name"]`
on `reflectance`, `eta`, `k`, `uroughness`, `vroughness`, and the
coating-layer parameters. Normal maps via `"string normalmap"`.

**Materials with documented degradation:** `subsurface` (→ diffuse with
declared reflectance), `measured` (→ default conductor), `hair` (→ grey
diffuse), `mix` (→ approximate diffuse). Each emits a named Warning at
compile time.

**Lights:** `LightSource "infinite"` (HDRI environment with importance
sampling), `LightSource "point"`, `LightSource "distant"`,
`LightSource "spot"`, `AreaLightSource "diffuse"`.

**Textures:** `Texture "imagemap"` (PNG / JPEG / TGA / BMP / HDR / PFM),
`Texture "constant"`. Wrap modes: `repeat` and `clamp` (`black`
degrades to clamp). Color space auto-detected from extension, explicit
`"string encoding"` overrides.

## Backend

A single-threaded reference renderer
(`src/backends/cpu/cpu_debug_renderer.cpp`) plus a production
multi-threaded path tracer
(`src/backends/cpu/cpu_path_tracer.cpp`) with:

- Median-split BVH over the triangle table, plus a linear pass over
  analytic spheres after the BVH walk.
- Multiple importance sampling combining BSDF, area-light, and
  environment-light samples.
- Delta-light handling (no MIS vs BSDF) for point, distant, and spot.
- Russian-roulette path termination starting at depth 3.
- ACES, Reinhard, and identity tone mappers.

## Showcase Scenes

| Scene | Purpose |
|---|---|
| `scenes/pbrt/hello_emissive/` | Smallest end-to-end demo: trianglemesh + area light (M0 sanity). |
| `scenes/pbrt/cornell_box_pbrt/` | Slice 1 — `Shape "sphere"` and `LightSource "point"`. |
| `scenes/pbrt/material_studio/` | Slice 2 — HDRI environment + material parameter textures. |
| `scenes/pbrt/texture_test/` | Slice 3 — wrap modes and normal-map data path. |
| `scenes/pbrt/dining_room/README.md` | Slice 4 — Bitterli's PBRT v4 dining-room (downloaded; gitignored). |

The first four scenes are committed and exercised by CTest. The
dining-room asset is large and CC-BY-licensed by its author, so the
repo links to the download workflow rather than redistributing.

## What's not in M1

- Spectral rendering (RGB only).
- Volumetrics / media.
- Adaptive sampling.
- CUDA backend.
- IES light profiles.
- Subdivision surfaces, displacement, hair primitives, curves.
- Alternative samplers (`halton` / `sobol` / `pmj02bn`); they parse
  but degrade to `independent` at compile time.
- True black-border wrap (degrades to clamp).
- Auto-tangent generation when trianglemesh has `uv` but no `S`.
- Camera-matrix interop with PBRT-v4 reference scenes that use
  non-`LookAt` `Transform` directives: a small camera-convention
  follow-up tracked against `scenes/pbrt/dining_room/README.md`.
