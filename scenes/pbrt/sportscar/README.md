# Sportscar (mmp/pbrt-v4-scenes)

YaoRay's M3 measured-BRDF anchor scene, and the scene that completes M3 (layered
+ measured). This directory intentionally stays empty in git — the asset lives in
Matt Pharr's `pbrt-v4-scenes` repository under Git LFS; we link to it instead of
redistributing. Unlike most `pbrt-v4-scenes` entries, the sportscar bundles its
`.bsdf` measured-paint files under `sportscar/bsdfs/` (CC0-licensed), making it
fully self-contained once downloaded.

## Download

`pbrt-v4-scenes` requires Git LFS. The full repo is large, so a sparse-checkout
that pulls only the `sportscar/` subtree is recommended:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set sportscar
git lfs pull --include="sportscar/**"
cd ..
mv pbrt-v4-scenes-tmp/sportscar ./sportscar
rm -rf pbrt-v4-scenes-tmp
```

The unpacked tree under `external/assets/pbrt/sportscar/` is gitignored via the
project-wide `external/assets/` rule.

Note: the scene has two self-contained entrypoints — `sportscar-sky.pbrt` (IBL
via an environment map) and `sportscar-area-lights.pbrt` (bilinear-mesh area
lights). There is no top-level `sportscar.pbrt` wrapper. Each entrypoint includes
`materials.pbrt` and `geometry/geometry.pbrt` and is complete on its own. All
referenced geometry (47 PLY meshes + floor quad), textures (`sky.exr`), and the
six measured `.bsdf` paint files are self-contained within the `sportscar/`
subtree — no sibling dependencies.

## Render

```bash
# From repo root, after building — IBL variant (reference):
./build/Release/yaoray render external/assets/pbrt/sportscar/sportscar-sky.pbrt --backend cpu

# Area-lights variant:
./build/Release/yaoray render external/assets/pbrt/sportscar/sportscar-area-lights.pbrt --backend cpu
```

The output image lands at the path declared in the scene's `Film "string
filename"` directive. The bundled scene targets 1920×1080. To reproduce this
repo's reference image at 1280×720 / 64 spp, temporarily edit the scene's `Film`
and `Sampler` blocks before rendering, then revert.

## What works in M3 (measured phase)

- `sportscar-sky.pbrt` parses, compiles, and renders end-to-end with no `Error:`
  diagnostics (see *Documented degradations* for graceful-degradation Warnings).
- **Real measured automotive-paint BRDFs** — the headline of this scene and the
  completion of M3. All six `.bsdf` measured-paint files load and evaluate via the
  full Dupuy-Jakob pipeline implemented across M3 Measured Slices 1-3:
  - Binary TensorFile reader (Slice 1)
  - `MeasuredBxDF::f` with spectral-to-RGB conversion at 3 wavelengths (Slice 2)
  - Data-driven importance sampling via `PiecewiseLinear2D::Sample` CDF inversion
    (Slice 3)
  - All six files are isotropic (`n_phi_i = 1`) and pass the updated vndf-shape
    validation (Slice 4 fix: independent square vndf resolution, not tied to `ndf`
    spatial dims).
- **6 unique measured paints** covering the car's visible surfaces:

  | BSDF file | Visual role |
  |---|---|
  | `cc_blue_agat_spec.bsdf` | Car body — vivid blue-to-teal color-shifting iridescent automotive paint |
  | `ilm_l3_37_metallic_spec.bsdf` | Metallic silver — wheels, hub caps, bolts, engine cover, wheel mounts (5 material declarations) |
  | `paper_white_spec.bsdf` | Floor plane |
  | `acrylic_felt_white_spec.bsdf` | Seat upholstery |
  | `laika_ceiling_paint_18_gray_spec.bsdf` | Brake rotors |
  | `vch_silk_blue_spec.bsdf` | Suspension arms |

- The car body renders the real `cc_blue_agat` BRDF — vivid blue-to-teal
  color-shifting iridescence visible under the IBL sky light. This is the primary
  visual indicator that the measured-BRDF pipeline is working end-to-end; earlier
  (pre-fix) renders showed uniform chrome because all measured materials fell back
  to conductor.
- The IBL (`sky.exr`) loads correctly as an infinite environment light and
  provides a spatially-varying outdoor illumination environment.
- The scene geometry — 7.16M triangles across 47 PLY meshes — compiles and BVH-
  builds without error. All PLY files are present in the sparse clone.
- Render stats at 1280×720 / 64 spp (dev sandbox): ~72s, 7,157,264 compiled
  triangles, 145.9M rays traced, ~2.19M rays/s, 96.9% non-black pixels, no NaN.

A reference render at 1280×720 / 64 spp lives at `docs/architecture/sportscar.png`.
Composition: a blue iridescent sports car on a white floor under an outdoor IBL
sky, showing color-shifting paint on the body panels, metallic wheels and hardware,
and white seat upholstery.

## Documented degradations (sportscar-specific)

These are graceful degradations with Warnings emitted at parse or compile time.
None block rendering; they affect specific materials.

- **Built-in named-SPD conductor references → generic metal fallback**: Three
  conductor materials (`Interior_Silver_phong_SG`, `MirrorMat_phong_SG`,
  `Interior_Monitor_phong_SG`) reference the built-in named spectral parameter
  `metal-Ag-eta` / `metal-Ag-k`. Built-in named SPD spectra are not supported
  (out of scope). These materials fall back to a generic metal. Warnings are
  emitted:
  ```
  [Material.eta] spectrum SPD file reference 'metal-Ag-eta' not supported
    (SPD parsing is out of scope); using generic metal fallback value
  [Material.k] spectrum SPD file reference 'metal-Ag-k' not supported
    (SPD parsing is out of scope); using generic metal fallback value
  ```
  *Render effect*: interior mirror surfaces and the monitor screen render as
  generic silver metal rather than physically accurate Ag. The car body, wheels,
  floor, seats, rotors, and suspension are unaffected — they use measured `.bsdf`
  files which load correctly.

## Camera convention

Sportscar uses the same world-to-camera CTM convention already documented in
`scenes/pbrt/dining_room/README.md`. No new convention note required.
