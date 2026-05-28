# YaoRay M1: Dining-Room Milestone — Design

**Date:** 2026-05-27
**Status:** Approved for implementation planning
**Predecessor:** `2026-05-27-yaoray-pbrt-architecture-reset-design.md` (M0)

## North Star

M1 is complete when YaoRay can render Benedikt Bitterli's
[`dining-room`](https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip)
PBRT v4 scene to a "visually close" quality bar (definition in §6) on the
existing CPU path-tracing backend, with three hand-crafted showcase scenes
exercising each major subsystem in isolation.

This is the first milestone in which YaoRay can render a public,
third-party PBRT v4 scene end to end. It is the natural successor to M0
(architecture reset) and the prerequisite for M2 (which will introduce
larger scenes such as Sponza/Bistro and begin CUDA backend work).

## Why Dining-Room

* Already documented as the project's first large-scene target in
  `docs/assets/pbrt-breakfast-local-benchmark.md`.
* Mid-scale (≈ 10^5–10^6 triangles, manageable on the CPU backend).
* Exercises the **complete required feature surface for M1** in one
  scene: HDRI window lighting, multiple textured diffuse / glossy / glass
  materials, plymesh meshes with explicit normals and uvs, and at least
  one analytic light.
* CC-BY licensed; we can render, screenshot, and ship comparisons.
* Widely used as a community benchmark, so a Bitterli reference render
  exists and visual differences are easy to spot.

## Scope

### In scope (must ship in M1)

**Geometry**

* `Shape "sphere"` (analytic implicit sphere with center + radius).
* `Shape "trianglemesh"` and `Shape "plymesh"`: pass through the `N`
  (per-vertex normal), `uv`, and `S` (tangent) parameters into
  `RenderVertex`. M0 parses these but discards them.

**Lights**

* `LightSource "infinite"` — HDRI environment map. Wires `mapname`,
  `scale`, and the existing rotation parameter into `RenderEnvironment`
  + `RenderEnvironmentDistribution`. The sampling math, distribution
  build, and CPU evaluation are already implemented in M0; only the
  scene compiler binding is new.
* `LightSource "distant"` — parallel ray light (direction + radiance).
  Used by dining-room for sun-through-window.
* `LightSource "point"` — isotropic point emitter (`from` + radiance,
  1/r² falloff).
* `LightSource "spot"` — point emitter with cone falloff (`from`, `to`,
  `coneangle`, `conedeltaangle`).
* `AreaLightSource "diffuse"` — unchanged from M0.

**Textures**

* `Texture "imagemap"` — load `.png`/`.jpg`/`.exr`/`.hdr` through the
  existing stb loader (HDR/EXR via stb_image's float path). Color-space
  inference: HDR/EXR ⇒ linear, LDR ⇒ sRGB unless overridden by
  `"string encoding"`. Wrap mode: `"repeat"` / `"clamp"` /
  `"black"` mapped to the existing `RenderTexture.wrap_mode`.
* `Texture "constant"` — folded at compile time into the constant
  channel of the consuming `TexParam`. No `RenderTexture` allocated.

**Material parameter binding**

The seven M0 material kinds (matte / conductor / dielectric /
thindielectric / coateddiffuse / coatedconductor / diffusetransmission)
keep their existing `TexParam1f` / `TexParam3f` fields. M1 changes the
scene compiler so that when a material parameter references a named
texture (e.g. `"texture reflectance" ["wood_albedo"]`), the named
texture is resolved to its `RenderTexture` index and stored in
`TexParam.texture`. The constant fallback in `TexParam.value` is kept
populated with a sensible default for safety.

Slots that must accept textures in M1 (the **concept**; the M0
`RenderMaterial` already has matching `TexParam` fields for these,
possibly under slightly different field names that the plan will pin
down):

* Surface reflectance / albedo (matte, diffuse, coateddiffuse,
  diffusetransmission, and the under-layer of coatedconductor).
* Anisotropic roughness `uroughness` / `vroughness` on conductor,
  dielectric, coatedconductor (and the interface layer of
  coateddiffuse).
* Complex IOR `eta`, `k` on conductor — RGB-as-spectrum only; no
  spectral upsampling in M1.
* Emission color for the (optional) non-area-light glow term on any
  material.

**Normal mapping**

* `"string normalmap" ["filename.png"]` on any material → load as linear
  texture, write index to `RenderMaterial.normal_map`. Tangent-space
  decode already exists in `src/backends/cpu/cpu_material.cpp`
  (`ResolveNormalMap`); M1 only fills the data path.

**Material degradation policy (mandatory, with diagnostics)**

For materials in `dining-room` (or any future scene) that PBRT v4
declares but M1 does not implement, the scene compiler **must** emit a
`Warning` diagnostic and substitute the documented fallback below.
Silent substitution is forbidden.

| Declared material            | M1 fallback                                                     |
| ---------------------------- | --------------------------------------------------------------- |
| `subsurface`                 | `diffuse` with the same `reflectance`                           |
| `measured`                   | `conductor` with default eta/k                                  |
| `hair`                       | `diffuse` reflectance `(0.5, 0.5, 0.5)`                         |
| `mix` (two-way blend)        | the average of the two component materials' `reflectance`, kind = `diffuse` |
| Layered material, depth > 2  | `coateddiffuse` with default parameters                         |
| **Any other unknown kind**   | `diffuse` reflectance `(0.5, 0.5, 0.5)` — catch-all safety net  |

### Out of scope (deferred to M2+)

* Shapes: `disk`, `cylinder`, `curve`, `loopsubdiv`, `bilinear`.
* Volumetrics / media (homogeneous, heterogeneous, nanovdb).
* Spectral rendering. RGB-only throughout.
* Full implementations of `subsurface`, `hair`, `measured` — they
  *parse* and *render* (via the fallbacks above) in M1 but do not
  produce physically correct output for those materials.
* Procedural textures (`marble`, `fbm`, `wrinkled`, `dots`, `windy`).
* `Texture "scale"` and `Texture "mix"` (texture-level multiply / blend,
  distinct from the `mix` *material*) — fold into M2 if a target scene
  needs them.
* CUDA / OptiX backend (M2+).
* Alternative samplers (`halton`, `sobol`, `pmj02bn`).
* Adaptive sampling, denoising, firefly clamping beyond the existing
  `radiance_clamp`.

## Architecture

The two-layer pipeline introduced in M0 is unchanged:

```
PbrtScene ──CompilePbrtScene──▶ RenderSceneIR ──Backend.Prepare──▶ Renderable
```

M1 fills in code paths that M0 left as stubs. There are no new
top-level libraries and no new directories. All changes localize to:

* `src/pbrt/pbrt_scene.cpp` — extend parser handlers for `LightSource`
  variants and `Texture` directive, capture `normalmap` parameter.
* `src/render/scene_compiler.cpp` — wire textures, environment, lights,
  vertex attributes, sphere shapes; emit degradation diagnostics.
* `include/yaoray/render/render_scene.hpp` — small additions:
  `RenderSphere` table, `RenderLight` (non-area) table.
* `src/render/bvh.cpp` — add sphere primitive support to `BuildBvh` and
  `IntersectBvh`.
* `src/render/shading.cpp` — extend `LocateTriangle` / `GeometricNormal`
  to also resolve spheres.
* `src/backends/cpu/cpu_path_tracer.cpp` — handle non-area `RenderLight`
  samples in `SampleDirectLight`; environment importance sampling path
  already wired in M0.
* `include/yaoray/render/light_sampling.hpp` (+ `.cpp`) — add
  `SampleDistantLight`, `SamplePointLight`, `SampleSpotLight` with PDFs.
* `tests/` — new fixtures (`scenes/pbrt/cornell_box_pbrt/...`,
  `material_studio/...`, `texture_test/...`).

### New / extended IR types

```cpp
// In render_scene.hpp

struct RenderSphere {
    Point3f center;
    float radius;
    int material_index;
    int area_light_index = -1;   // -1 if no area light
    bool flip_normals = false;
};

enum class RenderLightKind { Point, Distant, Spot };

struct RenderLight {
    RenderLightKind kind;
    Color3f radiance;            // L for Point/Spot; spectral irradiance for Distant
    Point3f position{};          // Point / Spot
    Vec3f direction{};           // Distant (toward scene) / Spot (cone axis)
    float cone_cos_inner = 1.0f; // Spot
    float cone_cos_outer = 1.0f; // Spot
};

struct RenderSceneIR {
    // ... existing M0 fields ...
    std::vector<RenderSphere> spheres;
    std::vector<RenderLight>  lights;     // Non-area, non-environment
};
```

`BvhHit` gains no fields — sphere hits use `primitive_index` with a
high bit (or a separate `kind` byte; decision is an implementation
detail for the plan). Sphere intersection records `bary_u` = `u` (atan2
azimuth) and `bary_v` = `v` (acos zenith) for analytic UV.

## Implementation Slices

Each slice is independently deliverable: a single PR that lands a
working new target scene, plus its unit tests. No slice depends on a
later slice. Each slice gets its own implementation plan.

### Slice 1: `cornell_box_pbrt` (≈ 1 week)

**Unlocks** the classic Cornell box with a mirror sphere and a glass
sphere, lit by an area light.

* `Shape "sphere"` end-to-end: parser, scene compiler, BVH bounds,
  intersection, analytic normal + UV.
* `LightSource "point"` end-to-end: parser, IR, compiler, CPU light
  sampling + PDF + MIS.
* Diagnostic infrastructure for material degradation (the `Warning`
  emitter, not yet the fallback substitutions — those land in Slice 4).

Deliverables:

* `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt` renders
  end-to-end through the CLI (256 spp, < 30 s on 11 threads).
* New unit tests: sphere intersect, sphere AABB, sphere UV, point light
  sampling PDF, point light MIS weight.

### Slice 2: `material_studio` (≈ 1.5 weeks)

**Unlocks** five spheres of different BSDFs side-by-side on a plane,
lit by a small HDRI.

* `LightSource "infinite"` wiring in the scene compiler. The HDRI
  loader, `RenderEnvironmentDistribution` builder, and CPU environment
  importance sampling already exist (M0); this slice connects the dots.
* `Texture "imagemap"` loader + `RenderSceneIR.textures` push. Use stb
  for LDR + HDR. Auto color-space (LDR ⇒ sRGB, HDR ⇒ linear). Honor an
  explicit `encoding "linear"` / `"sRGB"` override.
* `Texture "constant"` folded into `TexParam.value` at compile time;
  no `RenderTexture` allocated.
* Scene compiler resolves named-texture references in material
  parameters (reflectance, uroughness, vroughness, eta, k,
  interface_roughness, albedo, emit) and writes
  `TexParam.texture = <index>`.
* No new BSDF logic. The CPU material resolver
  (`ResolveCpuMaterialSample`) already samples textures via `TexParam`;
  M0 left `.texture = -1` and only used `.value`.

Deliverables:

* `scenes/pbrt/material_studio/material_studio.pbrt` renders
  (256 spp, < 60 s).
* HDRI asset: a small (≤ 2 MB) CC0 HDR sky from polyhaven, committed at
  `scenes/pbrt/material_studio/env/sky.hdr`.
* New unit tests: imagemap load + sRGB decode, HDRI environment IR
  binding, material parameter texture binding.

### Slice 3: `texture_test` (≈ 0.5–1 week)

**Unlocks** focused validation of the texture / normal-map pipeline.

* `normalmap` material parameter end-to-end: parser, compiler,
  `RenderMaterial.normal_map` index assignment.
* Wrap mode and color-space coverage: scenes that use repeat / clamp /
  black explicitly, plus a 16-bit linear PNG normal map to confirm no
  sRGB decoding is applied.
* No new BSDF or sampling logic.

Deliverables:

* `scenes/pbrt/texture_test/texture_test.pbrt` renders.
* Source PNG textures committed under
  `scenes/pbrt/texture_test/textures/` (each ≤ 64 KB).
* Unit tests: normal-map tangent-space decode, wrap mode behavior at
  uv ∈ {-0.1, 0, 0.5, 1.0, 1.1}, sRGB vs linear loading.

### Slice 4: `dining-room` (≈ 1.5 weeks)

**Unlocks** the M1 success criterion.

* `Shape "trianglemesh"` / `Shape "plymesh"`: copy `N`, `uv`, `S`
  arrays from the parser into `RenderVertex`. Default-generate a
  facet-normal vertex normal only when none was supplied.
* `LightSource "distant"` end-to-end: direction transform from the
  active CTM, infinite-PDF sampling pattern, MIS handling.
* `LightSource "spot"` end-to-end: cone falloff, IES-free,
  cosine PDF.
* Material degradation policy substitutions per §3 table; each
  substitution emits a `Warning` diagnostic naming the affected
  material.
* Download Bitterli `dining-room` to
  `external/assets/pbrt/dining-room/` (gitignored).
* Render, debug, tune until quality bar B is met against the Bitterli
  reference.

Deliverables:

* `dining-room` renders to a recognizable, clean image at 512 spp,
  max depth 8.
* New unit tests: distant light PDF, spot light cone falloff,
  trianglemesh vertex attribute pass-through, material degradation
  diagnostics fire correctly.
* README updated to point at dining-room as the showcase render.

## Quality Bar (Operational Definition of "Visually Close")

For `dining-room` at 512 spp, max depth 8, ACES tone map, default
exposure:

1. **No crashes / no errors.** Compiler emits zero `Error`
   diagnostics. `Warning` diagnostics are documented (each must be
   anticipated by §3 fallback table).
2. **All geometry present.** Triangle count and primitive count match
   the source `.pbrt` file (within 1 % to allow for known-bad
   primitives the parser is allowed to drop).
3. **Materials match category.** Every surface declared `metal`,
   `glass`, `coatedconductor`, `dielectric`, `thindielectric` uses the
   corresponding BSDF; nothing falls back to diffuse silently.
4. **Lighting is correct in character.** HDRI window light reaches the
   table; distant light (if present) casts the expected shadow
   direction; area light from a ceiling lamp (if present) creates the
   expected soft shadow.
5. **No fireflies above ½ % of pixels.** Pixel HDR magnitude bounded
   below `radiance_clamp` (default off ⇒ unlimited; if clamp is needed
   to pass, document it).
6. **No NaN / Inf pixels.** Verified by an automated CTest check.
7. **Visual A/B against Bitterli reference.** Side-by-side at
   matching crop / exposure shows no obvious composition, color,
   energy, or shadow discrepancy. Subtle BSDF-microfacet differences
   are acceptable; missing entire categories (e.g. all metal looks
   matte) are not.

## Testing Strategy

* **TDD for every new feature.** Each slice's implementation plan
  must list a failing test before implementation; this is enforced by
  the plan template.
* **Unit tests** cover individual functions (sphere intersect, light
  PDF, texture binding, etc.).
* **CLI / CTest end-to-end** runs each of the four target scenes:
  * Showcase scenes at low spp (8–32) with a soft sanity check
    (renders, no NaN, mean luminance in expected range).
  * `dining-room` at very low spp (4–8) as a smoke test — verifies the
    pipeline survives the scene's feature surface, not that the image
    is converged. The "real" 512 spp render is documentation-only,
    not in CI.
* **Visual regression** is a manual gate at slice boundaries: render
  the slice's target scene at production spp, compare against the
  previous reference, eyeball.

## Files & Modules

No new top-level modules. Files touched (∼) or created (+):

```
include/yaoray/render/render_scene.hpp        ∼  (RenderSphere, RenderLight)
include/yaoray/render/bvh.hpp                 ∼  (sphere support if needed)
include/yaoray/render/light_sampling.hpp      ∼  (non-area light samplers)
include/yaoray/render/shading.hpp             ∼  (LocateSphere, sphere normal)
include/yaoray/render/texture.hpp             ∼  (already has wrap mode; no change expected)

src/pbrt/pbrt_scene.cpp                       ∼  (LightSource variants, Texture, normalmap)
src/render/scene_compiler.cpp                 ∼  (heaviest changes: lights, textures, spheres, vertex attrs, degradation)
src/render/bvh.cpp                            ∼  (sphere primitive integration)
src/render/light_sampling.cpp                 ∼  (distant/point/spot)
src/render/shading.cpp                        ∼  (sphere resolver)
src/backends/cpu/cpu_path_tracer.cpp          ∼  (non-area light sampling in MIS loop)
src/backends/cpu/cpu_material.cpp             ∼  (texture sample path is reachable for the first time)

scenes/pbrt/cornell_box_pbrt/                 +  (Slice 1)
scenes/pbrt/material_studio/                  +  (Slice 2)
scenes/pbrt/texture_test/                     +  (Slice 3)

tests/sphere_tests.cpp                        +  (Slice 1)
tests/non_area_light_tests.cpp                +  (Slice 1 / 4)
tests/material_binding_tests.cpp              +  (Slice 2)
tests/texture_binding_tests.cpp               +  (Slice 2 / 3)
tests/normal_map_tests.cpp                    +  (Slice 3)
tests/material_degradation_tests.cpp          +  (Slice 4)

CMakeLists.txt                                ∼  (new test files)
README.md                                     ∼  (rewritten; M0 left it pointing at deleted TOML files)
docs/architecture/overview.md                 ∼  (refresh to two-layer pipeline + M1 surface)
```

## Documentation

The current `README.md` still documents the deleted TOML/OBJ/glTF
workflow and references removed scene files. **Updating `README.md`
to describe the post-M0 reality is part of Slice 4** (it would be
premature to rewrite it before M1 ships, since the showcase scenes
land along the way).

`docs/architecture/overview.md` gets a refresh at the end of M1 to
describe the two-layer pipeline as it is, not as it was first
sketched.

## Risk Register

* **Risk:** Dining-room uses a material variant we did not anticipate
  in the degradation table.
  **Mitigation:** The degradation infrastructure must be designed so
  that *any* unrecognized material kind falls back to diffuse with a
  named-kind warning, not just the ones in the table. The table is
  the *known* set; the catch-all is the safety net.

* **Risk:** HDRI sampling produces visible bias due to the existing
  M0 `RenderEnvironmentDistribution` having an issue under real-world
  HDRIs (M0 only tested it on synthetic data).
  **Mitigation:** Slice 2 includes a unit test that drives the M0
  distribution code with a real HDRI; if bias is detected, fix it in
  Slice 2 before moving on.

* **Risk:** Performance is too slow to debug iteratively against
  dining-room.
  **Mitigation:** Slice 4 starts with a tiny render resolution
  (128×128, 16 spp) to debug feature wiring, then scales up. Real
  performance work is out of M1 scope.

* **Risk:** PBRT v4 syntax edge cases (e.g. spectral parameters as
  `"spectrum"` rather than `"rgb"`) appear in dining-room.
  **Mitigation:** The parser's `Warning` path is the catch-all; any
  un-parseable parameter generates a warning and the parameter falls
  back to a documented default. Spectral values are interpreted as
  their RGB-equivalent at construction time (M0 already does this in
  the parser for known cases).

## Success Criteria (TL;DR)

M1 ships when:

1. The four target scenes (cornell, material_studio, texture_test,
   dining-room) all render through the CLI without errors.
2. dining-room's image meets the §6 quality bar.
3. CTest is green; new unit tests cover the new code paths.
4. README and architecture doc describe post-M1 reality.
