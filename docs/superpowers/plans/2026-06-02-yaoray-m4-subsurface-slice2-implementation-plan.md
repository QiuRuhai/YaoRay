# M4 Subsurface Slice 2 — BSSRDF Evaluation + Radius Sampling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `TabulatedBSSRDF` — the separable BSSRDF's radial profile `Sr`, directional term `Sw`, spatial term `Sp`, the combined value `S`, and the 1-D radius importance sampling `Sample_Sr`/`Pdf_Sr` — on top of Slice 1's `BSSRDFTable`, plus the `SampleCatmullRom2D` utility they need.

**Architecture:** Faithful port of pbrt-v4's `TabulatedBSSRDF` (`bssrdf.cpp`) and `SampleCatmullRom2D` (`util/interpolation.cpp`). The new `TabulatedBSSRDF` is a small value type constructed from per-RGB-channel `sigma_a`/`sigma_s` + `eta` + a reference to a Slice-1 `BSSRDFTable`. It computes `sigma_t`/`rho` per channel and evaluates/samples the radial profile via Catmull-Rom interpolation of the table. No geometry, no integrator — the exit-point machinery (`Sample_Sp`/`Pdf_Sp`, probe rays, BVH) is Slice 3, and material/scene wiring is Slice 4. RGB (3 channels), not spectral.

**Tech Stack:** C++20, `yr_test.hpp` micro-framework, CMake + MSVC, CTest.

**Base branch:** local `main` (now at `8428bb1`, after Slice 1 merged). **Worktree:** `m4-subsurface-slice2` (create off local `main`).

**Build & test (from worktree root):**
```
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```
Unit suite = `yaoray_tests` target (binary `build/Release/yaoray_tests.exe`). After Slice 1 the suite has **310 tests**. A clangd "stale index" (file-not-found / no-member / "no member clamp in std") is a known FALSE POSITIVE — only the MSVC build + ctest are authoritative.

---

## Context the implementer needs

- **Slice 1 surface already on `main`** (`include/yaoray/render/bssrdf.hpp`): free functions `FrDielectric(cos,eta)`, `HenyeyGreenstein(cos,g)`, `FresnelMoment1(eta)`, `FresnelMoment2(eta)`, `BeamDiffusionMS/SS(...)`, and:
  ```cpp
  struct BSSRDFTable {
      int n_rho = 0;
      int n_radius = 0;
      std::vector<float> rho_samples;     // [n_rho]
      std::vector<float> radius_samples;  // [n_radius]
      std::vector<float> profile;         // [n_rho*n_radius] = 2*pi*r*(MS+SS)
      std::vector<float> rho_eff;         // [n_rho]
      std::vector<float> profile_cdf;     // [n_rho*n_radius]
      BSSRDFTable(int n_rho_samples, int n_radius_samples);
  };
  void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable& table);
  ```
- **Slice 1 Catmull-Rom utilities** (`include/yaoray/render/catmull_rom.hpp`): `bool CatmullRomWeights(int n, const float* nodes, float x, int& offset, float weights[4])`, `float IntegrateCatmullRom(...)`, `float InvertCatmullRom(...)`. `src/render/catmull_rom.cpp` already has a file-local `template<typename Pred> int FindInterval(int n, Pred pred)` in its anonymous namespace — reuse it for `SampleCatmullRom2D`.
- **`Color3f`** (`include/yaoray/core/vec.hpp`) is `Vec3f` with fields `.x/.y/.z`; it has scalar `*`,`/`,`+`,`-` and `Dot`, but **no component-wise `Color3f*Color3f` and no `Clamp`**. Work per-channel with explicit `.x/.y/.z` and `std::max(0.0f, …)`.
- **`Pi`/`Inv4Pi`/`SafeSqrt`** live in `src/render/bssrdf.cpp`'s anonymous namespace already. New code in that file reuses them.
- **Faithfulness:** transcribe the pbrt math exactly — the `2*pi*rOptical` Jacobian, the `sigma_t*sigma_t` rendering-unit factor, the `1/eta` inside `FresnelMoment1` in `Sw`, and the `SampleCatmullRom2D` Horner forms must match pbrt so Slice 5 can compare against a pbrt reference render.

---

## File Structure

| File | Change |
|---|---|
| `include/yaoray/render/catmull_rom.hpp` | Add `SampleCatmullRom2D` declaration. |
| `src/render/catmull_rom.cpp` | Add `SampleCatmullRom2D` (reuses file-local `FindInterval` + `CatmullRomWeights`); add `#include <cmath>`. |
| `include/yaoray/render/bssrdf.hpp` | Add `BSSRDFTable::EvalProfile(int,int)` accessor; add `struct TabulatedBSSRDF` (full declaration); add `#include <yaoray/core/vec.hpp>` and `#include <cstddef>`. |
| `src/render/bssrdf.cpp` | Add `#include <yaoray/render/catmull_rom.hpp>` (if not already from Slice 1 — it is) and implement `TabulatedBSSRDF` methods. |
| `tests/bssrdf_eval_tests.cpp` | New — `TabulatedBSSRDF` evaluation tests (Task 1). |
| `tests/catmull_rom_2d_tests.cpp` | New — `SampleCatmullRom2D` tests (Task 2). |
| `tests/bssrdf_sample_tests.cpp` | New — `Sample_Sr`/`Pdf_Sr` tests (Task 3). |
| `CMakeLists.txt` | Register the three new test files in `yaoray_tests`. |

---

### Task 1: TabulatedBSSRDF construction + Sr / Sw / Sp / S

**Files:**
- Modify: `include/yaoray/render/bssrdf.hpp` (add `EvalProfile` + full `TabulatedBSSRDF` declaration + includes)
- Modify: `src/render/bssrdf.cpp` (implement constructor + `Sr`/`Sw`/`Sp`/`S`)
- Create: `tests/bssrdf_eval_tests.cpp`
- Modify: `CMakeLists.txt` (add `tests/bssrdf_eval_tests.cpp` to `yaoray_tests`)

`Sample_Sr`/`Pdf_Sr` are declared here (so the header is stable) but implemented in Task 3.

- [ ] **Step 1: Extend the header**

In `include/yaoray/render/bssrdf.hpp`, change the include block at the top from:
```cpp
#include <vector>
```
to:
```cpp
#include <cstddef>
#include <vector>

#include <yaoray/core/vec.hpp>
```

Add an accessor inside `struct BSSRDFTable` (after the `BSSRDFTable(int, int);` constructor line, before the closing `};`):
```cpp
    // profile[rho_index * n_radius + radius_index]
    float EvalProfile(int rho_index, int radius_index) const {
        return profile[(std::size_t)rho_index * n_radius + radius_index];
    }
```

Add the `TabulatedBSSRDF` declaration after `void ComputeBeamDiffusionBSSRDF(...)`, before the closing `}  // namespace yr`:
```cpp
// A separable, tabulated BSSRDF instance for one shading point and medium.
// Faithful port of pbrt-v4's TabulatedBSSRDF, restricted to RGB. Constructed from
// per-channel absorption/scattering coefficients, the relative IOR, and a
// precomputed BSSRDFTable (built once per (g, eta) by ComputeBeamDiffusionBSSRDF).
// Slice 2 implements the radial profile (Sr), directional term (Sw), spatial term
// (Sp == Sr of the surface distance), the combined value (S), and 1-D radius
// importance sampling (Sample_Sr / Pdf_Sr). Exit-point sampling (Sample_Sp) and
// scene wiring are later slices.
struct TabulatedBSSRDF {
    Color3f sigma_t;          // sigma_a + sigma_s, per channel
    Color3f rho;              // sigma_s / sigma_t, per channel (0 where sigma_t==0)
    float eta = 1.0f;         // relative IOR
    const BSSRDFTable* table = nullptr;

    TabulatedBSSRDF(const Color3f& sigma_a, const Color3f& sigma_s, float eta,
                    const BSSRDFTable& table);

    // Radial diffusion profile at surface distance r, per channel (rendering units).
    Color3f Sr(float r) const;

    // Normalized directional term for an exit/entry direction with cosine
    // cos_theta to the surface normal. Scalar (depends only on eta).
    float Sw(float cos_theta) const;

    // Spatial term: equals Sr of the surface distance between entry and exit.
    Color3f Sp(float r) const { return Sr(r); }

    // Full separable BSSRDF value: (1 - Fr(cos_theta_o)) * Sp(r) * Sw(cos_theta_i).
    Color3f S(float cos_theta_o, float r, float cos_theta_i) const;

    // Importance-sample a surface radius for channel ch (0..2) from u in [0,1).
    // Returns a non-negative radius, or -1 if that channel is non-scattering.
    float Sample_Sr(int ch, float u) const;

    // Area-measure pdf of sampling radius r on channel ch. Integrates to 1 over
    // the disk: integral over r of Pdf_Sr(ch,r) * 2*pi*r dr == 1.
    float Pdf_Sr(int ch, float r) const;
};
```

- [ ] **Step 2: Write the failing evaluation tests**

Create `tests/bssrdf_eval_tests.cpp`:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <yaoray/render/catmull_rom.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

// Build a shared table once (g=0, eta=1.33), reused across tests.
static const yr::BSSRDFTable& SkinTable() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

// Sr is non-negative, finite, and decays with radius for a scattering medium.
YR_TEST(bssrdf_sr_nonnegative_and_decays) {
    yr::TabulatedBSSRDF s({0.0011f, 0.0024f, 0.014f}, {2.55f, 3.21f, 3.77f}, 1.33f, SkinTable());
    yr::Color3f near_r = s.Sr(0.005f);
    yr::Color3f far_r = s.Sr(0.5f);
    YR_EXPECT_TRUE(std::isfinite(near_r.x) && near_r.x >= 0.0f);
    YR_EXPECT_TRUE(std::isfinite(far_r.x) && far_r.x >= 0.0f);
    YR_EXPECT_TRUE(near_r.x > far_r.x);  // closer to entry = stronger
}

// Sw is normalized: integral over the hemisphere of Sw(cos) * cos dω == 1.
// integral = 2*pi * integral_0^{pi/2} Sw(cos t) cos t sin t dt.
YR_TEST(bssrdf_sw_normalized) {
    yr::TabulatedBSSRDF s({0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    const int N = 100000;
    double acc = 0.0;
    for (int i = 0; i < N; ++i) {
        double theta = (i + 0.5) / N * (kPi / 2.0);
        double cos_t = std::cos(theta), sin_t = std::sin(theta);
        acc += s.Sw((float)cos_t) * cos_t * sin_t;
    }
    double integral = 2.0 * kPi * acc * (kPi / 2.0) / N;
    YR_EXPECT_NEAR((float)integral, 1.0f, 2e-2f);
}

// Profile normalization: the disk integral of Sr equals the table's effective
// albedo at this channel's rho. integral_0^Rmax Sr(r) * 2*pi*r dr ~= rhoEff(rho).
YR_TEST(bssrdf_sr_integrates_to_rho_eff) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    const yr::BSSRDFTable& t = SkinTable();

    // Expected: interpolate table.rho_eff at rho.x via Catmull-Rom weights.
    int off; float w[4] = {0, 0, 0, 0};
    YR_EXPECT_TRUE(yr::CatmullRomWeights(t.n_rho, t.rho_samples.data(), s.rho.x, off, w));
    float rho_eff = 0;
    for (int k = 0; k < 4; ++k) {
        int idx = off + k;
        if (idx >= 0 && idx < t.n_rho) rho_eff += w[k] * t.rho_eff[idx];
    }

    // Numerically integrate Sr over the disk up to the full profile support.
    double r_max = (double)t.radius_samples[t.n_radius - 1] / s.sigma_t.x;
    const int M = 20000;
    double acc = 0.0;
    for (int i = 0; i < M; ++i) {
        double r = (i + 0.5) / M * r_max;
        acc += s.Sr((float)r).x * 2.0 * kPi * r;
    }
    double integral = acc * r_max / M;
    YR_EXPECT_NEAR((float)integral, rho_eff, 3e-2f);
}

// S combines the entry Fresnel transmission, the spatial term, and Sw.
YR_TEST(bssrdf_s_combines_terms) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, SkinTable());
    float cos_o = 0.8f, cos_i = 0.6f, r = 0.05f;
    yr::Color3f full = s.S(cos_o, r, cos_i);
    float ft = 1.0f - yr::FrDielectric(cos_o, 1.33f);
    float expected_x = ft * s.Sp(r).x * s.Sw(cos_i);
    YR_EXPECT_NEAR(full.x, expected_x, 1e-6f * expected_x + 1e-9f);
    YR_EXPECT_TRUE(full.x >= 0.0f && std::isfinite(full.x));
}
```

- [ ] **Step 3: Register the test in CMake, build to confirm RED**

In `CMakeLists.txt`, add to `add_executable(yaoray_tests …)` (after `tests/bssrdf_tests.cpp`):
```cmake
    tests/bssrdf_eval_tests.cpp
```
Run: `cmake -S . -B build -DBUILD_TESTING=ON && cmake --build build --config Release`
Expected: **build FAILS** — `TabulatedBSSRDF` methods unresolved. Red state.

- [ ] **Step 4: Implement the constructor + Sr / Sw / Sp / S**

In `src/render/bssrdf.cpp`, confirm the include block has `#include <yaoray/render/catmull_rom.hpp>` (Slice 1's Task 4 added it; if absent, add it). Then add, immediately before the closing `}  // namespace yr`:

```cpp
TabulatedBSSRDF::TabulatedBSSRDF(const Color3f& sigma_a, const Color3f& sigma_s,
                                 float eta_, const BSSRDFTable& table_)
    : eta(eta_), table(&table_) {
    sigma_t = Color3f{sigma_a.x + sigma_s.x, sigma_a.y + sigma_s.y, sigma_a.z + sigma_s.z};
    rho = Color3f{sigma_t.x != 0 ? sigma_s.x / sigma_t.x : 0.0f,
                  sigma_t.y != 0 ? sigma_s.y / sigma_t.y : 0.0f,
                  sigma_t.z != 0 ? sigma_s.z / sigma_t.z : 0.0f};
}

Color3f TabulatedBSSRDF::Sr(float r) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};
    float out[3] = {0.0f, 0.0f, 0.0f};

    for (int ch = 0; ch < 3; ++ch) {
        // Unitless optical radius.
        float rOptical = r * st[ch];

        int rhoOffset, radiusOffset;
        float rhoW[4], radiusW[4];
        if (!CatmullRomWeights(table->n_rho, table->rho_samples.data(), rh[ch], rhoOffset, rhoW) ||
            !CatmullRomWeights(table->n_radius, table->radius_samples.data(), rOptical, radiusOffset, radiusW))
            continue;

        // 2-D Catmull-Rom interpolation of the profile. Boundary weights are
        // exactly 0, so out-of-range (offset+i)/(offset+j) indices are never read.
        float sr = 0;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float weight = rhoW[i] * radiusW[j];
                if (weight != 0)
                    sr += weight * table->EvalProfile(rhoOffset + i, radiusOffset + j);
            }
        }

        // Cancel the 2*pi*r marginal-pdf factor baked into the tabulated profile.
        if (rOptical != 0) sr /= 2 * Pi * rOptical;
        out[ch] = sr;
    }

    // Transform into rendering-space units (* sigma_t^2) and clamp to >= 0.
    return Color3f{std::max(0.0f, out[0] * st[0] * st[0]),
                   std::max(0.0f, out[1] * st[1] * st[1]),
                   std::max(0.0f, out[2] * st[2] * st[2])};
}

float TabulatedBSSRDF::Sw(float cos_theta) const {
    float c = 1 - 2 * FresnelMoment1(1.0f / eta);
    return (1 - FrDielectric(cos_theta, eta)) / (c * Pi);
}

Color3f TabulatedBSSRDF::S(float cos_theta_o, float r, float cos_theta_i) const {
    float ft = 1 - FrDielectric(cos_theta_o, eta);
    Color3f sp = Sp(r);
    float sw = Sw(cos_theta_i);
    return Color3f{ft * sp.x * sw, ft * sp.y * sw, ft * sp.z * sw};
}
```

- [ ] **Step 5: Build + run, verify GREEN**

Run: `cmake --build build --config Release && ctest --test-dir build --output-on-failure -C Release`
Expected: the four `bssrdf_sr_*`/`bssrdf_sw_*`/`bssrdf_s_*` tests PASS; full suite green (314 total). If `bssrdf_sr_integrates_to_rho_eff` or `bssrdf_sw_normalized` fails, investigate the transcription (Jacobian factor, `sigma_t^2`, the `1/eta` in `Sw`) — do NOT loosen tolerances or alter the formulas to force a pass; report your reasoning.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/bssrdf.hpp src/render/bssrdf.cpp tests/bssrdf_eval_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add TabulatedBSSRDF Sr/Sw/Sp/S evaluation (M4 slice 2)"
```

---

### Task 2: SampleCatmullRom2D utility

**Files:**
- Modify: `include/yaoray/render/catmull_rom.hpp` (declare `SampleCatmullRom2D`)
- Modify: `src/render/catmull_rom.cpp` (implement it; add `#include <cmath>`)
- Create: `tests/catmull_rom_2d_tests.cpp`
- Modify: `CMakeLists.txt` (add the test)

`SampleCatmullRom2D` inverts the radial CDF of a 2-D `(alpha, x)` table at a fixed `alpha` (blended across the 4 alpha-rows by Catmull-Rom weights). `Sample_Sr` (Task 3) calls it with `alpha = rho[ch]`.

- [ ] **Step 1: Declare in the header**

In `include/yaoray/render/catmull_rom.hpp`, add before the closing `}  // namespace yr`:
```cpp
// Importance-sample x from a 2-D Catmull-Rom table conditioned on `alpha`.
// nodes1 (size1) are the alpha nodes; nodes2 (size2) are the x nodes; `values`
// and `cdf` are row-major [size1 * size2] (function samples and their cumulative
// integral along x, as produced by IntegrateCatmullRom per row). Returns the
// sampled x. If `fval`/`pdf` are non-null, they receive the interpolated function
// value at the sample and the normalized pdf (fval / total-row-integral).
float SampleCatmullRom2D(int size1, int size2, const float* nodes1, const float* nodes2,
                         const float* values, const float* cdf, float alpha, float u,
                         float* fval = nullptr, float* pdf = nullptr);
```

- [ ] **Step 2: Write the failing tests**

Create `tests/catmull_rom_2d_tests.cpp`:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/catmull_rom.hpp>
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

// Use the real BSSRDF table as the 2-D distribution under test.
static const yr::BSSRDFTable& Table() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

// Sampled x stays within the node range, and pdf is positive & finite.
YR_TEST(sample_catmullrom2d_in_range) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];  // an interior albedo
    for (float u : {0.05f, 0.25f, 0.5f, 0.75f, 0.95f}) {
        float pdf = -1.0f;
        float x = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                         t.radius_samples.data(), t.profile.data(),
                                         t.profile_cdf.data(), alpha, u, nullptr, &pdf);
        YR_EXPECT_TRUE(x >= t.radius_samples[0] - 1e-4f);
        YR_EXPECT_TRUE(x <= t.radius_samples[t.n_radius - 1] + 1e-3f);
        YR_EXPECT_TRUE(std::isfinite(pdf) && pdf >= 0.0f);
    }
}

// The inverse CDF is monotonic in u: larger u maps to a larger (or equal) x.
YR_TEST(sample_catmullrom2d_monotonic_in_u) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];
    float prev = -1.0f;
    for (int i = 1; i < 20; ++i) {
        float u = i / 20.0f;
        float x = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                         t.radius_samples.data(), t.profile.data(),
                                         t.profile_cdf.data(), alpha, u);
        YR_EXPECT_TRUE(x >= prev - 1e-4f);
        prev = x;
    }
}

// u near 0 maps near the first node; u near 1 maps near the last node.
YR_TEST(sample_catmullrom2d_endpoints) {
    const yr::BSSRDFTable& t = Table();
    float alpha = t.rho_samples[60];
    float lo = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                      t.radius_samples.data(), t.profile.data(),
                                      t.profile_cdf.data(), alpha, 1e-4f);
    float hi = yr::SampleCatmullRom2D(t.n_rho, t.n_radius, t.rho_samples.data(),
                                      t.radius_samples.data(), t.profile.data(),
                                      t.profile_cdf.data(), alpha, 0.9999f);
    YR_EXPECT_TRUE(lo < hi);
    YR_EXPECT_NEAR(lo, t.radius_samples[0], 5e-2f);
}
```

- [ ] **Step 3: Register in CMake, build to confirm RED**

Add `    tests/catmull_rom_2d_tests.cpp` to `yaoray_tests`. Build; expect FAIL (unresolved `SampleCatmullRom2D`).

- [ ] **Step 4: Implement `SampleCatmullRom2D`**

In `src/render/catmull_rom.cpp`, add `#include <cmath>` to the include block (next to `#include <algorithm>`). Then add, before the closing `}  // namespace yr`:

```cpp
float SampleCatmullRom2D(int size1, int size2, const float* nodes1, const float* nodes2,
                         const float* values, const float* cdf, float alpha, float u,
                         float* fval, float* pdf) {
    // Interpolation weights for the alpha parameter (selects 4 rows).
    int offset;
    float weights[4];
    if (!CatmullRomWeights(size1, nodes1, alpha, offset, weights)) return 0;

    // Blend the 4 alpha-rows at column idx. Boundary weights are 0, so
    // (offset+i) out-of-range rows are never read.
    auto interpolate = [&](const float* array, int idx) {
        float value = 0;
        for (int i = 0; i < 4; ++i)
            if (weights[i] != 0)
                value += array[(offset + i) * size2 + idx] * weights[i];
        return value;
    };

    // Map u to a spline interval by inverting the interpolated cdf.
    float maximum = interpolate(cdf, size2 - 1);
    u *= maximum;
    int idx = FindInterval(size2, [&](int i) { return interpolate(cdf, i) <= u; });

    float f0 = interpolate(values, idx), f1 = interpolate(values, idx + 1);
    float x0 = nodes2[idx], x1 = nodes2[idx + 1];
    float width = x1 - x0;

    // Re-scale u using the interpolated cdf at the interval start.
    u = (u - interpolate(cdf, idx)) / width;

    // Approximate derivatives via finite differences of the interpolant.
    float d0, d1;
    if (idx > 0)
        d0 = width * (f1 - interpolate(values, idx - 1)) / (x1 - nodes2[idx - 1]);
    else
        d0 = f1 - f0;
    if (idx + 2 < size2)
        d1 = width * (interpolate(values, idx + 2) - f0) / (nodes2[idx + 2] - x0);
    else
        d1 = f1 - f0;

    // Invert the definite integral of the cubic to find t in [0,1].
    float t = (f0 != f1) ? (f0 - std::sqrt(std::max(0.0f, f0 * f0 + 2 * u * (f1 - f0)))) / (f0 - f1)
                         : (f0 != 0 ? u / f0 : 0.0f);
    float a = 0, b = 1, Fhat, fhat;
    while (true) {
        // Fall back to a bisection step when t is out of bounds.
        if (!(t >= a && t <= b)) t = 0.5f * (a + b);

        // Evaluate the integral F(t) and integrand f(t) in Horner form.
        Fhat = t * (f0 +
                    t * (0.5f * d0 +
                         t * ((1.0f / 3.0f) * (-2 * d0 - d1) + f1 - f0 +
                              t * (0.25f * (d0 + d1) + 0.5f * (f0 - f1)))));
        fhat = f0 +
               t * (d0 +
                    t * (-2 * d0 - d1 + 3 * (f1 - f0) +
                         t * (d0 + d1 + 2 * (f0 - f1))));

        // Stop when converged.
        if (std::abs(Fhat - u) < 1e-6f || b - a < 1e-6f) break;

        // Update bisection bounds and take a Newton step.
        if (Fhat - u < 0)
            a = t;
        else
            b = t;
        t -= (Fhat - u) / fhat;
    }

    if (fval) *fval = fhat;
    if (pdf) *pdf = (maximum != 0) ? fhat / maximum : 0.0f;
    return x0 + width * t;
}
```

Note on the initial `t` guess: pbrt seeds `t` from the quadratic approximation `(f0 - sqrt(f0^2 + 2u(f1-f0)))/(f0-f1)`; the Newton-bisection loop then refines it. The `(f0 != f1)` / `(f0 != 0)` guards keep it finite for degenerate flat segments. This is faithful to pbrt's behavior (pbrt computes the same seed).

- [ ] **Step 5: Build + run, verify GREEN**

Run the suite. Expect the three `sample_catmullrom2d_*` tests PASS; full suite green (317 total).

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/catmull_rom.hpp src/render/catmull_rom.cpp tests/catmull_rom_2d_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add SampleCatmullRom2D CDF inversion (M4 slice 2)"
```

---

### Task 3: Sample_Sr + Pdf_Sr

**Files:**
- Modify: `src/render/bssrdf.cpp` (implement `Sample_Sr` + `Pdf_Sr`)
- Create: `tests/bssrdf_sample_tests.cpp`
- Modify: `CMakeLists.txt` (add the test)

These complete `TabulatedBSSRDF` for the radial dimension: `Sample_Sr` draws a radius via `SampleCatmullRom2D`, and `Pdf_Sr` returns the area-measure pdf consistent with it (so `∫ Pdf_Sr(r)·2πr dr == 1`).

- [ ] **Step 1: Write the failing tests**

Create `tests/bssrdf_sample_tests.cpp`:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

// Sampled radius is valid; pdf there is positive and finite.
YR_TEST(bssrdf_sample_sr_valid) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    for (float u : {0.1f, 0.4f, 0.7f, 0.95f}) {
        float r = s.Sample_Sr(0, u);
        YR_EXPECT_TRUE(r >= 0.0f && std::isfinite(r));
        YR_EXPECT_TRUE(s.Pdf_Sr(0, r) > 0.0f && std::isfinite(s.Pdf_Sr(0, r)));
    }
}

// Non-scattering channel (sigma_s == 0 => sigma_t from sigma_a only, but here make
// the whole channel zero) returns -1 from Sample_Sr.
YR_TEST(bssrdf_sample_sr_nonscattering_channel) {
    yr::TabulatedBSSRDF s({0.0f, 0.02f, 0.02f}, {0.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    YR_EXPECT_NEAR(s.Sample_Sr(0, 0.5f), -1.0f, 1e-6f);  // channel 0 has sigma_t==0
    YR_EXPECT_TRUE(s.Sample_Sr(1, 0.5f) >= 0.0f);
}

// Area-measure pdf integrates to 1 over the disk:
// integral_0^Rmax Pdf_Sr(ch,r) * 2*pi*r dr ~= 1.
YR_TEST(bssrdf_pdf_sr_normalized) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    const yr::BSSRDFTable& t = Tbl();
    double r_max = (double)t.radius_samples[t.n_radius - 1] / s.sigma_t.x;
    const int M = 20000;
    double acc = 0.0;
    for (int i = 0; i < M; ++i) {
        double r = (i + 0.5) / M * r_max;
        acc += s.Pdf_Sr(0, (float)r) * 2.0 * kPi * r;
    }
    double integral = acc * r_max / M;
    YR_EXPECT_NEAR((float)integral, 1.0f, 3e-2f);
}

// Sampling distribution matches the pdf: the radial marginal pdf is
// Pdf_Sr(r)*2*pi*r. Estimate E[1] = (1/N) sum over sampled r_i of
// (radial pdf at r_i) / (radial pdf at r_i) trivially 1, so instead check the
// estimator E[g] for g(r)=1 via importance sampling is unbiased: draw r_i ~ Sr
// sampler and confirm mean of (2*pi*r_i * Pdf_Sr / radialSamplePdf) ... Simpler:
// confirm the sample mean radius is within the bulk of the distribution.
YR_TEST(bssrdf_sample_sr_distribution_sane) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    const int N = 4000;
    double sum_r = 0.0;
    int valid = 0;
    for (int i = 0; i < N; ++i) {
        float u = (i + 0.5f) / N;
        float r = s.Sample_Sr(0, u);
        if (r >= 0.0f) { sum_r += r; ++valid; }
    }
    YR_EXPECT_TRUE(valid == N);
    double mean_r = sum_r / valid;
    YR_EXPECT_TRUE(mean_r > 0.0 && std::isfinite(mean_r));  // strictly positive bulk
}
```

- [ ] **Step 2: Register in CMake, build to confirm RED**

Add `    tests/bssrdf_sample_tests.cpp` to `yaoray_tests`. Build; expect FAIL (unresolved `Sample_Sr`/`Pdf_Sr`).

- [ ] **Step 3: Implement `Sample_Sr` + `Pdf_Sr`**

In `src/render/bssrdf.cpp`, add before the closing `}  // namespace yr`:

```cpp
float TabulatedBSSRDF::Sample_Sr(int ch, float u) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};
    if (st[ch] == 0) return -1;
    float r = SampleCatmullRom2D(table->n_rho, table->n_radius,
                                 table->rho_samples.data(), table->radius_samples.data(),
                                 table->profile.data(), table->profile_cdf.data(),
                                 rh[ch], u);
    return r / st[ch];
}

float TabulatedBSSRDF::Pdf_Sr(int ch, float r) const {
    const float st[3] = {sigma_t.x, sigma_t.y, sigma_t.z};
    const float rh[3] = {rho.x, rho.y, rho.z};

    // Unitless optical radius.
    float rOptical = r * st[ch];

    int rhoOffset, radiusOffset;
    float rhoW[4], radiusW[4];
    if (!CatmullRomWeights(table->n_rho, table->rho_samples.data(), rh[ch], rhoOffset, rhoW) ||
        !CatmullRomWeights(table->n_radius, table->radius_samples.data(), rOptical, radiusOffset, radiusW))
        return 0;

    // Interpolated profile value and the matching effective albedo (normalizer).
    float sr = 0, rhoEff = 0;
    for (int i = 0; i < 4; ++i) {
        if (rhoW[i] == 0) continue;
        rhoEff += table->rho_eff[rhoOffset + i] * rhoW[i];
        for (int j = 0; j < 4; ++j) {
            if (radiusW[j] == 0) continue;
            sr += table->EvalProfile(rhoOffset + i, radiusOffset + j) * rhoW[i] * radiusW[j];
        }
    }
    if (rOptical != 0) sr /= 2 * Pi * rOptical;
    if (rhoEff <= 0) return 0;  // degenerate (non-scattering) guard
    return std::max(0.0f, sr * st[ch] * st[ch] / rhoEff);
}
```

The `rhoEff <= 0` guard is a small, safe addition over pbrt (which assumes `rhoEff > 0`); it keeps the pdf finite for a near-non-scattering channel instead of dividing by ~0. All other math is the faithful pbrt transcription.

- [ ] **Step 4: Build + run, verify GREEN**

Run the suite. Expect the four `bssrdf_*_sr_*`/`bssrdf_pdf_sr_*` tests PASS; full suite green (321 total). If `bssrdf_pdf_sr_normalized` fails, investigate the `sigma_t^2` factor and the `rhoEff` normalizer rather than loosening the tolerance.

- [ ] **Step 5: Commit**

```bash
git add src/render/bssrdf.cpp tests/bssrdf_sample_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add TabulatedBSSRDF radius importance sampling (M4 slice 2)"
```

---

## Self-Review (completed by plan author)

**1. Spec coverage.** Slice 2 of the M4 spec asks for "TabulatedBSSRDF Sr/Sw/Sp/S + radius importance sampling (Sample_Sr/Pdf_Sr)". Task 1 implements Sr/Sw/Sp/S; Task 2 adds the `SampleCatmullRom2D` dependency; Task 3 implements Sample_Sr/Pdf_Sr. The spec's research-grade validation surface is covered: profile normalization (`bssrdf_sr_integrates_to_rho_eff`), Sw normalization (`bssrdf_sw_normalized`), pdf normalization (`bssrdf_pdf_sr_normalized`), and sampler validity. Exit-point sampling (`Sample_Sp`) and material wiring are correctly out of scope (Slices 3–4).

**2. Placeholder scan.** No "TBD"/"handle edge cases"/"similar to". Every code step is complete and compilable; every command states its expected pass/fail outcome. The one prose-only test comment block in `bssrdf_sample_sr_distribution_sane` resolves to a concrete, compilable test (sample-mean sanity) — no placeholder code.

**3. Type consistency.** `TabulatedBSSRDF` is declared once (Task 1) with all five methods; Tasks 1 and 3 implement disjoint subsets of those exact signatures. `SampleCatmullRom2D(int,int,const float*,const float*,const float*,const float*,float,float,float*,float*)` is identical in the header (Task 2 Step 1), the implementation (Task 2 Step 4), and both call sites (`Sample_Sr`, the 2-D tests). `BSSRDFTable::EvalProfile(int,int)` matches its uses in `Sr` and `Pdf_Sr`. `Color3f` member access is `.x/.y/.z` throughout (no nonexistent component-wise operators used).

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-yaoray-m4-subsurface-slice2-implementation-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks. Use opus for Tasks 1 and 2 (profile interpolation + CDF-inversion math), sonnet acceptable for Task 3 (mechanical wiring of the two methods).
2. **Inline Execution** — via `superpowers:executing-plans` with checkpoints.
