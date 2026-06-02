# sssdragon (mmp/pbrt-v4-scenes)

YaoRay's M4 subsurface-scattering anchor scene. This scene replaces the
originally-planned `ganesha`, which was found during Slice 5 triage to use
`Material "coateddiffuse"` (an M3 material), not subsurface. The `sssdragon`
scene uses `Material "subsurface"` with the `Skin1` named preset, making it
the correct SSS anchor. This directory intentionally stays empty in git — the
asset lives in Matt Pharr's `pbrt-v4-scenes` repository under Git LFS; we
link to it instead of redistributing.

## Download

`pbrt-v4-scenes` requires Git LFS. The full repo is large, so a sparse-checkout
that pulls only the `sssdragon/` subtree is recommended:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set sssdragon
git lfs pull --include="sssdragon/**"
cd ..
mv pbrt-v4-scenes-tmp/sssdragon ./sssdragon
rm -rf pbrt-v4-scenes-tmp
```

The unpacked tree under `external/assets/pbrt/sssdragon/` is gitignored via the
project-wide `external/assets/` rule.

## Mesh prep

The dragon mesh ships as `geometry/dragon.ply.gz` — a gzip-compressed
binary-big-endian PLY file. YaoRay's PLY loader reads big-endian binary
natively but does not transparently decompress `.gz`; decompress it first:

```bash
cd external/assets/pbrt/sssdragon
gzip -dk geometry/dragon.ply.gz   # produces geometry/dragon.ply
```

The scene files (`dragon_10.pbrt`, `dragon_50.pbrt`, `dragon_250.pbrt`) reference
`geometry/dragon.ply.gz` by default. Edit each scene's `plymesh` `"string
filename"` to point at `geometry/dragon.ply` before rendering.

## Render

```bash
# From repo root, after building (Windows):
build/Release/yaoray.exe render external/assets/pbrt/sssdragon/dragon_50.pbrt --backend cpu

# Unix / WSL:
./build/yaoray render external/assets/pbrt/sssdragon/dragon_50.pbrt --backend cpu
```

The output image lands at the path declared in the scene's `Film "string
filename"` directive. The scene ships in three LOD/scale variants that differ
only in the `scale` multiplier applied to the subsurface coefficients:
`dragon_10.pbrt` (scale 10), `dragon_50.pbrt` (scale 50), `dragon_250.pbrt`
(scale 250). Higher scale values produce a more diffuse, waxy translucency.

The reference image (`docs/architecture/sssdragon.png`) was rendered at
480×360 / 128 spp. Reduce the `Film` resolution and `Sampler` count in the
scene file to speed up test renders.

## Material

The dragon uses `Material "subsurface"` with the `Skin1` named preset:

```
Material "subsurface"
  "string name"  "Skin1"
  "float scale"  50
  "float eta"    1.5
```

YaoRay evaluates this via a **separable tabulated BSSRDF** driven by the
photon beam diffusion radial profile (Habel et al. 2013; dipole lineage). The
`Skin1` preset maps to the `SubsurfaceParameterTable` coefficients from PBRT:
sigma_a = (0.032, 0.17, 0.48) mm^-1, sigma_s' = (0.74, 0.88, 1.01) mm^-1
(RGB). The `scale` multiplier is applied to both sigma_a and sigma_s before
the table is built. The interface is a smooth dielectric boundary (perfect
Fresnel) at entry and exit; `eta` sets the index of refraction. Direct
`sigma_a`/`sigma_s` parameterization is also supported as an alternative to
named presets.

## Reference image

`docs/architecture/sssdragon.png` — 480×360, 128 spp, `dragon_50.pbrt`.

## Known differences from the PBRT v4 reference

These are honest, documented discrepancies. None block rendering; all emit
Warnings at parse or compile time where applicable.

1. **Environment map projection.** The scene's IBL (`small_rural_road_equiarea.exr`)
   uses PBRT v4's equal-area octahedral projection. YaoRay samples environment
   maps as lat-long / equirectangular. The lighting direction is therefore
   approximate and environment sampling is higher-variance (visible as grain on
   the floor plane). Proper equal-area environment support is future work (M6).

2. **Film ISO / exposure ignored.** `Film "float iso"` is parsed but has no
   effect; tone-map at view time instead.

3. **Sampler degrades to `independent`.** The scene's `halton` sampler parses
   but degrades to `independent` at compile time (M1 policy). A Warning is
   emitted.

4. **Diffusion-approximation energy loss at high curvature.** The separable
   diffusion model samples probe rays to find exit points; where the medium's
   mean free path is comparable to feature size or surface curvature is high,
   some probe rays miss and the contribution is lost. This is a known limitation
   of the separable diffusion approximation — PBRT exhibits the same regime.
   It manifests as slightly darker thin features (fin tips, sharp ridges) at
   larger scale values.
