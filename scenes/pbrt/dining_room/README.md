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

A reference preview render (rendered with a custom interior camera
position to bypass the camera-convention quirk noted below) lives at
`docs/architecture/dining-room.png`.

## Known gap: camera transform convention (M2 follow-up)

The original `scene-v4.pbrt` opens with a 4×4 `Transform` directive that
encodes the camera's pose. Our M1 parser treats this matrix
consistently with how `LookAt` directives are constructed (a
camera-to-world convention with the camera origin in the last column),
but the dining-room asset's `Transform` matrix appears to follow a
different convention — when interpreted as we do, it places the camera
outside the room rather than inside it. As a result, rendering the
*unmodified* `scene-v4.pbrt` produces an image of the room's exterior
shell (a dark wedge over a bright sky), not the dining room interior.

To verify the underlying pipeline, the operator can either:

1. Replace the leading `Transform [...]` line with a manual `LookAt`
   pointing at the room contents (e.g. `LookAt 0 0 0   0 0 -1   0 1 0`
   places the camera at the origin looking down -Z, which lands inside
   the room).
2. Wait for the M2 follow-up that nails down the PBRT v4 camera-matrix
   convention against pbrt-v4's reference behavior.

The render reproduced in `docs/architecture/dining-room.png` uses the
manual-LookAt workaround.
