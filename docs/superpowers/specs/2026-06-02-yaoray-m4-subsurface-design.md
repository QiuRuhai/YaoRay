# YaoRay M4 — Subsurface Scattering (BSSRDF) Design

**Status:** approved (brainstorm) — pending spec review
**Date:** 2026-06-02
**Anchor scene:** ~~`ganesha`~~ → **`sssdragon`** (PBRT v4, mmp) — `ganesha.pbrt` was
found during Slice 5 triage to use `Material "coateddiffuse"` (an M3 material), not
subsurface; `sssdragon` is the correct SSS anchor.
**North Star alignment:** completes the PBRT v4 *material* feature surface — replaces the
`subsurface → diffuse` degradation with a faithful BSSRDF before the CUDA port (M5).

---

## 1. Goal

Implement physically-based subsurface scattering by faithfully reproducing PBRT v4's
**separable, tabulated BSSRDF driven by a photon beam diffusion profile**
(Habel et al. 2013; Jensen et al. 2001 dipole lineage). This is the project's first
material that is *not* a point-local BSDF: light enters at one surface point and exits at
another, which requires integrator-level and BVH-level support in addition to new BSDF
math.

The deliverable is `subsurface` materials that render the `ganesha` bust to a
reference-comparable image against PBRT v4, validated with research-grade energy /
normalization / reciprocity checks.

## 2. Scope

### In scope (the "focused subset")

- **Model:** separable tabulated BSSRDF; radial profile from photon beam diffusion.
- **Parameterization:**
  - Direct `sigma_a` + `sigma_s` (RGB spectra), with `scale` multiplier.
  - `reflectance` + `mfp` (mean free path) → inverted to `sigma_a`/`sigma_s` via the
    table's effective-albedo inversion (`SubsurfaceFromDiffuse`).
  - Phase asymmetry `g` (Henyey-Greenstein), baked into the profile via the table
    (parameterized at table-build time).
  - Index of refraction `eta` (default 1.33).
- **Channels:** RGB — three independent radial profiles sharing one table (per-channel ρ).
- **Interface:** smooth dielectric boundary (perfect Fresnel) at entry and exit.

### Out of scope (deferred)

- Named medium presets (`"skin1"`, `"marble"`, …) — table-driven coefficient lookup.
- Rough dielectric interface on the subsurface boundary.
- Anisotropic / heterogeneous media; true participating-media volumetric path tracing
  (candidate for M6).
- Spectral (>3 channel) profiles.

## 3. Physics & Algorithm (faithful to PBRT v4)

### 3.1 The separable BSSRDF

```
S(p_o, w_o, p_i, w_i) = (1 - Fr(cosθ_o)) · Sp(p_o, p_i) · Sw(w_i)
```

- **Sp(p_o, p_i) = Sr(‖p_o − p_i‖)** — spatial (radial) term; the photon-beam-diffusion
  profile evaluated at the surface distance, per RGB channel.
- **Sw(w_i) = (1 − Fr(cosθ_i)) / (c · π)** — normalized directional term, where
  `c = 1 − 2·FresnelMoment1(1/eta)`.
- **(1 − Fr(cosθ_o))** — entry-interface Fresnel transmission.

### 3.2 BSSRDFTable + photon beam diffusion (Slice 1 core)

`ComputeBeamDiffusionBSSRDF(g, eta, &table)` builds, once per (g, eta):

- A 2-D table `profile[rho][radius]` over `nRhoSamples` single-scattering albedos `rho`
  and `nRadiusSamples` optical radii.
- For each (rho, r): `profile = BeamDiffusionMS(rho_s, rho_a, g, eta, r) +
  BeamDiffusionSS(...)` — classical diffusion (multiple scattering) plus a single-scattering
  term, integrated numerically.
- `rhoEff[rho]` — the effective hemispherical albedo (integral of the profile over the
  disk), the inverse map from desired diffuse albedo to `rho`.
- Radius and rho sample positions are non-uniform (clustered near 0).

Helpers: `FresnelMoment1`, `FresnelMoment2` (polynomial approximations).

### 3.3 Catmull-Rom numeric utilities (Slice 1 infrastructure)

The table is interpolated and integrated with Catmull-Rom splines (matching PBRT):

- `CatmullRomWeights(nodes, x, &offset, weights[4])` — interpolation weights.
- `IntegrateCatmullRom(nodes, values, &cdf)` — builds the CDF used for radius sampling.
- `InvertCatmullRom(nodes, values, u)` — inverts the CDF for importance sampling.

These are self-contained, heavily unit-testable numeric functions (no scene state).

### 3.4 TabulatedBSSRDF evaluation (Slice 2)

- `Sr(r)` — per-channel: map albedo → `rho`, look up `profile` via 2-D Catmull-Rom over
  (rho, r/optical-radius), divide by `2π r` Jacobian; clamp negatives to 0.
- `Sw(w_i)`, `Sp(p_o, p_i)`, and the full `S(...)`.
- `Sample_Sr(channel, u) → r` and `Pdf_Sr(channel, r)` via the per-channel CDF (3.3).

### 3.5 Exit-point sampling — Sample_Sp / Pdf_Sp (Slice 3)

- `Sample_Sp`: choose a projection axis (3 axes, MIS weights 0.5 / 0.25 / 0.25 along the
  shading frame), choose an RGB channel, sample radius `r` via `Sample_Sr` and angle `φ`;
  build a **probe ray** parallel to the chosen axis, offset by `r`, of length spanning the
  expected profile support.
- Intersect the probe ray against the **same subsurface object**, collecting *all*
  intersections into a chain; pick one uniformly.
- `Pdf_Sp(p_i, n_i)`: MIS over the 3 axes × 3 channels, projecting the radial pdf through
  each axis using the geometric normal.

**New BVH capability:** "intersect a ray and return all hits belonging to a given object /
material" — restricted-target traversal that does not stop at the first hit. Implemented as
a bounded interaction chain to keep memory predictable.

### 3.6 Integrator flow (Slice 4)

In `cpu_path_tracer`, when a path vertex's material carries a BSSRDF:

1. Sample the entry dielectric interface BSDF as usual.
2. If the sampled direction *transmits into* the surface, hand off to the BSSRDF:
   `Sample_Sp` produces an exit point `p_i` and a surface interaction there; the exit point
   gets a `Sw`-based outgoing lobe (a normalized cosine/Fresnel BxDF adapter).
3. Perform direct lighting (MIS) at `p_i` and spawn the continuation ray from `p_i`.
4. Multiply throughput by `S / pdf`.

This is the single largest integrator change in the project; it is isolated to a new code
path that only engages for `Subsurface` materials — all existing BSDF paths are untouched.

## 4. File Plan

| File | Change |
|---|---|
| `include/yaoray/render/catmull_rom.hpp` + `src/render/catmull_rom.cpp` | **new** — Catmull-Rom weights / integrate / invert |
| `include/yaoray/render/bssrdf.hpp` + `src/render/bssrdf.cpp` | **new** — `BSSRDFTable`, `ComputeBeamDiffusionBSSRDF`, `TabulatedBSSRDF`, Fresnel moments, Sr/Sw/Sp/S, Sample_Sr/Pdf_Sr, Sample_Sp/Pdf_Sp |
| `include/yaoray/render/render_scene.hpp` | `RenderMaterialKind::Subsurface` + medium fields (`sigma_a`, `sigma_s`, `subsurface_g`, `subsurface_eta`, `subsurface_scale`, mfp/reflectance inputs); owning table storage on `RenderSceneIR` |
| `src/render/scene_compiler.cpp` | `subsurface` branch: compile medium params (direct or mfp-inverted), build/own the `BSSRDFTable`, drop the diffuse degradation |
| `src/backends/cpu/*` (BVH + path tracer) | probe-ray all-hits query; integrator subsurface entry/exit/continue flow |
| `scenes/pbrt/ganesha/README.md` | **new** — download workflow (git lfs, gitignored asset), like Pavilion/sportscar |
| `docs/architecture/overview.md` | mark M4 done; move `subsurface` into supported materials |

## 5. Slice Plan (one slice = one plan = one PR)

1. **Slice 1 — Profile core.** Catmull-Rom utilities + `BSSRDFTable` + photon beam
   diffusion precompute + Fresnel moments. Pure functions; no integrator. Heavy unit tests.
2. **Slice 2 — BSSRDF evaluation.** `TabulatedBSSRDF` Sr/Sw/Sp/S + radius
   importance sampling (Sample_Sr/Pdf_Sr). Unit-tested against the profile.
3. **Slice 3 — Exit-point sampling.** `Sample_Sp`/`Pdf_Sp` + BVH probe-ray all-hits query.
4. **Slice 4 — Integrator integration.** Wire the subsurface entry/exit/continue flow into
   `cpu_path_tracer`; medium params onto `RenderMaterial`; compiler branch. White-furnace
   energy validation.
5. **Slice 5 — ganesha anchor.** Download → smoke → discovery-driven triage/patch loop →
   full-quality render → reference comparison → per-scene README → docs refresh → mark M4
   done.

## 6. Validation (research-grade)

- **White furnace:** purely scattering medium (`sigma_a = 0`) with matched IOR → effective
  albedo → 1; assert no energy gain (mean throughput ≤ 1 + tolerance).
- **ρ_eff inversion roundtrip:** `albedo → rho → rhoEff(rho) ≈ albedo`.
- **Profile normalization:** disk integral of `Sr` over radius ≈ `rhoEff` for sampled rho.
- **Reciprocity:** `S(p_o,w_o,p_i,w_i)` symmetric under swapping endpoints (within the
  separable model's guarantees).
- **Determinism:** fixed RNG seed → identical `Sample_S` result.
- **ganesha vs PBRT reference:** committed reference image + qualitative/quantitative
  comparison, same methodology as M2/M3 anchors.
- **(Writeup-optional)** convergence curve vs sample count.

## 7. Risks

- **Integrator reach.** The exit-point flow touches the path tracer and BVH — highest-blast-
  radius change so far. Mitigation: isolate behind a `Subsurface`-only code path; keep all
  existing BSDF paths byte-identical; land BVH probe query (Slice 3) before integrator
  wiring (Slice 4) so each is independently tested.
- **Profile math correctness.** Beam diffusion is subtle (Fresnel moments, non-uniform
  sampling, Jacobians). Mitigation: Slice 1 is pure functions with normalization/inversion
  unit tests *before* anything renders; use opus for the BSSRDF math slices, sonnet for
  mechanical wiring.
- **ganesha parameterization mismatch.** If the scene uses a parameterization outside the
  focused subset (e.g. a named preset), Slice 5 triage surfaces it; fall back to the
  documented degradation policy (Warning + nearest supported parameterization) rather than
  blocking the milestone.

## 8. Acceptance

- `subsurface` performs real separable tabulated BSSRDF evaluation + sampling (no longer
  aliases to diffuse); a named Warning is emitted only for out-of-subset parameterizations.
- White-furnace, ρ_eff roundtrip, profile-normalization, reciprocity, and determinism tests
  pass.
- `ganesha` renders to a reference-comparable image; per-scene README committed.
- All prior tests stay green; total higher. No NaN/Inf in the ganesha render.
- `docs/architecture/overview.md` marks M4 done and lists `subsurface` as supported.
