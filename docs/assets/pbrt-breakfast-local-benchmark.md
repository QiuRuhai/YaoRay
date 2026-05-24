# PBRT Breakfast Local Benchmark

Breakfast is the first large local PBRT scene target for the SceneWorld frontend.
The asset files are intentionally local-only and must stay out of Git.

## Source

- Rendering Resources: <https://benedikt-bitterli.me/resources/>
- PBRT v4 package: <https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip>
- PBRT scene note: <https://www.pbrt.org/scenes-v3>

The PBRT scene note lists the original `breakfast` scene with attribution to
Wig42 and a CC-BY license note. Bitterli's Rendering Resources page provides
converted PBRT scene packages and notes that converted PBRT output may differ
from the original Tungsten renders.

## Local Layout

Use this ignored local root:

```text
external/assets/pbrt/breakfast/
```

Recommended download layout:

```text
external/assets/pbrt/breakfast/dining-room.zip
external/assets/pbrt/breakfast/extracted/
```

Do not commit the zip, expanded `.pbrt` files, meshes, textures, or generated
renders from this directory.

## Setup

```powershell
New-Item -ItemType Directory -Force external/assets/pbrt/breakfast
Invoke-WebRequest -Uri https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip -OutFile external/assets/pbrt/breakfast/dining-room.zip
Expand-Archive -Force external/assets/pbrt/breakfast/dining-room.zip external/assets/pbrt/breakfast/extracted
```

After extraction, the current package layout is:

```text
external/assets/pbrt/breakfast/extracted/dining-room/scene-v4.pbrt
external/assets/pbrt/breakfast/extracted/dining-room/models/*.ply
external/assets/pbrt/breakfast/extracted/dining-room/textures/*
```

The downloaded package currently contains one `.pbrt` entrypoint, 52 `.ply`
meshes, three `.tga` textures, one `.png`, one `.exr`, and one `.pfm`.

Inspect the entrypoint and directives:

```powershell
Get-ChildItem -Recurse external/assets/pbrt/breakfast/extracted -Filter *.pbrt
rg -n "Include|Shape|plymesh|trianglemesh|MakeNamedMaterial|NamedMaterial|AreaLightSource|LightSource|Texture|Material|Camera|Film|Transform|ConcatTransform|Rotate|Translate|Scale" external/assets/pbrt/breakfast/extracted
```

## Current Smoke Path

Small checked-in PBRT fixtures are covered by tests. Breakfast itself is a
manual benchmark because it is too large for the repository and CI.

Current entrypoint:

```powershell
external\assets\pbrt\breakfast\extracted\dining-room\scene-v4.pbrt
```

Smoke render command:

```powershell
build\Release\yaoray.exe render external\assets\pbrt\breakfast\extracted\dining-room\scene-v4.pbrt --backend cpu
```

Expected current limitations:

- the scene uses `Texture "..." "spectrum" "imagemap"` with `.tga` inputs, which are not yet mapped into render textures
- the scene uses `LightSource "distant"` and `LightSource "infinite"`; these currently warn rather than lowering to YaoRay lights
- `coateddiffuse` and `conductor` lower to current approximate material intent rather than full PBRT parity
- CPU render time is expected to be high before table-native BVH and future CUDA work
