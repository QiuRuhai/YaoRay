# YaoRay Cornell Box Foundation Design

## Context

YaoRay can now parse TOML scenes, import small OBJ meshes, compile render scenes, build a BVH, bind named diffuse/emissive materials, and write CPU debug PNG output. The project still lacks a stable reference scene that exercises these pieces together and can become the future path tracer showcase.

The Cornell Box is the right next target. Cornell publishes camera parameters, measured surface geometry, surface reflectance spectra, and light source spectrum through its Public Use Data page. This slice uses the official measured geometry as the reference, while keeping YaoRay's current RGB diffuse/emissive material model.

Sources:

- Cornell Box overview: https://bowers.cornell.edu/cornell-box
- Cornell Public Use Data: https://bowers.cornell.edu/computer-graphics/data

## Goals

- Add scene-authored inline quad mesh assets to TOML scene files.
- Compile inline quads into `RenderScene::triangles` by tessellating each quad into two triangles.
- Preserve existing path-based assets, `builtin:triangle`, OBJ import, material binding, transforms, BVH, and CLI behavior.
- Add `scenes/examples/cornell_box.toml` using Cornell's published measured geometry.
- Use RGB diffuse/emissive material approximations for the Cornell surfaces and light.
- Make the Cornell scene render through the current CPU debug backend and PNG output path.
- Document that this is a Cornell geometry foundation, not a spectral or final path-traced image match.

## Non-Goals

- No spectral rendering.
- No spectral-to-XYZ/RGB integration pipeline.
- No BRDF import from Cornell measurement files.
- No indirect lighting, path tracing, Russian roulette, MIS, or color bleeding.
- No texture support.
- No OBJ `.mtl`, glTF, or imported material support.
- No generic polygon triangulation or polygon holes.
- No per-face material IDs inside one asset.
- No claim that the current CPU direct-lighting output matches Cornell's physical photographs.

## Fidelity Target

This slice targets:

1. Official Cornell measured camera and visible scene geometry.
2. RGB approximations for white, red, green, and light materials.
3. A scene file that remains useful when a later CPU path tracer and spectral renderer are added.

It does not target physical measurement equivalence. Cornell's published material and light data are spectral. Matching those data physically requires a separate spectral rendering design.

## TOML Asset Schema

Extend `[[assets]]` so each asset is exactly one of:

- a path asset, using the existing `path = "..."`
- an inline quad asset, using `quads = [...]`

Example:

```toml
[[assets]]
name = "cornell_red_wall"
quads = [
  [[552.8, 0.0, 0.0], [549.6, 0.0, 559.2], [556.0, 548.8, 559.2], [556.0, 548.8, 0.0]]
]
```

Rules:

- `name` is still required and must be non-empty.
- `path` and `quads` are mutually exclusive.
- An asset with neither `path` nor `quads` is invalid.
- `quads` must be a non-empty array.
- Each quad must contain exactly four points.
- Each point must contain exactly three finite numeric values.
- The four points are ordered by winding. The compiler emits triangles `(p0, p1, p2)` and `(p0, p2, p3)`.
- Existing instance transforms apply to inline quads exactly as they apply to OBJ and `builtin:triangle` assets.

Diagnostics:

- `assets.path`: `must not be empty`
- `assets.quads`: `must be an array of quads`
- `assets.quads`: `must not be empty`
- `assets.quads`: `quad must contain exactly four points`
- `assets.quads`: `point must contain exactly three numeric values`
- `assets`: `must define exactly one of path or quads`

## Semantic Model

Add a semantic quad description:

```cpp
struct QuadDescription {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Point3f p3;
};
```

Extend `AssetDescription`:

```cpp
struct AssetDescription {
    std::string name;
    std::filesystem::path path;
    std::vector<QuadDescription> quads;
};
```

`path.empty() && !quads.empty()` means an inline quad asset. `!path.empty() && quads.empty()` means the existing path asset. The parser rejects all other combinations.

This avoids introducing a second scene-level mesh registry and keeps material binding through existing `[[instances]]`.

## Compiler Behavior

Change scene compilation from `name -> path` asset lookup to `name -> AssetDescription`.

For each instance:

1. Resolve its asset by name.
2. Resolve its material using existing material binding behavior.
3. If the asset has `quads`, append each quad as two render triangles with the resolved material index.
4. If the asset has `path`, keep existing `builtin:triangle` and OBJ behavior.
5. Apply the instance transform to every quad point before generating triangles.
6. Generate geometric normals from triangle winding.
7. Reject a quad if either generated triangle is degenerate.

Degenerate inline geometry diagnostics use:

- `assets.quads`: `quad produces degenerate triangle`

The compiler still builds a BVH after all triangles are appended.

## Cornell Scene

Create:

```text
scenes/examples/cornell_box.toml
```

Use millimeter-scale coordinates directly from the Cornell data. Do not normalize the scene to unit scale.

Render settings:

```toml
[render]
backend = "cpu"
width = 512
height = 512
spp = 1
max_depth = 5
seed = 1

[film]
output = "out/cornell_box.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [278.0, 273.0, -800.0]
target = [278.0, 273.0, 0.0]
fov_y = 39.3077
```

The field of view is derived from Cornell's published camera width/height and focal length.

Materials:

```toml
[[materials]]
name = "cornell_white"
albedo = [0.725, 0.710, 0.680]
emission = [0, 0, 0]

[[materials]]
name = "cornell_red"
albedo = [0.630, 0.065, 0.050]
emission = [0, 0, 0]

[[materials]]
name = "cornell_green"
albedo = [0.140, 0.450, 0.091]
emission = [0, 0, 0]

[[materials]]
name = "cornell_light"
albedo = [0.780, 0.780, 0.780]
emission = [17.0, 12.0, 4.0]
```

These RGB values are approximations for the current renderer. They are intentionally not stored as spectral data.

Lighting:

```toml
[[lights]]
type = "area"
position = [278.0, 548.8, 279.5]
size = [130.0, 105.0]
radiance = [17.0, 12.0, 4.0]
```

The visible light surface is also modeled as an emissive quad so primary rays can see it. The `[[lights]]` area light remains necessary because the current CPU debug renderer samples explicit area lights for direct lighting.

Geometry groups:

- `cornell_room_white`: floor, back wall, and four ceiling pieces around the light opening.
- `cornell_red_wall`: left red wall.
- `cornell_green_wall`: right green wall.
- `cornell_short_block`: five visible quads for the short block.
- `cornell_tall_block`: five visible quads for the tall block.
- `cornell_light_panel`: one emissive quad.

The ceiling must be split around the light rectangle. A single full ceiling quad plus a coplanar light quad would create overlapping geometry and unstable BVH hit selection.

The floor footprint holes listed in the Cornell data are not triangulated in this slice. The blocks have no bottom faces, and the hidden floor area under the blocks is not visible in the current opaque triangle renderer. Generic polygon holes remain out of scope.

Initial quad data:

```toml
[[assets]]
name = "cornell_room_white"
quads = [
  [[552.8, 0.0, 0.0], [0.0, 0.0, 0.0], [0.0, 0.0, 559.2], [549.6, 0.0, 559.2]],
  [[549.6, 0.0, 559.2], [0.0, 0.0, 559.2], [0.0, 548.8, 559.2], [556.0, 548.8, 559.2]],
  [[556.0, 548.8, 0.0], [556.0, 548.8, 227.0], [0.0, 548.8, 227.0], [0.0, 548.8, 0.0]],
  [[556.0, 548.8, 332.0], [556.0, 548.8, 559.2], [0.0, 548.8, 559.2], [0.0, 548.8, 332.0]],
  [[556.0, 548.8, 227.0], [556.0, 548.8, 332.0], [343.0, 548.8, 332.0], [343.0, 548.8, 227.0]],
  [[213.0, 548.8, 227.0], [213.0, 548.8, 332.0], [0.0, 548.8, 332.0], [0.0, 548.8, 227.0]]
]

[[assets]]
name = "cornell_green_wall"
quads = [
  [[0.0, 0.0, 559.2], [0.0, 0.0, 0.0], [0.0, 548.8, 0.0], [0.0, 548.8, 559.2]]
]

[[assets]]
name = "cornell_red_wall"
quads = [
  [[552.8, 0.0, 0.0], [549.6, 0.0, 559.2], [556.0, 548.8, 559.2], [556.0, 548.8, 0.0]]
]

[[assets]]
name = "cornell_short_block"
quads = [
  [[130.0, 165.0, 65.0], [82.0, 165.0, 225.0], [240.0, 165.0, 272.0], [290.0, 165.0, 114.0]],
  [[290.0, 0.0, 114.0], [290.0, 165.0, 114.0], [240.0, 165.0, 272.0], [240.0, 0.0, 272.0]],
  [[130.0, 0.0, 65.0], [130.0, 165.0, 65.0], [290.0, 165.0, 114.0], [290.0, 0.0, 114.0]],
  [[82.0, 0.0, 225.0], [82.0, 165.0, 225.0], [130.0, 165.0, 65.0], [130.0, 0.0, 65.0]],
  [[240.0, 0.0, 272.0], [240.0, 165.0, 272.0], [82.0, 165.0, 225.0], [82.0, 0.0, 225.0]]
]

[[assets]]
name = "cornell_tall_block"
quads = [
  [[423.0, 330.0, 247.0], [265.0, 330.0, 296.0], [314.0, 330.0, 456.0], [472.0, 330.0, 406.0]],
  [[423.0, 0.0, 247.0], [423.0, 330.0, 247.0], [472.0, 330.0, 406.0], [472.0, 0.0, 406.0]],
  [[472.0, 0.0, 406.0], [472.0, 330.0, 406.0], [314.0, 330.0, 456.0], [314.0, 0.0, 456.0]],
  [[314.0, 0.0, 456.0], [314.0, 330.0, 456.0], [265.0, 330.0, 296.0], [265.0, 0.0, 296.0]],
  [[265.0, 0.0, 296.0], [265.0, 330.0, 296.0], [423.0, 330.0, 247.0], [423.0, 0.0, 247.0]]
]

[[assets]]
name = "cornell_light_panel"
quads = [
  [[343.0, 548.8, 227.0], [343.0, 548.8, 332.0], [213.0, 548.8, 332.0], [213.0, 548.8, 227.0]]
]
```

Expected triangle count:

- room white: 6 quads -> 12 triangles
- red wall: 1 quad -> 2 triangles
- green wall: 1 quad -> 2 triangles
- short block: 5 quads -> 10 triangles
- tall block: 5 quads -> 10 triangles
- light panel: 1 quad -> 2 triangles
- total: 38 triangles

## Documentation

Update README and architecture docs:

- Cornell Box example exists as a geometry/material smoke scene.
- It uses official Cornell measured geometry with RGB material approximations.
- It is not a physically matched Cornell render yet.
- Spectral rendering and path tracing remain future work.

## Testing

Parser tests:

- Inline quad asset parses one quad.
- Inline quad asset parses multiple quads.
- Existing path assets still parse.
- Asset rejects both `path` and `quads`.
- Asset rejects neither `path` nor `quads`.
- Asset rejects malformed quad shape and malformed point shape.
- Unknown fields still reject.

Compiler tests:

- Inline quad compiles into two triangles.
- Multiple quads compile into `2 * quad_count` triangles.
- Inline quad triangles receive the instance material index.
- Instance transforms apply to inline quad points.
- Degenerate quads fail compilation with `assets.quads`.
- Existing `builtin:triangle` and OBJ compilation still pass.

CLI/example tests:

- `scenes/examples/cornell_box.toml` parses, compiles, and renders with CPU backend.
- CLI output includes `Compiled triangles: 38`.
- Output PNG exists and has a valid PNG signature.
- Existing minimal and OBJ example tests continue to pass.

Manual verification:

- Run:

```powershell
build\Debug\yaoray.exe render scenes\examples\cornell_box.toml --backend cpu
```

- Confirm `scenes/examples/out/cornell_box.png` exists.
- Confirm the image shows a square Cornell-like room with red and green walls, two white blocks, and a visible ceiling light.

The current image will not show indirect bounce or color bleeding. That visual gap is expected until the CPU path tracer work.

## Follow-Up Work

1. Add CPU path tracing with Lambertian BSDF sampling and emissive hits.
2. Use the Cornell Box scene as the first path tracer quality regression scene.
3. Add optional higher-sample renders for visual comparison.
4. Later, design spectral rendering and spectral-to-RGB output if physical Cornell matching becomes a project goal.
