# YaoRay Roadmap Refresh — Design

**Date:** 2026-05-29
**Status:** Approved — authoritative roadmap
**Supersedes:** `2026-05-28-yaoray-post-m1-roadmap-design.md`

This document replaces the post-M1 roadmap. M2 has shipped, M3 is in
progress, and the remaining route was re-derived from the current
state in a full-rewrite brainstorming pass. The predecessor doc is kept
for history but is no longer authoritative.

## North Star

**Unchanged: PBRT v4 scene coverage on CPU.** YaoRay aims to render as
many of the [PBRT v4 reference scenes](https://pbrt.org/scenes-v4) as
possible on a multi-threaded CPU backend at engineering-grade quality,
completing the material and geometry feature surface, and *then* porting
the frozen CPU surface to CUDA for GPU speedup. Breadth features
(volumetrics, hair, subdivision, spectral) trail after the GPU port as
interest-driven exploration.

```
M1 (done) ──▶ M2 (done) ──▶ M3 (in progress) ──▶ M4 ──▶ M5 ──▶ M6+
```

| Milestone | Anchor scene(s) | Headline | Status |
|---|---|---|---|
| **M1** | cornell_box / material_studio / texture_test / dining-room | PBRT v4 frontend + core BSDFs + textures | done |
| **M2** | barcelona-pavilion | SAH BVH + parallel build; large scenes | done |
| **M3** | killeroo-coated **+** sportscar | Advanced Materials I: stochastic layered (coated) **+** measured BRDF | in progress |
| **M4** | ganesha | Subsurface scattering (BSSRDF) | planned |
| **M5** | dining-room < 10 s / barcelona-pavilion < 1 min | CUDA backend — bit-for-bit GPU port of the frozen CPU surface | planned |
| **M6+** | by interest | Breadth: volumetrics/media, hair + curves, subdivision, spectral, denoiser, adaptive sampling, sampler polish | exploratory |

After M5 the project is a "feature-complete learning renderer with a GPU
backend." M6+ has no fixed order and no obligation that every item ships.

## Why this order

Chosen explicitly over alternatives in brainstorming (full route rewrite,
North Star = coverage, M3 = layered + measured, post-M3 spine =
*materials-complete → CUDA → breadth*):

* **Materials complete (through M4) before CUDA (M5).** Finishing the
  CPU material surface — layered (M3), measured (M3), subsurface (M4) —
  gives the CUDA port a *frozen, feature-complete, bit-for-bit*
  reference target in M5. This is the same "feature-complete CPU
  baseline → port" logic the predecessor doc used to place CUDA after
  advanced materials; it is preserved, with subsurface folded into the
  materials phase rather than left ambiguous.
* **measured joins layered in M3.** Both are reflection-class BSDFs with
  lower math risk than BSSRDF. Grouping them keeps the highest-risk
  subsurface math isolated in its own milestone.
* **subsurface isolated as M4.** BSSRDF (diffusion profile, importance
  sampling, energy conservation) is the highest-pitfall math in the
  remaining route. It earns a dedicated milestone with its own anchor
  (`ganesha`) so its risk does not bleed into other work.
* **CUDA at M5, not earlier.** The North Star is *coverage*, not raw
  performance. CUDA waits until the CPU material surface is complete so
  the GPU port has a stable target and the correctness gate (CPU vs GPU
  bit-for-bit within tolerance) is meaningful.
* **Breadth (M6+) after CUDA.** Volumetrics, hair, subdivision, and
  spectral are *additive* — they extend the renderable scene set but do
  not block the CUDA port. They trail after M5 on whichever backend, in
  interest-driven order.

---

## M3 — Advanced Materials I: Layered + Measured (in progress)

### Anchor scenes

* **`killeroo-coated`** (PBRT v4, mmp) — layered/coated material:
  specular/rough dielectric coat over a diffuse or conductor base.
  Forces the stochastic `LayeredBxDF` walk.
* **`sportscar`** (PBRT v4, mmp) — measured automotive-paint BRDF.
  Forces tabular measured-BRDF loading + angular interpolation.

### Unlocked features

* `coateddiffuse` / `coatedconductor`: real two-layer stochastic
  evaluation (Guo et al. 2018 position-free Monte Carlo) — rough
  dielectric coat + Beer-Lambert absorbing medium + diffuse/conductor
  base — replacing the current alias-to-base behaviour.
* `measured`: tabular BRDF evaluation with angular interpolation,
  replacing the M1 default-conductor degradation.

### Design references

* Layered materials: `2026-05-29-yaoray-m3-layered-materials-design.md`
  (committed). Locked decisions: 2-layer coated done for real; stochastic
  walk; Beer-Lambert medium; RNG threaded into all three BSDF entry
  points (Decision A).
* Measured BRDF: gets its own brainstorming + design spec when its
  slices start (see slice state below).

### Slice state (layered phase)

| Slice | Scope | Status |
|---|---|---|
| Slice 1 | RNG threading — `Rng&` through `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf`; per-pixel-sample stream in path tracer | done (PR #10) |
| Slice 2a | Stochastic `LayeredBxDF` `Sample_f` walk + `coateddiffuse`/`coatedconductor` wiring + coat-param compile + validation (white-furnace, coat-changes-appearance, determinism) + `coated_showcase` scene | planned (plan written) |
| Slice 2b | Stochastic `f`/`pdf` estimators for the layered BSDF (light-sampling MIS consistency — the 2a MIS gap) | planned |
| Slice 3 | `killeroo-coated` integration: render the unmodified scene, capture reference, patch whatever surfaces, update docs | planned |

### Slice state (measured phase)

| Slice | Scope | Status |
|---|---|---|
| Slice 4+ | Measured BRDF: tabular file loader, angular-domain interpolation, `measured` material wiring, `sportscar` integration + scene/CTest | to be brainstormed |

The measured-phase decomposition lands when its standalone brainstorming
runs; the layered phase is mid-flight and its slice plan is the current
working set.

### Quality bar

M3 ships when **all** are true.

* `coateddiffuse` / `coatedconductor` do real two-layer stochastic
  evaluation (no longer alias to base); `measured` does real tabular
  evaluation (no longer degrades to default conductor).
* `killeroo-coated` and `sportscar` each render to a
  reference-comparable composition with their headline material active —
  no silent diffuse/conductor fallback for the headline surface.
* The corresponding M1 degradation Warnings (`coated*` alias, `measured`
  → conductor) no longer fire for the anchor scenes' headline materials.
* New unit tests cover layered energy conservation (white-furnace ≤ 1 +
  tolerance), coat-changes-appearance, determinism-under-seed, and
  measured-BRDF interpolation correctness.
* No `Error:` diagnostics; any `Warning:` documented. No NaN / Inf pixels
  (CTest smoke check, as for M1/M2).
* All prior unit + CTest tests stay green; the new total is higher.

### Out of scope (M3)

* In-medium phase-function scattering (`g`) in the coat — Beer-Lambert
  absorption only.
* N-layer nesting beyond two interfaces.
* Subsurface, volumetrics, hair, subdivision, spectral.
* Arbitrary `mix` material composition.

### Risk register (M3)

* **Risk:** the stochastic layered walk introduces energy gain or
  fireflies. **Mitigation:** Russian-roulette reflect/transmit by Fresnel
  keeps throughput unbiased; white-furnace test asserts mean throughput
  ≤ 1 + tolerance; the 2a/2b split lets `Sample_f` land and be validated
  before the `f`/`pdf` MIS estimators.
* **Risk:** measured BRDF file format / parameterization is more involved
  than expected. **Mitigation:** triage on first contact with the
  `sportscar` data; if the full parameterization is too large, scope the
  measured phase to the representation that scene actually uses and
  document the gap.

---

## M4 — Subsurface Scattering / BSSRDF (planned)

### Anchor scene

* **`ganesha`** (PBRT v4, mmp) — small geometry (one bust), SSS-heavy.
  The BSSRDF / diffusion math is the centerpiece.

### Unlocked features

* `subsurface` material: real BSSRDF replacing the M1 diffuse
  degradation. Diffusion-profile evaluation, separable BSSRDF importance
  sampling, single-scattering / diffusion split as appropriate.

### Quality bar

* `ganesha` renders to a reference-comparable composition with visible
  subsurface translucency.
* The M1 `subsurface → diffuse` degradation Warning no longer fires for
  the anchor's headline material.
* New unit tests cover BSSRDF energy conservation and diffusion-profile
  sampling correctness.
* No NaN / Inf; all prior tests green.

### Estimated work

Medium-high. Math density is the highest in the remaining route. Code is
local to material/integration evaluation — no broad architectural
changes expected, but the sampling and energy-conservation pitfalls are
significant. Its own brainstorming + design spec + slice plan when it
starts.

---

## M5 — CUDA Backend (planned)

### Trigger

Begins after M4 ships, when the CPU material surface is complete and
frozen. The North Star defers CUDA to here deliberately: a stable,
feature-complete CPU baseline is the precondition for a meaningful
bit-for-bit GPU port.

### Architecture

Plug into the existing `RenderBackendKind::Cuda` slot on the `Backend`
interface; reuse the two-stage `Backend::Prepare → Backend::Render`
contract with GPU implementations:

* CUDA `Prepare`: copy the `RenderSceneIR` flat tables to device memory;
  build (or upload) a GPU-resident BVH; upload textures.
* CUDA `Render`: launch a kernel per pixel resolving the same shading
  code paths as the CPU backend, ported to device code.

### Quality bar

* `dining-room` renders **< 10 s** on the operator's GPU.
* `barcelona-pavilion` renders **< 1 minute** on the operator's GPU.
* CUDA output matches CPU output within a small per-channel tolerance for
  matched spp + seed + scene — same MIS, same Russian-roulette decisions,
  same sample sequences (correctness gate). The RNG threading from M3
  Slice 1 is what makes a reproducible cross-backend sample stream
  possible.

### Estimated work

Large — the single biggest milestone. Includes learning the CUDA
toolchain, porting BSDF/sampler/path code to device, GPU BVH (or OptiX)
integration, and debugging divergence / register pressure. Its own
brainstorming + design spec + slice plan when it starts.

---

## M6+ — Breadth / Exploration (exploratory)

Interest-driven, no fixed order, no obligation that all ship. Each item
that is taken up gets its own brainstorming → spec → plan cycle.

* **Volumetrics / media.** `Medium` interfaces; homogeneous absorption +
  isotropic scattering. Unlocks smoke / cloud scenes and volumetric SSS.
* **Hair / curve primitives.** PBRT v4 `Shape "curve"` + Marschner hair
  BSDF. Unlocks Bitterli's hair scenes and killeroo fur.
* **Subdivision surfaces.** PBRT v4 `Shape "loopsubdiv"` evaluated on
  demand. Unlocks teapot / killeroo subdivision variants.
* **Spectral rendering.** RGB → spectral pipeline; dispersion in glass;
  proper measured-BRDF reconstruction.
* **Denoiser integration.** OIDN or OptiX denoiser post-processing the
  HDR film before tone mapping.
* **Adaptive sampling.** Per-pixel sample budget driven by local variance.
* **Sampler / polish (D-class).** Halton / Sobol / pmj02bn samplers,
  `Texture "mix"`, auto-tangent (MikkT), true black-border wrap,
  per-vertex normal smoothing, multi-infinite-light combining, IES light
  profiles. Cherry-pickable anytime as small standalone PRs.

### Deferred small fixes (tracked, not milestone-bound)

From the external code review; land as small PRs when convenient, not
gating any milestone:

* Light-sampling PDF mismatch (P1#2).
* Include per-source-root search paths (P1#3).
* Input bounds validation (P2#4).
* `ObjectInstance` sphere + default-material handling (P2#5).

---

## Documentation strategy

* **`docs/architecture/overview.md`** carries the concise Roadmap *digest*
  (one row per milestone: anchor + headline + state) — the first file a
  reader sees. Refreshed alongside this doc.
* **This document** is the authoritative roadmap with per-milestone
  acceptance criteria, rationale, risk, and M3 slice state.
* **Each milestone** gets its own design spec + slice plans under
  `docs/superpowers/{specs,plans}/` when its planning starts. The route
  beyond M3 is intentionally light on M4/M5 internals because those
  designs depend on what M3 (and the operator's GPU) teach us.

This roadmap is a living document. When a milestone finishes or its scope
firms up, the relevant section is revised or replaced by that milestone's
full design spec.

## Out of scope for this roadmap

* Detailed M4 / M5 / M6+ designs — they get their own brainstorming cycle
  when their turn comes.
* A timeline. Work is driven by the operator's pace, not a schedule.
* Commit-by-commit slice plans — those land via the `writing-plans`
  workflow per milestone (M3's layered-phase plans already exist).
