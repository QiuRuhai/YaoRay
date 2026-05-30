# YaoRay M3 Measured Slice 2 — `f` evaluation (warp + spectral→RGB)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`). **Task 1 (the warp class) and Task 2 (`MeasuredBxDF::f`) are BSDF math — dispatch to the most capable model.**

**Goal:** Make `measured` materials evaluate their real BRDF: port PBRT v4's `PiecewiseLinear2D` warp, build the warps from the loaded `.bsdf` arrays, implement `MeasuredBxDF::f(wo, wi)` (Dupuy & Jakob 2018), convert the spectral table to RGB, and wire `EvaluateBsdf` for `Measured`. `SampleBsdf`/`PdfBsdf` stay aliased to the conductor in Slice 2 (Slice 3 adds data-driven sampling). Deterministic — no RNG.

**Architecture:** `PiecewiseLinear2D<Dimension>` (a faithful pbrt-v4 port: bilinear 2D grid + linear conditioning-axis blend; **no Catmull-Rom**). The Slice-1 `MeasuredBrdf` (raw arrays) gains built warps (`ndf`/`sigma` as `<0>`, `vndf`/`luminance` as `<2>`, `spectra` as `<3>`), built in `LoadMeasuredBrdf`. `MeasuredBxDF::f` does the half-vector→unit-square mapping, `vndf.Invert`, `spectra.Evaluate` at 3 RGB wavelengths, and assembles `f = spectra·ndf / (4·sigma·cosθ_i)`.

**Tech Stack:** C++20, CMake, `yr_test.hpp`, CTest.

---

## Scope

**Slice 2 delivers:** `PiecewiseLinear2D<Dim>` (constructor + `Evaluate` + `Invert`); the warps built into `MeasuredBrdf`; `MeasuredBxDF::f` + spectral→RGB (3 wavelengths); `EvaluateBsdf(Measured)` wired. `IsDeltaBsdf(Measured)` stays false.

**Slice 2 does NOT do:** `PiecewiseLinear2D::Sample` + `MeasuredBxDF::Sample`/`Pdf` (Slice 3 — `SampleBsdf`/`PdfBsdf` keep the conductor alias); sportscar (Slice 4); anisotropic.

**Test-data note:** the loaded eval is validated on a **synthetic `.bsdf` with constant non-zero tables** (assembly + frame + warp chain are checkable by hand) plus the analytic warp tests. Real-material correctness (does it match PBRT on a genuine `.bsdf`) is validated at the **Slice 4 sportscar render** (the real files are ~6.6 MB spectral — not vendored here). If a small real RGB `.bsdf` is readily obtainable, an optional energy test (ρ≤1) may be added, but it is not required for Slice 2.

---

## Authoritative algorithm reference (port to this — from pbrt-v4)

### `MeasuredBxDF::f(wo, wi)` (local frame, z = normal)
```
if (!SameHemisphere(wo, wi)) return 0
if (wo.z < 0) { wo = -wo; wi = -wi; }            // bring to upper hemisphere
wm = wo + wi; if (|wm|^2 == 0) return 0; wm = normalize(wm)
theta_o = acos(clamp(wo.z,-1,1)); phi_o = atan2(wo.y, wo.x)
theta_m = acos(clamp(wm.z,-1,1)); phi_m = atan2(wm.y, wm.x)
u_wo = ( theta2u(theta_o), phi2u(phi_o) )
u_wm = ( theta2u(theta_m), phi2u( isotropic ? (phi_m - phi_o) : phi_m ) )
u_wm.y = u_wm.y - floor(u_wm.y)                  // wrap phi to [0,1]
ui = vndf.Invert(u_wm, /*params=*/ phi_o, theta_o)   // -> {p (Vec2), pdf}
for each RGB channel c at wavelength wl_c:
    fr[c] = max(0, spectra.Evaluate(ui.p, phi_o, theta_o, wl_c))
denom = 4 * sigma.Evaluate(u_wo) * cosTheta_i      // cosTheta_i = wi.z (>0 in upper hemi)
return fr * ndf.Evaluate(u_wm) / denom             // guard denom>0 -> else 0
```
Helpers: `theta2u(t)=sqrt(t*(2/Pi))`, `phi2u(p)=p*(1/(2Pi))+0.5`. There is **no extra Jacobian** factor (`jacobian` flag is load-time only). `luminance` is NOT used in `f`.

**RGB wavelengths:** R = 630 nm, G = 532 nm, B = 467 nm (passed as the wavelength conditioning param to `spectra.Evaluate`).

**Local frame:** YaoRay BSDFs take world `wo`/`wi` + `normal`. Build an orthonormal basis (`normal` = local z; any tangent — isotropic makes the azimuth origin irrelevant), transform `wo`,`wi` to local, run the above. (Reuse the frame-building helper used elsewhere in `bsdf.cpp`, e.g. the cosine-hemisphere tangent construction, or a small `BuildFrame(normal)`.)

### `PiecewiseLinear2D<Dimension>` (pbrt-v4 `src/pbrt/util/sampling.h`)
Port faithfully. Members: `m_size` (xSize,ySize), `m_patch_size`, `m_inv_patch_size`, per-axis `m_param_size`/`m_param_strides`/`m_param_values`, and `m_data` / `m_marginal_cdf` / `m_conditional_cdf` (flat float vectors). `slices = product(param_size)`.
- **Constructor(`data, xSize, ySize, param_res[], param_values[], normalize, build_cdf`)**: copy data; if `build_cdf`: per-slice build conditional CDF (trapezoid integrate each row over x) + marginal CDF (trapezoid over rows using each row's last conditional value); then if `normalize` divide data + both CDFs by the marginal total. (ndf/sigma/spectra: `build_cdf=false, normalize=false` → store data only.)
- **`Evaluate(pos, params...)`**: bilinear over the 2D grid — scale `pos` by `m_inv_patch_size`, clamp the lower cell, fetch 4 corners via `lookup<Dim>` (recursive linear blend across the `Dim` conditioning axes using precomputed `param_weight`), FMA-blend, multiply by `HProd(m_inv_patch_size)`.
- **`Invert(sample, params...)`**: returns `{p, pdf}` — bilinear pdf at the fractional cell position, then invert the X conditional then Y marginal using the CDFs (the exact sequence is in `sampling.h`).
- `lookup<Dim>`: `Dim==0 → data[index]`; `Dim>0 → FMA(lookup<Dim-1>(i0), w[2Dim-2], lookup<Dim-1>(i1)*w[2Dim-1])` with `i1=i0+param_strides[Dim-1]*size`.
- **Param weights:** for each conditioning axis, binary-search `param_values` for the query value, compute the linear weight + the slice offset (`param_weight` array + `index` base). (Quote/port pbrt's `Evaluate` prologue that fills `param_weight` and `index`.)

**The implementer must fetch `src/pbrt/util/sampling.h` from pbrt-v4 and port `PiecewiseLinear2D`'s constructor + `Evaluate` + `Invert` exactly** (adapting `Vector2f/Point2f` → YaoRay `Vec2f`, `pstd::vector` → `std::vector`, `FMA` → `std::fma` or `a*b+c`). Slice 3 will add `Sample`. The bilinear `Evaluate` is analytically testable (Task 1).

---

## File Structure

| Path | Change |
|---|---|
| `include/yaoray/render/piecewise_linear_2d.hpp` (new) | `PiecewiseLinear2D<Dimension>` template (header-only; constructor + Evaluate + Invert). |
| `include/yaoray/render/measured_brdf.hpp` (modify) | Add built warps to `MeasuredBrdf`: `PiecewiseLinear2D<0> ndf, sigma; PiecewiseLinear2D<2> vndf, luminance; PiecewiseLinear2D<3> spectra;`. |
| `src/render/measured_brdf.cpp` (modify) | In `LoadMeasuredBrdf`, after validation, build the 5 warps from the raw arrays (per the constructor-call summary). |
| `src/render/bsdf.cpp` (modify) | Add `MeasuredBxDF::f` (anon-namespace `EvaluateMeasured(material, brdf, wo, wi, normal)`); wire `EvaluateBsdf(Measured)` to it (pass the `measured_brdfs[measured_index]`). Needs the IR scene — see note. |
| `tests/piecewise_linear_2d_tests.cpp` (new) | Evaluate (analytic bilinear) + Invert (valid) tests. |
| `tests/measured_eval_tests.cpp` (new) | `f` assembly known-value, finite/non-negative, deterministic, differs-from-conductor — on a constant-table synthetic `.bsdf`. |
| `tests/tensor_test_util.hpp` (modify) | Extend `WriteSyntheticBsdf` to accept constant fill values (non-zero ndf/sigma/vndf/spectra) so `f` is exercisable. |
| `CMakeLists.txt` (modify) | Register the 2 new test files. |

**`EvaluateBsdf` needs the `MeasuredBrdf`:** `EvaluateBsdf(material, wo, wi, normal, rng)` does not currently receive the `RenderSceneIR` (where `measured_brdfs` lives). Resolve this in Task 2 — the cleanest is to store a `const MeasuredBrdf*` pointer on `RenderMaterial` (set at compile time, pointing into `ir.measured_brdfs`) so `bsdf.cpp` can reach it without the scene. **Caution:** a pointer into a `std::vector` is invalidated if the vector reallocates — set the pointers AFTER all measured materials are loaded (a fix-up pass over materials once `measured_brdfs` is final), or store the brdf in a `std::vector<std::unique_ptr<MeasuredBrdf>>` / `std::shared_ptr` so addresses are stable. Pick stable storage; document it. (Verify how the CPU backend passes material data into `EvaluateBsdf` — if it already has scene access at the call site, passing the `MeasuredBrdf*` through is simpler.)

---

## Setting up the worktree

`EnterWorktree` name `m3-measured-slice2-eval`, off `main` (HEAD `52513f4`). Baseline:
```bash
cmake -S . -B build && cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c   # 256 / 0
cd build && ctest -C Release 2>&1 | tail -3 ; cd ..   # 9/9
```

---

## Task 1: Port `PiecewiseLinear2D` + build warps into `MeasuredBrdf` (BSDF math — strong model)

**Files:** create `include/yaoray/render/piecewise_linear_2d.hpp`, `tests/piecewise_linear_2d_tests.cpp`; modify `measured_brdf.{hpp,cpp}`, `CMakeLists.txt`.

- [ ] **Step 1: Fetch + port the class.** Fetch `https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h` and locate `PiecewiseLinear2D`. Port the constructor + `Evaluate` + `Invert` (NOT `Sample` — Slice 3) into `piecewise_linear_2d.hpp` as `template <int Dimension> class PiecewiseLinear2D`. Adapt types to YaoRay (`Vec2f`, `std::vector<float>`, `std::fma`). Keep the exact CDF-build + bilinear + param-blend logic. Header-only (templated).

- [ ] **Step 2: Failing analytic tests** `tests/piecewise_linear_2d_tests.cpp`:
```cpp
#include "yr_test.hpp"
#include <yaoray/render/piecewise_linear_2d.hpp>

YR_TEST(plinear2d_eval_constant_grid) {
    // 2x2 grid of all 1.0, no conditioning, no cdf, no normalize -> Evaluate == 1 everywhere.
    const float data[4] = {1,1,1,1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, /*normalize=*/false, /*build_cdf=*/false);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.25f, 0.75f}), 1.0f, 1e-5f);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.0f, 0.0f}), 1.0f, 1e-5f);
}
YR_TEST(plinear2d_eval_bilinear_ramp) {
    // 2x2 grid {0,1 / 0,1}: value depends only on x; Evaluate at x=0.5 (pos.x=0.5) -> ~0.5.
    const float data[4] = {0,1, 0,1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, false, false);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{0.5f, 0.5f}), 0.5f, 1e-5f);
    YR_EXPECT_NEAR(d.Evaluate(yr::Vec2f{1.0f, 0.5f}), 1.0f, 1e-4f);
}
YR_TEST(plinear2d_invert_returns_valid) {
    // A normalized cdf-built 2x2 -> Invert returns p in [0,1]^2 and pdf>=0.
    const float data[4] = {1,1,1,1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, true, true);
    auto s = d.Invert(yr::Vec2f{0.3f, 0.6f});
    YR_EXPECT_TRUE(s.p.x >= -1e-4f && s.p.x <= 1.0001f);
    YR_EXPECT_TRUE(s.p.y >= -1e-4f && s.p.y <= 1.0001f);
    YR_EXPECT_TRUE(s.pdf >= 0.0f && std::isfinite(s.pdf));
}
```
(Adapt the `PiecewiseLinear2D` constructor signature + `Invert` return type — `struct { Vec2f p; float pdf; }` — to whatever you port. Verify `YR_EXPECT_NEAR` etc.) Register the test. The exact bilinear values above assume pbrt's convention (`pos` already in [0,1], scaled by `inv_patch_size = xSize-1`); if your port's `Evaluate` expects a different input scaling, adjust the test positions to match the ported semantics (document the convention).

- [ ] **Step 3: Verify FAIL → implement → PASS.**

- [ ] **Step 4: Build the warps in `LoadMeasuredBrdf`.** Add the 5 `PiecewiseLinear2D` members to `MeasuredBrdf`; after validation build them from the raw arrays per the pbrt constructor-call summary:
  - `ndf = <0>(ndf_data, ndf_shape[1], ndf_shape[0], {}, {}, false, false)`
  - `sigma = <0>(sigma_data, sigma_shape[1], sigma_shape[0], {}, {}, false, false)`
  - `vndf = <2>(vndf_data, vndf_shape[3], vndf_shape[2], {n_phi_i, n_theta_i}, {phi_i, theta_i})` (normalize+cdf default true)
  - `luminance = <2>(lum_data, lum_shape[3], lum_shape[2], {n_phi_i, n_theta_i}, {phi_i, theta_i})`
  - `spectra = <3>(spectra_data, spectra_shape[4], spectra_shape[3], {n_phi_i, n_theta_i, n_wl}, {phi_i, theta_i, wavelengths}, false, false)`
  Keep the raw arrays or drop them (the warps own copies). Existing Slice-1 measured tests must still pass (the synthetic all-zero `.bsdf` builds without crashing — guard divides).

- [ ] **Step 5: Full suite (256 + 3) + CTest 9/9. Commit** — `feat(render): PiecewiseLinear2D warp + build measured warps`.

**Degenerate-data guard (do in Step 4):** the Slice-1 synthetic `.bsdf` files are all-zero, so a `vndf` with `build_cdf+normalize` has a zero marginal total → `1/0`. Guard the constructor: if the marginal total is `≤ 0` (or non-finite), **skip normalization** (leave data/CDFs as-is) instead of dividing — so all-zero files still load cleanly. The existing Slice-1 `measured_brdf_*` and `scene_compiler_measured_*` tests must stay green after warp-building lands in the loader.

---

## Task 2: `MeasuredBxDF::f` + spectral→RGB + wire `EvaluateBsdf` (BSDF math — strong model)

**Files:** modify `src/render/bsdf.cpp`, `include/yaoray/render/render_scene.hpp`, `src/render/scene_compiler.cpp`, `tests/tensor_test_util.hpp`, `CMakeLists.txt`; create `tests/measured_eval_tests.cpp`.

### Wiring: make the `MeasuredBrdf` reachable from `EvaluateBsdf`
`EvaluateBsdf(material, wo, wi, normal, rng)` has no scene access. Give `RenderMaterial` a stable pointer to its brdf:
- Change `RenderSceneIR.measured_brdfs` to `std::vector<std::unique_ptr<MeasuredBrdf>>` (heap addresses survive vector growth/moves). Update the Slice-1 compiler push: `ir.measured_brdfs.push_back(std::make_unique<MeasuredBrdf>(std::move(*m)));`. (The Slice-1 test asserting `measured_brdfs` non-empty still holds.)
- Add `const MeasuredBrdf* measured_brdf = nullptr;` to `RenderMaterial`; set it at compile right after the push: `material.measured_brdf = ir.measured_brdfs[idx].get();`. A copied `RenderMaterial` (the path tracer copies it from the hit) keeps the pointer; the IR outlives the render, so it stays valid.
- `EvaluateBsdf(Measured)`: if `material.measured_brdf != nullptr` → `return EvaluateMeasured(*material.measured_brdf, wo, wi, normal);` else fall through to the conductor alias (degraded materials have no brdf).

- [ ] **Step 1: Extend the synthetic helper + write failing tests.** In `tests/tensor_test_util.hpp`, add optional constant fills to `WriteSyntheticBsdf` so `f` is exercisable (default keeps current behavior):
```cpp
// add params with defaults; fill ndf/sigma/vndf/luminance/spectra with the given constants.
inline std::string WriteSyntheticBsdf(const char* path, int n_phi_i,
        float ndf_v=0.f, float sigma_v=0.f, float vndf_v=0.f, float lum_v=0.f, float spec_v=0.f) {
    // ... same dims; replace each zeros(N) with std::vector<float>(N, <the matching *_v>) ...
}
```
(Existing Slice-1 callers `WriteSyntheticBsdf(p, 2)` / `(p, 4)` keep all-zero behavior.) Then `tests/measured_eval_tests.cpp`:
```cpp
#include "yr_test.hpp"
#include "tensor_test_util.hpp"
#include <yaoray/render/measured_brdf.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>
#include <cmath>
#include <cstdio>

namespace { yr::Vec3f Up() { return yr::Vec3f{0,0,1}; } }

YR_TEST(measured_eval_assembly_known_value) {
    // Constant tables: ndf=N, sigma=S, spectra=P (vndf positive so the warp builds).
    // f = P*N / (4*S*cos_wi) on every RGB channel.
    const float N = 1.0f, S = 0.5f, P = 0.3f;
    const std::string p = yrtest::WriteSyntheticBsdf("measured_eval.bsdf", 2, N, S, 1.0f, 1.0f, P);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(m.has_value());
    // Build a RenderMaterial pointing at *m and call EvaluateBsdf, OR call EvaluateMeasured directly
    // if it is reachable. Simplest: construct a RenderMaterial{kind=Measured, measured_brdf=&*m}.
    yr::RenderMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.3f, 0.0f, 1.0f});
    yr::Rng rng{1u};
    const yr::Color3f f = yr::EvaluateBsdf(mat, wo, wi, n, rng);
    const float cos_wi = yr::Dot(wi, n);
    const float expect = P * N / (4.0f * S * cos_wi);
    YR_EXPECT_NEAR(f.x, expect, 1e-3f);
    YR_EXPECT_NEAR(f.y, expect, 1e-3f);
    YR_EXPECT_NEAR(f.z, expect, 1e-3f);
    std::remove(p.c_str());
}
YR_TEST(measured_eval_finite_nonneg_and_deterministic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_eval2.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::RenderMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    yr::Rng ra{2u}, rb{2u};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.5f, 0.9f});
    const yr::Color3f a = yr::EvaluateBsdf(mat, wo, wi, n, ra);
    const yr::Color3f b = yr::EvaluateBsdf(mat, wo, wi, n, rb);
    YR_EXPECT_TRUE(std::isfinite(a.x) && a.x >= 0.0f);
    YR_EXPECT_EQ(a.x, b.x); YR_EXPECT_EQ(a.y, b.y); YR_EXPECT_EQ(a.z, b.z);
    // wi in lower hemisphere -> 0 (reflection only)
    const yr::Color3f below = yr::EvaluateBsdf(mat, wo, yr::Normalize(yr::Vec3f{0.1f,0.1f,-1.0f}), n, ra);
    YR_EXPECT_NEAR(below.x, 0.0f, 1e-6f);
    std::remove(p.c_str());
}
```
Register `tests/measured_eval_tests.cpp`. (Verify `RenderMaterial` has the `measured_brdf` field after the wiring step; verify `EvaluateBsdf` is declared in `bsdf.hpp` with the `Rng&` param. If constructing a bare `RenderMaterial` in the test is awkward, instead compile a `measured` `PbrtScene` and pull the material from the result — mirror `scene_compiler_measured_tests.cpp`.)

- [ ] **Step 2: Run, verify FAIL** (Measured still aliases conductor → wrong value).

- [ ] **Step 3: Implement `EvaluateMeasured`** in `bsdf.cpp` (anon namespace), to the algorithm reference above:
  - Build a local frame from `normal` (z = normal; any orthonormal tangent/bitangent — reuse the existing tangent-basis construction in `bsdf.cpp`, e.g. from `SampleCosineHemisphere`). Transform `wo`, `wi` to local (`Dot` with tangent/bitangent/normal).
  - Run the `f` algorithm: `SameHemisphere` guard (local z signs), flip if `wo.z<0`, half-vector, `theta2u`/`phi2u`, `u_wm` (isotropic uses `phi_m - phi_o`, wrapped), `vndf.Invert(u_wm, phi_o, theta_o)`, then for the 3 RGB wavelengths `spectra.Evaluate(ui.p, phi_o, theta_o, wl)`, assemble `fr * ndf.Evaluate(u_wm) / (4 * sigma.Evaluate(u_wo) * cos_wi)`. Guard `cos_wi>0` and `denom>0` (else return black). Return `Color3f`.
  - Wavelength constants: `R=630, G=532, B=467` nm.

- [ ] **Step 4: Wire `EvaluateBsdf(Measured)`** to call `EvaluateMeasured(*material.measured_brdf, wo, wi, normal)` when the pointer is set (else conductor alias). Leave `PdfBsdf`/`SampleBsdf` aliased to conductor (Slice 3). `IsDeltaBsdf(Measured)` stays false.

- [ ] **Step 5: Run** — new eval tests PASS; full suite (256 + 3 + 3) ; CTest 9/9 (no measured scene in CTest, so renders unaffected). The known-value test pins the assembly; finiteness/determinism/reflection-only pin robustness.

- [ ] **Step 6: Commit**
```
feat(bsdf): real measured BRDF f evaluation (Dupuy-Jakob; spectral->RGB)

Implements MeasuredBxDF::f for measured materials: half-vector ->
unit-square mapping (isotropic phi canonicalization) -> vndf inverse
warp -> spectra lookup at 3 RGB wavelengths (630/532/467 nm) ->
f = spectra * ndf / (4 * sigma * cos). Builds a local frame from the
shading normal (isotropic-safe). EvaluateBsdf now returns the real
measured response via a stable MeasuredBrdf* on RenderMaterial.
Sample/Pdf stay conductor-aliased until Slice 3. Validated by a
constant-table assembly known-value test + finite/deterministic/
reflection-only. Real-material correctness lands at the Slice 4
sportscar render.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 3: PR + merge

- [ ] **Step 1:** `git log --oneline 52513f4..HEAD` — expect 2 commits (warp+build, f+wire).
- [ ] **Step 2:** push `worktree-m3-measured-slice2-eval`; `gh pr create --base main` — body: PiecewiseLinear2D port (bilinear, no Catmull-Rom) + warp building; MeasuredBxDF::f + spectral→RGB (3 wavelengths) + local frame + stable MeasuredBrdf*; scope (Sample/Pdf in Slice 3); validation (analytic warp tests + constant-table assembly; real-data at Slice 4).
- [ ] **Step 3:** address review; re-run tests.
- [ ] **Step 4:** merge (operator-gated); finish per `superpowers:finishing-a-development-branch` (ff main; remove worktree — manual fallback if `ExitWorktree` no-ops; delete remote branch).

---

## Self-Review Notes

- **Spec coverage:** Slice 2 of the measured spec — `PiecewiseLinear2D` eval + warp building (Task 1), `MeasuredBxDF::f` + spectral→RGB + `EvaluateBsdf` (Task 2), PR (Task 3). Sample/Pdf are Slice 3; sportscar is Slice 4.
- **Key simplification (from research):** the measured path is **pure bilinear** — no Catmull-Rom port needed (confirmed against pbrt-v4 `bxdfs.cpp` + `sampling.h`).
- **Spectral→RGB:** evaluate `spectra` at 3 fixed wavelengths (630/532/467) — faithful, cheap, matches PBRT's per-wavelength `Evaluate`. `luminance` is sampling-only (not in `f`).
- **Wiring:** `RenderSceneIR.measured_brdfs` becomes `vector<unique_ptr<MeasuredBrdf>>` for stable addresses; `RenderMaterial.measured_brdf` pointer set at compile; survives the path tracer's material copy. Documented lifetime: IR outlives render.
- **Degenerate guard:** the Slice-1 all-zero synthetic `.bsdf` must still load after warp-building lands — normalization skips on a zero/non-finite marginal total.
- **Test-data honesty:** Slice 2 validates code correctness (analytic warp + constant-table `f` assembly + finite/deterministic/reflection-only); **real-material correctness is deferred to the Slice 4 sportscar render** (real `.bsdf` files are ~6.6 MB spectral — not vendored). Flagged in the PR body.
- **Type consistency:** `PiecewiseLinear2D<Dim>{Evaluate, Invert}`; `MeasuredBrdf{ndf,sigma,vndf,luminance,spectra}` warps; `RenderMaterial.measured_brdf`; `EvaluateMeasured(brdf, wo, wi, normal)`; wavelengths 630/532/467 — consistent across tasks.
- **Worktree branch** `m3-measured-slice2-eval` consistent across Setup and Task 3.
