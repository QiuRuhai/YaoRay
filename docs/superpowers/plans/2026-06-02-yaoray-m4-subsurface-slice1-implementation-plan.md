# M4 Subsurface Slice 1 — Profile Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the pure-math foundation of YaoRay's BSSRDF — Catmull-Rom numeric utilities, dielectric Fresnel + Fresnel moments + Henyey-Greenstein helpers, the photon beam diffusion integrands, and the precomputed `BSSRDFTable` — with no integrator or scene wiring, fully unit-tested.

**Architecture:** Faithful port of pbrt-v4's `util/interpolation.cpp` (Catmull-Rom) and `bssrdf.cpp` (beam diffusion). Two new self-contained modules under the `yaoray_render` static library: `catmull_rom.{hpp,cpp}` (generic spline utilities) and `bssrdf.{hpp,cpp}` (scattering helpers + `BSSRDFTable` + `ComputeBeamDiffusionBSSRDF`). Everything is a free function or POD struct in namespace `yr`; nothing touches the BVH, path tracer, or `RenderMaterial`. Slice 2 will consume these to evaluate and sample the profile.

**Tech Stack:** C++20, the in-repo `yr_test.hpp` micro-framework (`YR_TEST` / `YR_EXPECT_NEAR` / `YR_EXPECT_TRUE`), CMake + MSVC/clang, CTest.

**Base branch:** local `main` (`5ee59f5`). **Worktree:** `m4-subsurface-slice1` (create via `superpowers:using-git-worktrees` at execution start).

**Build & test commands (run from the worktree root):**
```
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```
The unit suite is the `yaoray_tests` target; on Windows the binary is `build/Release/yaoray_tests.exe`, elsewhere `build/yaoray_tests`. Running it directly prints `[PASS]`/`[FAIL]` per `YR_TEST`. A test "fails to compile" counts as a failing test for the red step.

---

## Context the implementer needs

- **Test framework.** `tests/yr_test.hpp` provides `YR_TEST(name) { ... }` (auto-registers), `YR_EXPECT_TRUE(expr)`, `YR_EXPECT_EQ(a,b)`, `YR_EXPECT_NEAR(actual, expected, eps)`. Tests throw on failure; `test_main.cpp` runs the registry. No GoogleTest.
- **Module convention.** Public header in `include/yaoray/render/<name>.hpp`, implementation in `src/render/<name>.cpp`, the `.cpp` added to the `yaoray_render` STATIC library source list in `CMakeLists.txt` (around lines 57–70). New test `.cpp` files are added to the `add_executable(yaoray_tests …)` list (around lines 90–140).
- **Namespace & constants.** Everything lives in `namespace yr`. There is no shared `Pi` constant; each `.cpp` declares its own `constexpr float Pi = 3.14159265358979323846f;` in an anonymous namespace (see `src/render/bsdf.cpp:9`). Follow that pattern; also declare `constexpr float Inv4Pi = 0.07957747154594767f;` locally where needed.
- **Types.** `yr::Color3f` is an alias of `yr::Vec3f` (`include/yaoray/core/vec.hpp`). Slice 1 needs only `float` and `std::vector<float>` — no vector math.
- **Faithfulness.** Numbers and formulas below are transcribed from pbrt-v4. Do **not** "simplify" or "clean up" the polynomials, the `2πr` profile Jacobian, the `(i + 0.5)/nSamples` sampling offsets, or the radius/rho discretization — they must match pbrt exactly so Slice 5 can compare against a pbrt reference render.
- **No parallelism.** pbrt parallelizes the per-rho table fill with `ParallelFor`; Slice 1 uses a plain sequential loop (the precompute is a one-time cost and we want bit-deterministic output for the determinism test). Do not add threads.

---

## File Structure

| File | Responsibility |
|---|---|
| `include/yaoray/render/catmull_rom.hpp` | Declarations: `CatmullRomWeights`, `IntegrateCatmullRom`, `InvertCatmullRom` (generic 1-D spline utilities, raw-pointer API). |
| `src/render/catmull_rom.cpp` | Implementations + a file-local `FindInterval` template. |
| `include/yaoray/render/bssrdf.hpp` | Declarations: `FrDielectric`, `HenyeyGreenstein`, `FresnelMoment1`, `FresnelMoment2`, `BeamDiffusionMS`, `BeamDiffusionSS`, `struct BSSRDFTable`, `ComputeBeamDiffusionBSSRDF`. |
| `src/render/bssrdf.cpp` | Implementations + file-local `Pi`/`Inv4Pi`/`SafeSqrt`. |
| `tests/catmull_rom_tests.cpp` | Unit tests for the spline utilities. |
| `tests/bssrdf_tests.cpp` | Unit tests for Fresnel/HG helpers (Task 2), beam diffusion (Task 3), and the table (Task 4). |
| `CMakeLists.txt` | Add the two `.cpp` sources to `yaoray_render`; add the two test files to `yaoray_tests`. |

---

### Task 1: Catmull-Rom numeric utilities

**Files:**
- Create: `include/yaoray/render/catmull_rom.hpp`
- Create: `src/render/catmull_rom.cpp`
- Create: `tests/catmull_rom_tests.cpp`
- Modify: `CMakeLists.txt` (add `src/render/catmull_rom.cpp` to `yaoray_render`; add `tests/catmull_rom_tests.cpp` to `yaoray_tests`)

These three free functions are the spline machinery the diffusion table needs: `IntegrateCatmullRom` builds the per-rho CDF and returns the effective albedo; `CatmullRomWeights` and `InvertCatmullRom` are consumed in Slice 2 but land here because they are one cohesive, independently-testable module. The API uses raw `const float*` + `int n` (matching the `tensor_file` raw-array style) instead of pbrt's `pstd::span`.

- [ ] **Step 1: Create the header**

Create `include/yaoray/render/catmull_rom.hpp`:

```cpp
#pragma once

// Catmull-Rom spline utilities — faithful port of pbrt-v4 (util/interpolation.cpp).
// Generic 1-D helpers used to interpolate, integrate, and importance-sample the
// tabulated subsurface diffusion profile. Raw-pointer API (const float* + int n)
// to match the rest of the render layer; no allocation, no throwing.

namespace yr {

// Compute the four Catmull-Rom basis weights for evaluating a spline at `x` over
// the monotonically increasing `nodes` (size n). On success returns true, sets
// `offset` so the weights apply to nodes/values indices [offset, offset+3]
// (clamped at the ends, where the corresponding weight is 0), and writes
// weights[0..3]. Returns false (leaving outputs untouched) if x is outside
// [nodes[0], nodes[n-1]].
bool CatmullRomWeights(int n, const float* nodes, float x, int& offset, float weights[4]);

// Integrate the Catmull-Rom spline defined by (x[i], values[i]) for i in [0,n),
// writing the cumulative integral into cdf[0..n-1] (cdf[0]==0). Returns the total
// integral (== cdf[n-1]).
float IntegrateCatmullRom(int n, const float* x, const float* values, float* cdf);

// Invert the Catmull-Rom spline: return the position x* in [x[0], x[n-1]] at which
// the spline through (x[i], values[i]) equals `u`. `values` must be monotonically
// non-decreasing (e.g. a CDF). Clamps to the endpoints when u is out of range.
float InvertCatmullRom(int n, const float* x, const float* values, float u);

}  // namespace yr
```

- [ ] **Step 2: Write the failing tests**

Create `tests/catmull_rom_tests.cpp`:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/catmull_rom.hpp>
#include <cmath>

// Helper: evaluate the Catmull-Rom spline at x by combining the basis weights
// with the sample values, mirroring how Slice 2 will look up the profile.
static float EvalSpline(int n, const float* nodes, const float* values, float x) {
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    if (!yr::CatmullRomWeights(n, nodes, x, offset, w)) return 0.0f;
    float acc = 0.0f;
    for (int k = 0; k < 4; ++k) {
        int idx = offset + k;
        if (idx >= 0 && idx < n) acc += w[k] * values[idx];
    }
    return acc;
}

// Catmull-Rom reproduces linear functions exactly: weights applied to samples of
// f(x)=2+3x must return f at an interior point, and the weights must sum to 1.
YR_TEST(catmullrom_weights_reproduce_linear) {
    const float nodes[5] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    float values[5];
    for (int i = 0; i < 5; ++i) values[i] = 2.0f + 3.0f * nodes[i];
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(yr::CatmullRomWeights(5, nodes, 2.4f, offset, w));
    YR_EXPECT_NEAR(w[0] + w[1] + w[2] + w[3], 1.0f, 1e-5f);
    YR_EXPECT_NEAR(EvalSpline(5, nodes, values, 2.4f), 2.0f + 3.0f * 2.4f, 1e-4f);
}

// Out-of-range x returns false.
YR_TEST(catmullrom_weights_out_of_range) {
    const float nodes[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    int offset = 0;
    float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(!yr::CatmullRomWeights(4, nodes, -0.5f, offset, w));
    YR_EXPECT_TRUE(!yr::CatmullRomWeights(4, nodes, 3.5f, offset, w));
}

// Integrating samples of a linear function gives the exact trapezoidal area, the
// CDF starts at 0 and is monotonically non-decreasing.
YR_TEST(catmullrom_integrate_linear_exact) {
    const float x[5] = {0.0f, 0.5f, 1.5f, 2.0f, 3.0f};  // non-uniform nodes
    float values[5];
    for (int i = 0; i < 5; ++i) values[i] = 1.0f + 2.0f * x[i];  // f(x)=1+2x
    float cdf[5] = {0, 0, 0, 0, 0};
    float total = yr::IntegrateCatmullRom(5, x, values, cdf);
    // Exact integral of 1+2x over [0,3] = [x + x^2]_0^3 = 3 + 9 = 12.
    YR_EXPECT_NEAR(total, 12.0f, 1e-3f);
    YR_EXPECT_NEAR(cdf[0], 0.0f, 1e-6f);
    YR_EXPECT_NEAR(cdf[4], total, 1e-5f);
    for (int i = 1; i < 5; ++i) YR_EXPECT_TRUE(cdf[i] >= cdf[i - 1]);
}

// Invert is the inverse of forward evaluation: pick x*, evaluate the spline value
// v* there, then InvertCatmullRom(v*) must recover x*.
YR_TEST(catmullrom_invert_roundtrip) {
    const float x[6] = {0.0f, 0.4f, 1.0f, 1.7f, 2.5f, 3.0f};
    float values[6];
    for (int i = 0; i < 6; ++i) values[i] = x[i] * x[i];  // monotone increasing on [0,3]
    const float xstar = 1.3f;
    const float vstar = EvalSpline(6, x, values, xstar);
    float recovered = yr::InvertCatmullRom(6, x, values, vstar);
    YR_EXPECT_NEAR(recovered, xstar, 2e-3f);
}

// Invert clamps out-of-range u to the endpoints.
YR_TEST(catmullrom_invert_clamps) {
    const float x[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    const float values[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    YR_EXPECT_NEAR(yr::InvertCatmullRom(4, x, values, -5.0f), 0.0f, 1e-6f);
    YR_EXPECT_NEAR(yr::InvertCatmullRom(4, x, values, 99.0f), 3.0f, 1e-6f);
}
```

- [ ] **Step 3: Register the source + test in CMake, verify the tests fail**

In `CMakeLists.txt`, add to the `yaoray_render` source list (after `src/render/measured_brdf.cpp`, line ~69):
```cmake
    src/render/catmull_rom.cpp
```
And add to the `add_executable(yaoray_tests …)` list (after `tests/measured_sample_tests.cpp`, line ~139):
```cmake
    tests/catmull_rom_tests.cpp
```

Run: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --config Release`
Expected: **build FAILS** — `catmull_rom.cpp` does not exist / linker errors for the three functions. This is the red state.

- [ ] **Step 4: Implement the utilities**

Create `src/render/catmull_rom.cpp`:

```cpp
#include <yaoray/render/catmull_rom.hpp>

#include <algorithm>

namespace yr {
namespace {

// Binary search for the last interval whose left node satisfies pred(i)==true,
// clamped to [0, n-2]. Faithful port of pbrt's FindInterval.
template <typename Pred>
int FindInterval(int n, Pred pred) {
    int size = n - 2;
    int first = 1;
    while (size > 0) {
        int half = size >> 1;
        int middle = first + half;
        if (pred(middle)) {
            first = middle + 1;
            size -= half + 1;
        } else {
            size = half;
        }
    }
    return std::clamp(first - 1, 0, n - 2);
}

}  // namespace

bool CatmullRomWeights(int n, const float* nodes, float x, int& offset, float weights[4]) {
    if (!(x >= nodes[0] && x <= nodes[n - 1])) return false;

    int idx = FindInterval(n, [&](int i) { return nodes[i] <= x; });
    offset = idx - 1;
    float x0 = nodes[idx], x1 = nodes[idx + 1];

    float t = (x - x0) / (x1 - x0);
    float t2 = t * t, t3 = t2 * t;

    weights[1] = 2 * t3 - 3 * t2 + 1;
    weights[2] = -2 * t3 + 3 * t2;

    if (idx > 0) {
        float w0 = (t3 - 2 * t2 + t) * (x1 - x0) / (x1 - nodes[idx - 1]);
        weights[0] = -w0;
        weights[2] += w0;
    } else {
        float w0 = t3 - 2 * t2 + t;
        weights[0] = 0;
        weights[1] -= w0;
        weights[2] += w0;
    }

    if (idx + 2 < n) {
        float w3 = (t3 - t2) * (x1 - x0) / (nodes[idx + 2] - x0);
        weights[1] -= w3;
        weights[3] = w3;
    } else {
        float w3 = t3 - t2;
        weights[1] -= w3;
        weights[2] += w3;
        weights[3] = 0;
    }
    return true;
}

float IntegrateCatmullRom(int n, const float* x, const float* values, float* cdf) {
    float sum = 0;
    cdf[0] = 0;
    for (int i = 0; i < n - 1; ++i) {
        float x0 = x[i], x1 = x[i + 1];
        float f0 = values[i], f1 = values[i + 1];
        float width = x1 - x0;

        float d0, d1;
        if (i > 0)
            d0 = width * (f1 - values[i - 1]) / (x1 - x[i - 1]);
        else
            d0 = f1 - f0;
        if (i + 2 < n)
            d1 = width * (values[i + 2] - f0) / (x[i + 2] - x0);
        else
            d1 = f1 - f0;

        sum += ((d0 - d1) * (1.0f / 12.0f) + (f0 + f1) * 0.5f) * width;
        cdf[i + 1] = sum;
    }
    return sum;
}

float InvertCatmullRom(int n, const float* x, const float* values, float u) {
    if (!(u > values[0])) return x[0];
    if (!(u < values[n - 1])) return x[n - 1];

    int i = FindInterval(n, [&](int idx) { return values[idx] <= u; });
    float x0 = x[i], x1 = x[i + 1];
    float f0 = values[i], f1 = values[i + 1];
    float width = x1 - x0;

    float d0, d1;
    if (i > 0)
        d0 = width * (f1 - values[i - 1]) / (x1 - x[i - 1]);
    else
        d0 = f1 - f0;
    if (i + 2 < n)
        d1 = width * (values[i + 2] - f0) / (x[i + 2] - x0);
    else
        d1 = f1 - f0;

    float a = 0, b = 1, t = 0.5f;
    for (int iter = 0; iter < 100; ++iter) {
        if (!(t > a && t < b)) t = 0.5f * (a + b);
        float t2 = t * t, t3 = t2 * t;
        float fhat = (2 * t3 - 3 * t2 + 1) * f0 + (-2 * t3 + 3 * t2) * f1 +
                     (t3 - 2 * t2 + t) * d0 + (t3 - t2) * d1;
        float fhatDeriv = (6 * t2 - 6 * t) * f0 + (-6 * t2 + 6 * t) * f1 +
                          (3 * t2 - 4 * t + 1) * d0 + (3 * t2 - 2 * t) * d1;
        if (std::abs(fhat - u) < 1e-6f) break;
        if (fhat - u < 0)
            a = t;
        else
            b = t;
        t -= (fhat - u) / fhatDeriv;
    }
    return x0 + t * width;
}

}  // namespace yr
```

- [ ] **Step 5: Build and run the tests — verify they pass**

Run: `cmake --build build --config Release && ctest --test-dir build --output-on-failure -C Release`
Expected: build succeeds; all five `catmullrom_*` tests `[PASS]`; the whole prior suite stays green.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/catmull_rom.hpp src/render/catmull_rom.cpp tests/catmull_rom_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add Catmull-Rom spline utilities (M4 slice 1)"
```

---

### Task 2: Fresnel + Henyey-Greenstein helpers

**Files:**
- Create: `include/yaoray/render/bssrdf.hpp`
- Create: `src/render/bssrdf.cpp`
- Create: `tests/bssrdf_tests.cpp`
- Modify: `CMakeLists.txt` (add `src/render/bssrdf.cpp` to `yaoray_render`; add `tests/bssrdf_tests.cpp` to `yaoray_tests`)

This task creates the `bssrdf` module with the four scalar scattering helpers the beam-diffusion integrands depend on. `FrDielectric` and `HenyeyGreenstein` are exposed publicly (faithful to pbrt's `scattering.h`; reused later by the integrator). The Fresnel *moments* `FresnelMoment1/2` are the polynomial fits pbrt uses for the diffusion boundary conditions.

- [ ] **Step 1: Create the header (full Slice 1 surface)**

Create `include/yaoray/render/bssrdf.hpp` with the complete public surface (later tasks fill in the bodies; declaring them now keeps the header stable across tasks):

```cpp
#pragma once

// Subsurface scattering core — faithful port of pbrt-v4 (bssrdf.cpp + scattering.h).
// Slice 1: scattering helpers, photon beam diffusion integrands, and the
// precomputed BSSRDFTable. No integrator, no scene types — pure math.

#include <vector>

namespace yr {

// Scalar dielectric Fresnel reflectance for unpolarized light. `eta` is the
// relative IOR (transmitted / incident). cos_theta_i may be negative (light from
// the far side); eta is inverted internally in that case. Returns 1 on total
// internal reflection.
float FrDielectric(float cos_theta_i, float eta);

// Henyey-Greenstein phase function value for the angle whose cosine is cos_theta
// and asymmetry parameter g in (-1, 1). Normalizes to 1 over the sphere.
float HenyeyGreenstein(float cos_theta, float g);

// Polynomial fits to the first and second moments of the Fresnel reflectance,
// used as diffusion boundary conditions. `eta` is the relative IOR.
float FresnelMoment1(float eta);
float FresnelMoment2(float eta);

// Photon beam diffusion — multiple-scattering term. Returns the diffuse fluence
// contribution at surface radius r for a semi-infinite homogeneous medium with
// the given scattering/absorption coefficients, phase asymmetry g, and IOR eta.
float BeamDiffusionMS(float sigma_s, float sigma_a, float g, float eta, float r);

// Photon beam diffusion — single-scattering term at surface radius r.
float BeamDiffusionSS(float sigma_s, float sigma_a, float g, float eta, float r);

// Precomputed, separable diffusion profile sampled over (single-scattering albedo
// rho, optical radius r). Built once per (g, eta) by ComputeBeamDiffusionBSSRDF.
struct BSSRDFTable {
    int n_rho = 0;
    int n_radius = 0;
    std::vector<float> rho_samples;     // [n_rho]      discretized albedos
    std::vector<float> radius_samples;  // [n_radius]   discretized radii
    std::vector<float> profile;         // [n_rho*n_radius]  2*pi*r*(MS+SS)
    std::vector<float> rho_eff;         // [n_rho]      effective hemispherical albedo
    std::vector<float> profile_cdf;     // [n_rho*n_radius]  per-rho radial CDF

    BSSRDFTable(int n_rho_samples, int n_radius_samples);
};

// Fill `table.profile`, `table.rho_eff`, and `table.profile_cdf` for the given
// phase asymmetry g and relative IOR eta. Deterministic (sequential).
void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& table);

}  // namespace yr
```

- [ ] **Step 2: Write the failing tests for the helpers**

Create `tests/bssrdf_tests.cpp`:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

constexpr float kInv4Pi = 0.07957747154594767f;

// Normal-incidence dielectric reflectance for eta=1.5 is ((1.5-1)/(1.5+1))^2 = 0.04.
YR_TEST(frdielectric_normal_incidence) {
    YR_EXPECT_NEAR(yr::FrDielectric(1.0f, 1.5f), 0.04f, 1e-3f);
}

// Matched IOR transmits fully: reflectance 0.
YR_TEST(frdielectric_matched_ior) {
    YR_EXPECT_NEAR(yr::FrDielectric(0.7f, 1.0f), 0.0f, 1e-6f);
}

// Total internal reflection: light leaving a denser medium (eta<1 relative) at a
// grazing angle beyond the critical angle reflects fully.
YR_TEST(frdielectric_total_internal_reflection) {
    // eta = 1/1.5 ~ 0.667; critical angle cos ~ sqrt(1 - eta^2). A small cos
    // (grazing) is beyond critical -> full reflection.
    YR_EXPECT_NEAR(yr::FrDielectric(0.1f, 1.0f / 1.5f), 1.0f, 1e-5f);
}

// Isotropic phase function (g=0) equals 1/(4*pi) for any angle.
YR_TEST(henyey_greenstein_isotropic) {
    YR_EXPECT_NEAR(yr::HenyeyGreenstein(0.3f, 0.0f), kInv4Pi, 1e-6f);
    YR_EXPECT_NEAR(yr::HenyeyGreenstein(-0.8f, 0.0f), kInv4Pi, 1e-6f);
}

// Forward scattering (g>0) peaks in the forward direction (cos_theta=1) and is
// strongest there relative to backward.
YR_TEST(henyey_greenstein_forward_bias) {
    float fwd = yr::HenyeyGreenstein(1.0f, 0.5f);
    float bwd = yr::HenyeyGreenstein(-1.0f, 0.5f);
    YR_EXPECT_TRUE(fwd > bwd);
    YR_EXPECT_TRUE(std::isfinite(fwd) && std::isfinite(bwd));
}

// At eta=1 there is no interface, so the first Fresnel moment is ~0.
YR_TEST(fresnel_moment1_no_interface) {
    YR_EXPECT_NEAR(yr::FresnelMoment1(1.0f), 0.0f, 1e-2f);
}

// Both moments are finite and positive for a typical eta=1.33.
YR_TEST(fresnel_moments_finite) {
    YR_EXPECT_TRUE(std::isfinite(yr::FresnelMoment1(1.33f)) && yr::FresnelMoment1(1.33f) > 0.0f);
    YR_EXPECT_TRUE(std::isfinite(yr::FresnelMoment2(1.33f)));
}
```

- [ ] **Step 3: Register source + test in CMake, verify failure**

In `CMakeLists.txt` add `src/render/bssrdf.cpp` to `yaoray_render` (after `src/render/catmull_rom.cpp`) and `tests/bssrdf_tests.cpp` to `yaoray_tests` (after `tests/catmull_rom_tests.cpp`).

Run: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --config Release`
Expected: **build FAILS** — `bssrdf.cpp` missing / unresolved symbols.

- [ ] **Step 4: Implement the module skeleton + the four helpers**

Create `src/render/bssrdf.cpp` (this file grows in Tasks 3–4; start with the helpers):

```cpp
#include <yaoray/render/bssrdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float Inv4Pi = 0.07957747154594767f;

inline float SafeSqrt(float x) { return std::sqrt(std::max(0.0f, x)); }

}  // namespace

float FrDielectric(float cos_theta_i, float eta) {
    cos_theta_i = std::clamp(cos_theta_i, -1.0f, 1.0f);
    if (cos_theta_i < 0) {
        eta = 1.0f / eta;
        cos_theta_i = -cos_theta_i;
    }
    float sin2_theta_i = 1.0f - cos_theta_i * cos_theta_i;
    float sin2_theta_t = sin2_theta_i / (eta * eta);
    if (sin2_theta_t >= 1.0f) return 1.0f;  // total internal reflection
    float cos_theta_t = SafeSqrt(1.0f - sin2_theta_t);

    float r_parl = (eta * cos_theta_i - cos_theta_t) / (eta * cos_theta_i + cos_theta_t);
    float r_perp = (cos_theta_i - eta * cos_theta_t) / (cos_theta_i + eta * cos_theta_t);
    return 0.5f * (r_parl * r_parl + r_perp * r_perp);
}

float HenyeyGreenstein(float cos_theta, float g) {
    float denom = 1.0f + g * g + 2.0f * g * cos_theta;
    return Inv4Pi * (1.0f - g * g) / (denom * SafeSqrt(denom));
}

float FresnelMoment1(float eta) {
    float eta2 = eta * eta, eta3 = eta2 * eta, eta4 = eta3 * eta, eta5 = eta4 * eta;
    if (eta < 1)
        return 0.45966f - 1.73965f * eta + 3.37668f * eta2 - 3.904945f * eta3 +
               2.49277f * eta4 - 0.68441f * eta5;
    return -4.61686f + 11.1136f * eta - 10.4646f * eta2 + 5.11455f * eta3 -
           1.27198f * eta4 + 0.12746f * eta5;
}

float FresnelMoment2(float eta) {
    float eta2 = eta * eta, eta3 = eta2 * eta, eta4 = eta3 * eta, eta5 = eta4 * eta;
    if (eta < 1)
        return 0.27614f - 0.87350f * eta + 1.12077f * eta2 - 0.65095f * eta3 +
               0.07883f * eta4 + 0.04860f * eta5;
    float r_1 = -547.033f + 45.3087f / eta3 - 218.725f / eta2 + 458.843f / eta +
                404.557f * eta - 189.519f * eta2 + 54.9327f * eta3 -
                9.00603f * eta4 + 0.63942f * eta5;
    return r_1;
}

}  // namespace yr
```

- [ ] **Step 5: Build and run — verify the helper tests pass**

Run: `cmake --build build --config Release && ctest --test-dir build --output-on-failure -C Release`
Expected: all seven helper tests `[PASS]`; full suite green.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/bssrdf.hpp src/render/bssrdf.cpp tests/bssrdf_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add Fresnel + Henyey-Greenstein scattering helpers (M4 slice 1)"
```

---

### Task 3: Photon beam diffusion integrands

**Files:**
- Modify: `src/render/bssrdf.cpp` (add `BeamDiffusionMS` + `BeamDiffusionSS`)
- Modify: `tests/bssrdf_tests.cpp` (append tests)

These two functions are the heart of the diffusion model: each numerically integrates (100 fixed samples) the dipole / single-scattering contribution to the surface fluence at radius `r`. Transcribe the formulas exactly — the constants and the `(i + 0.5)/nSamples` exponential-importance sampling of depth must match pbrt.

- [ ] **Step 1: Write the failing tests (append to `tests/bssrdf_tests.cpp`)**

```cpp
// Beam diffusion terms are non-negative and finite for a typical skin-like medium.
YR_TEST(beam_diffusion_nonnegative_finite) {
    const float sigma_s = 2.0f, sigma_a = 0.01f, g = 0.0f, eta = 1.33f;
    for (float r : {0.001f, 0.01f, 0.1f, 0.5f, 1.0f}) {
        float ms = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, r);
        float ss = yr::BeamDiffusionSS(sigma_s, sigma_a, g, eta, r);
        YR_EXPECT_TRUE(std::isfinite(ms) && ms >= 0.0f);
        YR_EXPECT_TRUE(std::isfinite(ss) && ss >= 0.0f);
    }
}

// The multiple-scattering fluence decays with radius (more spreading = less return
// far away). Compare a near and a far radius.
YR_TEST(beam_diffusion_ms_decays_with_radius) {
    const float sigma_s = 2.0f, sigma_a = 0.01f, g = 0.0f, eta = 1.33f;
    float near_r = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, 0.02f);
    float far_r = yr::BeamDiffusionMS(sigma_s, sigma_a, g, eta, 0.8f);
    YR_EXPECT_TRUE(near_r > far_r);
}

// More absorption reduces the multiple-scattering response at a fixed radius.
YR_TEST(beam_diffusion_ms_absorption_reduces) {
    const float sigma_s = 2.0f, g = 0.0f, eta = 1.33f, r = 0.1f;
    float low_abs = yr::BeamDiffusionMS(sigma_s, 0.01f, g, eta, r);
    float high_abs = yr::BeamDiffusionMS(sigma_s, 0.5f, g, eta, r);
    YR_EXPECT_TRUE(low_abs > high_abs);
}
```

- [ ] **Step 2: Build and run — verify the new tests fail**

Run: `cmake --build build --config Release`
Expected: **build FAILS** — `BeamDiffusionMS`/`BeamDiffusionSS` unresolved.

- [ ] **Step 3: Implement the integrands (insert into `src/render/bssrdf.cpp` before the closing `}  // namespace yr`)**

```cpp
float BeamDiffusionMS(float sigma_s, float sigma_a, float g, float eta, float r) {
    const int nSamples = 100;
    float Ed = 0;

    // Reduced scattering coefficients and albedo.
    float sigmap_s = sigma_s * (1 - g);
    float sigmap_t = sigma_a + sigmap_s;
    float rhop = sigmap_s / sigmap_t;

    // Non-classical diffusion coefficient and effective transport coefficient.
    float D_g = (2 * sigma_a + sigmap_s) / (3 * sigmap_t * sigmap_t);
    float sigma_tr = std::sqrt(sigma_a / D_g);

    // Linear extrapolation distance and exitance scale factors.
    float fm1 = FresnelMoment1(eta), fm2 = FresnelMoment2(eta);
    float ze = -2 * D_g * (1 + 3 * fm2) / (1 - 2 * fm1);
    float cPhi = 0.25f * (1 - 2 * fm1), cE = 0.5f * (1 - 3 * fm2);

    for (int i = 0; i < nSamples; ++i) {
        // Exponential-importance-sampled real source depth.
        float zr = -std::log(1 - (i + 0.5f) / nSamples) / sigmap_t;
        float zv = -zr + 2 * ze;  // virtual (mirror) source
        float dr = std::sqrt(r * r + zr * zr);
        float dv = std::sqrt(r * r + zv * zv);

        // Dipole fluence rate and vector irradiance.
        float phiD = Inv4Pi / D_g *
                     (std::exp(-sigma_tr * dr) / dr - std::exp(-sigma_tr * dv) / dv);
        float EDn = Inv4Pi *
                    (zr * (1 + sigma_tr * dr) * std::exp(-sigma_tr * dr) / (dr * dr * dr) -
                     zv * (1 + sigma_tr * dv) * std::exp(-sigma_tr * dv) / (dv * dv * dv));

        float E = phiD * cPhi + EDn * cE;
        float kappa = 1 - std::exp(-2 * sigmap_t * (dr + zr));
        Ed += rhop * rhop * std::exp(-sigmap_t * zr) * kappa * E / nSamples;
    }
    return Ed;
}

float BeamDiffusionSS(float sigma_s, float sigma_a, float g, float eta, float r) {
    float sigma_t = sigma_a + sigma_s;
    float rho = sigma_s / sigma_t;
    float tCrit = r * SafeSqrt(1 - 1 / (eta * eta));
    float Ess = 0;
    const int nSamples = 100;
    for (int i = 0; i < nSamples; ++i) {
        float ti = tCrit - std::log(1 - (i + 0.5f) / nSamples) / sigma_t;
        float d = std::sqrt(r * r + ti * ti);
        float cosTheta_o = ti / d;
        Ess += rho * std::exp(-sigma_t * (d + ti)) / (d * d) *
               HenyeyGreenstein(cosTheta_o, g) * (1 - FrDielectric(-cosTheta_o, eta)) *
               std::abs(cosTheta_o);
    }
    return Ess / nSamples;
}
```

Note: `SafeSqrt`, `Inv4Pi`, and `Pi` are already defined in the anonymous namespace at the top of this file (Task 2). `FresnelMoment1/2`, `HenyeyGreenstein`, `FrDielectric` are the public functions from Task 2.

- [ ] **Step 4: Build and run — verify the new tests pass**

Run: `cmake --build build --config Release && ctest --test-dir build --output-on-failure -C Release`
Expected: the three `beam_diffusion_*` tests `[PASS]`; full suite green.

- [ ] **Step 5: Commit**

```bash
git add src/render/bssrdf.cpp tests/bssrdf_tests.cpp
git commit -m "feat(bssrdf): add photon beam diffusion integrands (M4 slice 1)"
```

---

### Task 4: BSSRDFTable + ComputeBeamDiffusionBSSRDF

**Files:**
- Modify: `src/render/bssrdf.cpp` (add the `BSSRDFTable` constructor + `ComputeBeamDiffusionBSSRDF`)
- Modify: `tests/bssrdf_tests.cpp` (append tests)

This assembles the integrands into the precomputed table: a geometric radius discretization, an albedo discretization clustered near the high-albedo end, the `2πr·(MS+SS)` profile, and the per-rho CDF + effective albedo via `IntegrateCatmullRom`. `rho_eff` being monotonic in `rho` is the invariant Slice 2 relies on to invert "desired albedo → rho".

- [ ] **Step 1: Write the failing tests (append to `tests/bssrdf_tests.cpp`)**

```cpp
#include <yaoray/render/catmull_rom.hpp>  // (already pulled transitively; explicit for clarity)

// The table builds without NaNs: profile finite & non-negative, CDFs monotone and
// starting at 0, rho_eff finite in [0,1.05].
YR_TEST(bssrdf_table_well_formed) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(/*g=*/0.0f, /*eta=*/1.33f, table);

    YR_EXPECT_EQ((int)table.profile.size(), 100 * 64);
    YR_EXPECT_EQ((int)table.rho_eff.size(), 100);

    for (float v : table.profile) YR_EXPECT_TRUE(std::isfinite(v) && v >= 0.0f);

    for (int i = 0; i < table.n_rho; ++i) {
        YR_EXPECT_TRUE(std::isfinite(table.rho_eff[i]));
        YR_EXPECT_TRUE(table.rho_eff[i] >= 0.0f && table.rho_eff[i] <= 1.05f);
        const float* cdf = &table.profile_cdf[i * table.n_radius];
        YR_EXPECT_NEAR(cdf[0], 0.0f, 1e-6f);
        for (int j = 1; j < table.n_radius; ++j) YR_EXPECT_TRUE(cdf[j] >= cdf[j - 1]);
    }
}

// Effective albedo increases monotonically with the single-scattering albedo:
// rho_eff[0] (rho~0) is tiny, rho_eff[last] (rho~1) is the largest, and the array
// never decreases.
YR_TEST(bssrdf_table_rho_eff_monotonic) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);

    YR_EXPECT_NEAR(table.rho_eff.front(), 0.0f, 1e-2f);
    YR_EXPECT_TRUE(table.rho_eff.back() > table.rho_eff.front());
    for (int i = 1; i < table.n_rho; ++i)
        YR_EXPECT_TRUE(table.rho_eff[i] >= table.rho_eff[i - 1] - 1e-4f);
}

// First radius node is 0 and radii increase geometrically (faithful discretization).
YR_TEST(bssrdf_table_radius_discretization) {
    yr::BSSRDFTable table(100, 64);
    yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
    YR_EXPECT_NEAR(table.radius_samples[0], 0.0f, 1e-9f);
    YR_EXPECT_NEAR(table.radius_samples[1], 2.5e-3f, 1e-9f);
    for (int j = 2; j < table.n_radius; ++j)
        YR_EXPECT_TRUE(table.radius_samples[j] > table.radius_samples[j - 1]);
}

// Determinism: two independent builds with identical params are bit-for-bit equal.
YR_TEST(bssrdf_table_deterministic) {
    yr::BSSRDFTable a(50, 32), b(50, 32);
    yr::ComputeBeamDiffusionBSSRDF(0.2f, 1.4f, a);
    yr::ComputeBeamDiffusionBSSRDF(0.2f, 1.4f, b);
    for (size_t i = 0; i < a.profile.size(); ++i) YR_EXPECT_EQ(a.profile[i], b.profile[i]);
    for (size_t i = 0; i < a.rho_eff.size(); ++i) YR_EXPECT_EQ(a.rho_eff[i], b.rho_eff[i]);
}
```

- [ ] **Step 2: Build and run — verify the new tests fail**

Run: `cmake --build build --config Release`
Expected: **build FAILS** — `BSSRDFTable` constructor and `ComputeBeamDiffusionBSSRDF` unresolved.

- [ ] **Step 3: Implement the table (insert into `src/render/bssrdf.cpp`)**

Add `#include <yaoray/render/catmull_rom.hpp>` to the include block at the top of `src/render/bssrdf.cpp`, then add before the closing `}  // namespace yr`:

```cpp
BSSRDFTable::BSSRDFTable(int n_rho_samples, int n_radius_samples)
    : n_rho(n_rho_samples),
      n_radius(n_radius_samples),
      rho_samples(n_rho_samples),
      radius_samples(n_radius_samples),
      profile((std::size_t)n_rho_samples * n_radius_samples),
      rho_eff(n_rho_samples),
      profile_cdf((std::size_t)n_rho_samples * n_radius_samples) {}

void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& t) {
    // Geometric radius discretization: 0, 2.5e-3, then *1.2 each step.
    t.radius_samples[0] = 0.0f;
    t.radius_samples[1] = 2.5e-3f;
    for (int i = 2; i < t.n_radius; ++i)
        t.radius_samples[i] = t.radius_samples[i - 1] * 1.2f;

    // Albedo discretization clustered toward rho=1.
    for (int i = 0; i < t.n_rho; ++i)
        t.rho_samples[i] = (1 - std::exp(-8.0f * i / (float)(t.n_rho - 1))) /
                           (1 - std::exp(-8.0f));

    for (int i = 0; i < t.n_rho; ++i) {
        for (int j = 0; j < t.n_radius; ++j) {
            float rho = t.rho_samples[i];
            float r = t.radius_samples[j];
            t.profile[(std::size_t)i * t.n_radius + j] =
                2 * Pi * r *
                (BeamDiffusionMS(rho, 1 - rho, g, eta, r) +
                 BeamDiffusionSS(rho, 1 - rho, g, eta, r));
        }
        // Effective albedo + radial CDF for this rho row.
        t.rho_eff[i] = IntegrateCatmullRom(
            t.n_radius, t.radius_samples.data(),
            &t.profile[(std::size_t)i * t.n_radius],
            &t.profile_cdf[(std::size_t)i * t.n_radius]);
    }
}
```

- [ ] **Step 4: Build and run — verify all table tests pass**

Run: `cmake --build build --config Release && ctest --test-dir build --output-on-failure -C Release`
Expected: the four `bssrdf_table_*` tests `[PASS]`; full suite green; no NaN/Inf assertions trip.

- [ ] **Step 5: Commit**

```bash
git add src/render/bssrdf.cpp tests/bssrdf_tests.cpp
git commit -m "feat(bssrdf): add BSSRDFTable + photon beam diffusion precompute (M4 slice 1)"
```

---

## Self-Review (completed by plan author)

**1. Spec coverage.** Slice 1 of the M4 spec asks for: Catmull-Rom utilities (Task 1 ✓), `BSSRDFTable` + `ComputeBeamDiffusionBSSRDF` + photon beam diffusion precompute (Tasks 3–4 ✓), Fresnel moments (Task 2 ✓), pure functions / no integrator (✓ — nothing here touches BVH, path tracer, or `RenderMaterial`), heavy unit tests including profile normalization (rho_eff via IntegrateCatmullRom ✓) and the inversion *prerequisite* (rho_eff monotonic ✓ — the full albedo→rho roundtrip is a Slice 2 concern since it needs the inverse lookup). No Slice 1 spec item is unaddressed.

**2. Placeholder scan.** No "TBD"/"handle edge cases"/"similar to" — every code step contains complete, compilable code and exact commands with expected pass/fail outcomes.

**3. Type consistency.** Function names and signatures are identical across header (Task 2 declares the full surface), tests, and implementations: `CatmullRomWeights(int, const float*, float, int&, float[4])`, `IntegrateCatmullRom(int, const float*, const float*, float*)`, `InvertCatmullRom(int, const float*, const float*, float)`, `FrDielectric(float,float)`, `HenyeyGreenstein(float,float)`, `FresnelMoment1/2(float)`, `BeamDiffusionMS/SS(float,float,float,float,float)`, `BSSRDFTable(int,int)` with members `n_rho`/`n_radius`/`rho_samples`/`radius_samples`/`profile`/`rho_eff`/`profile_cdf`, `ComputeBeamDiffusionBSSRDF(float,float,BSSRDFTable&)`. The header is created complete in Task 2 so later tasks only add bodies — no signature drift.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-yaoray-m4-subsurface-slice1-implementation-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review (spec then quality) between tasks, fast iteration. Use opus for the beam-diffusion math (Tasks 3–4), sonnet for the mechanical utility/helper tasks (Tasks 1–2).
2. **Inline Execution** — execute tasks in this session via `superpowers:executing-plans` with checkpoints.
