# YaoRay M3 Slice 3 — killeroo-coated Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the unmodified `mmp/pbrt-v4-scenes` **killeroo-coated** `.pbrt` end-to-end at 1280×720 / 64 spp on the dev sandbox, with the real two-layer `coateddiffuse` / `coatedconductor` BSDFs (shipped in Slices 2a + 2b) visibly driving the coated killeroo surfaces. Commit the reference image plus a per-scene README, and refresh the project-wide docs to mark the **M3 layered phase** complete (the `measured` phase / sportscar is the remaining M3 work and stays open).

**Architecture:** This slice is **discovery-driven**, not feature-driven — identical in shape to M2 Slice 3 (Barcelona Pavilion). Task 1 downloads the asset and runs a low-res smoke render to surface every PBRT v4 directive YaoRay doesn't yet handle. Task 2 is a triage-and-patch loop: each discovered issue lands as its own commit — small gaps become real handlers in `src/render/scene_compiler.cpp` / `src/pbrt/pbrt_scene.cpp` with a unit test; large gaps degrade gracefully via the existing `MaterialFallbackWarning` pattern with a documented entry in the killeroo-coated README. Task 3 promotes to full quality. Tasks 4–6 are documentation and PR.

**Tech Stack:** C++20, CMake 3.24, `git` + `git lfs` for the asset, `yr_test.hpp` for any new unit tests, `gh` CLI for the PR.

---

## Spec Coverage

This plan implements **Slice 3** from `docs/superpowers/specs/2026-05-29-yaoray-m3-layered-materials-design.md`:

1. **killeroo-coated asset download** (Task 1).
2. **Smoke + triage + full render** (Tasks 1, 2, 3).
3. **Per-scene README** mirroring `barcelona_pavilion`/`dining_room` (Task 4).
4. **Reference image committed** at `docs/architecture/killeroo-coated.png` (Task 3).
5. **Documentation refresh** — `README.md` showcase entry; `docs/architecture/overview.md` showcase row + M3 status note (Task 5).
6. **PR + merge** (Task 6).

**Quality bar (per M3 spec §"Quality bar"):**

- The unmodified killeroo-coated entrypoint renders to a composition matching the asset's bundled reference.
- `coateddiffuse` / `coatedconductor` perform real two-layer stochastic evaluation (already true after Slices 2a+2b) — the coated killeroo shows a clearcoat Fresnel highlight / grazing response, NOT a flat base look. `MaterialFallbackWarning` does NOT fire for the coated kinds (they are real now).
- No `Error:` compiler diagnostics. Any `Warning:` is either an anticipated M1 degradation (for *other* materials, e.g. `measured`/`subsurface`) or has a documented entry in the killeroo-coated README.
- No NaN / Inf pixels.
- killeroo-coated completes in **≤ 10 minutes** at 1280×720 / 64 spp on the dev sandbox (soft target per M3 spec §Performance, NOT a merge gate — stochastic layered BSDFs are slower, and the in-`SampleLayered` pdf walk from 2b roughly doubles coated-bounce cost).

**Important scope nuance (roadmap refresh):** The 2026-05-29 roadmap refresh redefined **M3 = layered + measured**. This slice finishes the **layered phase** (killeroo-coated). It does NOT complete M3 — the `measured` BRDF phase (sportscar) is the remaining M3 work. Task 5's docs edits therefore mark the layered anchor done and note measured is pending; they do NOT flip the M3 roadmap row to `done`.

**Out of scope (deferred):**

- `measured` BRDF real implementation (the other M3 phase — its own slice/spec, sportscar anchor).
- `subsurface` (M4), volumetrics, new sampler types, spectral, auto-tangent generation — all keep their existing degrade/fallback behavior.
- Rough-coat GGX interfaces; in-medium scattering (coat stays smooth, as 2a/2b).

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `scenes/pbrt/killeroo_coated/README.md` | killeroo-coated download + render workflow, what works, any documented gaps. Mirrors `scenes/pbrt/barcelona_pavilion/README.md`. |
| `docs/architecture/killeroo-coated.png` | Final 1280×720 / 64 spp reference render. |

**Modified files (the project-wide refresh — Task 5):**

| Path | Change |
|------|--------|
| `README.md` | Add killeroo-coated entry to the showcase table; note the M3 layered phase rendering. |
| `docs/architecture/overview.md` | Add killeroo-coated to the showcase table; update the M3 roadmap-row headline / a note to reflect the layered anchor done with measured pending (DO NOT set M3 status to `done`). |

**Modified files (Task 2 — discovered during smoke render; cannot pre-enumerate):**

| Path | Change |
|------|--------|
| `src/render/scene_compiler.cpp` (possible) | New handler or new degradation entry per discovered issue. |
| `src/pbrt/pbrt_scene.cpp` (possible) | Parser-level capture if a directive isn't reaching the IR. |
| `src/backends/cpu/*` or `src/render/*` (possible) | e.g. `ObjectInstance` instancing support if killeroo-coated instances the model (a likely candidate — see Task 1 Step 8). |
| `tests/scene_compiler_*.cpp` (one per real patch) | Unit test covering the new handler / degradation. |

The "possible" rows are placeholders for the triage outcome. Task 2 turns each surfaced issue into a concrete entry.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `0e056ab`, post-PR-#13). Use the harness-native `EnterWorktree` tool with name `m3-slice3-killeroo-coated`.

Verify the baseline before any change (MSVC multi-config → `build/Release/`):

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c   # expect 224 PASS / 0 FAIL
cd build && ctest --output-on-failure -C Release 2>&1 | tail -5   # expect 9/9 PASS
cd ..
```

`yaoray_tests.exe` ignores `--filter`; run the full binary from the worktree ROOT and grep. clangd stale-index diagnostics are false positives — trust the MSVC build. All Slice 3 commits land on the worktree branch.

---

## Task 1: Download the killeroo-coated asset and capture the smoke render

**Files:**

- Create on disk (gitignored): `external/assets/pbrt/killeroo-coated/` (unpacked asset tree).
- Create on disk (transient, do not commit): `slice3-smoke-output.log`, `slice3-smoke-output.png`, `slice3-smoke-report.md`.

This task produces a deliverable: the list of every `Error:` and `Warning:` the compiler emits on the unmodified killeroo-coated `.pbrt`, plus a low-res image of what renders before any patches. That list drives Task 2.

`mmp/pbrt-v4-scenes` stores assets via `git lfs`; the killeroo-coated directory is `killeroo-coated/`.

- [ ] **Step 1: Confirm prerequisites**

```bash
git lfs version
git --version
```

Both must be installed. If `git lfs version` errors, install Git LFS and run `git lfs install` once globally.

- [ ] **Step 2: Clone (sparse + LFS) only the killeroo-coated directory**

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set killeroo-coated
git lfs pull --include="killeroo-coated/**"
cd ..
mv pbrt-v4-scenes-tmp/killeroo-coated ./killeroo-coated
rm -rf pbrt-v4-scenes-tmp
ls killeroo-coated/
cd ../../..
```

Expected: `killeroo-coated/` exists under `external/assets/pbrt/` with at least one `.pbrt` file plus supporting geometry/textures. (If the scene `Include`s shared geometry from a sibling directory — e.g. a `killeroo/` or `geometry/` subtree — note the missing-include Warnings in Step 7 and decide in Task 2 whether to also sparse-checkout that sibling or document the degradation. Mirror the Pavilion `landscape/` precedent.)

- [ ] **Step 3: Identify the scene entrypoint**

```bash
ls external/assets/pbrt/killeroo-coated/*.pbrt
```

Pick the canonical entrypoint — the one with `Film`, `Camera`, and `WorldBegin` (not a fragment/included file). Likely `killeroo-coated.pbrt`. Record it; the rest of the plan refers to it as `<KILLEROO_PBRT>` — substitute the actual filename everywhere.

- [ ] **Step 4: Build Release (if not current)**

```bash
cmake --build build --config Release
ls build/Release/yaoray.exe
```

- [ ] **Step 5: Make a low-res smoke copy of the scene**

Copy the entrypoint and edit the COPY (leave the original untouched for Task 3):

```bash
cp external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT> \
   external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT>.smoke
```

In the `.smoke` copy, set the `Film` to a small resolution and the sampler to low spp:

```
Film "rgb"
    "string filename" "killeroo-coated-smoke.png"
    "integer xresolution" [160]
    "integer yresolution" [120]
Sampler "independent" "integer pixelsamples" [4]
```

(Match the original scene's aspect ratio when picking x/y. If the `Film`/`Sampler` live in an `Include`d file, modify a `.smoke` copy of that include instead and point the entrypoint copy at it. Track every file you copy so Task 3 can delete the `.smoke` copies. If the original `Film` writes `.exr`, keep `.png` for the smoke so visual inspection is easy.)

- [ ] **Step 6: Run the smoke render and capture output**

```bash
./build/Release/yaoray.exe render \
    external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT>.smoke \
    --backend cpu \
    > slice3-smoke-output.log 2>&1
mv external/assets/pbrt/killeroo-coated/killeroo-coated-smoke.png slice3-smoke-output.png 2>/dev/null || true
```

At 160×120 / 4 spp this completes in seconds-to-minutes regardless of how broken the scene is. If it hangs >10 min at this resolution, abort and treat as a bug (degenerate BVH / light loop).

- [ ] **Step 7: Scan the log for diagnostics**

```bash
grep -E "Error:|Warning:" slice3-smoke-output.log | sort | uniq -c | sort -rn
```

Frequency-sorted list of every diagnostic — the input to Task 2. Illustrative shapes:

- `Error: unknown directive 'XYZ'` → parser gap (small patch).
- `Error: ...ObjectInstance...` / instancing not handled → likely the headline killeroo-coated patch (see Step 8).
- `Warning: material kind 'measured' ...` / `'subsurface' ...` → anticipated, existing fallback handles it.
- `Warning: missing file '../killeroo/...'` → missing sibling include (decide: sparse-checkout it, or document degrade).

- [ ] **Step 8: Visually inspect + note the instancing question**

Open `slice3-smoke-output.png`. The killeroo model(s) should be recognizable even at 160×120. killeroo scenes frequently place **multiple instances** of the killeroo model via `ObjectBegin`/`ObjectInstance` with different materials (the "coated" variants). If the smoke render shows only ONE killeroo (or none) where the reference has several, `ObjectInstance` support is likely the central Task 2 item. Note in the report: how many killeroo instances the reference shows vs. how many rendered, and whether `ObjectBegin`/`ObjectInstance` appear in the scene file (`grep -nE "ObjectBegin|ObjectInstance" external/assets/pbrt/killeroo-coated/*.pbrt`).

- [ ] **Step 9: Write the smoke report** (worktree-only working note, NOT committed)

Create `slice3-smoke-report.md`:

```markdown
# killeroo-coated smoke render report

**Scene entrypoint:** external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT>
**Resolution:** 160x120 / 4 spp
**Smoke render time:** Xs (wall clock)
**ObjectInstance present?:** <yes/no — count of ObjectBegin/ObjectInstance>
**Killeroo instances expected vs rendered:** <N expected / M rendered>

## Errors
<paste lines>

## New warnings (not in the existing M1 substitution table)
<paste lines>

## Anticipated warnings (already covered by M1 substitution table)
<paste lines or "(none)">

## Visual issues
<missing instances, all-black regions, NaN swathes, or "(none — composition looks like killeroo-coated)">

## Triage candidates
<For each Error and each NEW warning: "small patch" or "degrade with Warning". One line each.>
```

- [ ] **Step 10: Confirm nothing accidental staged**

```bash
git status
```

Expected: working tree clean (asset gitignored under `external/assets/`; the `.log`/`.png`/`.md` are local artifacts — do NOT commit them).

---

## Task 2: Triage-and-patch loop (discovery-driven)

**Files:** TBD per issue, from Task 1's smoke report.

A **loop**: for each Error or new Warning, run a sub-cycle. Order by severity — Errors first (block any render), then NEW warnings producing visibly wrong output, then warnings whose fallback is acceptable.

**Decision rule per issue:**

| Severity | Cost to patch properly | Action |
|---|---|---|
| Error (parse/compile fails) | Small (param handler, name typo, missing default, instancing wiring) | Small patch + unit test. |
| Error | Large (new BSDF, volumetrics, loopsubdiv) | Cannot ship without degrade. Add a graceful-degrade handler at the dispatch site, emit a `Warning:` via `MaterialFallbackWarning` (or analogous), pick a sensible fallback, document the gap in the killeroo-coated README (Task 4). |
| New Warning (compile OK but degraded) | Small | Is the fallback acceptable for this scene? If yes, document only. If no, write a real handler. |
| New Warning | Large | Document in the README. No code change. |

**Likely headline item — `ObjectInstance`.** If Task 1 Step 8 found instancing, the proper patch is real instancing support (parse `ObjectBegin`/`ObjectEnd`/`ObjectInstance` into `PbrtScene`, then emit instanced primitives into `RenderSceneIR` — each instance is the object's geometry under the instance's CTM + bound material). This is the most substantial possible patch and may itself be sizable; if it proves large, consult the controller before committing to a full instancing implementation vs. a flattening approach (expand each `ObjectInstance` into concrete transformed primitives at compile time — simpler, correct for static scenes, costs memory for many instances; killeroo-coated has few, so flattening is acceptable). Either way it gets a unit test.

**Commit discipline:** each patch is one commit; each degrade is one commit. Re-smoke after each commit to confirm the issue is resolved and no new one surfaced. The existing material substitution pattern lives at `src/render/scene_compiler.cpp` (`MaterialFallbackWarning` helper + the `subsurface`/`measured`/`hair`/`mix` cases) — use it as the model for any degrade.

### Sub-task template — Small patch

Replace `<ISSUE>` with the actual identifier (e.g. `object-instance`, `material-param-foo`).

- [ ] **Step 1: Write the failing unit test**

Create `tests/scene_compiler_<ISSUE>_tests.cpp` (or extend an existing file). Pattern:

```cpp
#include "yr_test.hpp"
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {
yr::PbrtScene MakeSceneWith<ISSUE>() {
    yr::PbrtScene pbrt;
    // ... minimal scene exercising the directive ...
    return pbrt;
}
} // namespace

YR_TEST(scene_compiler_<ISSUE>_handles_it) {
    yr::PbrtScene pbrt = MakeSceneWith<ISSUE>();
    yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(/* specific structural assertion: e.g. instanced primitive count */);
}
```

Register the new test file in `CMakeLists.txt` (the `yaoray_tests` source list).

- [ ] **Step 2: Run, verify the test FAILS**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -E "<ISSUE>|FAIL"
```

Expected: FAIL matching the smoke-report symptom.

- [ ] **Step 3: Implement the patch**

In `src/render/scene_compiler.cpp` (most common) or `src/pbrt/pbrt_scene.cpp` (if the parser isn't capturing the directive at all). If it adds a new enum value (RenderMaterialKind etc.), extend the enum in `include/yaoray/render/render_scene.hpp` AND every switch the compiler's `-Wswitch` flags.

- [ ] **Step 4: Run the unit test, verify it PASSES**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep "<ISSUE>"
```

- [ ] **Step 5: Re-smoke-render**

```bash
./build/Release/yaoray.exe render \
    external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT>.smoke --backend cpu \
    > slice3-smoke-output.log 2>&1
grep -E "Error:|Warning:" slice3-smoke-output.log | sort | uniq -c | sort -rn
```

Verify the patched issue is gone. If a NEW issue surfaced (patch unblocked further parsing), append it to `slice3-smoke-report.md` and continue the loop.

- [ ] **Step 6: Full unit suite + CTest — no regression**

```bash
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c
cd build && ctest --output-on-failure -C Release 2>&1 | tail -5 ; cd ..
```

Expected: all existing 224 tests + the new one(s) PASS; 9/9 CTest.

- [ ] **Step 7: Commit**

```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_<ISSUE>_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(scene_compiler): handle <directive/kind/param> for killeroo-coated

<One paragraph: the gap, where it surfaced, what the handler does,
what test covers it.>

killeroo-coated smoke render no longer errors on this directive.
All existing tests + the new unit test pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Sub-task template — Graceful degradation

- [ ] **Step 1: Write the failing unit test**

```cpp
YR_TEST(scene_compiler_<ISSUE>_degrades_gracefully) {
    yr::PbrtScene pbrt = MakeSceneWith<ISSUE>();
    yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.errors.empty());
    bool found_warning = false;
    for (const yr::SceneDiagnostic& d : result.diagnostics) {
        if (d.severity == yr::SceneDiagnosticSeverity::Warning &&
            d.message.find("<issue-keyword>") != std::string::npos) { found_warning = true; break; }
    }
    YR_EXPECT_TRUE(found_warning);
    YR_EXPECT_TRUE(/* fallback IR shape assertion */);
}
```

Register in `CMakeLists.txt`.

- [ ] **Step 2: Run, verify the test FAILS** (compiler currently errors instead of warning+falling back).

- [ ] **Step 3: Implement the degrade** at the dispatch site that emits the error — emit a `Warning:` via the `MaterialFallbackWarning`-style helper and set a sensible fallback (default diffuse for materials, constant texture for textures, omit/skip for unsupported lights/geometry). Match the existing substitution-table shape in `scene_compiler.cpp`.

- [ ] **Step 4: Run the unit test, verify it PASSES.**

- [ ] **Step 5: Re-smoke-render** (same as small-patch Step 5) — confirm the issue went Error → anticipated Warning and the render progresses.

- [ ] **Step 6: Full unit suite + CTest** (same as small-patch Step 6).

- [ ] **Step 7: Append to the "Documented gaps" section of `slice3-smoke-report.md`**

```
- `<directive/kind>` → degraded to `<fallback>`: <one-sentence reason>.
```

- [ ] **Step 8: Commit**

```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_<ISSUE>_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(scene_compiler): degrade <directive/kind> to <fallback>

killeroo-coated uses <directive>, which YaoRay doesn't directly
support. Degrades to <fallback> with a documented Warning via the
same pattern used for subsurface / measured / hair / mix. Acceptable
because <reason>; documented in scenes/pbrt/killeroo_coated/README.md.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

### Loop termination

The loop ends when:

- The smoke render completes with NO `Error:` diagnostics.
- Every `Warning:` is either anticipated (M1 substitution table) or has an entry in `slice3-smoke-report.md`'s "Documented gaps" section.
- The 160×120 PNG shows recognizable killeroo-coated composition — the coated killeroo model(s) present in the right places, with the coated surfaces NOT looking like a flat untextured base.

If after a few patches the smoke render still produces structural artifacts (large all-black regions, NaN swathes, all instances missing), STOP and report BLOCKED with the symptoms — the controller decides whether it's a deeper path-tracer/BVH bug rather than a directive gap.

---

## Task 3: Full quality render + reference image

**Files:**
- Create on disk (committed): `docs/architecture/killeroo-coated.png`.
- Delete on disk (worktree cleanup): the `.smoke` scene copies, `slice3-smoke-output.log`, `slice3-smoke-output.png`, any intermediate smoke `.png`.

- [ ] **Step 1: Clean up the smoke artifacts**

```bash
rm -f external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT>.smoke
rm -f external/assets/pbrt/killeroo-coated/killeroo-coated-smoke.png
rm -f slice3-smoke-output.log slice3-smoke-output.png
```

Delete any `.smoke` copies of included files too. Keep `slice3-smoke-report.md` (Task 4 uses it).

- [ ] **Step 2: Run the full quality render**

The bundled scene may target a different resolution/spp (and possibly `.exr`). To reproduce this repo's reference at 1280×720 / 64 spp, temporarily edit the original scene's `Film` + `Sampler` (mirror the Pavilion README's "edit then revert" note), OR render at the bundled settings if they're already close. Use `.png` output for the committed reference.

```bash
time ./build/Release/yaoray.exe render \
    external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT> \
    --backend cpu \
    2>&1 | tee slice3-full-render.log
```

Record wall-clock time. Soft target ≤ 10 min (NOT a merge gate). The output path is the scene's `Film "string filename"` — find it via the log line `Rendered image: <path>`.

If NaN/Inf pixels appear (pink/green speckle, or diverged hit/miss ratios), that IS a blocker — investigate before continuing (could be a coated-MIS edge case under real lighting; check whether disabling MIS or clamping changes it, and report).

- [ ] **Step 3: Move the rendered PNG to docs**

```bash
mv external/assets/pbrt/killeroo-coated/<RENDERED_FILENAME>.png docs/architecture/killeroo-coated.png
```

(If the scene wrote `.exr`, also tone-map/export a `.png` for the committed reference — or set the `Film` to `.png` for the reference render. The committed reference must be a viewable PNG.)

- [ ] **Step 4: Verify the reference image**

Open `docs/architecture/killeroo-coated.png`:
- Composition matches killeroo-coated's bundled reference (the killeroo model(s), correct count/poses, the ground/backdrop, the lighting).
- The coated surfaces show a **clearcoat highlight / Fresnel grazing response** — visibly a two-layer coated look, not a flat base. (This is the M3 layered-phase payoff.)
- No NaN/Inf speckle, no missing-geometry holes.

If any fail, debug before committing.

- [ ] **Step 5: Commit the reference image**

```bash
git add docs/architecture/killeroo-coated.png
git commit -m "$(cat <<'EOF'
docs(architecture): killeroo-coated reference render at 1280x720 / 64 spp

Full quality render of mmp/pbrt-v4-scenes killeroo-coated. Render time:
<X>s on the dev sandbox (Windows, MSVC, Release, multi-thread CPU PT).
The coated killeroo surfaces show the real two-layer LayeredBxDF
(clearcoat Fresnel highlight + grazing response) shipped in M3
Slices 2a + 2b — no longer aliasing to the bare base.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Delete the local render log**

```bash
rm -f slice3-full-render.log
git status   # clean (asset gitignored; report still present for Task 4)
```

---

## Task 4: killeroo-coated per-scene README

**Files:** Create `scenes/pbrt/killeroo_coated/README.md` (committed). Model: `scenes/pbrt/barcelona_pavilion/README.md`.

- [ ] **Step 1: Create the README** (substitute `<>` placeholders from Tasks 1–3):

```markdown
# killeroo-coated (mmp/pbrt-v4-scenes)

YaoRay's M3 layered-materials anchor scene. This directory stays empty in
git — the asset lives in Matt Pharr's `pbrt-v4-scenes` repository under
Git LFS; we link to it instead of redistributing.

## Download

`pbrt-v4-scenes` requires Git LFS. A sparse-checkout that pulls only
`killeroo-coated/` is recommended:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set killeroo-coated
git lfs pull --include="killeroo-coated/**"
cd ..
mv pbrt-v4-scenes-tmp/killeroo-coated ./killeroo-coated
rm -rf pbrt-v4-scenes-tmp
```

The unpacked tree under `external/assets/pbrt/killeroo-coated/` is
gitignored via the project-wide `external/assets/` rule.
<If the scene Includes a sibling subtree (e.g. shared killeroo geometry),
add the `git sparse-checkout add <sibling>` note here, mirroring how the
Pavilion README documents the `landscape/` subtree.>

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/killeroo-coated/<KILLEROO_PBRT> --backend cpu
```

The output lands at the path in the scene's `Film "string filename"`
directive. To reproduce this repo's reference at 1280x720 / 64 spp,
temporarily edit the `Film` + `Sampler` blocks, then revert.

## What works in M3 (layered phase)

- `<KILLEROO_PBRT>` parses, compiles, and renders end-to-end with no
  `Error:` diagnostics. <List the Task 2 patches that cleared blockers,
  e.g. "ObjectInstance instancing landed in this slice (Patch ...).">
- **`coateddiffuse` / `coatedconductor` perform real two-layer stochastic
  evaluation** (M3 Slices 2a + 2b): a smooth dielectric clearcoat over a
  diffuse/conductor base with Beer-Lambert absorption, MIS-consistent
  sample + f + pdf. The coated killeroo shows a clearcoat Fresnel
  highlight and grazing-angle response — not the flat base look of the
  pre-M3 alias.
- <Geometry summary — e.g. "The killeroo PLY mesh(es) load via the M1
  plymesh path; N instances place the model via ObjectInstance.">
- <Lighting summary — HDRI infinite / area lights as the scene uses.>
- The SAH binned + parallel BVH builds <NODES> nodes in <T>s. Render:
  1280x720 / 64 spp in ~<R>s on the dev sandbox.

A reference render lives at `docs/architecture/killeroo-coated.png`.

## Documented degradations (killeroo-coated-specific)

<Bullet list, one per documented gap from slice3-smoke-report.md's
"Documented gaps" section:
- `<directive/kind>` → `<fallback>`: <one-sentence reason>.
If none surfaced (only M1-anticipated warnings fired), write:
"(none — every killeroo-coated directive lands in a direct handler or in
the M1 substitution table without new entries)".>

## Camera convention

<If killeroo-coated exposed a new camera/scene convention, document it.
Otherwise: "Uses the same world-to-camera CTM convention documented in
scenes/pbrt/dining_room/README.md. No new convention note required.">
```

- [ ] **Step 2: Verify the README** — `cat scenes/pbrt/killeroo_coated/README.md`; no leftover `<placeholder>` markers, valid markdown, correct file links.

- [ ] **Step 3: Commit the README**

```bash
git add scenes/pbrt/killeroo_coated/README.md
git commit -m "$(cat <<'EOF'
docs(scenes): killeroo-coated per-scene README

Mirrors scenes/pbrt/barcelona_pavilion/README.md: Git LFS download
workflow, render command, what works on the M3 layered phase (real
coated* BSDFs), and killeroo-coated-specific documented degradations.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Delete the local smoke report**

```bash
rm -f slice3-smoke-report.md
git status   # clean
```

---

## Task 5: Project-wide docs refresh

**Files:** `README.md`, `docs/architecture/overview.md`.

**Nuance (do NOT over-claim):** Slice 3 finishes the M3 **layered phase**. The `measured` phase (sportscar) is still open, so **M3's roadmap status stays `in progress`**. These edits add killeroo-coated to the showcases and note the layered anchor is rendering — they do NOT flip the M3 row to `done`.

- [ ] **Step 1: Update `README.md`**

Open `README.md`. Add a killeroo-coated row to the Showcase Scenes table (mirror the Pavilion row's tone):

```markdown
| `scenes/pbrt/killeroo_coated/` | mmp's PBRT v4 killeroo-coated — M3 layered-materials anchor (asset downloaded via `git lfs`; see the per-scene README). |
```

If `README.md` has a status sentence mentioning the current milestone, update it to note M3's layered phase (real `coated*`) is rendering, with `measured` still in progress. Keep it accurate — do NOT claim M3 complete. Save.

- [ ] **Step 2: Update `docs/architecture/overview.md`**

Two edits:

**Edit A** — Showcase Scenes table (around line 75, under the Pavilion row). Add:

```markdown
| `scenes/pbrt/killeroo_coated/README.md` | mmp's PBRT v4 killeroo-coated — M3 layered-materials anchor (downloaded via `git lfs`; gitignored). |
```

**Edit B** — the M3 roadmap row (line ~109). Currently:

```markdown
| **M3** | `killeroo-coated` + `sportscar` | Advanced Materials I: stochastic layered (`coated*`) + `measured` BRDF | in progress |
```

Keep the status `in progress` but make the headline reflect that the layered half is done and measured remains, e.g.:

```markdown
| **M3** | `killeroo-coated` + `sportscar` | Advanced Materials I: stochastic layered (`coated*`) **done** (killeroo-coated renders); `measured` BRDF (sportscar) pending | in progress |
```

(The Materials paragraph already states `coated*` do full stochastic two-layer evaluation — added in Slice 2b. No change needed there, unless you want to append "exercised by the killeroo-coated anchor scene.") Save.

- [ ] **Step 3: Verify the docs** — `cat README.md | head -90` and re-read the overview Showcase + Roadmap sections. No broken markdown; M3 row still `in progress`; killeroo-coated in both showcase tables.

- [ ] **Step 4: Commit the docs refresh**

```bash
git add README.md docs/architecture/overview.md
git commit -m "$(cat <<'EOF'
docs: add killeroo-coated to showcases; M3 layered phase rendering

Adds the killeroo-coated anchor to the README and overview showcase
tables and notes the M3 layered phase (real coated* materials) renders
end-to-end. M3 stays in progress — the measured BRDF phase (sportscar)
is the remaining M3 work.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline 0e056ab..HEAD
```

Expected: N Task 2 patch/degrade commits (oldest first) + Task 3 reference image + Task 4 README + Task 5 docs refresh. If Task 2 produced zero commits (killeroo-coated rendered straight from main), the branch has 3 commits.

- [ ] **Step 2: Push and open the PR** (use the actual branch name `EnterWorktree` reports — it prefixes `worktree-`)

```bash
git push -u origin <branch>
gh pr create --base main --head <branch> --title "feat: M3 Slice 3 — killeroo-coated integration (layered phase done)" --body "$(cat <<'EOF'
## Summary

- Render the unmodified mmp/pbrt-v4-scenes killeroo-coated end-to-end at 1280x720 / 64 spp in <X>s on the dev sandbox, with the real two-layer `coateddiffuse`/`coatedconductor` BSDFs (M3 Slices 2a+2b) driving the coated surfaces.
- Commit the reference image to `docs/architecture/killeroo-coated.png`.
- Add `scenes/pbrt/killeroo_coated/README.md` (Git LFS download workflow, what works, documented degradations).
- Refresh `README.md` + `docs/architecture/overview.md`: killeroo-coated in the showcase tables; M3 layered phase noted as rendering (M3 stays in progress — measured/sportscar pending).

## killeroo-coated compatibility patches

<Bullet list of Task 2 commits, one line each (e.g. `- <sha> feat: ObjectInstance instancing`). If zero, write "(none — rendered straight from main)".>

## Documented degradations (killeroo-coated-specific)

<Mirror the README's Documented degradations section, or "(none)".>

## Quality bar (per M3 spec)

- [x] killeroo-coated renders to completion at 1280x720 / 64 spp in <X>s (soft target <= 10 min).
- [x] Composition matches the bundled reference; coated surfaces show a real clearcoat highlight (not the bare-base alias).
- [x] No `Error:` diagnostics. Each `Warning:` is M1-anticipated or documented in the per-scene README.
- [x] No NaN/Inf pixels.
- [x] All 224 unit tests + 9 CTest entries still pass (+<K> new tests from Task 2 patches, if any).

## Scope

Finishes the **M3 layered phase**. The **measured** BRDF phase (sportscar) is the remaining M3 work and is NOT in this slice.

## Test plan

- [x] `yaoray_tests` — <224+K>/<224+K> PASS
- [x] `ctest -C Release` — 9/9 PASS
- [x] killeroo-coated smoke (160x120 / 4 spp) — no Error diagnostics
- [x] killeroo-coated full (1280x720 / 64 spp) — composition verified vs bundled reference; no NaN/Inf

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

Replace `<X>` (render time) and `<K>` (new test count); fill the patch + degradation lists from Tasks 2 and 4.

- [ ] **Step 3: Address review feedback** — fix on the branch with new commits (no force-push, no amend); re-run `yaoray_tests` + `ctest` after each fix.

- [ ] **Step 4: Merge (operator-gated).** After the operator merges, finish per `superpowers:finishing-a-development-branch`: sync local `main` (`git fetch` + ff), remove the worktree, delete the merged remote branch. (If `ExitWorktree` reports no active session, clean up manually: `git worktree remove .claude/worktrees/<dir> --force` + `git branch -D <branch>` + `git push origin --delete <branch>` — the seam seen in Slice 2b.)

---

## Self-Review Notes

- **Spec coverage:** every Slice 3 deliverable from the M3 spec §"Slice 3 — killeroo-coated integration" is mapped — download + smoke (Task 1), triage/patch loop (Task 2), full render + reference (Task 3), per-scene README (Task 4), docs refresh (Task 5), PR (Task 6).
- **Discovery-driven:** Task 2's SPECIFIC patches can't be pre-enumerated; the plan gives the decision rule (patch vs degrade), the existing-pattern reference (`MaterialFallbackWarning` + substitution table), commit discipline (one issue = one commit), and the loop-termination criterion. `ObjectInstance` is flagged as the likely headline item with a patch-vs-flatten escalation note.
- **Roadmap-refresh nuance handled:** Task 5 explicitly keeps M3 `in progress` (measured/sportscar pending) and only marks the layered anchor done — reconciling the older M3 spec's "Slice 3 marks M3 done" wording with the 2026-05-29 roadmap refresh that split M3 into layered + measured.
- **Real-BSDF payoff is a quality gate, not new code:** the coated BSDFs are already real (2a+2b). Task 3 Step 4's visual check (clearcoat highlight, not flat base) is how this slice proves the layered work pays off on a real asset; no BSDF code changes are expected in this slice (only compatibility patches).
- **Worktree cleanup:** Task 3 Step 1/6 and Task 4 Step 4 remove `.smoke` copies + `slice3-*.log/.png/.md` before the PR; the asset is gitignored under `external/assets/`. Task 6 Step 4 includes the manual-cleanup fallback for the `ExitWorktree` "no active session" seam observed in Slice 2b.
- **Asset entrypoint placeholder** `<KILLEROO_PBRT>` is pinned in Task 1 Step 3 and reused everywhere.
- **Worktree branch** `m3-slice3-killeroo-coated` consistent across Setup and Task 6 (the harness will prefix `worktree-`).
- **No CTest entry for killeroo-coated** — the asset is gitignored (same as dining-room / Pavilion); CTest stays at 9 committed scenes.
