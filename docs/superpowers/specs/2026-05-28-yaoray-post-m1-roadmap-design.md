# YaoRay Post-M1 Roadmap — Design

**Date:** 2026-05-28
**Status:** Approved for implementation planning
**Predecessor:** `2026-05-27-yaoray-m1-dining-room-design.md` (M1)

## North Star

YaoRay's remaining development path has three named milestones plus an
open-ended exploration phase:

```
M1 (done) ──▶ M2 (Large Scenes) ──▶ M3 (Advanced Materials) ──▶ M4 (CUDA) ──▶ M5+ (by interest)
```

**Target end state:** YaoRay renders most of the [PBRT v4 reference
scenes](https://pbrt.org/scenes-v4) on CPU at engineering-grade
performance, and optionally on CUDA for substantial GPU speedup. After
M4 the project is a "feature-complete learning renderer"; M5+ is
exploration driven by the author's interests rather than a fixed
backlog.

## Why this order

* **M2 before M4 (CUDA).** Per the operator's preference, CUDA is
  deferred until CPU rendering "feels too slow" — that pain is the
  motivation. Choosing a large anchor scene for M2 (Barcelona
  Pavilion) is the most direct way to surface that pain. The SAH BVH
  + parallel-build work M2 requires is also infrastructure CUDA will
  reuse (acceleration structure quality, scene data packing, profile
  tooling).
* **M2 before M3 (advanced materials).** Large-scene performance
  delivers immediate visual wins (a new recognizable architectural
  render) and pressures correctness across the existing feature surface.
  M3's advanced materials are math-heavy and don't depend on scene
  scale — they can land at any later milestone without coupling.
* **M3 before M4 (CUDA).** Once M3 ships, the CPU baseline is
  feature-complete and stable, so the CUDA port has a fixed target to
  match bit-for-bit.

---

## M2 — Large Scenes / Barcelona Pavilion (detailed)

### Anchor scene

Mies van der Rohe's Barcelona Pavilion, in the official PBRT v4
conversion from
[`mmp/pbrt-v4-scenes`](https://github.com/mmp/pbrt-v4-scenes). Roughly
50–100 thousand triangles (final number measured during integration),
modernist architecture, marble floor, glass walls, reflective water,
chrome cross-piece column, HDRI sky.

The asset is large and CC-licensed by its respective authors, so it
follows the dining-room pattern: downloaded to
`external/assets/pbrt/barcelona-pavilion/` (gitignored), documented
with a per-scene README under `scenes/pbrt/barcelona_pavilion/`.

### Quality bar

M2 ships when **all** of these are true.

**Scene correctness (must)**

* The unmodified Pavilion `.pbrt` renders to an image whose
  composition matches the asset's bundled reference render — marble
  floor, glass walls, reflective water, chrome column, HDRI-lit sky
  all visible and recognizable.
* Every surface declared `conductor`, `dielectric`, `coateddiffuse`,
  etc. uses its proper BSDF — no silent diffuse fallback.
* No `Error:` compiler diagnostics. Any `Warning:` is documented
  against the M1 degradation table or a new explicit reason.
* No NaN / Inf pixels (CTest automation re-uses M1's smoke check).

**Performance gate (must)**

* `dining-room` renders **≥ 2×** faster than the M1 baseline of
  ≈ 210 s at 1280×720 / 64 spp on the same hardware. Concrete
  target: **≤ 105 seconds**.
* `barcelona-pavilion` renders to completion in **≤ 30 minutes** at
  1280×720 / 64 spp.

**Performance stretch goal**

* `dining-room` renders **≥ 3×** faster (≤ 70 s). This requires the
  SAH BVH to clearly outperform median-split on the dining-room
  geometry; if the bottleneck is shading (likely the case), 2× is the
  realistic ceiling and 3× would require additional work like SIMD
  ray packets that are explicitly out of M2 scope.

### Work breakdown

**(a) SAH BVH** replaces the current median-split BVH.

* Surface Area Heuristic split: for each axis, evaluate candidate
  splits via bucket-binning (typical: 12 buckets per axis), pick the
  partition that minimizes traversal cost
  `c_T + (sum_left * A_left + sum_right * A_right) / A_parent` for
  some traversal-vs-intersection cost ratio.
* Same `RenderBvh` external layout (no IR changes); the build
  algorithm changes inside `BuildBvh`.
* Must not regress correctness — all existing BVH unit tests and
  scene CTest entries stay green.

**(b) Parallel BVH construction.**

* Top-down recursive build parallelized at coarse subtrees (e.g.,
  build root + first few levels serially, then spawn threads for the
  resulting subtrees).
* No changes to `RenderBvh` data layout — only the build is
  parallel.
* Must produce a build identical (or near-identical, within
  floating-point determinism) to the serial SAH BVH so traversal
  output stays deterministic.

**(c) Pavilion integration patches** — discovered, not pre-planned.

This mirrors M1 Slice 4's "TGA + PFM support landed because
dining-room demanded them" pattern. Likely candidates the integration
may force:

* `Texture "scale"` (texture-level multiply) if Pavilion's marble or
  water uses it.
* Multiple `LightSource "infinite"` handling — currently the second
  silently overwrites the first; if Pavilion needs both a sky and a
  ground-bounce HDRI, fix the parser to combine or pick deterministically.
* Spectral parameters PBRT v4 may carry that we currently parse as
  3-float RGB but actually have more channels — fix tolerantly.
* Whatever else surfaces. The list is closed by Pavilion, not by us.

### Slice decomposition (preliminary)

Final slice plan lands when M2's separate brainstorming runs. Best
current guess:

* **Slice 1**: SAH BVH (correctness — replace median-split, all
  existing tests + benchmarks still green; document new render times).
* **Slice 2**: Parallel BVH construction (performance — measure build
  time improvements; render time should be unchanged from Slice 1).
* **Slice 3**: Barcelona Pavilion integration (compatibility — patch
  whatever surfaces, render the unmodified scene, capture reference
  image, update docs).

Three slices, smaller than M1's four. If Slice 3 surfaces unusually
many compatibility patches, the slice can split into Slice 3a
(compatibility) + Slice 3b (final integration), but only if the work
is clearly orthogonal.

### Out of scope (deferred to M5+ or later)

* SIMD ray packets / Embree-class traversal optimizations.
* Texture cache / streaming — added only if Pavilion's textures don't
  fit in memory budget.
* CUDA work (deferred to M4).
* Advanced materials work (deferred to M3).
* Pre-planned polish items (Halton/Sobol samplers, EXR output,
  auto-tangent generation, `Texture "mix"`, true black-border wrap,
  per-vertex normal smoothing, multi-infinite-light proper handling
  unless Pavilion forces it). Each polish item lands as its own small
  PR between M2 and M3 if motivated.

### Risk register

* **Risk:** SAH BVH gives less than 2× speedup because shading
  dominates render time. **Mitigation:** measure carefully; if the
  gate isn't met, the failure is honest, not catastrophic — open an
  M2.5 mini-milestone for additional perf work (ray packets, SIMD
  intrinsics) rather than holding M2.
* **Risk:** Pavilion uses a PBRT v4 feature we have neither in M1 nor
  in our M2 compatibility list, that is hard to implement.
  **Mitigation:** triage on first surface — if it's small, fix in
  Slice 3; if it's large, route through the material-degradation
  framework or skip the affected geometry with a Warning, document
  the gap as an M3+ follow-up.
* **Risk:** parallel BVH build introduces nondeterminism. **Mitigation:**
  unit-test that the parallel build's `triangle_indices` vector is
  bitwise identical to the serial build's output for a fixed scene;
  if not, force serialization with a build option until determinism
  is restored.

---

## M3 — Advanced Materials (sketch)

### Anchor scene

To be decided at M2 finish. Candidates:

* **`ganesha`** (PBRT v4, mmp) — small geometry (one bust), SSS-heavy.
  Forces real subsurface implementation; the BSSRDF / dipole math is
  the centerpiece.
* **`sportscar`** (PBRT v4, mmp) — moderate geometry, measured
  automotive-paint BRDF. Forces tabular BRDF loading + bilinear
  interpolation in 4D angular space.
* **`killeroo-coated`** (PBRT v4, mmp) — moderate geometry, nested
  layered material (specular coat over coated diffuse over
  conductor). Forces proper layered-evaluation infrastructure.

### Unlocked features

* `subsurface` material: real BSSRDF replacing the M1 diffuse
  degradation.
* `measured` material: tabular BRDF eval with 4D angular
  interpolation, replacing the M1 default-conductor degradation.
* `layered` material: nested coating beyond M1's fixed two-layer
  `coateddiffuse` / `coatedconductor`.

### Quality bar

* The anchor scene renders to a reference-comparable composition.
* The corresponding M1 degradation Warning no longer fires for the
  scene's headline material.
* New unit tests cover BSSRDF energy conservation, measured BRDF
  bilinear correctness, layered material energy balance.

### Estimated work

Medium. Math density is higher than M2 (BSSRDF in particular has
several pitfalls — diffusion profile, sampling, energy
conservation), but the code is local to material evaluation. No
broad architectural changes expected.

---

## M4 — CUDA Backend (sketch)

### Trigger condition

After M3 ships, when the operator decides CPU render times feel
"too slow." The default expectation is that this happens; the actual
timing is driven by the operator's experience, not by the schedule.

### Architecture

Plug into the existing `RenderBackendKind::Cuda` slot on the
`Backend` interface. Reuse the existing two-stage
`Backend::Prepare → Backend::Render` interface,
substituting GPU implementations:

* CUDA `Prepare`: copies the `RenderSceneIR` flat tables to device
  memory; builds a GPU-resident BVH (or interfaces with OptiX); uploads
  textures.
* CUDA `Render`: launches a kernel per pixel; resolves the same
  shading code paths as the CPU backend, ported to device code.

### Quality bar

* `dining-room` renders **< 10 s** on the operator's GPU.
* `barcelona-pavilion` renders **< 1 minute** on the operator's GPU.
* CUDA output matches CPU output within a small per-channel tolerance
  for matched spp + seed + scene (correctness gate — same MIS, same
  Russian-roulette decisions, same sample sequences).

### Estimated work

Large. Single largest milestone in the roadmap. Includes learning
CUDA toolchain, porting BSDF/sampler/path code to device, GPU BVH
implementation (or OptiX integration), debugging GPU divergence and
register pressure issues.

---

## M5+ — Exploration

Driven by the operator's curiosity. No fixed order, no obligation
that all of these ship. Plausible candidates:

* **Denoiser integration**: OIDN or OptiX denoiser, post-process the
  HDR film before tone mapping.
* **EXR HDR output**: store unfiltered float radiance for re-tone-mapping
  workflows.
* **Volumetric rendering**: `Medium` interfaces, homogeneous absorption
  + isotropic scattering — unlocks smoke / cloud / SSS-via-volume.
* **Hair / curve primitives**: PBRT v4 `Shape "curve"` and the Marschner
  hair BSDF — unlocks Bitterli's hair scenes.
* **Subdivision surfaces**: PBRT v4 `Shape "loopsubdiv"` evaluated on
  demand — unlocks `teapot`, `killeroo` subdivision variants.
* **Spectral rendering**: full RGB → spectral pipeline, dispersion in
  glass, proper measured BRDF reconstruction.
* **Adaptive sampling**: per-pixel sample budget driven by local
  variance.
* **D-class polish**: Halton / Sobol samplers, `Texture "mix"`,
  `Texture "scale"`, auto-tangent (MikkT), true black-border wrap,
  per-vertex normal smoothing, multi-infinite-light combining, IES
  light profiles.

D-class polish items can be cherry-picked anytime between M2 and M5+
as small standalone PRs; they don't require milestone alignment.

---

## Documentation strategy

* **`docs/architecture/overview.md`** gains a concise "Roadmap"
  section at the end — one or two lines per milestone naming the
  target scene + headline feature + state (planned / in progress /
  done). High-visibility summary; this file is read first.
* **`docs/superpowers/specs/2026-05-28-yaoray-post-m1-roadmap-design.md`**
  (this document) is the authoritative roadmap. Each milestone keeps
  its detailed acceptance criteria here.
* **Each upcoming milestone** gets its own design spec and slice
  plans in `docs/superpowers/{specs,plans}/` when planning starts —
  the post-M1 roadmap is intentionally light on M3 and M4 details
  because their final designs depend on what we learn during M2.

This roadmap is itself a living document. When M2 finishes, the M3
section may be replaced by a full M3 design spec, and so on.

---

## Out of scope for this roadmap

* Detailed M3, M4, M5+ designs. They get their own brainstorming
  cycle when their turn comes; this roadmap exists to establish
  ordering, intent, and the M2 commitment.
* A timeline. Work is driven by the operator's pace, not a schedule.
* Specific commit-by-commit slice plans for M2. Those land via the
  `writing-plans` workflow once M2's standalone brainstorming
  finishes.
