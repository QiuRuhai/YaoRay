# Dining Room (Bitterli PBRT v4)

YaoRay's M1 anchor scene. This directory intentionally stays empty in git —
the asset is large and licensed CC-BY by Benedikt Bitterli; we link to
his resources instead of redistributing.

## Download

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
curl -L -o dining-room.zip "https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip"
unzip dining-room.zip
```

Or visit https://benedikt-bitterli.me/resources/ and grab the PBRT-v4 link
for *Dining Room* manually.

The unpacked tree under `external/assets/pbrt/dining-room/` is gitignored
via the project-wide `external/assets/` rule.

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/dining-room/scene-v4.pbrt --backend cpu
```

The output PNG lands at the path declared in the scene's `Film "string
filename"` directive — typically `dining-room.png` in the scene directory.

## What works in M1

- The 269,538-triangle scene parses, compiles, and renders end-to-end
  with no `Error:` diagnostics from our compiler.
- All 15 declared materials (`diffuse`, `conductor`, `coateddiffuse`)
  resolve to YaoRay's BSDFs directly — no degradation warnings.
- 3 imagemap textures (`.tga` format) load via the M1 TGA support and
  bind to material `reflectance` slots.
- The skydome HDRI (`Skydome.pfm` — Portable Float Map) loads via the
  M1 PFM support and drives the `LightSource "infinite"` environment
  importance sampler.
- The BVH builds at depth 18; rays trace at ~5 million per second on
  11 CPU threads at the smoke resolutions used during development.

A reference render with the M1 pipeline lives at
`docs/architecture/dining-room.png` (480x270 / 16 spp / max depth 8).
It uses the camera workaround described below.

## Known gap: camera Y sign in the Tungsten-exported Transform (M2 follow-up)

The original `scene-v4.pbrt` opens with a 4×4 `Transform` directive
whose translation column is `(0.460, -2.14, 9.88)`. The scene's
geometry has its floor at Y ≈ `-1.52` and ceiling at Y ≈ `+7.90`
(verified by sampling vertex positions across all 52 PLY meshes). With
Y = `-2.14` the camera sits *below* the floor, looking up at the
underside of the room from outside. Rendering the unmodified scene
produces a black wedge (floor underside) over a bright HDRI sky.

Flipping the camera Y sign (Y = `+2.14`) places the camera at a
sensible eye height inside the room and reproduces the Tungsten
reference (`TungstenRender.png`) composition: table with chairs, two
pendant lamps, a framed picture on the left wall, and vertical blinds
covering the window on the right. This is what
`docs/architecture/dining-room.png` shows.

The discrepancy is consistent with a Y-axis sign convention difference
between Tungsten's matrix exporter and PBRT v4. Resolving "which side
is correct" requires running the unmodified scene through pbrt-v4
itself for comparison; that follow-up is tracked as M2 work.

To reproduce the M1 reference render, after downloading the asset,
copy the scene and replace its leading `Transform [...]` line with:

```pbrt
LookAt 0.46 2.14 9.88   0 1 0   0 1 0
```

(Same XZ position, Y sign flipped, looking at a point one unit above
the origin with +Y as up.) The pipeline then handles the resulting
269K-triangle / 15-material / HDRI-lit scene cleanly.
