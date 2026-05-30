# YaoRay M3 Slice 2b — Layered f / pdf estimators (MIS consistency) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Tasks 1 and 2 are BSDF math — dispatch them to the most capable available model.**

**Goal:** Replace the base-aliased `EvaluateBsdf` / `PdfBsdf` for `coateddiffuse` / `coatedconductor` with **stochastic layered estimators** (a position-free Monte Carlo `f` and a mutually-consistent `pdf`), and make `SampleLayered` return that real `pdf` instead of the `1.0` proxy. This closes the documented Slice 2a light-sampling MIS gap: the path tracer's light-sampling branch will light coated surfaces with the coat's real response, and the BSDF/light-sampling MIS weights become consistent.

**Architecture:** Two new estimator functions in `src/render/bsdf.cpp`'s anonymous namespace — `EvaluateLayered(material, wo, wi, normal, rng, conductor_base)` and `PdfLayered(material, wo, wi, normal, rng, conductor_base)` — reuse the same smooth-coat + Beer-Lambert + base-BSDF building blocks as `SampleLayered`. `EvaluateBsdf` / `PdfBsdf`'s coated branches delegate to them; `SampleLayered`'s exit return sets `pdf = PdfLayered(...)`. Smooth clearcoat (perfect Fresnel), Beer-Lambert absorbing medium (no scattering), diffuse/conductor base — identical scope to 2a.

**Tech Stack:** C++20, CMake 3.24, `yr_test.hpp`, CTest. Uses `yr::Rng` (Slice 1) — already threaded into all three entry points.

---

## Why this is the riskiest slice — and how the plan de-risks it

The stochastic `f` estimator (connect-to-`wi` at each interface) and the consistent `pdf` estimator are the densest BSDF math in M3. The plan does NOT rely on getting hand-written reference code perfectly right. Instead:

- The reference code below is a **scaffold** built to PBRT v4's `LayeredBxDF::f` / `::PDF` design (the method the spec mandates), specialized to our smooth coat + absorption-only medium + opaque base. **The unit tests are the contract.** If a sign / Jacobian / direction is wrong, the tests below catch it — exactly as the 2a exit-refraction sign bug was caught.
- The decisive correctness pin is **furnace-via-eval**: for a white diffuse base with zero absorption and a smooth coat, the directional albedo computed by integrating `EvaluateBsdf` (ρ = ∫ f·cosθ dω, Monte-Carlo) must match the `SampleBsdf` white-furnace energy from 2a (~0.995). This ties `f` to `Sample_f` — the entire point of 2b.
- **Reciprocity** (`f(wo,wi) ≈ f(wi,wo)`) is a second independent pin on the `f` estimator.
- **pdf consistency**: the mean of `PdfLayered(wo,wi)` over many rng draws must match the mean of the `pdf` that `SampleLayered` attaches when it happens to sample that same `wi` — both estimate the same density.

**Implementers on Tasks 1 & 2: treat PBRT v4 `LayeredBxDF::f` and `LayeredBxDF::PDF` (file `src/pbrt/bxdfs.h` in pbrt-v4) as the authoritative reference. Reconstruct them for our specialization. The scaffold is a starting point, the tests are law. Do not weaken a test to make the scaffold pass.**

---

## Scope (what 2b is and is NOT)

**2b delivers:**
- `EvaluateLayered` — a stochastic, unbiased estimate of the layered BSDF `f(wo, wi)` for the non-delta (scattering-base) case. Reflection only (opaque base, both `wo`/`wi` above).
- `PdfLayered` — a stochastic estimate of the solid-angle pdf of `SampleLayered` producing `wi` from `wo`, mutually consistent with the sampling routine.
- `EvaluateBsdf` / `PdfBsdf` coated branches call these (no longer alias to base). The `TODO(2b)` comments are removed.
- `SampleLayered`'s non-specular exit returns `pdf = PdfLayered(...)` (replacing the `1.0` proxy). The specular-reflect early-return stays `pdf = 1.0, specular = true` (delta — MIS handles it).
- New `coat_nsamples` field on `RenderMaterial` (PBRT `nsamples`, default 1) controlling estimator walks averaged per call.
- Validation: reciprocity, furnace-via-eval (matches Sample energy), coat-aware (eval ≠ bare base), pdf-consistency, determinism, render-level MIS-consistency (BSDF-only vs MIS converge on `coated_showcase`).

**2b explicitly does NOT do (deferred):**
- Rough (microfacet GGX) coat interfaces — coat stays SMOOTH (perfect Fresnel), matching 2a. `coating_roughness` remains compiled-but-unused.
- In-medium phase-function scattering (`g`); N-layer nesting; transmissive (two-sided) layered stacks (our base is opaque → reflection only).
- killeroo-coated integration (Slice 3); measured BRDF (later M3).

---

## Geometry / semantics contract (must obey; the tests pin these)

Reuse 2a's verified conventions exactly (they already live in `SampleLayered`):

- `normal` `n` points to exterior; a direction `d` is "above" iff `Dot(d, n) > 0`. Both `wo` and `wi` are above for the reflection case 2b handles.
- Coat IOR `ce = max(1, coating_ior)`. `FresnelDielectric(cos, 1, ce)` entering; `FresnelDielectric(cos, ce, 1)` from below.
- **Enter (air→medium)** of an above direction `d`: `Refract(d, n, 1/ce, d_t)` → `d_t` is DOWN-going.
- **Exit (medium→air)** of an up-going internal `w`: `Refract(w, n, ce, e); e = -e` → up-going air direction (the 2a negation).
- **The internal up-going direction that exits to a given above `wi`** is `wi_internal = -Refract(wi, n, 1/ce)` (reversibility of refraction; `Refract(wi, n, 1/ce)` is down-going, negate to get the up-going partner). The implementer MUST verify this against the `directions_are_consistent` unit test (Task 1 Step 1c).
- Base BSDF sees the bottom interface with the SAME `n` (upward). When the walk arrives going down with direction `w` (down, `Dot(w,n)<0`), the base's "wo" is `-w` (up). Connecting to the exit direction uses `wi_internal` (up).

**MIS semantics (from `cpu_path_tracer.cpp`, do not break):**
- `EvaluateBsdf` returns `f(wo, wi)` WITHOUT the `cos(wi)` factor (the tracer multiplies `cos_surface` and divides by `pdf_light`).
- `PdfBsdf` returns the solid-angle pdf of sampling `wi` (for `PowerHeuristic`).
- `SampleLayered`'s returned `.pdf` is consumed as `previous_bounce.bsdf_pdf` for emissive/env-hit MIS — it must be the SAME quantity `PdfBsdf` estimates.
- `IsDeltaBsdf` already returns `true` for CoatedConductor with a smooth base (both interfaces specular → all-delta) and `false` for CoatedDiffuse / rough-base CoatedConductor. 2b does NOT change `IsDeltaBsdf`. For the delta case the tracer skips light sampling, so `f`/`pdf` are only exercised on scattering bases — but they must still return `0` / `0` safely if ever called with a delta base.

---

## File Structure

| Path | Change |
|------|--------|
| `include/yaoray/render/render_scene.hpp` | Add `int coat_nsamples = 1;` to `RenderMaterial` (PBRT `nsamples`). |
| `src/render/scene_compiler.cpp` | `coateddiffuse`/`coatedconductor` branches read `nsamples` → `coat_nsamples` (`IntParam`, default 1). |
| `src/render/bsdf.cpp` | Add `EvaluateLayered` + `PdfLayered` in the anonymous namespace; wire `EvaluateBsdf`/`PdfBsdf` coated branches to them (remove the `TODO(2b)` alias comments); set `SampleLayered`'s non-specular exit `pdf = PdfLayered(...)`. |
| `tests/layered_bsdf_tests.cpp` | Extend: reciprocity, furnace-via-eval, coat-aware-eval, pdf-consistency, eval/pdf determinism, eval-energy. |
| `tests/scene_compiler_coated_tests.cpp` | Extend: `coat_nsamples` read. |
| `CMakeLists.txt` | (No new files; existing test targets already registered.) Add a render-level MIS-consistency note if a new CTest is added in Task 3 (optional). |

**No new .cpp/.hpp** — the estimators live in `bsdf.cpp` alongside `SampleLayered` and the shared helpers, same rationale as 2a.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `cbfb95e`, post-PR-#11 + PR-#12). Use the harness-native `EnterWorktree` tool with name `m3-slice2b-layered-eval`.

Baseline check (MSVC multi-config → `build/Release/`):

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -aoE "\[PASS\]|\[FAIL\]" | sort | uniq -c   # expect 213 PASS / 0 FAIL
cd build && ctest --output-on-failure -C Release 2>&1 | tail -5   # expect 9/9 PASS
cd ..
```

Note: `yaoray_tests.exe` ignores `--filter`; run the full binary from the worktree ROOT and grep. clangd `coat_*` "no member" diagnostics are stale-index false positives — trust the MSVC build.

---

## Task 0: `coat_nsamples` param (small, do first)

**Files:** `include/yaoray/render/render_scene.hpp`, `src/render/scene_compiler.cpp`, `tests/scene_compiler_coated_tests.cpp`.

- [ ] **Step 1: Add the field.** In `struct RenderMaterial`, after `coat_maxdepth`, add:

```cpp
    int coat_nsamples = 1;   // PBRT nsamples: stochastic f/pdf walks averaged per call
```

- [ ] **Step 2: Failing test.** In `tests/scene_compiler_coated_tests.cpp`, add a test that a `coatedconductor` scene with `"integer nsamples" [4]` compiles to `coat_nsamples == 4`. Mirror the existing `..._reads_thickness` test's fixture (named-material pattern). Build, confirm it FAILS (default 1 ≠ 4).

- [ ] **Step 3: Read the param.** In both the `coateddiffuse` and `coatedconductor` branches of `CompileMaterial`, add:

```cpp
        material.coat_nsamples = std::max(1, IntParam(FindParam(params, "nsamples"), 1));
```

- [ ] **Step 4: Verify + commit.** 214 PASS / 0 FAIL, CTest 9/9. Commit:

```
feat(material): coat_nsamples param for layered f/pdf estimators (M3 2b)
```

---

## Task 1: `EvaluateLayered` — the stochastic f estimator (BSDF MATH — strongest model)

**Files:** Modify `src/render/bsdf.cpp` (add `EvaluateLayered` in the anon namespace; wire `EvaluateBsdf` coated branches). Modify `tests/layered_bsdf_tests.cpp`.

**What `f` must be:** the BSDF value `f(wo, wi)` of the layered material for the **non-delta transmitted lobe** (reflection; opaque base; both `wo`/`wi` above). The coat's specular reflection is a delta — it contributes **0** to `f` at a generic `wi` (it lives only in `SampleLayered`). So `EvaluateLayered` estimates only the light that transmits into the coat, scatters off the base (one or more times, with TIR between bounces), and transmits back out toward `wi`.

**The hard part — and how the tests pin it.** The exit-coupling factor (the cosine on the connection direction, the exit Fresnel transmittance, and the refraction solid-angle Jacobian) is the pitfall. The plan does NOT ask you to derive the Jacobian analytically. Instead: implement the structure below, then **calibrate the single marked factor against two simultaneous constraints** that uniquely determine it:
1. **Furnace-via-eval** (absolute scale): white diffuse base + zero absorption ⇒ directional albedo `ρ(wo) = ∫ f·cosθ dω ≈ 1 − F(wo)` (≈0.955 at the test angle). This forces the constant AND multiple-scattering energy recovery.
2. **Reciprocity** (symmetry): `f(wo,wi) ≈ f(wi,wo)`. This forces the cosine/Jacobian form.

A factor that satisfies BOTH is correct. **The authoritative reference is PBRT v4 `LayeredBxDF::f` (`src/pbrt/bxdfs.h`), specialized to: smooth dielectric top (perfect Fresnel), absorption-only medium (no HG phase sampling — just Beer-Lambert `Tr`), opaque base. Reconstruct it; the scaffold is a starting structure.**

- [ ] **Step 1a: Convention sanity test.** Add to `tests/layered_bsdf_tests.cpp` a test pinning the internal-direction convention (helper is `static` or duplicated locally for the test as needed, OR assert via a public path). Minimal version — assert the round-trip that the up-going internal direction `wi_internal` exits to `wi`:

```cpp
YR_TEST(layered_eval_internal_direction_roundtrips) {
    // For a smooth coat IOR 1.5, the internal up-going direction that exits to
    // an above wi is -Refract(wi, n, 1/ce); refracting it back out (ce, then
    // negate per the 2a exit convention) must recover wi.
    // This mirrors the math EvaluateLayered relies on; if it fails, the
    // estimator's connection direction is wrong.
    // (Implement using the same Refract used by the walk; if Refract is in an
    // anonymous namespace, expose a thin test-only helper or replicate the 3
    // lines here with the documented convention and assert against a hand
    // direction. Keep it a real assertion, not a tautology.)
}
```

(If `Refract` is not reachable from the test TU, replicate the documented formula in the test and assert it produces an up-going vector whose re-exit ≈ `wi`. The point is to lock the convention before the estimator depends on it.)

- [ ] **Step 1b: Write the failing estimator tests** (add to `tests/layered_bsdf_tests.cpp`):

```cpp
// Helper: Monte-Carlo directional albedo of EvaluateBsdf, cosine-sampling wi.
// rho(wo) = ∫ f(wo,wi) cosθ_wi dω  ≈  mean over cosine-sampled wi of f * π.
static float DirectionalAlbedoViaEval(const yr::RenderMaterial& m, yr::Vec3f wo,
                                      yr::Vec3f n, unsigned seed, int N) {
    yr::Rng rng{seed};
    double acc = 0.0;
    for (int i = 0; i < N; ++i) {
        // cosine-sample wi in the upper hemisphere about n
        const yr::Vec2f u = rng.NextFloat2();
        const float r = std::sqrt(std::clamp(u.x, 0.0f, 1.0f));
        const float phi = 2.0f * 3.14159265358979f * u.y;
        const float z = std::sqrt(std::max(0.0f, 1.0f - u.x));
        // build a frame around n
        const yr::Vec3f helper = std::fabs(n.z) < 0.999f ? yr::Vec3f{0,0,1} : yr::Vec3f{1,0,0};
        const yr::Vec3f t = yr::Normalize(yr::Cross(helper, n));
        const yr::Vec3f b = yr::Cross(n, t);
        const yr::Vec3f wi = yr::Normalize(t * (r*std::cos(phi)) + b * (r*std::sin(phi)) + n * z);
        const yr::Color3f f = yr::EvaluateBsdf(m, wo, wi, n, rng);
        // pdf_cos = cosθ/π ; estimator of ∫ f cosθ dω is mean of f*cosθ/pdf_cos = mean of f*π
        acc += MaxComp(f) * 3.14159265358979f;
    }
    return static_cast<float>(acc / N);
}

YR_TEST(layered_eval_furnace_matches_sample_energy) {
    // White diffuse base, zero absorption, smooth coat IOR 1.5. The diffuse
    // (non-specular) lobe's directional albedo must recover almost all of the
    // (1 - F(wo)) energy that enters the coat (multiple internal scattering
    // returns the TIR'd light). A single-scatter-only estimator collapses to
    // ~0.8 and fails the lower bound.
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    const float rho = DirectionalAlbedoViaEval(m, wo, n, 13u, 40000);
    YR_EXPECT_TRUE(rho >= 0.90f);   // multiple-scattering energy recovered
    YR_EXPECT_TRUE(rho <= 1.02f);   // no energy gain
}

YR_TEST(layered_eval_is_reciprocal) {
    // f(wo,wi) ~= f(wi,wo) within Monte-Carlo tolerance (averaged).
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.6f, 0.5f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.2f, 0.5f, 0.8f});
    auto meanF = [&](yr::Vec3f a, yr::Vec3f b) {
        yr::Rng rng{77u};
        double s = 0.0; const int N = 60000;
        for (int i = 0; i < N; ++i) s += MaxComp(yr::EvaluateBsdf(m, a, b, n, rng));
        return static_cast<float>(s / N);
    };
    const float f_ab = meanF(wo, wi);
    const float f_ba = meanF(wi, wo);
    YR_EXPECT_TRUE(std::fabs(f_ab - f_ba) <= 0.05f * std::max(f_ab, f_ba) + 1e-4f);
}

YR_TEST(layered_eval_differs_from_bare_base) {
    // The coat shifts the grazing-angle response vs a bare Lambertian.
    yr::RenderMaterial coated = MakeCoatedDiffuse(yr::Color3f{0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare; bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = yr::Color3f{0.5f, 0.5f, 0.5f};
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.9f, 0.0f, 0.2f});   // grazing
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{-0.9f, 0.0f, 0.2f});  // grazing
    yr::Rng ra{5u}, rb{5u};
    double cs = 0, bs = 0; const int N = 40000;
    for (int i = 0; i < N; ++i) { cs += MaxComp(yr::EvaluateBsdf(coated, wo, wi, n, ra)); }
    const float bare_f = MaxComp(yr::EvaluateBsdf(bare, wo, wi, n, rb));
    const float coated_mean = static_cast<float>(cs / N);
    YR_EXPECT_TRUE(std::fabs(coated_mean - bare_f) > 0.005f);
}

YR_TEST(layered_eval_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng ra{321u}, rb{321u};
    const yr::Color3f a = yr::EvaluateBsdf(m, wo, wi, n, ra);
    const yr::Color3f b = yr::EvaluateBsdf(m, wo, wi, n, rb);
    YR_EXPECT_EQ(a.x, b.x); YR_EXPECT_EQ(a.y, b.y); YR_EXPECT_EQ(a.z, b.z);
}
```

Run; expected: `furnace_matches_sample_energy` and `differs_from_bare_base` FAIL today (coated `EvaluateBsdf` aliases bare Lambertian: ρ ≈ albedo·? and coated==bare). `reciprocal` may pass trivially (Lambertian is reciprocal) — that's fine, it becomes a real constraint once the estimator lands.

- [ ] **Step 2: Implement `EvaluateLayered`** in the anonymous namespace (near `SampleLayered`). Reference scaffold — **the `EXIT_COUPLING` factor is the calibration target** (see the two-constraint note above):

```cpp
// Stochastic estimate of the layered BSDF f(wo, wi) (reflection, opaque base,
// smooth coat). The coat's specular reflection is a delta and is NOT included
// here. Reconstructed from PBRT v4 LayeredBxDF::f specialized to a smooth top
// + absorption-only medium. CALIBRATE the EXIT_COUPLING factor against the
// furnace-via-eval + reciprocity tests; do not weaken the tests.
Color3f EvaluateLayered(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
                        Rng& rng, bool conductor_base) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
        return Color3f{};   // opaque base ⇒ reflection only
    }
    const float ce = std::max(1.0f, material.coating_ior);

    RenderMaterial base;
    if (conductor_base) {
        base.kind = RenderMaterialKind::Conductor;
        base.reflectance = material.reflectance;
        base.uroughness = material.uroughness;
        base.vroughness = material.vroughness;
    } else {
        base.kind = RenderMaterialKind::Diffuse;
        base.reflectance = material.reflectance;
    }

    // Internal directions (see Geometry contract).
    Vec3f wo_t;                       // wo refracted in (down-going)
    if (!Refract(wo, normal, 1.0f / ce, wo_t)) return Color3f{};
    Vec3f wi_down;                    // wi refracted in (down-going)
    if (!Refract(wi, normal, 1.0f / ce, wi_down)) return Color3f{};
    const Vec3f wi_internal = -wi_down;   // up-going internal dir that exits to wi
    if (!IsAboveSurface(wi_internal, normal)) return Color3f{};

    const float T_enter = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wo, normal)), 1.0f, ce);
    const float T_exit  = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wi, normal)), 1.0f, ce);
    if (T_enter <= 0.0f || T_exit <= 0.0f) return Color3f{};

    const int ns = std::max(1, material.coat_nsamples);
    Color3f f_sum{0.0f, 0.0f, 0.0f};

    for (int s = 0; s < ns; ++s) {
        // Enter: throughput carries the entry transmittance.
        Color3f beta{T_enter, T_enter, T_enter};
        Vec3f w = wo_t;   // down-going, heading to the base
        for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
            // Descend through medium to the base.
            beta = ApplyBeerLambert(beta, material.coat_absorption, material.coat_thickness, w, normal);

            // CONNECT to wi at the base: base BSDF from -w (incoming, up) toward wi_internal (up).
            const Color3f base_f = EvaluateBsdf(base, -w, wi_internal, normal, rng);
            if (!IsBlack(base_f)) {
                // Beer-Lambert for the upward exit traversal along wi_internal.
                const Color3f tr_exit = ApplyBeerLambert(Color3f{1.0f,1.0f,1.0f},
                    material.coat_absorption, material.coat_thickness, wi_internal, normal);
                // ---- EXIT_COUPLING (CALIBRATE THIS) ----
                // Start point: T_exit * |cos(wi_internal·n)| . The correct factor
                // (incl. any refraction Jacobian) is the one that makes
                // furnace-via-eval (~0.955) AND reciprocity both pass.
                const float exit_coupling = T_exit * AbsDot(wi_internal, normal);
                // ----------------------------------------
                f_sum = f_sum + Color3f{
                    beta.x * base_f.x * tr_exit.x * exit_coupling,
                    beta.y * base_f.y * tr_exit.y * exit_coupling,
                    beta.z * base_f.z * tr_exit.z * exit_coupling,
                };
            }

            // Continue the walk: sample the base to get the next internal up dir.
            const BsdfSample bs = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
            if (!bs.valid || IsBlack(bs.weight) || !IsAboveSurface(bs.wi, normal)) break;
            beta = Color3f{beta.x*bs.weight.x, beta.y*bs.weight.y, beta.z*bs.weight.z};

            // Ascend through medium to the coat-from-below.
            beta = ApplyBeerLambert(beta, material.coat_absorption, material.coat_thickness, bs.wi, normal);

            // At the coat from below: reflect back down (continue) with prob F,
            // else the energy exits in some non-wi direction (no more connections).
            const float f_back = FresnelDielectric(std::max(0.0f, Dot(bs.wi, normal)), ce, 1.0f);
            if (rng.NextFloat() < f_back) {
                w = Reflect(bs.wi, normal);   // down-going again
                if (IsAboveSurface(w, normal)) break;   // must go down
                continue;
            }
            break;   // exits away from wi; this walk contributes no further
        }
    }
    return f_sum / static_cast<float>(ns);
}
```

**IMPLEMENTER NOTE (read PBRT first):** The `exit_coupling` start point (`T_exit · |cosθ_internal|`) is almost certainly missing a refraction Jacobian and/or a `1/π`-class normalization. Do NOT ship the scaffold blind. Reconstruct PBRT v4 `LayeredBxDF::f`'s connection term for a smooth exit interface (where `wis = top.Sample_f(wi, …, Transmission)` is deterministic and its `f/pdf` already carries the transmittance and the radiance Jacobian), and let the furnace-via-eval (`ρ ∈ [0.90, 1.02]`) and reciprocity tests force the correct factor. If you cannot make BOTH pass simultaneously, the bug is structural (likely the connection direction `wi_internal`, the `-w` base argument, or the multiple-scattering loop), not just the constant — re-derive against PBRT.

- [ ] **Step 3: Wire `EvaluateBsdf`.** Split the coated kinds out of the base fall-through groups:

```cpp
        case RenderMaterialKind::CoatedDiffuse:
            return EvaluateLayered(material, wo, wi, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return EvaluateLayered(material, wo, wi, normal, rng, /*conductor_base=*/true);
```

Remove `CoatedDiffuse` from the `Diffuse`/`DiffuseTransmission`/`Mix` group and `CoatedConductor` from the `Conductor` group **in `EvaluateBsdf` only**, and delete the `TODO(2b)` comment there. Leave `PdfBsdf` for Task 2.

- [ ] **Step 4: Run + calibrate.** Build; run the 5 new tests. Calibrate `exit_coupling` until `furnace_matches_sample_energy` and `is_reciprocal` both pass; confirm `differs_from_bare_base` and determinism pass; full suite green. CTest 9/9.

- [ ] **Step 5: Commit.**

```
feat(bsdf): stochastic layered f estimator for coated* EvaluateBsdf

Replaces the base-aliased EvaluateBsdf for CoatedDiffuse/CoatedConductor
with a position-free Monte Carlo f(wo,wi) estimator (smooth coat +
Beer-Lambert medium + diffuse/conductor base, multiple internal
scattering). Reconstructed from PBRT v4 LayeredBxDF::f. Validated by
furnace-via-eval (directional albedo recovers ~1-F(wo)), reciprocity,
coat-aware grazing response, and determinism. PdfBsdf still aliases base
(Task 2). All <N> unit tests + 9 CTest pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 2: `PdfLayered` — consistent stochastic pdf + `SampleLayered.pdf` (BSDF MATH — strongest model)

**Files:** Modify `src/render/bsdf.cpp` (add `PdfLayered`; wire `PdfBsdf` coated branches; set `SampleLayered`'s non-specular exit `pdf`). Modify `tests/layered_bsdf_tests.cpp`.

**What `pdf` must be:** the solid-angle density of `SampleLayered` producing the non-specular exit direction `wi` from `wo`. It must be mutually consistent with the sampling routine and with `EvaluateLayered` so MIS is unbiased. The coat's specular reflection is a delta (excluded — handled by the `specular` flag).

**Calibration pin (analogous to f):** for a white diffuse base with zero absorption, `∫ pdf(wo,·) dω` over the upper hemisphere ≈ the probability of a non-specular exit ≈ `1 − F(wo)` (≈0.955 at the test angle): all entering energy exits non-specularly. Estimate the integral by uniform-hemisphere sampling: `mean(pdf)·2π`. This forces the pdf's normalization/Jacobian. **Authoritative reference: PBRT v4 `LayeredBxDF::PDF` (`src/pbrt/bxdfs.h`), smooth-coat specialization.**

- [ ] **Step 1: Failing tests** (add to `tests/layered_bsdf_tests.cpp`):

```cpp
// Estimate ∫ pdf(wo, wi) dω over the upper hemisphere by uniform sampling:
// mean(pdf) * 2π.
static float PdfHemisphereIntegral(const yr::RenderMaterial& m, yr::Vec3f wo,
                                   yr::Vec3f n, unsigned seed, int N) {
    yr::Rng rng{seed};
    double acc = 0.0;
    const float twoPi = 2.0f * 3.14159265358979f;
    for (int i = 0; i < N; ++i) {
        const yr::Vec2f u = rng.NextFloat2();          // uniform hemisphere about n
        const float z = std::clamp(u.x, 0.0f, 1.0f);   // cosθ
        const float r = std::sqrt(std::max(0.0f, 1.0f - z*z));
        const float phi = twoPi * u.y;
        const yr::Vec3f helper = std::fabs(n.z) < 0.999f ? yr::Vec3f{0,0,1} : yr::Vec3f{1,0,0};
        const yr::Vec3f t = yr::Normalize(yr::Cross(helper, n));
        const yr::Vec3f b = yr::Cross(n, t);
        const yr::Vec3f wi = yr::Normalize(t*(r*std::cos(phi)) + b*(r*std::sin(phi)) + n*z);
        acc += yr::PdfBsdf(m, wo, wi, n, rng);          // uniform pdf = 1/2π ⇒ ∫ ≈ mean*2π
    }
    return static_cast<float>(acc / N) * twoPi;
}

YR_TEST(layered_pdf_integrates_to_exit_probability) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    const float integral = PdfHemisphereIntegral(m, wo, n, 91u, 40000);
    YR_EXPECT_TRUE(integral >= 0.85f);   // ≈ 1 - F(wo) ≈ 0.955
    YR_EXPECT_TRUE(integral <= 1.05f);
}

YR_TEST(layered_pdf_is_nonnegative_and_finite) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.6f, 0.6f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    yr::Rng rng{17u};
    for (int i = 0; i < 3000; ++i) {
        const yr::Vec3f wi = yr::Normalize(yr::Vec3f{rng.NextFloat()*2-1, rng.NextFloat()*2-1, rng.NextFloat()*0.5f+0.5f});
        const float p = yr::PdfBsdf(m, wo, wi, n, rng);
        YR_EXPECT_TRUE(std::isfinite(p) && p >= 0.0f);
    }
}

YR_TEST(layered_pdf_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.1f, 0.3f, 0.9f});
    yr::Rng ra{43u}, rb{43u};
    YR_EXPECT_EQ(yr::PdfBsdf(m, wo, wi, n, ra), yr::PdfBsdf(m, wo, wi, n, rb));
}

YR_TEST(layered_sample_sets_real_pdf_not_proxy) {
    // After Task 2, a non-specular layered sample carries pdf>0 (a real
    // estimate). Many samples: those flagged specular keep pdf=1.0; the
    // non-specular exits must report a finite positive pdf that is not the
    // constant 1.0 proxy on average.
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.7f, 0.7f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});
    yr::Rng rng{61u};
    int nonspec = 0; double pdf_acc = 0.0;
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (s.valid && !s.specular) { ++nonspec; pdf_acc += s.pdf; YR_EXPECT_TRUE(s.pdf > 0.0f); }
    }
    YR_EXPECT_TRUE(nonspec > 0);
    const double mean_pdf = pdf_acc / std::max(1, nonspec);
    YR_EXPECT_TRUE(std::fabs(mean_pdf - 1.0) > 1e-3);   // not the 1.0 proxy
}
```

Run; expected: `integrates_to_exit_probability` FAILS today (coated `PdfBsdf` aliases the Lambertian `cos/π`, which integrates to ~1 over the full hemisphere but is the WRONG distribution — and `sample_sets_real_pdf_not_proxy` FAILS because SampleLayered still returns `pdf=1.0`).

- [ ] **Step 2: Implement `PdfLayered`** in the anon namespace. Scaffold — **`EXIT_PDF_COUPLING` is the calibration target** (pinned by the integral test):

```cpp
// Stochastic estimate of the solid-angle pdf of SampleLayered producing the
// non-specular exit wi from wo. Mutually consistent with EvaluateLayered and
// SampleLayered. Reconstructed from PBRT v4 LayeredBxDF::PDF (smooth coat).
float PdfLayered(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal,
                 Rng& rng, bool conductor_base) {
    if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) return 0.0f;
    const float ce = std::max(1.0f, material.coating_ior);

    RenderMaterial base;
    if (conductor_base) {
        base.kind = RenderMaterialKind::Conductor;
        base.reflectance = material.reflectance;
        base.uroughness = material.uroughness; base.vroughness = material.vroughness;
    } else {
        base.kind = RenderMaterialKind::Diffuse; base.reflectance = material.reflectance;
    }

    Vec3f wo_t, wi_down;
    if (!Refract(wo, normal, 1.0f/ce, wo_t)) return 0.0f;
    if (!Refract(wi, normal, 1.0f/ce, wi_down)) return 0.0f;
    const Vec3f wi_internal = -wi_down;
    if (!IsAboveSurface(wi_internal, normal)) return 0.0f;

    const float T_enter = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wo, normal)), 1.0f, ce);
    const float T_exit  = 1.0f - FresnelDielectric(std::max(0.0f, Dot(wi, normal)), 1.0f, ce);
    if (T_enter <= 0.0f || T_exit <= 0.0f) return 0.0f;

    const int ns = std::max(1, material.coat_nsamples);
    double pdf_sum = 0.0;
    for (int s = 0; s < ns; ++s) {
        float reach = T_enter;        // probability of having entered and reached here
        Vec3f w = wo_t;               // down-going at base
        for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
            // pdf of the base scattering -w toward wi_internal, coupled out:
            const float base_pdf = PdfBsdf(base, -w, wi_internal, normal, rng);
            // ---- EXIT_PDF_COUPLING (CALIBRATE) ----
            // Start point: reach * base_pdf * T_exit . The correct coupling (incl.
            // any refraction solid-angle Jacobian relating wi_internal's dω to
            // wi's dω) is the one that makes ∫pdf dω ≈ (1 - F(wo)).
            pdf_sum += reach * base_pdf * T_exit;
            // ---------------------------------------
            // continue: sample base, update reach by the continuation probabilities
            const BsdfSample bs = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
            if (!bs.valid || !IsAboveSurface(bs.wi, normal)) break;
            const float f_back = FresnelDielectric(std::max(0.0f, Dot(bs.wi, normal)), ce, 1.0f);
            reach *= f_back;           // probability it reflects back down to scatter again
            if (reach <= 0.0f) break;
            w = Reflect(bs.wi, normal);
            if (IsAboveSurface(w, normal)) break;
        }
    }
    return static_cast<float>(pdf_sum / ns);
}
```

**IMPLEMENTER NOTE:** As with `f`, the `EXIT_PDF_COUPLING` start point is likely missing the refraction Jacobian. Reconstruct PBRT v4 `LayeredBxDF::PDF` and let `layered_pdf_integrates_to_exit_probability` (`∫ ≈ 0.955 ∈ [0.85, 1.05]`) force the factor. The same Jacobian appears in `f`'s `exit_coupling` and here — they must be consistent. PBRT also blends a small clamp/uniform floor into the returned pdf to keep MIS robust against a zero estimate; consider a tiny floor only if the render-level MIS-consistency check (Task 3) shows fireflies, and document it.

- [ ] **Step 3: Wire `PdfBsdf`.** Split coated kinds out (as for `EvaluateBsdf`):

```cpp
        case RenderMaterialKind::CoatedDiffuse:
            return PdfLayered(material, wo, wi, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return PdfLayered(material, wo, wi, normal, rng, /*conductor_base=*/true);
```

Remove them from the base groups in `PdfBsdf` and delete the `TODO(2b)` comment there.

- [ ] **Step 4: Set `SampleLayered`'s real pdf.** In `SampleLayered`, the non-specular exit return currently is `BsdfSample{wexit, throughput, 1.0f, true, false}`. Replace the `1.0f` proxy with a real estimate:

```cpp
        const float exit_pdf = PdfLayered(material, wo, wexit, normal, rng, conductor_base);
        return BsdfSample{wexit, throughput, std::max(exit_pdf, 1.0e-4f), true, false};
```

Remove the `TODO(2b)` comment at that return. **Leave the specular-reflect early return unchanged** (`pdf = 1.0f, specular = true`). Note: `PdfLayered` must be defined ABOVE `SampleLayered` in the file, or forward-declared — `SampleLayered` now calls it. (`EvaluateLayered`/`PdfLayered` call `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf` which are declared in the header, so ordering within the anon namespace only matters for `SampleLayered → PdfLayered`; place `PdfLayered` before `SampleLayered`, or add a forward declaration.)

- [ ] **Step 5: Verify.** Build; the 4 new pdf tests pass (calibrate `EXIT_PDF_COUPLING`); the 2a `SampleBsdf` determinism + white-furnace tests still pass (the extra rng consumption shifts the stream but the estimator is unbiased — mean energy still in range). Full suite green; CTest 9/9.

- [ ] **Step 6: Verify the specular-MIS path is unaffected.** Read `EmissiveHitMisWeight`/`EnvironmentHitMisWeight` in `cpu_path_tracer.cpp`: confirm a `previous.specular` bounce short-circuits to MIS weight 1 (so the coat's specular reflect lobe, `pdf=1.0`, is handled as a delta). If there is NO specular short-circuit, that is PRE-EXISTING behavior shared by conductor/dielectric — do NOT fix it in 2b; just note it in the PR. (2b's only `pdf` change for the specular lobe is leaving it at 1.0.)

- [ ] **Step 7: Commit.**

```
feat(bsdf): consistent stochastic layered pdf; real SampleLayered pdf

Replaces base-aliased PdfBsdf for coated kinds with a stochastic pdf
estimator consistent with EvaluateLayered + SampleLayered, and sets
SampleLayered's non-specular exit pdf to the real estimate (was the 1.0
proxy). Closes the 2a light-sampling MIS gap. Reconstructed from PBRT v4
LayeredBxDF::PDF. Validated by pdf-integral ≈ 1-F(wo), non-negativity,
determinism, and real-sample-pdf. All <N> unit tests + 9 CTest pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 3: MIS-consistency validation + render smoke

**Files:** Modify `tests/layered_bsdf_tests.cpp` (one cross-consistency test). Re-render `coated_showcase`. Optionally touch `docs/architecture/overview.md` (one line).

**The headline cross-consistency test** ties all three estimators together without needing the path tracer. Under a uniform white environment (radiance 1), reflected radiance = directional albedo ρ(wo). Estimated two ways it must agree up to the specular delta that light-sampling cannot capture:

- `ρ_bsdf` = mean of `SampleBsdf` weight (includes the specular reflect lobe + the diffuse lobe) ≈ 0.995 (the 2a furnace value).
- `ρ_light` = `∫ f(wo,wi)·cosθ dω` (the diffuse lobe only; the smooth coat's specular reflection is a delta and contributes 0 to `f`).
- The specular reflect fraction at entry ≈ `F(wo)`.

Consistency: **`ρ_bsdf ≈ ρ_light + F(wo)`** — exactly the MIS partition (BSDF sampling catches the delta, light sampling catches the rest), pinning `f`, the specular lobe, and `Sample` as one coherent set.

- [ ] **Step 1: Write the cross-consistency test:**

```cpp
YR_TEST(layered_mis_partition_is_consistent) {
    // ρ_bsdf (Sample, all lobes) ≈ ρ_light (Eval integral, diffuse lobe only) + F(wo) (specular).
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});

    yr::Rng rs{2024u}; double sb = 0.0; const int N = 40000;
    for (int i = 0; i < N; ++i) { const auto s = yr::SampleBsdf(m, wo, n, rs.NextFloat2(), rs); if (s.valid) sb += MaxComp(s.weight); }
    const float rho_bsdf = static_cast<float>(sb / N);

    const float rho_light = DirectionalAlbedoViaEval(m, wo, n, 2025u, 40000);   // from Task 1

    // F(wo): dielectric Fresnel at entry, IOR 1.5 — replicate the formula here
    // (do NOT leave it 0); or call a public Fresnel shim if you add one.
    const float cos_o = yr::Dot(wo, n);
    float F = 0.0f;
    { const float ei=1.0f, et=1.5f; float c=std::clamp(cos_o,-1.0f,1.0f);
      const float s2t = (ei/et)*(ei/et)*std::max(0.0f,1.0f-c*c);
      if (s2t < 1.0f) { const float ct=std::sqrt(1.0f-s2t);
        const float rp=((et*c)-(ei*ct))/((et*c)+(ei*ct));
        const float rs2=((ei*c)-(et*ct))/((ei*c)+(et*ct));
        F = 0.5f*(rp*rp+rs2*rs2);} else F=1.0f; }
    YR_EXPECT_TRUE(F > 0.0f);

    YR_EXPECT_TRUE(std::fabs(rho_bsdf - (rho_light + F)) <= 0.06f);
}
```

Run; expected: FAILS pre-2b (coated `Sample` already does the real walk so `ρ_bsdf≈0.995`, but `ρ_light` is the aliased Lambertian integral, breaking the partition). After Tasks 1–2 it PASSES.

- [ ] **Step 2: Render smoke.** Re-render `coated_showcase` (full + smoke) with the verified CLI command. Confirm via stdout + a cheap PNG stat: no `Error`/NaN/Inf, non-zero hits, bounded max pixel (no MIS-induced fireflies). The coated spheres are now ALSO directly lit through the real `f` (previously their direct lighting used the bare base). If fireflies appear, revisit the `PdfLayered` floor (Task 2 note) and document.

- [ ] **Step 3: Whole suite + CTest.** Expect **224 PASS / 0 FAIL** (213 baseline + 1 Task 0 + 5 Task 1 + 4 Task 2 + 1 Task 3) and CTest **9/9**.

- [ ] **Step 4 (optional docs):** add one line to `docs/architecture/overview.md`'s materials paragraph noting `coateddiffuse`/`coatedconductor` now do full stochastic two-layer evaluation (sample + f + pdf, MIS-consistent). Keep M3-status / killeroo updates for Slice 3.

- [ ] **Step 5: Commit.**

```
test(bsdf): MIS-partition cross-consistency for coated materials

rho_bsdf (Sample, all lobes) ~= rho_light (Eval integral) + F(wo)
(specular delta) — pins f, the specular lobe, and Sample as one coherent
set and confirms the 2a MIS gap is closed. Re-renders coated_showcase
(no NaN/fireflies). All 224 unit tests + 9 CTest pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
```

---

## Task 4: PR + merge

- [ ] **Step 1: Verify branch.** `git log --oneline cbfb95e..HEAD` — expect ~4–5 commits (Task 0 param, Task 1 f, Task 2 pdf, Task 3 validation; plus any review-fix).

- [ ] **Step 2: Push + PR.** `git push -u origin <branch>` then `gh pr create --base main --head <branch>` with a body covering: closes the 2a light-sampling MIS gap; stochastic `f`/`pdf` for coated; `SampleLayered` returns real pdf; validation (furnace-via-eval, reciprocity, pdf-integral, MIS-partition); scope (smooth coat, reflection only; deferred items). Use the actual branch name `EnterWorktree` reports (it prefixes `worktree-`).

- [ ] **Step 3: Address review feedback**; re-run tests after each fix.

- [ ] **Step 4: Merge (operator-gated).** After merge, finish per `superpowers:finishing-a-development-branch`: sync local `main` (`git fetch` + ff), `ExitWorktree` (remove), delete the merged remote branch.

---

## Self-Review Notes

- **Spec coverage:** spec's `EvaluateBsdf` "stochastic f estimator (next-event connection to wi)" → Task 1; `BsdfPdf` "stochastic estimate consistent with SampleBsdf" → Task 2; spec risk "stochastic f/pdf break MIS consistency" → MIS-partition test (Task 3) + furnace-via-eval + pdf-integral. `nsamples` param → Task 0.
- **Documented deviations:** (1) coat stays SMOOTH (rough coat deferred, as 2a); (2) reflection only (opaque base — no transmissive layered stack); (3) the `f`/`pdf` exit-coupling factors are calibrated against the energy/integral tests rather than derived inline — flagged because the refraction Jacobian is the known pitfall and PBRT is the named authoritative reference.
- **Why tests-as-contract is sound:** furnace-via-eval (absolute `f` scale + multiple-scattering recovery), reciprocity (`f` symmetry), pdf-integral (pdf normalization), and MIS-partition (`ρ_bsdf ≈ ρ_light + F`) are four independent constraints that together uniquely pin the estimator — an error in the connection direction, the `-w` base argument, the Jacobian, or the multiple-scattering loop fails at least one. Same philosophy that caught 2a's exit-sign bug.
- **Type/consistency:** `EvaluateLayered`/`PdfLayered(material, wo, wi, normal, Rng&, bool conductor_base)` mirror `SampleLayered`; all reuse `Refract`/`FresnelDielectric`/`Reflect`/`ApplyBeerLambert`/`IsAboveSurface`/`IsBlack` + base `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf`. `coat_nsamples` matches the `coat_*` field family. `PdfLayered` placed before `SampleLayered` (or forward-declared) since `SampleLayered` calls it.
- **No regression:** 2a's `SampleBsdf` determinism + white-furnace + conductor tests still pass — the extra rng consumed by the in-`SampleLayered` `PdfLayered` call shifts the stream but the estimators are unbiased; assertions are ranges/equalities that still hold. `IsDeltaBsdf` unchanged; delta (smooth-conductor-base) coated still skips light sampling and `f`/`pdf` return 0 safely.
- **Worktree branch** `m3-slice2b-layered-eval` consistent across Setup and Task 4.
