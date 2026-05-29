# Killeroo Coated (mmp/pbrt-v4-scenes)

YaoRay's M3 layered-materials anchor scene. This directory intentionally stays
empty in git — the asset lives in Matt Pharr's `pbrt-v4-scenes` repository under
Git LFS; we link to it instead of redistributing.

## Download

`pbrt-v4-scenes` requires Git LFS. The full repo is large, so a sparse-checkout
that pulls only the `killeroos/` subtree is recommended:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set killeroos
git lfs pull --include="killeroos/**"
cd ..
mv pbrt-v4-scenes-tmp/killeroos ./killeroo-coated
rm -rf pbrt-v4-scenes-tmp
```

The unpacked tree under `external/assets/pbrt/killeroo-coated/` is gitignored via
the project-wide `external/assets/` rule.

Note: in `pbrt-v4-scenes` all killeroo variants (`killeroo-coated-gold.pbrt`,
`killeroo-gold.pbrt`, `killeroo-simple.pbrt`, `killeroo-moving.pbrt`) live
together under the single `killeroos/` subtree. The `mv` above renames that to
`killeroo-coated/` so the render command below works as written. Unlike the
Pavilion scene, there are no sibling subtree dependencies — all referenced
geometry, SPD files, and textures are self-contained within `killeroos/`.

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/killeroo-coated/killeroo-coated-gold.pbrt --backend cpu
```

The output image lands at the path declared in the scene's `Film "string
filename"` directive. The bundled scene targets a 1368×1026 (4:3) image at
256 spp — to reproduce this repo's reference image at 1280×720 / 64 spp,
temporarily edit the scene's `Film` and `Sampler` blocks before rendering,
then revert.

## What works in M3 (layered phase)

- `killeroo-coated-gold.pbrt` parses, compiles, and renders end-to-end with no
  `Error:` diagnostics.
- **Real two-layer coated BSDFs** — the headline of this scene. `coateddiffuse`
  and `coatedconductor` now perform full stochastic two-layer evaluation: smooth
  dielectric clearcoat over the base layer, Beer-Lambert transmittance through
  the coat, and MIS-consistent sample + f + pdf (M3 Slices 2a and 2b). The
  coated killeroo displays a clearcoat Fresnel highlight over the gold conductor
  base, which is the primary visual indicator that the layered BSDF stack is
  working.
- `coatedconductor` reads `interface.roughness` for the coat layer (M3 Slice 3
  patch); the scene declares `"float interface.roughness" [ 0.02 ]` for a
  slightly rough coat.
- `Shape "disk"` is tessellated to a triangle fan at compile time (M3 Slice 3
  patch). The disk area light (r=150, z=800, L=[50,50,50]) is the scene's
  primary key light; it is now a fully emissive primitive.
- `Shape "loopsubdiv"` compiles the base-mesh control vertices and faces directly
  to a `trianglemesh`, giving the killeroo at base-mesh resolution with correct
  silhouette and gold coated material (M3 Slice 3 patch). See *Documented
  degradations* for the visual consequence.
- `"spectrum" conductor.eta / conductor.k` with SPD filenames (`Au.eta.spd`,
  `Au.k.spd`) are recognized and replaced with representative RGB eta/k values
  from a small static metal table (M3 Slice 3 patch). The result reads
  distinctively gold rather than a white mirror or wrong-colour fallback.
- Parse-stage warnings (`CoordSysTransform`, `ReverseOrientation`, etc.) are
  now printed unconditionally to stderr regardless of parse success (M3 Slice 3
  patch).
- The `diffuse` floor and wall materials bind a `"spectrum" "scale"` texture
  (grid lines × 0.5) over `textures/lines.png`, using the M2-era compile-time
  scale fold.
- Render stats at 1280×720 / 64 spp (Windows, MSVC Release, 11 threads): ~35.75s,
  8,354 compiled triangles, 5,493 BVH nodes, max depth 18, ~172M rays, 71% hit
  rate.

A reference render at 1280×720 / 64 spp lives at
`docs/architecture/killeroo-coated.png`. Composition: the killeroo figure in
a corner formed by a diffuse floor and two white walls, lit by the large
overhead disk area light. The gold clearcoat surface shows a visible Fresnel
highlight on the upper surfaces and smooth specular reflection where the coat
faces the light. The base mesh's faceted silhouette (no Loop subdivision) is
visible at the extremities.

## Documented degradations (killeroo-coated-specific)

These are graceful degradations with Warnings emitted at compile or parse time.
None block rendering; they affect specific visual aspects.

- **`Shape "loopsubdiv"` → faceted base-mesh trianglemesh**: Loop subdivision
  is out of scope for M3 Slice 3. The control-mesh vertices and faces are compiled
  directly into a flat `trianglemesh` (equivalent to zero subdivision levels),
  producing a faceted appearance on curved surfaces and extremities. A Warning is
  emitted. *Render effect*: the killeroo silhouette is coarser than the intended
  smooth subdivided mesh; material behaviour and lighting are otherwise correct.
- **Au SPD files → RGB eta/k approximation**: SPD file parsing is out of scope.
  `conductor.eta` and `conductor.k` parameters that reference `spds/Au.*.spd`
  filenames are detected and replaced with representative RGB (eta, k) values for
  gold from a static metal table (Au/Ag/Cu/Al). A Warning is emitted. *Render
  effect*: the gold colouring is recognisably correct but is a trichromatic
  approximation rather than a full spectral integral; slight hue deviation from
  a physically accurate gold SPD is expected.
- **`CoordSysTransform "camera"` → skipped, distant fill light mis-directed**:
  The `CoordSysTransform` directive requires maintaining a named coordinate system
  table and is deferred. The `LightSource "distant"` fill light (L=[0.2,0.2,0.2])
  that follows it uses an identity transform instead of the camera frame, so its
  direction is incorrect and it contributes negligible illumination. A Warning is
  emitted at parse time. *Render effect*: the fill light is absent in practice;
  the disk area light is the sole effective light source and provides adequate
  key illumination for a recognizable lit render.
- **`ReverseOrientation` → ignored**: The `ReverseOrientation` directive is not
  implemented; it is silently skipped with a parse-stage Warning. *Render effect*:
  surfaces that rely on reversed normals may shade incorrectly, but no such
  surface is visible in the primary killeroo-coated-gold composition.

## Camera convention

Killeroo uses the same world-to-camera CTM convention already documented in
`scenes/pbrt/dining_room/README.md`. No new convention note required.
