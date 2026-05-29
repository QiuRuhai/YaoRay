# YaoRay M3: Layered Materials (killeroo-coated) — Design

**Date:** 2026-05-29
**Status:** Approved for implementation planning
**Predecessor:** `2026-05-28-yaoray-post-m1-roadmap-design.md` (post-M1 roadmap; M3 sketch)

## North Star

M3 is complete when YaoRay can render the
[`mmp/pbrt-v4-scenes`](https://github.com/mmp/pbrt-v4-scenes)
**killeroo-coated** scene straight from its unmodified `.pbrt` file, with
a composition matching the asset's bundled reference render, and the
`coateddiffuse` and `coatedconductor` materials perform **real two-layer
stochastic evaluation** instead of aliasing to their base BSDF.

The accompanying infrastructure — threading a random-number source into
the BSDF evaluation interface, and a `LayeredBxDF` that orchestrates a
position-free Monte Carlo random walk between layer interfaces — is the
first time YaoRay's BSDF evaluation becomes stochastic. It lays a
foundation that future stochastic materials (subsurface, etc.) can reuse.

## Why layered first

The post-M1 roadmap's M3 sketch bundled three independent advanced-material
subsystems (subsurface / measured / nested layered), each a multi-week
effort. Following M2's "one anchor scene + the features it forces" model,
M3 focuses on exactly one: **layered materials**, anchored by killeroo-coated.

Rationale for picking layered over measured or subsurface:

* **Foundational cleanup.** YaoRay's `coateddiffuse` and `coatedconductor`
  are currently *fake* — `bsdf.cpp` aliases them to plain diffuse/conductor
  evaluation, ignoring the coat entirely. Real layered evaluation makes
  these two already-"supported" materials physically honest.
* **Localized.** The work lives in BSDF evaluation. No path-tracer
  architecture change (unlike subsurface, which would need volumetrics that
  YaoRay does not have).
* **Risk-controlled.** Medium difficulty. The stochastic walk has sampling
  subtleties but is well-documented (Guo et al. 2018) and is exactly what
  PBRT v4 uses, so the reference image is directly comparable.

Measured (sportscar) and subsurface (ganesha) remain candidates for later
material milestones.

## Scope (locked decisions)

* **Anchor scene:** killeroo-coated (mmp/pbrt-v4-scenes).
* **Materials made real:** PBRT v4's `coateddiffuse` (dielectric coat over a
  diffuse substrate) and `coatedconductor` (dielectric coat over a conductor
  substrate). These are PBRT v4's native two-layer materials; killeroo-coated
  uses one or both.
* **Evaluation method:** stochastic `LayeredBxDF` random walk (Guo et al.
  2018, "A Position-Free Monte Carlo Approach for Layered Materials"), the
  method PBRT v4 itself uses. Not an analytic approximation.
* **Inter-layer medium:** the two interfaces plus a **Beer-Lambert
  absorbing medium** between them (thickness + absorption). No in-medium
  phase-function scattering.
* **RNG threading (architecture decision A):** add a `RNG&` parameter to the
  three BSDF entry points (`EvaluateBsdf`, `SampleBsdf`, `BsdfPdf`). All
  materials accept it; only layered materials use it. Non-layered materials
  remain deterministic and ignore the parameter.

## Architecture

Three reusable building blocks plus one orchestrator:

```text
LayeredBxDF (orchestrates the stochastic walk)
    ├── top interface:   rough dielectric (the coat) — reuses the existing
    │                     dielectric BSDF logic (GGX rough dielectric:
    │                     reflect + refract)
    ├── inter-layer:     Beer-Lambert absorbing medium (thickness + absorption)
    └── bottom interface: diffuse (coateddiffuse) or conductor
                          (coatedconductor) — reuses the existing base BSDF logic
```

* **Reuse, don't reinvent.** The top interface is YaoRay's existing
  dielectric BSDF (already implemented in `bsdf.cpp`). The bottom interface
  is the existing diffuse or conductor BSDF. `LayeredBxDF` only orchestrates:
  it strings the interfaces together via a random walk and applies medium
  absorption between bounces.
* **RNG threading (decision A).** `EvaluateBsdf(material, wo, wi, normal, RNG&)`,
  `SampleBsdf(material, wo, normal, sample, RNG&)`, and
  `BsdfPdf(material, wo, wi, normal, RNG&)` gain a trailing `RNG&` parameter.
  Non-layered materials ignore it; the `CoatedDiffuse` / `CoatedConductor`
  dispatch branches route into `LayeredBxDF`, which consumes random numbers
  for the walk. The path tracer already owns a per-sample/per-thread RNG and
  passes it through.
* **Data flow.** `scene_compiler.cpp` compiles `coateddiffuse` /
  `coatedconductor` into a `RenderMaterial` carrying the coat parameters
  (coat roughness, coat IOR/eta, medium thickness, medium absorption/albedo)
  plus the base parameters (reflectance for diffuse; eta/k/roughness for
  conductor), tagged `kind = CoatedDiffuse` / `CoatedConductor`. `bsdf.cpp`'s
  dispatch on those two kinds runs the `LayeredBxDF` walk instead of the
  current alias.

## The stochastic layered walk (Guo 2018)

Evaluation is a position-free Monte Carlo estimator. All three operations
become stochastic for layered materials.

**`SampleBsdf` (sample an outgoing direction):**

1. Incoming direction `wo` hits the top interface (rough dielectric).
   Importance-sample reflect vs. refract:
   * **Reflect** → this is the coat's specular lobe; exit immediately with
     that direction.
   * **Refract** → enter the medium, traveling downward.
2. Walk loop (until exit or `maxdepth`):
   * Propagate through the medium: attenuate throughput by Beer-Lambert
     `exp(-σ · thickness / |cosθ|)`.
   * At the bottom interface: sample the base BSDF (diffuse/conductor) →
     a new upward direction.
   * Propagate back up through the medium: attenuate again.
   * At the top interface from below: importance-sample reflect vs. refract.
     * **Refract out** → exit; return direction + throughput + pdf.
     * **Reflect back down** → continue the loop.
3. Throughput accumulates along the walk; the pdf is estimated from the
   per-bounce sampling decisions.

**`EvaluateBsdf` — `f(wo, wi)` with a known `wi` (used by light sampling / MIS):**
same random walk from `wo`, but at each interface interaction also performs a
next-event-style connection to `wi` through that interface's BSDF `f`,
accumulating the weighted contribution. This is the stochastic `f` estimator —
unbiased but noisy.

**`BsdfPdf` — `PDF(wo, wi)`:** a stochastic estimate consistent with
`SampleBsdf`, for MIS weighting.

**Parameters (PBRT v4 names):** `maxdepth` (default 10, walk bounce cap),
`nsamples` (default 1, walks averaged per evaluation — higher = less noise,
slower), coat `roughness` and `eta` (default 1.5), medium `thickness`
(default 0.01) and absorption. The precise mapping of PBRT's `albedo` /
`thickness` to a Beer-Lambert absorption coefficient is pinned during
Slice 2 implementation.

## Slices

Mirrors M2's "plumbing → algorithm → anchor scene" structure. Each slice
ships as its own PR-sized chunk.

### Slice 1 — RNG threading

**Goal:** Add a `RNG&` parameter to the three BSDF entry points and update
all call sites. **Zero behavior change** — every existing material ignores
the new parameter, so all 202 unit tests and 8 CTest entries stay green.

**Files:**
* Modify: `include/yaoray/render/bsdf.hpp` — add `RNG&` to the three
  function signatures.
* Modify: `src/render/bsdf.cpp` — thread the parameter through; existing
  material branches ignore it.
* Modify: `src/backends/cpu/cpu_path_tracer.cpp` (and any other BSDF call
  sites) — pass the path tracer's existing per-sample RNG.
* Modify: existing BSDF unit tests — pass a seeded RNG; assert unchanged
  results.

**Deliverables:**
* All 202 unit tests + 8 CTest entries pass, output unchanged.
* The BSDF interface now carries an RNG, ready for Slice 2's stochastic walk.

**Out of scope:** the `LayeredBxDF` itself; killeroo-coated.

### Slice 2 — `LayeredBxDF` + coated\* wiring + validation

**Goal:** Implement the stochastic layered walk; wire `CoatedDiffuse` and
`CoatedConductor` to use it; validate energy conservation and that the coat
visibly changes appearance.

**Files:**
* Create: `src/render/layered_bxdf.cpp` (+ header) — the `LayeredBxDF` walk
  (sample / eval / pdf), composing the existing dielectric top interface,
  Beer-Lambert medium, and diffuse/conductor base interface.
* Modify: `src/render/bsdf.cpp` — the `CoatedDiffuse` / `CoatedConductor`
  dispatch branches call `LayeredBxDF` instead of aliasing to the base.
* Modify: `src/render/scene_compiler.cpp` — compile the coat parameters
  (coat roughness, coat eta, thickness, absorption) onto the `RenderMaterial`
  for the two coated kinds.
* Modify: `include/yaoray/render/render_scene.hpp` — add coat-parameter
  fields to `RenderMaterial` if not already present.
* Create: `tests/layered_bxdf_tests.cpp` — energy conservation (white
  furnace), coat-changes-appearance, determinism-under-fixed-seed.
* Create: `scenes/pbrt/coated_showcase/coated_showcase.pbrt` (+ CTest entry)
  — a hand-authored synthetic scene with a row of coated spheres, exercising
  both coated kinds. Mirrors the `material_studio` committed-scene pattern.

**Deliverables:**
* `coateddiffuse` / `coatedconductor` perform real two-layer evaluation.
* White-furnace energy-conservation test passes.
* The synthetic coated-showcase scene renders and is added to CTest.
* All prior tests stay green.

**Out of scope:** killeroo-coated; in-medium scattering.

### Slice 3 — killeroo-coated integration

**Goal:** Render the unmodified killeroo-coated scene end to end, capture the
reference image, document the workflow.

**Files:**
* Create: `scenes/pbrt/killeroo_coated/README.md` — download + render
  workflow (mirrors `dining_room` / `barcelona_pavilion`).
* Create: `docs/architecture/killeroo-coated.png` — reference render.
* Modify: `README.md` — add killeroo-coated to the showcase list.
* Modify: `docs/architecture/overview.md` — bump M3 status (sketch → done);
  note real layered materials in the materials section.
* Modify (likely): `src/render/scene_compiler.cpp` or `src/pbrt/pbrt_scene.cpp`
  — whatever compatibility patches the scene forces (M2-Slice-3 pattern:
  patches discovered during integration, not pre-planned).

**Process (M2-Slice-3 discovery loop):**
1. Download killeroo-coated from `mmp/pbrt-v4-scenes` (Git LFS sparse
   checkout) to `external/assets/pbrt/killeroo-coated/`.
2. Smoke-render at low resolution to discover unsupported directives.
3. Triage each: small patch (handler) or graceful degrade (Warning +
   documented gap).
4. Full render at 1280×720 / 64 spp once composition is correct.
5. Save reference image; write the per-scene README.

**Deliverables:**
* killeroo-coated renders to completion at production quality.
* Reference image + per-scene README committed (asset stays gitignored).
* `README.md` and `docs/architecture/overview.md` reflect M3 completion.

**Out of scope:** advanced materials beyond layered; additional showcase
scenes.

## Quality bar

### Scene correctness (must)

* The unmodified killeroo-coated entrypoint renders to a composition
  matching the asset's bundled reference.
* `coateddiffuse` / `coatedconductor` perform real two-layer stochastic
  evaluation. Verifiable: the same sphere rendered with a coated material
  vs. its bare base material must look **visibly different** (the coat adds a
  Fresnel specular highlight and shifts grazing-angle response). They no
  longer alias to the base, and `MaterialFallbackWarning` does not fire for
  them (they were always "supported"; now they are real).
* No `Error:` compiler diagnostics. Any `Warning:` is either an anticipated
  M1 degradation (for *other* materials) or has a documented reason in the
  killeroo-coated README.
* No NaN / Inf pixels.

### Energy conservation (must)

* White-furnace test: a layered material under a uniform white environment
  with no absorption does not gain energy — average reflected throughput
  ≤ 1 within tolerance. Bounded energy loss is acceptable.

### Performance (soft)

* killeroo-coated completes in ≤ 10 minutes at 1280×720 / 64 spp on the dev
  sandbox. Stochastic layered BSDFs are slower than single-layer; this is a
  soft target, not a merge gate.

## Testing strategy

* **Energy conservation (new):** `LayeredBxDF` sampled many times in a white
  furnace; assert mean throughput ≤ 1 + tolerance.
* **Coat changes appearance (new):** coated sphere vs. bare-base sphere;
  assert the BSDF value differs at some viewing angle (proves the coat is
  active).
* **Determinism (new):** same RNG seed → same result (stochastic but
  reproducible).
* **RNG-threading regression (Slice 1):** all existing materials behave
  identically after gaining the RNG parameter (202 tests stay green).
* **Synthetic scene validation (Slice 2):** a hand-authored coated-showcase
  `.pbrt`, exercised by CTest (committed-scene tier, like `material_studio`).
* **No CTest entry for killeroo-coated** (asset gitignored, same pattern as
  dining-room / Pavilion).

## Risk register

* **Risk:** stochastic BSDF evaluation introduces noise / fireflies.
  **Mitigation:** PBRT's `nsamples` plus the path tracer's existing
  per-sample averaging; clamp if needed.
* **Risk:** stochastic `f` / `pdf` break MIS consistency and bias the result.
  **Mitigation:** follow PBRT v4's design exactly (the stochastic `f` and
  `pdf` are constructed to be mutually consistent); validate against a
  pure-path (no-MIS) render and the white-furnace test.
* **Risk:** energy non-conservation (a common layered-evaluation bug).
  **Mitigation:** the white-furnace unit test intercepts it.
* **Risk:** killeroo-coated uses an unsupported PBRT v4 directive.
  **Mitigation:** triage at first occurrence in Slice 3 (M2-Slice-3 pattern):
  small patch if cheap, documented graceful degrade if large.
* **Risk:** the coat dielectric interface reuse doesn't cleanly compose with
  the walk (e.g., the existing dielectric BSDF assumes a single surface, not
  an interface in a stack). **Mitigation:** Slice 2 may need a thin adapter
  around the existing dielectric eval; budget for it.

## Out of scope (later milestones)

* `subsurface` (BSSRDF / volumetrics), `measured` (tabular BRDF), `hair`,
  real `mix` — the other M3-sketch materials, each its own milestone.
* In-medium phase-function scattering (`g`) — only Beer-Lambert absorption
  is implemented.
* Arbitrary N-layer nesting beyond two layers.
* Spectral rendering (RGB only, unchanged).
