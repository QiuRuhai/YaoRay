# YaoRay M2 Slice 3 — Barcelona Pavilion Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the unmodified `mmp/pbrt-v4-scenes` Barcelona Pavilion `.pbrt` end-to-end at 1280×720 / 64 spp in ≤ 30 minutes on the dev sandbox, commit the reference image plus a per-scene README, and refresh the project-wide docs to mark M2 done.

**Architecture:** This slice is **discovery-driven**, not feature-driven. Task 1 downloads the asset and runs a smoke render at low resolution to surface every PBRT v4 directive YaoRay doesn't yet handle. Task 2 is a triage-and-patch loop: each discovered issue lands as its own commit — small gaps become real handlers in `src/render/scene_compiler.cpp` or `src/pbrt/pbrt_scene.cpp` with a unit test, large gaps degrade gracefully via the existing `MaterialFallbackWarning` pattern with a documented entry in the Pavilion README. Task 3 promotes to full quality. Tasks 4–6 are documentation and PR.

**Tech Stack:** C++20, CMake 3.24, `curl` + `unzip` for asset download, `yr_test.hpp` for any new unit tests, `gh` CLI for the PR.

---

## Spec Coverage

This plan implements **Slice 3** from `docs/superpowers/specs/2026-05-28-yaoray-m2-barcelona-pavilion-design.md`:

1. **Pavilion asset download** (Task 1).
2. **Smoke + triage + full render** (Tasks 1, 2, 3).
3. **Per-scene README** mirroring dining-room's structure (Task 4).
4. **Reference image committed** at `docs/architecture/barcelona-pavilion.png` (Task 3).
5. **Documentation refresh** — `docs/architecture/overview.md` roadmap row "M2 → done" + "Median-split BVH" wording update + showcase update; `README.md` showcase entry (Task 5).
6. **PR + merge** (Task 6).

**Quality bar (per M2 spec §"Scene correctness"):**

- Pavilion renders to a composition with polished marble floor, glass walls, reflective water pool, chrome cross-piece, HDRI-lit sky.
- Every surface declared `conductor`, `dielectric`, `coateddiffuse`, `coatedconductor` uses its proper BSDF.
- No `Error:` compiler diagnostics. Each `Warning:` is either an anticipated material/wrap-mode degrade (already covered by the M1 substitution table) or has a new explicit entry in the Pavilion README documenting what fell back to what.
- No NaN / Inf pixels.
- Render time ≤ 30 minutes at 1280×720 / 64 spp on the operator's reference machine (the dev sandbox; same machine the Slice 1/2 measurements ran on).

**Out of scope (deferred to M3+):**

- New BSDFs (real `subsurface`, real `measured`, real nested `layered`).
- Volumetrics / media.
- New sampler types (`halton`, `sobol`, `pmj02bn`) — these already parse and degrade to `independent`; Pavilion uses whatever it uses, and we keep the existing fallback.
- Spectral parameter support — RGB-only stays.
- Auto-tangent generation for trianglemeshes with `uv` but no `S`.
- `Texture "ptex"` if Pavilion uses it — degrade with documented Warning.

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `scenes/pbrt/barcelona_pavilion/README.md` | Pavilion download + render workflow, what works, any documented gaps. Mirrors `scenes/pbrt/dining_room/README.md`. |
| `docs/architecture/barcelona-pavilion.png` | Final 1280×720 / 64 spp reference render. |

**Modified files (the project-wide refresh):**

| Path | Change |
|------|--------|
| `README.md` | Add Pavilion entry to the showcase table; mention M2 milestone done. |
| `docs/architecture/overview.md` | Update line ~53 "Median-split BVH" wording → "SAH BVH with parallel construction"; bump M2 row in roadmap table (`planned` → `done`); optionally add Pavilion to the showcase table. |

**Modified files (Task 2 — discovered during smoke render; cannot pre-enumerate):**

| Path | Change |
|------|--------|
| `src/render/scene_compiler.cpp` (possible) | New handler or new degradation entry per discovered issue. |
| `src/pbrt/pbrt_scene.cpp` (possible) | Parser-level capture if a directive isn't reaching the IR at all. |
| `src/render/texture.cpp` or similar (possible) | New texture format support if Pavilion needs one. |
| `tests/scene_compiler_*.cpp` (one per real patch) | Unit test covering the new handler / degradation. |

The "possible" rows are placeholders for the triage outcome. Task 2 turns each surfaced issue into a concrete entry here.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `07398a3`, post-PR-#6). Use the harness-native `EnterWorktree` tool with name `m2-slice3-barcelona-pavilion` (or have a controller run `superpowers:using-git-worktrees`).

Verify the baseline before any change:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe        # expect 180/180 PASS
cd build && ctest --output-on-failure -C Release   # expect 8/8 PASS
cd ..
```

All Slice 3 commits land on the worktree branch.

---

## Task 1: Download the Pavilion asset and capture the smoke render

**Files:**

- Create on disk (gitignored): `external/assets/pbrt/barcelona-pavilion/` (unpacked asset tree).
- Create on disk (transient, do not commit): `slice3-smoke-output.log`, `slice3-smoke-output.png`.

This task is **concrete and produces a deliverable**: a list of every `Error:` and `Warning:` the compiler emits when given the unmodified Pavilion `.pbrt`, plus a low-resolution image showing what gets rendered before any patches. That list drives Task 2.

The mmp/pbrt-v4-scenes repo doesn't publish `.zip` archives — it's a normal git repo where assets are stored via `git lfs`. The Pavilion directory is `barcelona-pavilion/`.

- [ ] **Step 1: Confirm prerequisites**

```bash
git lfs version
git --version
```

Both must be installed. If `git lfs version` errors, install Git LFS first (`brew install git-lfs` on macOS, the Windows installer on Windows, package manager on Linux), then run `git lfs install` once globally.

- [ ] **Step 2: Clone (sparse + LFS) only the Pavilion directory**

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set barcelona-pavilion
git lfs pull --include="barcelona-pavilion/**"
cd ..
mv pbrt-v4-scenes-tmp/barcelona-pavilion ./barcelona-pavilion
rm -rf pbrt-v4-scenes-tmp
ls barcelona-pavilion/
cd ../../..
```

Expected: `barcelona-pavilion/` exists under `external/assets/pbrt/` and contains at least one `.pbrt` file plus its supporting assets (textures, geometry).

- [ ] **Step 3: Identify the scene entrypoint**

```bash
ls external/assets/pbrt/barcelona-pavilion/*.pbrt
```

Pick the canonical entrypoint. Convention: usually a top-level `.pbrt` file (often `scene-v4.pbrt`, possibly `pavilion-v4.pbrt`, or `barcelona-pavilion.pbrt` — pick the one that is NOT a fragment/included file). If multiple candidates exist, open them and pick the one that has a `Film`, `Camera`, and `WorldBegin` block (the others are includes).

Record the chosen filename. The rest of the plan refers to it as `<PAVILION_PBRT>` — substitute the actual filename everywhere.

- [ ] **Step 4: Build Release (if not already current)**

```bash
cmake --build build --config Release
ls build/Release/yaoray.exe
```

- [ ] **Step 5: Patch the scene's `Film` to 160×90 / 4 spp for the smoke render**

The Pavilion's bundled Film directive is for the production render (typically 1280×720 / 64+ spp). For the smoke render we need lower resolution. The cleanest way is to **copy** the scene file and modify the copy, leaving the original untouched for Task 3:

```bash
cp external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT> \
   external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT>.smoke
```

Then open the copy and edit the `Film` block to:

```
Film "rgb"
    "string filename" "barcelona-pavilion-smoke.png"
    "integer xresolution" [160]
    "integer yresolution" [90]
```

And the integrator / sampler / spp:

```
Sampler "independent" "integer pixelsamples" [4]
```

(If the original scene file uses a different sampler type, leave the sampler line as-is — YaoRay degrades unknown samplers to independent. Just adjust the `pixelsamples` count if specified.)

If the `Film` block is not at the top level (it's inside an `Include`), find the included file and modify the `.smoke` copy of the include instead. Track every file you copy/modify in this step — Task 3 will delete the `.smoke` copies.

- [ ] **Step 6: Run the smoke render and capture the output**

```bash
./build/Release/yaoray.exe render \
    external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT>.smoke \
    --backend cpu \
    > slice3-smoke-output.log 2>&1
```

This will likely take 30 seconds to a few minutes at 160×90 / 4 spp regardless of how broken the scene is — the renderer either completes or errors out within that window. If the render hangs for >10 minutes at this resolution, abort and treat it as a bug to investigate (likely a degenerate BVH or infinite light loop).

Move the output PNG (its name comes from the `Film "string filename"` you set in Step 5) next to the log:

```bash
mv external/assets/pbrt/barcelona-pavilion/barcelona-pavilion-smoke.png \
   slice3-smoke-output.png
```

- [ ] **Step 7: Scan the log for diagnostics**

```bash
grep -E "Error:|Warning:" slice3-smoke-output.log | sort | uniq -c | sort -rn
```

This gives a frequency-sorted list of every diagnostic the compiler emitted. Capture this list — it is the input to Task 2.

Common shapes of what you might see (illustrative — exact contents depend on Pavilion):

- `Error: unknown directive 'XYZ' at line N` → parser doesn't know a directive at all (Task 2 small patch).
- `Error: material parameter 'foo' missing for kind 'bar'` → handler missing a required param (Task 2 small patch).
- `Warning: material kind 'subsurface' is not directly supported...` → anticipated, the existing fallback handles it.
- `Warning: sampler 'halton' degraded to independent` → anticipated.
- `Error: unsupported texture type 'ptex'` → real gap (Task 2 likely degrade to constant texture + Warning + document).

- [ ] **Step 8: Visually inspect the smoke render**

Open `slice3-smoke-output.png`. Even at 160×90, the composition should be recognizable: floor / walls / pool / sky. If it's all-black, all-NaN, or shows missing geometry blocks, log the structural issue — it's a Task 2 priority.

- [ ] **Step 9: Write the smoke report**

Append a short report to the worktree as `slice3-smoke-report.md` (do not commit yet — this lives only in the worktree as a working note for Task 2):

```markdown
# Pavilion smoke render report

**Scene entrypoint:** external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT>
**Resolution:** 160x90 / 4 spp
**Smoke render time:** Xs (wall clock)

## Errors

<paste lines>

## New warnings (not in the existing M1 substitution table)

<paste lines>

## Anticipated warnings (already covered by M1 substitution table)

<paste lines or "(none)">

## Visual issues

<bullet list of structural problems visible in the PNG: missing geometry blocks, all-black regions, etc., or "(none — composition looks like Pavilion)">

## Triage candidates

<For each Error and each NEW warning, propose: "small patch" or "degrade with Warning". One line per issue.>
```

- [ ] **Step 10: Commit the report**

The `slice3-smoke-report.md` file goes ONLY into the worktree as a working artifact and is removed before the PR opens. Do NOT commit it. The asset itself is gitignored; the log and PNG are local artifacts.

Verify nothing accidental staged:

```bash
git status
```

Expected output: working tree clean (the asset is gitignored under `external/assets/`).

---

## Task 2: Triage-and-patch loop (discovery-driven)

**Files:** TBD per issue. The list comes from Task 1's smoke report.

This task is a **loop**: for each Error or new Warning surfaced in Task 1, run a sub-cycle. Order issues by severity — Errors first (they block any render at all), then NEW warnings that produce visibly wrong output, then warnings that produce a less-wrong-but-still-OK fallback.

**Decision rule per issue:**

| Severity | Cost to patch properly | Action |
|---|---|---|
| Error (parse fails / compile fails) | Small (param handler, name typo, missing default) | Small patch + unit test. |
| Error | Large (new BSDF, volumetrics, loopsubdiv) | Cannot ship without degrade. Add a graceful-degrade handler at the dispatch site, emit a `Warning:` via `MaterialFallbackWarning` (or analogous), pick a sensible fallback (default diffuse for materials, constant texture for textures, skip for lights, etc.), document the gap in the Pavilion README (Task 4). |
| New Warning (compile OK but degraded) | Small | Decide: is the fallback acceptable for Pavilion? If yes, no code change — just document in the Pavilion README. If no, write a real handler (small patch). |
| New Warning | Large | Document in the Pavilion README. No code change. |

**Commit discipline:** each patch is one commit. Each degrade is one commit. Each issue-by-issue documentation note is part of Task 4's README commit (not separately).

**Re-smoke after each commit** to confirm the issue is resolved and no new ones surfaced.

### Sub-task template — Small patch

Each small patch follows this template. Replace `<ISSUE>` with the actual issue identifier (e.g., `material-kind-foo`, `texture-format-bar`).

- [ ] **Step 1: Write the failing unit test**

Create `tests/scene_compiler_<ISSUE>_tests.cpp` (or extend an existing test file if one already covers this directive). Pattern (using `MakeMinimalScene` or whatever fixture closest to your case):

```cpp
#include "yr_test.hpp"
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::PbrtScene MakeSceneWith<ISSUE>() {
    yr::PbrtScene pbrt;
    // ... minimal scene that exercises the directive ...
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_<ISSUE>_handles_the_param) {
    yr::PbrtScene pbrt = MakeSceneWith<ISSUE>();
    yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    // Replace with the specific assertion that the patch must satisfy.
    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(/* specific structural assertion */);
}
```

Register the new test file in `CMakeLists.txt` (find the `yaoray_tests` target — there's a list of `tests/*.cpp` files; add the new one).

- [ ] **Step 2: Run, verify the test FAILS**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe --filter=scene_compiler_<ISSUE>
```

Expected: FAIL with whatever error matches the smoke-report symptom.

- [ ] **Step 3: Implement the patch**

The patch goes in `src/render/scene_compiler.cpp` (most common) or `src/pbrt/pbrt_scene.cpp` (only if the parser isn't even capturing the directive into `PbrtScene`). Look for the dispatch site that emits the error and add the handler there.

If the patch adds a new RenderMaterialKind/RenderTextureKind/etc., extend the relevant enum in `include/yaoray/render/render_scene.hpp` AND any switch statements that need a case for it (the compiler's `-Wswitch` will tell you which).

- [ ] **Step 4: Run the unit test, verify it PASSES**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe --filter=scene_compiler_<ISSUE>
```

Expected: PASS.

- [ ] **Step 5: Re-smoke-render the Pavilion**

```bash
./build/Release/yaoray.exe render \
    external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT>.smoke \
    --backend cpu \
    > slice3-smoke-output.log 2>&1
grep -E "Error:|Warning:" slice3-smoke-output.log | sort | uniq -c | sort -rn
```

Verify the patched issue no longer appears. If a NEW issue surfaced (the patch unblocked further parsing), append it to `slice3-smoke-report.md` and continue.

- [ ] **Step 6: Run the full unit suite to confirm no regression**

```bash
./build/Release/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: all existing 180 tests + the new one(s) PASS. All 8 CTest entries PASS.

- [ ] **Step 7: Commit**

```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_<ISSUE>_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(scene_compiler): handle <directive/kind/param> for Pavilion

<One paragraph: what the gap was, where it surfaced, what the
handler does, and what test covers it.>

Pavilion smoke render no longer errors on this directive.
All existing tests + the new unit test pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Sub-task template — Graceful degradation

Each degrade follows this template.

- [ ] **Step 1: Write the failing unit test**

```cpp
YR_TEST(scene_compiler_<ISSUE>_degrades_gracefully) {
    yr::PbrtScene pbrt = MakeSceneWith<ISSUE>();
    yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);

    YR_EXPECT_TRUE(result.errors.empty());

    // The diagnostic must include the issue identifier so operators can
    // grep for it in real-scene compile logs.
    bool found_warning = false;
    for (const yr::SceneDiagnostic& d : result.diagnostics) {
        if (d.severity == yr::SceneDiagnosticSeverity::Warning &&
            d.message.find("<issue-keyword>") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    YR_EXPECT_TRUE(found_warning);

    // Verify the fallback IR shape: e.g., for a material degraded to diffuse,
    // assert that the resulting material in render scene IR has Diffuse kind.
    YR_EXPECT_TRUE(/* fallback shape assertion */);
}
```

Register in CMakeLists.txt.

- [ ] **Step 2: Run, verify the test FAILS**

(The current compiler errors instead of warning + falling back.)

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe --filter=scene_compiler_<ISSUE>
```

Expected: FAIL.

- [ ] **Step 3: Implement the degrade**

Modify the dispatch site that currently emits the error to instead emit a `Warning:` via a helper analogous to `MaterialFallbackWarning` (defined at `src/render/scene_compiler.cpp:33-37`) and use a sensible fallback. The existing material substitution table at `src/render/scene_compiler.cpp:513-547` is the model — each `subsurface`/`measured`/`hair`/`mix`/etc. case calls `MaterialFallbackWarning(scene, type)` and then sets a fallback `material.kind`.

Example pattern (replace `<KIND>` with the actual unsupported kind):

```cpp
} else if (type == "<KIND>") {
    diagnostics.push_back(MaterialFallbackWarning(scene, type));
    material.kind = RenderMaterialKind::Diffuse;  // sensible fallback
    // For materials, read the most likely common parameter for the fallback
    // (e.g., `reflectance` if present), or leave defaults.
}
```

For non-material directives (textures, lights, samplers), follow the same shape: emit a `Warning:` with the directive name and reason, populate IR with a sensible fallback (constant texture, omit the light, etc.).

- [ ] **Step 4: Run the unit test, verify it PASSES**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe --filter=scene_compiler_<ISSUE>
```

Expected: PASS.

- [ ] **Step 5: Re-smoke-render the Pavilion**

Same as the small-patch template Step 5. Verify the issue went from Error to anticipated Warning, and the smoke render now completes (or progresses to the next blocker).

- [ ] **Step 6: Run the full unit suite + CTest**

Same as small-patch template Step 6.

- [ ] **Step 7: Append to the "Documented gaps" section of `slice3-smoke-report.md`**

Add a line like:

```
- `<directive/kind>` → degraded to `<fallback>`: <one-sentence reason>.
```

This list becomes part of the Pavilion README in Task 4.

- [ ] **Step 8: Commit**

```bash
git add src/render/scene_compiler.cpp tests/scene_compiler_<ISSUE>_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(scene_compiler): degrade <directive/kind> to <fallback>

Pavilion uses <directive>, which YaoRay doesn't directly support.
Degrades to <fallback> with a documented Warning emitted through
the same MaterialFallbackWarning pattern used for subsurface /
measured / hair / mix.

The fallback is acceptable for Pavilion because <reason>. The gap
is documented in scenes/pbrt/barcelona_pavilion/README.md (added
in a later Slice 3 commit).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Loop termination

The triage-and-patch loop ends when:

- The smoke render completes with NO `Error:` diagnostics.
- Every `Warning:` is either anticipated (already in M1's substitution table) or has an entry in `slice3-smoke-report.md`'s "Documented gaps" section.
- The smoke render's 160×90 PNG shows recognizable Pavilion composition (floor, walls, pool, sky, marble columns/cross — even at low res these should be visible as colored regions in the right places).

If after a small number of patches the smoke render still produces structural artifacts (large all-black regions, NaN swathes), that's a signal that something deeper is wrong than per-directive degradation can fix. STOP and report BLOCKED with the structural symptoms — the controller decides whether to escalate (it might be a real BVH or path-tracer bug exposed by Pavilion's scale, not a directive gap).

---

## Task 3: Full quality render + reference image

**Files:**

- Create on disk (committed): `docs/architecture/barcelona-pavilion.png`.
- Delete on disk (worktree cleanup): the `.smoke` scene copies from Task 1, `slice3-smoke-output.log`, `slice3-smoke-output.png`, any intermediate `.png` from the smoke loop.

- [ ] **Step 1: Clean up the smoke artifacts**

```bash
rm -f external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT>.smoke
rm -f external/assets/pbrt/barcelona-pavilion/barcelona-pavilion-smoke.png
rm -f slice3-smoke-output.log slice3-smoke-output.png
```

If you copied an Include file's `.smoke` variant, delete that too.

`slice3-smoke-report.md` stays in the worktree for now — Task 4 uses it to write the README.

- [ ] **Step 2: Run the full quality render**

```bash
time ./build/Release/yaoray.exe render \
    external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT> \
    --backend cpu \
    2>&1 | tee slice3-full-render.log
```

Record the wall-clock time. Target: ≤ 30 minutes (1800 seconds).

The output PNG lands at the path declared in the original `Film "string filename"` directive in the scene file — typically next to the scene, e.g., `external/assets/pbrt/barcelona-pavilion/barcelona-pavilion.png` or similar. Find it via the log line `Rendered image: <path>`.

If the render takes substantially longer than 30 minutes (e.g., 45–60+), check whether one of the patches in Task 2 introduced a perf regression (e.g., a degraded fallback that's much slower than expected). Profile briefly; if the cause isn't obvious or fixing it is large, document the actual time in the PR description and ship — the 30-minute target is a soft goal, not a hard merge gate (the M2 spec calls it out as a stretch target with mitigation: "ship M2 with whatever the actual render time is" if it exceeds 30 min for non-trivial reasons).

If the render produces NaN/Inf pixels (visible as bright pink/green spots, or the renderer reports `Hits` vs `Misses` ratios that suggest path tracing diverged), that IS a blocker — investigate before continuing.

- [ ] **Step 3: Move the rendered PNG to the docs directory**

```bash
mv external/assets/pbrt/barcelona-pavilion/<RENDERED_FILENAME>.png docs/architecture/barcelona-pavilion.png
```

Replace `<RENDERED_FILENAME>` with whatever the log said.

- [ ] **Step 4: Verify the reference image**

Open `docs/architecture/barcelona-pavilion.png`. Quality check:

- Composition matches Pavilion: marble floor, glass walls, reflective water pool with sky reflection, chrome cross-piece column, HDRI sky.
- No NaN/Inf (pink/green) speckle.
- No missing geometry holes.
- Materials look plausible (glass is transparent, chrome is reflective, marble is diffuse + slight highlight).

If any of these fail, debug before committing.

- [ ] **Step 5: Commit the reference image**

```bash
git add docs/architecture/barcelona-pavilion.png
git commit -m "$(cat <<'EOF'
docs(architecture): Pavilion reference render at 1280x720 / 64 spp

Full quality render of mmp/pbrt-v4-scenes barcelona-pavilion at
1280x720 / 64 spp. Render time: <X>s on the dev sandbox (Windows,
MSVC 19.51, Release, 11-thread CPU PT).

The composition matches Pavilion's bundled reference: polished
marble floor, glass walls, reflective water pool, chrome
cross-piece, HDRI sky.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Replace `<X>` with the actual render time in seconds.

- [ ] **Step 6: Delete the local render log**

```bash
rm -f slice3-full-render.log
git status   # should be clean
```

---

## Task 4: Pavilion per-scene README

**Files:**

- Create: `scenes/pbrt/barcelona_pavilion/README.md` (committed).

Model: `scenes/pbrt/dining_room/README.md` (already on main). The new README mirrors that file's section structure: header, Download, Render, What works, optional Camera convention (only if Pavilion exposed something new), reference to the committed image.

- [ ] **Step 1: Create the README**

Write `scenes/pbrt/barcelona_pavilion/README.md` with this content (replace placeholders inside `<>` with actual values from Tasks 1–3):

```markdown
# Barcelona Pavilion (mmp/pbrt-v4-scenes)

YaoRay's M2 anchor scene. This directory intentionally stays empty in git —
the asset lives in Matt Pharr's `pbrt-v4-scenes` repository with `git lfs`
and we link to it rather than redistributing.

## Download

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
git clone --filter=blob:none --sparse https://github.com/mmp/pbrt-v4-scenes.git pbrt-v4-scenes-tmp
cd pbrt-v4-scenes-tmp
git sparse-checkout set barcelona-pavilion
git lfs pull --include="barcelona-pavilion/**"
cd ..
mv pbrt-v4-scenes-tmp/barcelona-pavilion ./barcelona-pavilion
rm -rf pbrt-v4-scenes-tmp
```

(Requires `git lfs` installed. The `barcelona-pavilion/` subtree under
`external/assets/pbrt/` is gitignored via the project-wide
`external/assets/` rule.)

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/barcelona-pavilion/<PAVILION_PBRT> --backend cpu
```

The output PNG lands at the path declared in the scene's `Film "string
filename"` directive.

## What works in M2

- The <NN>-triangle scene parses, compiles, and renders end-to-end with
  no `Error:` diagnostics from our compiler.
- <Material summary — e.g., "All M declared materials (`diffuse`,
  `conductor`, `dielectric`, `coateddiffuse`, `coatedconductor`) resolve
  to YaoRay's BSDFs directly; <N> materials degrade per the documented
  substitution policy below.">
- <Texture summary — e.g., "<N> imagemap textures (formats: PNG, JPEG, TGA,
  HDR, PFM) load via the M1 texture loaders and bind correctly to
  reflectance / eta / roughness slots.">
- The HDRI environment (`<SKYDOME_FILENAME>`) drives the
  `LightSource "infinite"` importance sampler.
- The BVH builds at depth <D> (SAH binned, parallel construction). Build
  time on the dev sandbox: <T>s. Render time: <R>s wall-clock at
  1280x720 / 64 spp.

A reference render at 1280x720 / 64 spp lives at
`docs/architecture/barcelona-pavilion.png`.

## Documented degradations (Pavilion-specific)

The following Pavilion directives degrade gracefully via the existing
M1 substitution pattern (`MaterialFallbackWarning` + sensible fallback).
Each is also enumerated under "Materials with documented degradation" in
`docs/architecture/overview.md`:

<Bullet list, one per documented gap from slice3-smoke-report.md's
"Documented gaps" section. Format:
- `<directive/kind>` → `<fallback>`: <one-sentence reason>.
If no Pavilion-specific gaps surfaced (only M1's anticipated ones fired),
write: "(none — every Pavilion directive lands in a direct handler or in
the M1 substitution table without new entries)" instead.>
```

If Pavilion exposed a NEW camera/scene convention issue that wasn't already covered by dining-room's "Camera convention" section, add a section documenting it. Otherwise omit the camera section — dining-room's README already established the world-to-camera convention and Pavilion either matches it (no new note needed) or breaks it (a Task 2 patch would have already fixed it and the diagnostic note should appear in the documented degradations section).

- [ ] **Step 2: Verify the README renders cleanly**

```bash
cat scenes/pbrt/barcelona_pavilion/README.md
```

Sanity check: no leftover `<placeholder>` markers, no broken markdown, links to the right files.

- [ ] **Step 3: Commit the README**

```bash
git add scenes/pbrt/barcelona_pavilion/README.md
git commit -m "$(cat <<'EOF'
docs(scenes): Pavilion per-scene README

Mirrors scenes/pbrt/dining_room/README.md structure: download
instructions, render command, what works on M2, and the list of
Pavilion-specific documented degradations (if any).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Delete the local smoke report**

```bash
rm -f slice3-smoke-report.md
git status   # should be clean
```

---

## Task 5: Project-wide docs refresh

**Files:**

- Modify: `README.md` — add Pavilion to the showcase table; bump the M1-done sentence to reflect that M2 anchor is now also rendering.
- Modify: `docs/architecture/overview.md` — update the M1-era "Median-split BVH" wording (line ~53); bump the M2 roadmap-table row (`planned` → `done`); optionally add Pavilion to the showcase table.

- [ ] **Step 1: Update `README.md`**

Open `README.md`. Two edits:

**Edit A** — line ~13 (the "Current Status" section). Change:

```markdown
The M1 milestone is complete. The renderer handles:
```

to:

```markdown
M1 and M2 are complete. The renderer handles:
```

And update the bullet about the BVH (line ~16). Currently reads:

```markdown
- A multi-threaded CPU path tracer with median-split BVH, MIS over BSDF / area-light / environment samples, Russian-roulette termination, ACES/Reinhard/identity tone mapping, and PNG output.
```

Change to:

```markdown
- A multi-threaded CPU path tracer with SAH-binned BVH and parallel BVH construction, MIS over BSDF / area-light / environment samples, Russian-roulette termination, ACES/Reinhard/identity tone mapping, and PNG output.
```

**Edit B** — the Showcase Scenes table (around line 67–75). Add a Pavilion row at the bottom of the table, mirroring the dining-room row's tone:

```markdown
| `scenes/pbrt/barcelona_pavilion/` | mmp's PBRT v4 Barcelona Pavilion (asset downloaded separately; see the per-scene README for the `git lfs` workflow). |
```

Optionally update the M2-still-planned reference in line 17 if any. Save the file.

- [ ] **Step 2: Update `docs/architecture/overview.md`**

Open `docs/architecture/overview.md`. Three edits:

**Edit A** — line 53 (the "Median-split BVH" bullet in the Backend section). Currently:

```markdown
- Median-split BVH over the triangle table, plus a linear pass over
  analytic spheres after the BVH walk.
```

Change to:

```markdown
- SAH-binned BVH (12 buckets, c_T = 0.5) with parallel top-down
  construction (deterministic, thread-local merge); a linear pass over
  analytic spheres runs after the BVH walk.
```

**Edit B** — the M2 row in the roadmap table at line ~96. Currently:

```markdown
| **M2** | `barcelona-pavilion` (mmp PBRT v4) | SAH BVH + parallel BVH build; dining-room renders ≥ 2× faster | planned |
```

Change to:

```markdown
| **M2** | `barcelona-pavilion` (mmp PBRT v4) | SAH BVH + parallel BVH build; dining-room renders ≥ 2× faster | done |
```

**Edit C** — the showcase table at the bottom of the file (around lines 63–73). Add a Pavilion row:

```markdown
| `scenes/pbrt/barcelona_pavilion/README.md` | mmp's PBRT v4 Barcelona Pavilion (downloaded; gitignored). |
```

(Place it under the dining-room row — same pattern.)

Save the file.

- [ ] **Step 3: Verify the docs render cleanly**

```bash
cat README.md | head -80
cat docs/architecture/overview.md
```

Sanity check: no broken markdown, both edits landed, the M2 row in the roadmap table shows "done".

- [ ] **Step 4: Commit the docs refresh**

```bash
git add README.md docs/architecture/overview.md
git commit -m "$(cat <<'EOF'
docs: mark M2 complete; add Pavilion to showcase tables

- README.md: bump status sentence to M2-done; replace median-split
  reference with SAH + parallel BVH; add Pavilion to showcase table.
- docs/architecture/overview.md: rewrite the M1-era median-split bullet
  to describe the M2 SAH + parallel-construction reality; flip the M2
  roadmap row from planned to done; add Pavilion to the showcase table.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline 07398a3..HEAD
```

Expected commit count: 1 (Task 3 reference image) + 1 (Task 4 README) + 1 (Task 5 docs refresh) + N (Task 2 patches/degrades, depends on what surfaced). Order:

1. Task 2 patches/degrades (N commits, oldest first by topological order)
2. Task 3 reference image commit
3. Task 4 README commit
4. Task 5 docs refresh commit

If Task 2 produced zero commits (Pavilion happened to render straight from main with no patches needed), the branch has 3 commits total.

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin m2-slice3-barcelona-pavilion
gh pr create --title "feat: M2 Slice 3 - Barcelona Pavilion integration (M2 done)" --body "$(cat <<'EOF'
## Summary

- Render the unmodified mmp/pbrt-v4-scenes Barcelona Pavilion at 1280x720 / 64 spp end-to-end in <X>s on the dev sandbox.
- Commit the reference image to `docs/architecture/barcelona-pavilion.png`.
- Add `scenes/pbrt/barcelona_pavilion/README.md` documenting the `git lfs` download workflow, what works, and any Pavilion-specific documented degradations.
- Refresh `README.md` and `docs/architecture/overview.md`: M1+M2 done, SAH + parallel BVH wording, Pavilion in showcase tables, M2 row flipped to `done` in the roadmap.

## Pavilion compatibility patches

<Bullet list of Task 2 commits, one bullet each, with one-line summary
each. Format:
- `<commit_sha>` <commit subject>

If Task 2 produced zero commits, write: "(none — Pavilion rendered
straight from main without any directive-level patches)" instead.>

## Pavilion-specific documented degradations

<Bullet list mirroring the README's Documented degradations section. Or
"(none)" if there are no Pavilion-specific gaps beyond the M1 substitution
table.>

## Quality bar (per M2 spec)

- [x] Pavilion renders to completion at 1280x720 / 64 spp in <X>s (target: <= 30 min).
- [x] Composition matches Pavilion's bundled reference: polished marble floor, glass walls, reflective water pool, chrome cross-piece, HDRI sky.
- [x] No `Error:` compiler diagnostics. Each `Warning:` is either M1-anticipated or documented in the Pavilion README.
- [x] No NaN/Inf pixels.
- [x] All 180 unit tests + 8 CTest entries still pass (+<K> new tests from Task 2 patches if any).

## Test plan

- [x] `yaoray_tests.exe` — <180+K>/<180+K> PASS
- [x] `ctest --output-on-failure -C Release` — 8/8 PASS
- [x] Pavilion smoke render (160x90 / 4 spp) — no Error diagnostics
- [x] Pavilion full render (1280x720 / 64 spp) — composition verified visually against bundled reference
- [x] M2 status updated in `docs/architecture/overview.md` (planned -> done)

## Closes

M2 milestone. Next: M3 (Advanced Materials — real `subsurface` + `measured` + nested `layered`).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Replace `<X>` (render time) and `<K>` (new unit test count) with actual values. Fill in the patch list and degradation list from Tasks 2 and 4.

- [ ] **Step 3: Address review feedback**

If review surfaces issues, fix on the worktree branch with new commits (no force-push, no amend). Re-run `yaoray_tests.exe` + `ctest` after each fix.

- [ ] **Step 4: Merge**

When the PR is approved and the operator confirms, merge via the GitHub UI. Then locally:

```bash
git checkout main
git pull origin main
git worktree remove .worktrees/m2-slice3-barcelona-pavilion
git branch -D m2-slice3-barcelona-pavilion
```

The worktree branch is gone; Slice 3 (and M2 as a whole) is in `main`. M3 can be planned next.

---

## Self-Review Notes

- **Spec coverage:** every Slice 3 deliverable from §"Slice 3 — Barcelona Pavilion integration" of the M2 spec is mapped to a task: download (Task 1), smoke render (Task 1), triage + patch (Task 2), full render (Task 3), README (Task 4), docs refresh (Task 5), PR (Task 6).
- **TDD discipline:** every Task 2 sub-cycle starts with a failing unit test for the patched/degraded behavior, then implements, then re-runs to verify pass. The discovery loop nature of Task 2 means the SPECIFIC tests can't be pre-written, but the TEMPLATE for each sub-cycle is concrete.
- **Discovery uncertainty:** Task 2 is explicitly exploratory. The plan provides the decision rule (patch vs degrade), the existing-pattern reference (`MaterialFallbackWarning` + substitution table at scene_compiler.cpp:33-37 and 513-547), the commit discipline (one issue = one commit), and the loop-termination criterion (no Errors, every Warning anticipated or documented, composition recognizable). It does NOT pre-enumerate patches.
- **Perf gate:** ≤ 30 min is a soft target per the M2 spec's risk register. The plan calls this out in Task 3 Step 2 with the spec's contingency (ship with the actual time, document the gap).
- **Worktree cleanup:** Task 3 Step 6 and Task 4 Step 4 explicitly remove the in-flight worktree artifacts (`slice3-smoke-output.log`, `slice3-smoke-output.png`, `slice3-smoke-report.md`, `slice3-full-render.log`, the `.smoke` scene copy) before PR open. The asset itself is gitignored under `external/assets/` so nothing about Pavilion's bulk lands in git.
- **Asset filename placeholder:** the plan uses `<PAVILION_PBRT>` as a placeholder that gets pinned in Task 1 Step 3. Every subsequent reference uses the same placeholder so the implementer substitutes once.
- **Documented degradations propagation:** Task 2 sub-cycle Step 7 appends to `slice3-smoke-report.md`'s "Documented gaps" section. Task 4 Step 1 then copies that list into the Pavilion README. This ensures every degrade fires a Warning at compile time AND has a paper-trail entry in the per-scene README.
- **Worktree branch name** `m2-slice3-barcelona-pavilion` used in Setup, Task 6 Step 2 push, and Task 6 Step 4 cleanup.
