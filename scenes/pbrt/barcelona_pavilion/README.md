# Barcelona Pavilion (mmp/pbrt-v4-scenes)

YaoRay's M2 anchor scene. This directory intentionally stays empty in git —
the asset lives in Matt Pharr's `pbrt-v4-scenes` repository under Git LFS;
we link to it instead of redistributing.

## Download

`pbrt-v4-scenes` requires Git LFS. The full repo is large, so a
sparse-checkout that pulls only `barcelona-pavilion/` is recommended:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set barcelona-pavilion
git lfs pull --include="barcelona-pavilion/**"
cd ..
mv pbrt-v4-scenes-tmp/barcelona-pavilion ./barcelona-pavilion
rm -rf pbrt-v4-scenes-tmp
```

The unpacked tree under `external/assets/pbrt/barcelona-pavilion/` is
gitignored via the project-wide `external/assets/` rule.

Users who also want the surrounding vegetation (Pavilion's `geometry.pbrt`
references trees and shrubs from a sibling `landscape/` subtree) can run
`git sparse-checkout add landscape` in the tmp dir before the LFS pull,
then move `landscape` into `external/assets/pbrt/` next to
`barcelona-pavilion`. This adds significant download size. Without it,
the building still renders cleanly — the missing vegetation files emit
documented Warnings (see *Documented degradations* below).

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/barcelona-pavilion/pavilion-day.pbrt --backend cpu
```

The output PNG lands at the path declared in the scene's `Film "string
filename"` directive. The bundled scene targets a 1600×850 EXR at
256 spp — to reproduce this repo's reference image at 1280×720 / 64 spp,
temporarily edit the scene's `Film` and `Sampler` blocks before
rendering, then revert.

## What works in M2

- `pavilion-day.pbrt` parses, compiles, and renders end-to-end with no
  `Error:` diagnostics from our compiler. The four M1-era blockers
  cleared in this slice (M2 Slice 3 Patches 2a–2d).
- All declared materials resolve to YaoRay's BSDFs: `dielectric` (glass
  walls), `conductor` (chrome cross-piece column), `coateddiffuse`
  (marble + travertine), `diffuse` (Barcelona chair frames, statue,
  benches). One `MakeNamedMaterial` of kind `measured` (`leather_white`)
  follows the M1 substitution policy and degrades to conductor with a
  Warning.
- Several imagemap textures (PNG / JPG for marble, travertine, wood,
  concrete) load via the M1 texture loaders and bind to material
  `reflectance` and `displacement` slots.
- The `scale` texture class — Pavilion uses 7 of these for
  brightness-multiplied imagemaps (concrete walls × 0.64, grass × 0.5,
  pavement × 0.64 / 0.7, water bump × 0.005) — compiles as a
  compile-time fold (M2 Slice 3 Patch 2d): the inner imagemap's texels
  are multiplied by the scale factor at compile time, producing an
  ordinary `RenderTexture` binding indistinguishable from a regular
  imagemap at sample time. Per-channel RGB scale arrays are not yet
  handled (single-float only), but Pavilion only uses single-float
  scales.
- The `Sampler "halton"` directive degrades to `independent` per the
  M1 anticipated policy.
- The SAH binned + parallel BVH (M2 Slices 1 & 2) builds 87,263 nodes
  over the Pavilion-only geometry in 0.07s (`Prepare seconds`). Render:
  1280×720 / 64 spp completes in ~38s on the dev sandbox (Windows,
  MSVC 19.51, Release, 11-thread CPU PT); rays trace at ~4.6 million
  per second.

A reference render at 1280×720 / 64 spp lives at
`docs/architecture/barcelona-pavilion.png`. Composition: the
cantilevered concrete roof (Pavilion's signature horizontal mass),
polished travertine marble walls and floor, the chrome cross-piece
column, the reflective water pool with stone tile floor visible
through the water, Barcelona chairs in the interior, and the
silhouette of Georg Kolbe's *Alba* statue visible through the glass
walls (the pool reflects the statue and the building edges). The sky
is uniform white (see below); the surrounding park trees are absent
(landscape/ subtree not bundled).

## Documented degradations (Pavilion-specific)

These are gaps not in the M1 anticipated table — each is M2-spec-compliant
graceful degradation with a documented Warning emitted at compile time.
None of them block rendering; they affect specific visual aspects.

- **`landscape/` vegetation imagemap textures missing** (40 occurrences):
  Pavilion's `geometry.pbrt` references trees and foliage textures under
  `../landscape/textures/*.png`. Users who didn't pull the sibling
  `landscape/` subtree see Warnings + a neutral mid-grey (0.5, 0.5, 0.5)
  fallback texel (M2 Slice 3 Patch 2a). *Render effect*: vegetation
  surfaces (if their meshes are present) render as flat mid-grey.
- **`landscape/` vegetation PLY meshes missing** (283 occurrences): same
  root cause — `geometry.pbrt` references PLY meshes under
  `../landscape/geometry/*.ply`. Missing meshes produce Warnings and
  contribute no geometry (M2 Slice 3 Patch 2b). *Render effect*: trees
  and shrubs are absent from the scene; the building renders normally.
- **`sky.exr` HDR envmap** (1 occurrence): Pavilion's
  `LightSource "infinite"` uses OpenEXR. YaoRay's M2 HDR support covers
  `.hdr` and `.pfm`; `.exr` is on the M5+ exploratory list. The patch
  substitutes a 1×1 white texture so the environment importance sampler
  still runs uniformly (M2 Slice 3 Patch 2c); the scene's `L` and
  `scale` parameters apply normally. *Render effect*: the sky lacks
  `sky.exr`'s directional gradient (no warm/cool sun bias), but indirect
  illumination from the uniform white sky still drives the marble +
  chrome highlights and the water reflection.

## Camera convention

Pavilion uses the same world-to-camera CTM convention already
documented in `scenes/pbrt/dining_room/README.md`. No new convention
note required.
