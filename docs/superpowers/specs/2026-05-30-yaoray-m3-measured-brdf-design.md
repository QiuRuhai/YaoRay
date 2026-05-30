# YaoRay M3: Measured BRDF (sportscar) — Design

**Date:** 2026-05-30
**Status:** Approved for implementation planning
**Predecessor:** `2026-05-29-yaoray-m3-layered-materials-design.md` (M3 layered phase, done) and `2026-05-29-yaoray-roadmap-refresh-design.md` (M3 = layered + measured)

## North Star

M3's measured phase is complete when YaoRay's `measured` material performs **real Dupuy & Jakob 2018 tabulated-BRDF evaluation** — loading the `.bsdf` tensor files PBRT v4 uses (the EPFL RGL material database format), evaluating and importance-sampling them faithfully — instead of degrading to a default conductor. The anchor is the `mmp/pbrt-v4-scenes` **sportscar** scene, whose automotive paint uses these measured materials; it should render with the real measured paint, composition matching the bundled reference.

This finishes M3 (layered phase already shipped). After it, the CPU material surface (diffuse, conductor, dielectric, thin, coated×2, measured) is feature-complete except subsurface (M4).

## Why faithful (locked decision)

The operator chose the **faithful Dupuy-Jakob** path (over a tabular approximation or a smarter degradation), consistent with the layered phase's "make it real" choices. Faithful means: read the actual `.bsdf` tensor format, port the warp + spline interpolation, and use the format's **data-driven importance sampling** (the representation is explicitly built to be importance-sampling-ready; its visible-NDF *is* the sampling density, so `f` and `pdf` are mutually consistent and MIS is clean — the same quality bar as layered, but here the evaluation is a **deterministic table lookup**, not a stochastic walk: no noise, and it does not consume the BSDF RNG).

## Scope (locked decisions)

* **Anchor scene:** sportscar (mmp/pbrt-v4-scenes).
* **Material made real:** PBRT v4's `measured` material (`"string filename"` → a `.bsdf` tensor file).
* **Representation:** Dupuy & Jakob 2018 ("An Adaptive Parameterization for Efficient Material Acquisition and Rendering"), the method PBRT v4 itself uses. Not an analytic fit.
* **Anisotropy:** **isotropic-only** first. Covers sportscar's car paint and most RGL materials. Anisotropic (brushed metal / fabric) is deferred; if the sportscar asset turns out to need it, that surfaces during Slice 4 integration and is triaged then (degrade with Warning, or a follow-up).
* **Color:** RGB (YaoRay is RGB-only, unchanged). The tensor's luminance table drives sampling; the spectral/RGB grid drives the value.
* **Evaluation is deterministic** (table lookup). The `Rng&` parameter on the BSDF entry points (from the layered phase) is ignored by `measured`, exactly as the non-layered materials ignore it.
* **Graceful degradation preserved:** a corrupt file, an anisotropic file (while isotropic-only), or any load failure → the existing conductor fallback + a `Warning` (the "never `Error:`" policy holds).

## Architecture

Three new units plus a dispatch wiring:

```text
.bsdf tensor file ──TensorReader──▶ MeasuredBrdf (in-memory tables + warp)
                                        │
RenderSceneIR.measured_brdfs[]  ◀───────┘   (loaded once at compile time, indexed per material)
RenderMaterial { kind = Measured, measured_index }
                                        │
bsdf.cpp dispatch(Measured) ──▶ MeasuredBxDF::{f, Sample, Pdf}
                                  (warp + Catmull-Rom spline interpolation + data-driven vNDF sampling)
```

1. **TensorReader** (new, ~150 lines): reads PBRT's binary "tensor file" container — a header plus named typed arrays (`theta_i`, `phi_i`, `ndf`, `sigma`, `vndf` + its marginal/conditional CDFs, `luminance`, `spectra`/RGB, `wavelengths`, plus `isotropic` and `jacobian` flags). The format is small and documented; YaoRay writes its **own minimal reader** (no external dependency, consistent with the project's vendoring style). It validates the fields it needs and reports a structured failure (→ graceful degrade) on anything malformed or unsupported (e.g. anisotropic while isotropic-only).

2. **MeasuredBrdf** (new data structure): the loaded tables plus the Dupuy-Jakob `PiecewiseLinear2D` warp distribution (marginal + conditional, supporting `sample` / `invert` / `eval`). Isotropic reduces the warp's dimensionality. Stored in a flat `RenderSceneIR.measured_brdfs` table (mirroring how `textures` are stored); `RenderMaterial` gains a `measured_index` (-1 when not measured).

3. **MeasuredBxDF** (new, inside the `bsdf.cpp` evaluation surface): `f` / `Sample` / `Pdf`, a faithful port of PBRT's `MeasuredBxDF` + the `PiecewiseLinear2D` warp + Catmull-Rom spline weights. A new `RenderMaterialKind::Measured` is added and dispatched in all three BSDF entry points.

**Data-flow / compile wiring:** the `measured` compile branch stops degrading; it resolves `filename` against the scene's source roots, runs the TensorReader, pushes the result into `measured_brdfs`, sets `kind = Measured` + `measured_index`. On any failure it falls back to the conductor degrade + Warning.

## The BSDF math (faithful Dupuy-Jakob, isotropic)

The representation stores the BRDF in a domain **already warped for importance sampling**. The reusable core is a `PiecewiseLinear2D` distribution (marginal + conditional CDFs with `sample`/`invert`/`eval`) plus Catmull-Rom spline interpolation.

**`f(wo, wi)` (deterministic):**
1. Reflection hemisphere only (measured data is reflectance); for isotropic, rotate the frame so the incident azimuth is canonical.
2. Half-vector `wm = normalize(wo + wi)`; map `(theta_i, wm)` through the stored warp (**inverse** warp, `PiecewiseLinear2D::invert`) to grid coordinates `(u, v)`.
3. `f = spectra(u, v)` (RGB value, Catmull-Rom interpolated) `× ndf(...) / (4 · sigma(theta_i))`, times a global scale. `sigma` is the projected-area normalization.

**`Sample(wo, u2d)`:**
1. Sample the **visible-NDF (vndf)** via marginal/conditional CDF inversion using the 2D sample → warped `(u,v)` → half-vector `wm`.
2. `wi = 2 (wo · wm) wm − wo` (reflect about `wm`).
3. `pdf = vndf(...) / (4 |wo · wm|)` (the reflection Jacobian). Returns `weight = f · cos(wi) / pdf` per YaoRay's `SampleBsdf` convention.

**`Pdf(wo, wi)`:** forward-evaluate the vndf at the warped `wm` coordinates → density `/ (4 |wo · wm|)`.

**Why MIS is clean:** the vndf *is* the sampling density, so `f` and `pdf` are mutually consistent by construction. Unlike layered, evaluation is a deterministic lookup — no stochastic estimator, no firefly risk.

**The two large ports:** (1) the tensor-file reader; (2) the `PiecewiseLinear2D` warp class (`sample`/`invert`/`eval`) + Catmull-Rom weights. Isotropic-only lowers the warp dimensionality and simplifies both.

## Slices

Mirrors the layered phase's "plumbing → algorithm → sampling → anchor scene" cadence. Each slice ships as its own PR.

### Slice 1 — Tensor reader + MeasuredBrdf table + compile wiring

**Goal:** Load a `.bsdf` file into the `MeasuredBrdf` representation and carry it through compilation. No rendering change yet.

* New: TensorReader; `MeasuredBrdf` struct + `PiecewiseLinear2D` storage (the warp tables held, eval/sample come in Slices 2-3).
* Modify: `RenderSceneIR` gains `measured_brdfs[]`; `RenderMaterial` gains `measured_index` (-1 default); `RenderMaterialKind::Measured` added.
* Modify: `scene_compiler.cpp` `measured` branch loads the file (graceful degrade on failure); BSDF dispatch for `Measured` temporarily aliases the conductor (no eval yet).
* Validation: parse a real (vendored small) and a synthetic `.bsdf`, assert fields; corrupt / anisotropic file degrades with a Warning. All existing tests stay green.

### Slice 2 — Evaluation `f` + warp/spline interpolation

**Goal:** Real `measured` `f(wo, wi)`.

* New: `PiecewiseLinear2D::eval` + Catmull-Rom interpolation + `MeasuredBxDF::f`; wire `EvaluateBsdf` for `Measured`.
* Validation: `f` finite and non-negative; directional albedo `ρ = ∫ f·cosθ dω ≤ 1` (white-furnace-style energy check, no gain); approximate reciprocity `f(wo,wi) ≈ f(wi,wo)`; a known value reproduced for the vendored test material.

### Slice 3 — Data-driven `Sample` + `Pdf`

**Goal:** Real importance sampling consistent with `f`.

* New: `PiecewiseLinear2D::sample`/`invert` (the vNDF CDF inversion); `MeasuredBxDF::Sample`/`Pdf`; wire `SampleBsdf` / `PdfBsdf` for `Measured`.
* Validation: sampled `wi` valid (upper hemisphere, finite); `∫ pdf dω ≈ 1`; furnace (mean sampled weight ≤ 1 + tolerance); `f`/`pdf` consistency; determinism (fixed 2D sample → fixed result).

### Slice 4 — sportscar integration (discovery-driven)

**Goal:** Render the unmodified sportscar end to end; capture a reference; document.

* Process mirrors the killeroo-coated / Pavilion discovery loop: download (Git LFS sparse), smoke render, triage each unsupported directive (small patch or documented degrade), full render, reference image, per-scene README, project-wide docs refresh.
* Deliverables: `docs/architecture/sportscar.png`, `scenes/pbrt/sportscar/README.md`, README + overview showcase rows, **M3 roadmap row → done** (both phases now complete). No CTest entry (asset gitignored).

## Quality bar

### Scene correctness (must)
* The unmodified sportscar entrypoint renders to a composition matching the asset's bundled reference; the car paint shows its real measured response (not the conductor fallback).
* `MaterialFallbackWarning` no longer fires for `measured` materials whose `.bsdf` loads successfully.
* No `Error:` diagnostics; any `Warning:` is M1-anticipated (for other materials) or documented in the sportscar README.
* No NaN / Inf pixels.

### Energy / sampling (must)
* `f` directional albedo `≤ 1` (white-furnace-style); `∫ pdf dω ≈ 1`; `f`/`pdf` consistent.

### Performance (soft)
* sportscar completes in a reasonable wall-clock on the dev sandbox (measured eval is a cheap deterministic lookup; the cost driver is scene scale, not the BSDF). Not a merge gate.

## Testing strategy

* **Test data (dependency):** unit tests need `.bsdf` files. **Both** of:
  * **(a)** vendor one **small real `.bsdf`** from the RGL database (CC-licensed, tens of KB) for evaluation/sampling tests against genuine data;
  * **(b)** a **synthetic minimal `.bsdf`** written byte-by-byte by a test helper, for reader edge cases (truncation, bad magic, unsupported flags) with zero external dependency.
  sportscar's bundled `.bsdf` files drive the Slice 4 integration render.
* **Reader (Slice 1):** field-level assertions on (a) and (b); malformed/anisotropic → structured failure → degrade.
* **Eval (Slice 2):** energy (ρ ≤ 1), finiteness, reciprocity, a reproduced known value.
* **Sampling (Slice 3):** valid directions, pdf normalization, furnace, f/pdf consistency, determinism.
* **Integration (Slice 4):** sportscar smoke + full render, no NaN, composition check. No CTest entry (asset gitignored, same as killeroo-coated / Pavilion / dining-room).

## Risk register

* **Risk:** the tensor-file format has undocumented corners. **Mitigation:** PBRT v4's reader is the reference; vendor a real `.bsdf` early (Slice 1) and diff field-by-field against PBRT's parse; degrade gracefully on anything unrecognized.
* **Risk:** the `PiecewiseLinear2D` warp + Catmull-Rom math is subtle (the highest-pitfall part). **Mitigation:** port PBRT faithfully; pin with energy (ρ ≤ 1), reciprocity, pdf-normalization, and f/pdf-consistency tests — the same tests-as-contract approach that caught the layered sign bugs. Dispatch the math slices to the most capable model.
* **Risk:** sportscar uses an anisotropic `.bsdf` (while isotropic-only). **Mitigation:** detect in the reader → graceful degrade + documented Warning; if it materially hurts the reference, scope a follow-up for anisotropy.
* **Risk:** sportscar uses other unsupported PBRT v4 directives (geometry, lights). **Mitigation:** the Slice 4 discovery loop triages them (killeroo-coated pattern).

## Out of scope (later milestones)

* **Anisotropic** measured BRDFs (deferred; isotropic-only first).
* `subsurface` (M4), volumetrics, hair, real `mix` — separate milestones.
* Spectral rendering (RGB only, unchanged).
* Writing/exporting `.bsdf` files (read-only).
* Re-fitting or re-parameterizing measured data (load PBRT-format files as-is).
