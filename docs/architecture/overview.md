# YaoRay Architecture Overview

YaoRay is a physically-based offline path tracer that consumes PBRT v4
scene files and produces HDR images via a multi-threaded CPU backend.
A CUDA backend is on the roadmap (see below).

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

## Supported PBRT v4 Surface

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

**Textures:** `Texture "imagemap"` (PNG / JPEG / TGA / BMP / HDR / PFM / EXR),
`Texture "constant"`. Wrap modes: `repeat` and `clamp` (`black`
degrades to clamp). Color space auto-detected from extension, explicit
`"string encoding"` overrides. EXR (OpenEXR) loads via vendored
tinyexr and goes through the same HDR envmap path as `.hdr` / `.pfm`.

## Backend

A single-threaded reference renderer
(`src/backends/cpu/cpu_debug_renderer.cpp`) plus a production
multi-threaded path tracer
(`src/backends/cpu/cpu_path_tracer.cpp`) with:

- SAH-binned BVH (12 buckets, c_T = 0.5) with deterministic parallel
  top-down construction, plus a linear pass over analytic spheres
  after the BVH walk.
- Multiple importance sampling combining BSDF, area-light, and
  environment-light samples.
- Delta-light handling (no MIS vs BSDF) for point, distant, and spot.
- Russian-roulette path termination starting at depth 3.
- ACES, Reinhard, and identity tone mappers.

## Showcase Scenes

| Scene | Purpose |
|---|---|
| `scenes/pbrt/hello_emissive/` | Smallest end-to-end demo: trianglemesh + area light. |
| `scenes/pbrt/cornell_box_pbrt/` | `Shape "sphere"` + `LightSource "point"` + mirror/glass spheres. |
| `scenes/pbrt/material_studio/` | HDRI environment + each supported BSDF on a row of spheres. |
| `scenes/pbrt/texture_test/` | Texture wrap modes + normal-map data path. |
| `scenes/pbrt/dining_room/README.md` | Bitterli's PBRT v4 dining-room (downloaded; gitignored). |
| `scenes/pbrt/barcelona_pavilion/README.md` | mmp's PBRT v4 Barcelona Pavilion — M2 anchor scene (downloaded via `git lfs`; gitignored). |

The first four scenes are committed and exercised by CTest. The
dining-room and Pavilion assets are large and live under permissive
licenses elsewhere, so the repo links to download workflows rather
than redistributing.

## Not currently supported

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

## Roadmap

```
M1+M2 (done) ──▶ M3 (Advanced Materials) ──▶ M4 (CUDA) ──▶ M5+ (by interest)
```

| Milestone | Anchor scene | Headline | Status |
|---|---|---|---|
| **M2** | `barcelona-pavilion` (mmp PBRT v4) | SAH BVH + parallel BVH build; dining-room renders ≥ 2× faster | done |
| **M3** | TBD — `ganesha` / `sportscar` / `killeroo-coated` | Real `subsurface` + `measured` + nested `layered` materials | sketch |
| **M4** | `dining-room` < 10 s, `barcelona-pavilion` < 1 min | CUDA backend filling the existing `RenderBackendKind::Cuda` slot | sketch |
| **M5+** | by interest | Denoiser, volumetrics, hair, subdivision, spectral, polish | exploratory |

See [`docs/superpowers/specs/2026-05-28-yaoray-post-m1-roadmap-design.md`](../superpowers/specs/2026-05-28-yaoray-post-m1-roadmap-design.md)
for the full roadmap design, including detailed M2 acceptance criteria,
risk register, and rationale for the milestone ordering.
