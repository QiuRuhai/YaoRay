# YaoRay M3 Measured Slice 3 — data-driven `Sample` / `Pdf`

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`). **Task 1 (warp Sample) and Task 2 (MeasuredBxDF Sample/Pdf) are BSDF math — dispatch to the most capable model.**

**Goal:** Give `measured` materials their real **importance sampling**: port `PiecewiseLinear2D::Sample`, implement `MeasuredBxDF::Sample`/`Pdf` (two-stage luminance→vndf warp + the reflection/domain Jacobian), and wire `SampleBsdf`/`PdfBsdf`. This replaces the Slice-2 conductor alias for sampling and makes `f`/sample/pdf a mutually-consistent set (the measured representation is built to importance-sample — so MIS is clean). Deterministic given the sampler's 2D input; no RNG stream consumed beyond the supplied sample.

**Architecture:** Add `PiecewiseLinear2D<Dim>::Sample(sample, params...) -> {p, pdf}` (the CDF inversion). Add `SampleMeasured`/`PdfMeasured` in `bsdf.cpp`: `Sample` does `luminance.Sample` then `vndf.Sample` → `wi`, reuses `EvaluateMeasured` for `f`, and computes the solid-angle pdf via the Jacobian; `Pdf` mirrors `MeasuredBxDF::PDF`. Wire the two coated... (measured) branches in `SampleBsdf`/`PdfBsdf`.

**Tech Stack:** C++20, CMake, `yr_test.hpp`, CTest.

---

## Scope

**Slice 3 delivers:** `PiecewiseLinear2D::Sample`; `SampleMeasured`/`PdfMeasured`; `SampleBsdf(Measured)`/`PdfBsdf(Measured)` wired to them. `IsDeltaBsdf(Measured)` stays false (it's glossy — light sampling + MIS apply).

**Slice 3 does NOT do:** sportscar (Slice 4); anisotropic. `EvaluateBsdf` (Slice 2) is unchanged.

**Test-data note:** like Slice 2, validated on the **constant-table synthetic `.bsdf`** — the decisive check is **sample/pdf consistency** (`PdfBsdf(wo, sampled_wi) ≈ sample.pdf`, exact since both are deterministic warp evaluations) + valid directions + determinism. Real-material correctness (and the visual energy check) lands at the **Slice 4 sportscar render**.

---

## Authoritative algorithm reference (port to this — pbrt-v4)

### `PiecewiseLinear2D<Dim>::Sample(Vec2f sample, params...) -> {Vec2f p, float pdf}`
The inverse of `Invert` (already ported). Fetch `https://raw.githubusercontent.com/mmp/pbrt-v4/master/src/pbrt/util/sampling.h` and port `Sample` verbatim:
1. Clamp `sample` away from {0,1}. Compute the param-blend weights + `slice_offset` (same prologue as `Evaluate`/`Invert`).
2. **Row (marginal):** `FindInterval` over the param-blended `marginal_cdf` for `row` with `marginal(row) < sample.y`; subtract; quadratic-solve `sample.y` using row masses `r0`,`r1` (`is_const` branch when `|r0-r1|` tiny).
3. **Column (conditional):** rescale `sample.x` by the blended row mass; `FindInterval` the conditional CDF for `col`; subtract; quadratic-solve `sample.x` using the 4 data corners `c0`,`c1`.
4. Return `p = ((col+sample.x)*patch_size.x, (row+sample.y)*patch_size.y)`, `pdf = ((1-sample.x)*c0 + sample.x*c1) * HProd(inv_patch_size)`.
The `SafeSqrt`, `m_patch_size`, `HProd` helpers/members kept in Slice 2 are exactly for this.

### `SampleMeasured(brdf, wo, normal, Vec2f u) -> BsdfSample` (local frame, z=normal)
```
build local frame; transform wo to local
if (lwo.z <= 0) { lwo = -lwo; flipWi = true; } else flipWi = false
theta_o = acos(clamp(lwo.z)); phi_o = atan2(lwo.y, lwo.x)
s1 = brdf.luminance_warp.Sample(u, phi_o, theta_o)      // {p, lum_pdf}
s2 = brdf.vndf_warp.Sample(s1.p, phi_o, theta_o)        // {u_wm, vndf_pdf}
phi_m = u2phi(s2.p.y); theta_m = u2theta(s2.p.x)
if (isotropic) phi_m += phi_o
wm = SphericalDirection(sin(theta_m), cos(theta_m), phi_m)   // local
wi_local = Reflect(lwo, wm) = 2*Dot(lwo,wm)*wm - lwo
if (wi_local.z <= 0) return invalid
sinTheta_m = sin(theta_m)
pdf = s2.vndf_pdf * s1.lum_pdf / ( 4*Dot(lwo,wm) * max(2*Pi*Pi*s2.p.x*sinTheta_m, 1e-6) )
if (flipWi) wi_local = -wi_local
wi_world = local->world(wi_local)
f = EvaluateMeasured(brdf, wo, wi_world, normal)    // reuse Slice 2 (its own frame+Invert; fine)
if (pdf <= 0 || !finite) return invalid
return { wi_world, weight = f * |Dot(wi_world,normal)| / pdf, pdf, valid=true, specular=false }
```
Helpers: `u2theta(u)=u*u*(Pi/2)`, `u2phi(u)=(2u-1)*Pi`. `SphericalDirection(sinT,cosT,phi) = (sinT*cos(phi), sinT*sin(phi), cosT)`.

### `PdfMeasured(brdf, wo, wi, normal) -> float` (mirror `MeasuredBxDF::PDF`)
```
local frame; transform wo, wi
if (lwo.z * lwi.z <= 0) return 0                       // SameHemisphere
if (lwo.z < 0) { lwo=-lwo; lwi=-lwi; }
wm = lwi + lwo; if (|wm|^2==0) return 0; wm = normalize(wm)
theta_o=acos(clamp(lwo.z)); phi_o=atan2(lwo.y,lwo.x)
theta_m=acos(clamp(wm.z)); phi_m=atan2(wm.y,wm.x)
u_wm = ( theta2u(theta_m), phi2u(isotropic ? phi_m-phi_o : phi_m) ); u_wm.y -= floor(u_wm.y)
ui = brdf.vndf_warp.Invert(u_wm, phi_o, theta_o)       // {sample=ui.p, vndfPDF=ui.pdf}
lum = brdf.luminance_warp.Evaluate(ui.p, phi_o, theta_o)
sinTheta_m = sqrt(wm.x^2 + wm.y^2)
jac = 4*Dot(lwo,wm) * max(2*Pi*Pi*u_wm.x*sinTheta_m, 1e-6)
return (jac > 0) ? vndfPDF * lum / jac : 0
```
`theta2u`/`phi2u` from Slice 2. **`SampleMeasured`'s returned `pdf` and `PdfMeasured(wo, wi)` are equal** for the same (wo,wi) — the consistency contract (Task 2's headline test).

---

## File Structure

| Path | Change |
|---|---|
| `include/yaoray/render/piecewise_linear_2d.hpp` (modify) | Add `Sample(Vec2f, params...)`. |
| `src/render/bsdf.cpp` (modify) | Add `SampleMeasured` + `PdfMeasured` (anon ns); wire `SampleBsdf(Measured)` + `PdfBsdf(Measured)` to use `material.measured_brdf` (else conductor fallthrough). |
| `tests/piecewise_linear_2d_tests.cpp` (modify) | Sample tests: uniform → identity-ish; Sample↔Invert round-trip + pdf consistency. |
| `tests/measured_eval_tests.cpp` (modify) OR new `tests/measured_sample_tests.cpp` | Sample valid-direction, **sample/pdf consistency**, determinism, differs-from-conductor. |
| `CMakeLists.txt` (modify, if new test file) | Register. |

---

## Setting up the worktree

`EnterWorktree` name `m3-measured-slice3-sampling`, off `main` (HEAD `0ea6915`). Baseline:
```bash
cmake -S . -B build && cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c   # 266 / 0
cd build && ctest -C Release 2>&1 | tail -3 ; cd ..   # 9/9
```

---

## Task 1: Port `PiecewiseLinear2D::Sample` (BSDF math — strong model)

**Files:** modify `include/yaoray/render/piecewise_linear_2d.hpp`, `tests/piecewise_linear_2d_tests.cpp`.

- [ ] **Step 1: Failing tests** (add to `piecewise_linear_2d_tests.cpp`):
```cpp
YR_TEST(plinear2d_sample_uniform_is_identity) {
    // Uniform 2x2, normalized+cdf -> Sample(u) ~ u, pdf ~ 1.
    const float data[4] = {1,1,1,1};
    yr::PiecewiseLinear2D<0> d(data, 2, 2, {}, {}, true, true);
    auto s = d.Sample(yr::Vec2f{0.3f, 0.7f});
    YR_EXPECT_NEAR(s.p.x, 0.3f, 1e-3f);
    YR_EXPECT_NEAR(s.p.y, 0.7f, 1e-3f);
    YR_EXPECT_NEAR(s.pdf, 1.0f, 1e-3f);
}
YR_TEST(plinear2d_sample_invert_roundtrip) {
    // Non-uniform 3x3 -> Sample then Invert returns the original sample + matching pdf.
    const float data[9] = {0.2f,0.5f,0.9f, 0.3f,0.7f,1.0f, 0.1f,0.4f,0.8f};
    yr::PiecewiseLinear2D<0> d(data, 3, 3, {}, {}, true, true);
    const yr::Vec2f u{0.42f, 0.63f};
    auto s = d.Sample(u);
    auto inv = d.Invert(s.p);
    YR_EXPECT_NEAR(inv.p.x, u.x, 2e-3f);
    YR_EXPECT_NEAR(inv.p.y, u.y, 2e-3f);
    YR_EXPECT_NEAR(inv.pdf, s.pdf, 1e-3f * s.pdf + 1e-4f);   // same density
    YR_EXPECT_TRUE(s.pdf > 0.0f && std::isfinite(s.pdf));
}
```
(`Invert(Sample(u).p)` returns the original `u` because they are exact inverses; the pdf at that point is the same regardless of direction. Adapt to the ported `Sample` return type `{Vec2f p; float pdf;}`.)

- [ ] **Step 2: Verify FAIL → Step 3: port `Sample` → verify PASS.** Full suite (266 + 2) + CTest 9/9.

- [ ] **Step 4: Commit** — `feat(render): PiecewiseLinear2D::Sample (CDF inversion) for measured sampling`.

---

## Task 2: `SampleMeasured` / `PdfMeasured` + wire `SampleBsdf` / `PdfBsdf` (BSDF math — strong model)

**Files:** modify `src/render/bsdf.cpp`; create `tests/measured_sample_tests.cpp`; modify `CMakeLists.txt`.

- [ ] **Step 1: Failing tests** `tests/measured_sample_tests.cpp`:
```cpp
#include "yr_test.hpp"
#include "tensor_test_util.hpp"
#include <yaoray/render/measured_brdf.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>
#include <cmath>
#include <cstdio>

namespace { yr::Vec3f Up(){ return yr::Vec3f{0,0,1}; } }

YR_TEST(measured_sample_valid_directions) {
    // Constant non-zero tables so the warps sample.
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    YR_EXPECT_TRUE(m.has_value());
    yr::RenderMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{4u};
    int valid = 0;
    for (int i = 0; i < 4000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(mat, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        ++valid;
        YR_EXPECT_TRUE(std::isfinite(s.wi.x) && std::isfinite(s.wi.y) && std::isfinite(s.wi.z));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);          // reflection (upper hemisphere)
        YR_EXPECT_TRUE(s.pdf > 0.0f && std::isfinite(s.pdf));
        YR_EXPECT_TRUE(std::isfinite(s.weight.x) && s.weight.x >= 0.0f);
    }
    YR_EXPECT_TRUE(valid > 0);
    std::remove(p.c_str());
}
YR_TEST(measured_sample_pdf_matches_pdfbsdf) {
    // The headline consistency check: PdfBsdf(wo, sampled_wi) == sample.pdf (deterministic).
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp2.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::RenderMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    yr::Rng rng{9u};
    int checked = 0;
    for (int i = 0; i < 2000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(mat, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        const float p_eval = yr::PdfBsdf(mat, wo, s.wi, n, rng);
        YR_EXPECT_TRUE(std::fabs(p_eval - s.pdf) <= 1e-2f * s.pdf + 1e-4f);
        ++checked;
    }
    YR_EXPECT_TRUE(checked > 0);
    std::remove(p.c_str());
}
YR_TEST(measured_sample_deterministic) {
    const std::string p = yrtest::WriteSyntheticBsdf("measured_smp3.bsdf", 2, 1.0f, 0.5f, 1.0f, 1.0f, 0.4f);
    std::string err; auto m = yr::LoadMeasuredBrdf(p, err);
    yr::RenderMaterial mat; mat.kind = yr::RenderMaterialKind::Measured; mat.measured_brdf = &*m;
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec2f u{0.37f, 0.62f};
    yr::Rng ra{1u}, rb{1u};
    const yr::BsdfSample a = yr::SampleBsdf(mat, wo, n, u, ra);
    const yr::BsdfSample b = yr::SampleBsdf(mat, wo, n, u, rb);
    YR_EXPECT_EQ(a.valid, b.valid);
    if (a.valid) { YR_EXPECT_EQ(a.wi.x, b.wi.x); YR_EXPECT_EQ(a.pdf, b.pdf); }
    std::remove(p.c_str());
}
```
Register `tests/measured_sample_tests.cpp`. (The `pdf_matches_pdfbsdf` tolerance is small because both are deterministic warp evaluations — if it needs >1e-2 relative, the Jacobian or the Sample/Invert pdf is inconsistent; investigate rather than loosening.)

- [ ] **Step 2: Verify FAIL** (Measured still conductor-aliased in `SampleBsdf`/`PdfBsdf` → pdf won't match the measured pdf).

- [ ] **Step 3: Implement `SampleMeasured` + `PdfMeasured`** in `bsdf.cpp` (anon ns) per the algorithm reference. Build the local frame with the SAME deterministic tangent construction `EvaluateMeasured` uses (so the frames agree). Reuse `EvaluateMeasured` for `f` inside `SampleMeasured`. Guard: `pdf>0` and finite, `wi.z>0`, `Dot(lwo,wm)>0`; invalid sample → `BsdfSample{}`. `u2theta(u)=u*u*(Pi/2)`, `u2phi(u)=(2u-1)*Pi`.

- [ ] **Step 4: Wire** `SampleBsdf(Measured)` → `material.measured_brdf ? SampleMeasured(*material.measured_brdf, wo, normal, sample) : <conductor alias>` and `PdfBsdf(Measured)` → `material.measured_brdf ? PdfMeasured(*material.measured_brdf, wo, wi, normal) : <conductor alias>`. (Pull `Measured` out of the conductor fall-through group in those two functions, as Slice 2 did for `EvaluateBsdf`.) `EvaluateBsdf` + `IsDeltaBsdf` unchanged.

- [ ] **Step 5: Run** — the 3 new tests PASS (esp. `pdf_matches_pdfbsdf`); full suite (266 + 2 + 3 = 271) ; CTest 9/9. Slice-1/2 measured tests + all prior stay green.

- [ ] **Step 6: Commit**
```
feat(bsdf): data-driven measured BRDF Sample/Pdf (Dupuy-Jakob)

Implements MeasuredBxDF Sample/Pdf: two-stage luminance->vndf warp
sampling of the microfacet direction -> wi = reflect(wo, wm), with the
reflection + domain Jacobian (1 / (4*dot(wo,wm) * 2pi^2*u_x*sin_m)) for
the solid-angle pdf; Pdf mirrors it via vndf.Invert + luminance.Evaluate.
SampleBsdf/PdfBsdf now use the real measured importance sampling instead
of the conductor alias. f/sample/pdf are mutually consistent
(PdfBsdf(wo, sampled_wi) == sample.pdf) -> clean MIS. Real-material
validation lands at the Slice 4 sportscar render.

All 271 unit tests + 9 CTest pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 3: PR + merge

- [ ] **Step 1:** `git log --oneline 0ea6915..HEAD` — expect 2 commits (warp Sample, Sample/Pdf+wire).
- [ ] **Step 2:** push `worktree-m3-measured-slice3-sampling`; `gh pr create --base main` — body: PiecewiseLinear2D::Sample; MeasuredBxDF Sample/Pdf (two-stage warp + Jacobian); SampleBsdf/PdfBsdf wired; the **sample/pdf-consistency** validation; scope (sportscar Slice 4); real-data deferred.
- [ ] **Step 3:** address review; re-run tests.
- [ ] **Step 4:** merge (operator-gated); finish per `superpowers:finishing-a-development-branch` (ff main; remove worktree — manual fallback if `ExitWorktree` no-ops; delete remote branch).

---

## Self-Review Notes

- **Spec coverage:** Slice 3 of the measured spec — `PiecewiseLinear2D::Sample` (Task 1), `MeasuredBxDF` Sample/Pdf + wiring (Task 2), PR (Task 3). sportscar is Slice 4.
- **Consistency is the contract:** `SampleMeasured`'s returned pdf and `PdfMeasured(wo, wi)` are the same quantity (`vndf_pdf · lum / jacobian`) computed via Sample vs Invert — exact inverses by construction. The `pdf_matches_pdfbsdf` test pins it tightly; a loose match signals a Jacobian/inverse bug, not Monte-Carlo noise (this is deterministic).
- **Single 2D sample:** the two-stage `luminance.Sample(u)` → `vndf.Sample(luminance.p)` consumes only the one `Vec2f` the sampler supplies (pbrt's `uc` is unused for measured). `SampleBsdf`'s existing `Vec2f sample` suffices; no extra RNG draw.
- **Frame agreement:** `SampleMeasured` and `EvaluateMeasured`/`PdfMeasured` build the same deterministic tangent frame from `normal`; isotropic evaluation is rotation-invariant anyway, so `f`/pdf are frame-robust.
- **Jacobian:** the reflection (`4·Dot(wo,wm)`) + domain (`2π²·u_x·sinθ_m`) Jacobian is applied explicitly in both Sample and Pdf (not folded into the warp pdf, which only carries `HProd(inv_patch_size)`). Identical expression in both → consistency.
- **Test-data honesty:** constant-table synthetic validates code correctness (consistency + validity + determinism); real-material + energy validation is the Slice 4 sportscar render. Flagged in the PR.
- **Type consistency:** `PiecewiseLinear2D::Sample → {p,pdf}`; `SampleMeasured(brdf,wo,normal,u)→BsdfSample`; `PdfMeasured(brdf,wo,wi,normal)→float`; reuses `EvaluateMeasured`, `*_warp`, `u2theta/u2phi/theta2u/phi2u`. Consistent.
- **Worktree branch** `m3-measured-slice3-sampling` consistent across Setup and Task 3.
