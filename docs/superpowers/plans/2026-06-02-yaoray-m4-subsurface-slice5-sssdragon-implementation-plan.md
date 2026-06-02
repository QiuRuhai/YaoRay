# M4 Subsurface Slice 5 — sssdragon Anchor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Anchor M4 on a real PBRT v4 subsurface scene — render mmp's `sssdragon` to a reference-comparable image, add the minimal faithful `"Skin1"` named-preset + `scale` support it needs, fix whatever the smoke render surfaces, and close out M4 (per-scene README + docs).

**Discovery note (why not ganesha):** the roadmap named `ganesha` as the M4 anchor, but `ganesha.pbrt` uses `Material "coateddiffuse"` (an M3 material), not `subsurface`. The canonical PBRT v4 subsurface scene is **`sssdragon`** (`Material "subsurface" "string name" "Skin1" "float scale" 50 "float eta" 1.5`). This slice pivots the anchor to `sssdragon` and corrects the roadmap.

**Architecture:** Mostly discovery-driven (download → smoke → triage → render → docs), like the M2 Pavilion / M3 killeroo / M3 sportscar anchor slices. One up-front code task adds `"Skin1"` (pbrt's `SubsurfaceParameterTable` coefficients) + the `scale` multiplier + the `"string name"` path to the `subsurface` compiler branch, staying within the focused-subset spirit (one faithful preset; unknown presets degrade with a Warning).

**Tech Stack:** C++20, `yr_test.hpp`, CMake + MSVC, CTest. The asset lives under Git LFS in `mmp/pbrt-v4-scenes` (gitignored under `external/assets/`).

**Base branch:** local `main` (now at `6a10d2f`, after Slice 4 merged). **Worktree:** `m4-subsurface-slice5`.

**Asset location (already downloaded by the controller):** `external/assets/pbrt/sssdragon/` containing `dragon_10.pbrt` / `dragon_50.pbrt` / `dragon_250.pbrt`, `geometry/dragon.ply.gz`, `geometry/meshes_0.ply`, `textures/small_rural_road_equiarea.exr`.

**Build & render:**
```
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
build/Release/yaoray.exe render external/assets/pbrt/sssdragon/dragon_50.pbrt --backend cpu
```
After Slice 4 the unit suite has **339 tests**. clangd "stale index" errors are FALSE POSITIVES.

---

## Context the implementer needs

- **The `subsurface` compiler branch** (`src/render/scene_compiler.cpp`, the `} else if (type == "subsurface") {` block landed in Slice 4): currently reads `sigma_a`/`sigma_s` via `TexParam3fFromParams(...).value`, `eta` via `FloatParam(FindParam(params,"eta"),1.33f)`, builds a `BSSRDFTable(100,64)` with `ComputeBeamDiffusionBSSRDF(0, eta, *table)`, and degrades to diffuse + `MaterialFallbackWarning` only when scattering is zero. Param helpers in this file: `FindParam`, `FloatParam(ptr, fallback)`, `StringParam(ptr, fallback)`, `TexParam3fFromParams(...)`. `Warning(scene, "tag", "msg")` builds a diagnostic.
- **pbrt `Skin1` coefficients** (pbrt-v3/v4 `SubsurfaceParameterTable`, units mm⁻¹): `sigma_a = (0.032, 0.17, 0.48)`, reduced scattering `sigma_s' = (0.74, 0.88, 1.01)`. Our table is built with `g = 0`, so `sigma_s = sigma_s'` is consistent. pbrt's `subsurface` multiplies both by `scale`.
- **Material fields** (Slice 4): `RenderMaterial{ RenderMaterialKind kind; Color3f sigma_a, sigma_s; float bssrdf_eta; int bssrdf_index; const BSSRDFTable* bssrdf_table; }`; `RenderSceneIR::bssrdf_tables` (vector of `unique_ptr<BSSRDFTable>`).
- **Known render risks to investigate during triage (Tasks 2–3):**
  1. **`dragon.ply.gz` is gzip-compressed.** The PLY loader likely expects uncompressed `.ply`. Options: decompress in place (`gunzip -k`) and point the scene at `dragon.ply`, OR add transparent `.gz` handling to the loader. Prefer the smallest change that keeps the committed scene-doc honest (document any local tweak in the README, like the sportscar README documents the Film-resolution edit).
  2. **Equal-area environment map.** `small_rural_road_equiarea.exr` uses PBRT v4's equal-area octahedral parameterization, NOT lat-long/equirectangular. YaoRay's environment importance sampling assumes lat-long. Expect an environment-orientation/projection mismatch; triage decides whether to degrade (Warning) or handle it. The dragon itself (SSS) is the subject — env fidelity is secondary.
  3. **`Film "float iso" 400`** — film exposure/ISO; if unsupported it is ignored → overall brightness differs (tone-map at view time; acceptable).
  4. **`halton` sampler** already degrades to `independent` (M1 policy).
  5. **The pedestal mesh** `meshes_0.ply` uses `coateddiffuse` — supported (M3).
- **Per-scene README pattern**: see `scenes/pbrt/sportscar/README.md` (download workflow via sparse Git-LFS checkout, render commands, reference-image note).
- **Reference-image convention**: prior anchors committed a downsized PNG under `docs/architecture/` (e.g. `docs/architecture/sportscar.png`). Mirror that.
- **The bar is "reference-comparable," not pixel-exact** — the prior anchors (Pavilion/killeroo/sportscar) matched composition + material character, not exact pixels.

---

### Task 1: "Skin1" named preset + `scale` support

**Files:** `src/render/scene_compiler.cpp`, `tests/scene_compiler_subsurface_tests.cpp` (append), `CMakeLists.txt` (no change — file already registered).

- [ ] **Step 1: Append a failing test to `tests/scene_compiler_subsurface_tests.cpp`**

Following that file's existing harness (read it first), add a test that compiles a `subsurface` material with `"string name" "Skin1"`, `"float scale" 50`, `"float eta" 1.5` and asserts:
- compiled `material.kind == RenderMaterialKind::Subsurface`
- `material.bssrdf_eta == 1.5f`
- `material.sigma_s.x ≈ 0.74 * 50 = 37.0f` (within 1e-2) and `material.sigma_a.x ≈ 0.032 * 50 = 1.6f`
- `material.bssrdf_table != nullptr`

Use the same parse/compile helper the existing subsurface test uses; mirror its assertion style. (If the param-construction harness for a `"string name"` param is unclear, read how another test builds a `"string"`-typed `PbrtParam` and copy it.)

- [ ] **Step 2: Build, confirm RED** (the preset path doesn't exist yet, so `sigma_s.x` will be the direct-default `2.55`, not `37.0`).

- [ ] **Step 3: Extend the `subsurface` compiler branch in `src/render/scene_compiler.cpp`**

At the top of the `subsurface` branch, before reading `sigma_a`/`sigma_s`, insert the preset + scale logic:
```cpp
        const std::string preset = StringParam(FindParam(params, "name"), "");
        const float scale = FloatParam(FindParam(params, "scale"), 1.0f);
        Color3f sigma_a;
        Color3f sigma_s;
        if (!preset.empty()) {
            // pbrt SubsurfaceParameterTable (mm^-1). Our table uses g=0, so the
            // reduced scattering coefficient sigma_s' is used directly as sigma_s.
            if (preset != "Skin1") {
                diagnostics.push_back(Warning(scene, "Material.subsurface",
                    "unsupported subsurface preset '" + preset + "'; using Skin1 coefficients."));
            }
            sigma_a = Color3f{0.032f, 0.17f, 0.48f};
            sigma_s = Color3f{0.74f, 0.88f, 1.01f};
        } else {
            sigma_a = TexParam3fFromParams(params, "sigma_a",
                Color3f{0.0011f, 0.0024f, 0.014f}, bindings, scene, diagnostics).value;
            sigma_s = TexParam3fFromParams(params, "sigma_s",
                Color3f{2.55f, 3.21f, 3.77f}, bindings, scene, diagnostics).value;
        }
        sigma_a = sigma_a * scale;
        sigma_s = sigma_s * scale;
        const float eta = FloatParam(FindParam(params, "eta"), 1.33f);
```
Then keep the existing "is it scattering? build table : degrade" logic, but using these `sigma_a`/`sigma_s`/`eta` locals (remove the now-duplicated reads of `sigma_a`/`sigma_s`/`eta` further down). Ensure the final branch still sets `kind=Subsurface`, `material.sigma_a/sigma_s/bssrdf_eta`, builds the table, and sets `bssrdf_index`/`bssrdf_table` exactly as before.

- [ ] **Step 4: Build + run; confirm the new test PASSES and the full suite is green (340 total). Commit.**
```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_subsurface_tests.cpp
git commit -m "feat(bssrdf): subsurface Skin1 named preset + scale (M4 slice 5)"
```

---

### Task 2: Smoke render + diagnostics

**Files:** none committed yet (investigation). The asset is already at `external/assets/pbrt/sssdragon/`.

- [ ] **Step 1: Build the CLI** (`cmake --build build --config Release`).

- [ ] **Step 2: Handle `dragon.ply.gz`.** Try the smoke render of `dragon_50.pbrt`; if the PLY loader errors on the `.gz`, decompress: `gunzip -k external/assets/pbrt/sssdragon/geometry/dragon.ply.gz` (yields `dragon.ply`) and make a LOCAL working copy of `dragon_50.pbrt` (e.g. `dragon_50_local.pbrt`) that references `geometry/dragon.ply`, OR add `.gz` support to the loader if that is cleaner. Record which you did.

- [ ] **Step 3: Low-resolution smoke render** — temporarily render at low res / low spp for speed (e.g. copy the scene and set `xresolution`/`yresolution` to 320×240 and `pixelsamples` to 16, or pass CLI overrides if supported). Run:
```
build/Release/yaoray.exe render <local dragon_50 scene> --backend cpu
```
- [ ] **Step 4: Capture ALL diagnostics** (warnings/errors printed at compile/render) and whether an output image was produced. Note: NaN/black/garbage regions, missing geometry, environment orientation, brightness. Write a concise findings list (this drives Task 3). Do NOT fix yet — just triage.

- [ ] **Step 5: Report findings to the controller** (no commit). List each issue with severity and a proposed fix.

---

### Task 3: Triage + patch loop (discovery-driven)

**Files:** whatever the findings require (loader, environment, scene_compiler, …) + tests for any code fix.

- [ ] For each issue from Task 2, in priority order: write a focused fix (with a regression test where it is a code change in a unit-testable path), rebuild, re-smoke, confirm improvement. Keep the full unit suite green after each fix. Commit each fix separately with a clear message. Likely items: `.ply.gz` handling, equal-area env-map projection (degrade-with-Warning is acceptable if full support is too large), any subsurface-specific artifact (e.g. exit-normal orientation on the concave dragon, energy loss from probe misses). 
- [ ] If the white-furnace-style energy concern from Slice 4 (low return at large mean-free-path) shows up as an obviously-too-dark dragon, investigate the probe miss handling / exit-normal face-forwarding; but if the dragon reads as a plausible waxy translucent surface, that is the bar — note residual differences in the README rather than chasing pixel-exactness.
- [ ] Stop when the dragon renders as a recognizable, artifact-free (no NaN/fireflies-beyond-norm) translucent subsurface surface in the correct composition.

---

### Task 4: Full-quality render + reference image

- [ ] Render `dragon_50.pbrt` at a committauthor-friendly resolution (e.g. 1280×720 or the scene's native, reduced spp if needed for time) to an EXR/PNG.
- [ ] Tone-map / convert to a downsized PNG and commit it as `docs/architecture/sssdragon.png` (mirror the other anchors' committed reference images).
- [ ] Sanity-check: no NaN/Inf, plausible translucent appearance, correct silhouette.
- [ ] Commit the reference image.

---

### Task 5: Per-scene README + docs refresh + close M4

**Files:** `scenes/pbrt/sssdragon/README.md` (new), `docs/architecture/overview.md`, the roadmap spec.

- [ ] **Step 1:** Create `scenes/pbrt/sssdragon/README.md` mirroring `scenes/pbrt/sportscar/README.md`: the sparse Git-LFS download workflow (set `sssdragon`, `git lfs pull --include="sssdragon/**"`), the `.ply.gz` decompression note, the render command, the reference-image note, and an honest "known differences" section (equal-area env handling, ISO/exposure, the SSS energy/curvature note).
- [ ] **Step 2:** Update `docs/architecture/overview.md`: move `subsurface` from "documented degradation" into supported Materials (note: separable tabulated BSSRDF, Dupuy/Habel beam diffusion, isotropic, RGB, `Skin1` preset + direct sigma_a/sigma_s; smooth interface); mark **M4 done** in the roadmap table; change the M4 anchor from `ganesha` to `sssdragon`; add the `sssdragon` row to the Showcase Scenes table.
- [ ] **Step 3:** Update the roadmap spec (`docs/superpowers/specs/2026-06-02-yaoray-m4-subsurface-design.md` and/or the roadmap-refresh spec) to record that the M4 anchor is `sssdragon` (ganesha is coateddiffuse, not SSS).
- [ ] **Step 4:** Run the full unit suite + CTest once more; confirm green.
- [ ] **Step 5:** Commit the docs.

---

## Self-Review (completed by plan author)

**1. Spec coverage.** Slice 5 of the M4 spec asks for: download → smoke → triage → full render → reference comparison → per-scene README → docs refresh → mark M4 done. Tasks 2–5 cover those. The spec's risk register explicitly anticipated the named-preset case ("Slice 5 triage surfaces it"); Task 1 resolves it faithfully (`Skin1`) rather than degrading, per the user's decision, with unknown presets still degrading + Warning. The ganesha→sssdragon anchor correction is captured in Task 5 Step 3.

**2. Placeholder scan.** Task 1 is complete literal code. Tasks 2–5 are intentionally discovery-driven (the triage fixes cannot be pre-written — they depend on what the smoke render surfaces), exactly like the committed M2/M3 anchor-slice plans; each step states a concrete action and a stop condition.

**3. Type consistency.** Task 1 uses the Slice-4 `RenderMaterial` fields and the existing compiler helpers verbatim; the `Skin1` coefficients and `scale` semantics match pbrt's `SubsurfaceParameterTable` + `subsurface` material. No new types.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-yaoray-m4-subsurface-slice5-sssdragon-implementation-plan.md`. Subagent-Driven recommended: sonnet for Task 1 (mechanical preset table) and Tasks 4–5 (render/docs); a capable model for Tasks 2–3 (discovery triage). Note: Tasks 2–4 require running the renderer on the downloaded asset and iterating — they are not pure TDD.
