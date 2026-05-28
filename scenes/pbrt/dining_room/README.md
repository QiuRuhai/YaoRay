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

A reference render at 1280×720 / 64 spp / max depth 65 lives at
`docs/architecture/dining-room.png`. It is produced by the unmodified
`scene-v4.pbrt` straight from Bitterli's download — no workarounds.
Composition matches the bundled `TungstenRender.png` reference:
dining table with chairs, two pendant lamps, framed artwork on the
back wall, and vertical blinds covering the window on the right.

## Camera convention

PBRT v4 stores the CTM at the camera point as the **world-to-camera**
matrix. YaoRay's PBRT parser now matches that convention (see
`Inverse()` in `src/core/transform.cpp` and the `Inverse(camera_transform)`
call in `CompileCamera`). Both `LookAt eye target up` directives and
explicit `Transform [...]` directives produce the same CTM convention,
so PBRT v4 scenes exported by Tungsten, the official pbrt-v4
converter, or any other tool that follows the spec work out of the
box.

(Earlier M0/M1 development used the opposite convention — directly
treating the CTM as camera-to-world. The dining-room scene exposed
this and motivated the fix.)
