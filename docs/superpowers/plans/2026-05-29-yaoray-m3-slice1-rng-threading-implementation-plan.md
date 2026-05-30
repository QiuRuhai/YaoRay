# YaoRay M3 Slice 1 — RNG Threading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Thread a backend-agnostic random-number source (`Rng`) into YaoRay's three BSDF entry points (`EvaluateBsdf`, `PdfBsdf`, `SampleBsdf`) and every call site, with **zero behavior change** — all 202 unit tests + 8 CTest entries stay green with identical output. This is pure plumbing that sets up M3 Slice 2's stochastic `LayeredBxDF`.

**Architecture:** Introduce a minimal header-only `Rng` (PCG32) in the **core** layer (`include/yaoray/core/rng.hpp`). The render-layer BSDF interface takes `Rng&` as a trailing parameter. The CPU path tracer creates a per-pixel-sample `Rng` (a distinct stream alongside the existing `CpuSampler`) and threads it through `TracePath` → `EstimateDirectLight` / `EstimateDirectEnvironmentLight` → the BSDF calls. No material reads the `Rng` yet, so output is byte-identical.

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. No new third-party libraries.

---

## Why a new core-layer `Rng` (correction to the spec's implementation note)

The M3 spec's decision A said "thread the existing RNG into the BSDF interface; don't introduce a new RNG type." On inspection that is **not possible without a layering violation**:

- The only RNG in the codebase is `CpuSampler` (`include/yaoray/backends/cpu/cpu_sampler.hpp`), which lives in the **CPU backend layer** and already `#include`s `yaoray/render/render_scene.hpp` (it depends on the render layer).
- `EvaluateBsdf` / `PdfBsdf` / `SampleBsdf` live in `src/render/bsdf.cpp` — the **render layer**, which must stay backend-agnostic (a future CUDA backend reuses it).
- Passing `CpuSampler&` into the render-layer BSDF would make render depend on the CPU backend, creating a **circular dependency** (`render → backend → render`).

Therefore Slice 1 introduces a tiny, dependency-free `Rng` in the **core** layer (which both render and backend already depend on). The BSDF takes `Rng&`. `CpuSampler` keeps its own internal RNG for stratified pixel/light dimensions; the path tracer additionally constructs a plain `Rng` per pixel-sample for BSDF-internal randomness. This keeps layering clean and gives Slice 2's layered walk the uncorrelated uniform stream it needs (the walk length is unbounded, so pre-drawn fixed samples — like the existing `Vec2f sample` argument — cannot work).

Because no material consumes the `Rng` in Slice 1, the choice of seed/stream is behavior-neutral here; it only becomes load-bearing in Slice 2.

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `include/yaoray/core/rng.hpp` | Header-only PCG32 `Rng`: `explicit Rng(uint64_t)`, `float NextFloat()` → [0,1), `Vec2f NextFloat2()`. |
| `tests/rng_tests.cpp` | Unit tests: determinism (same seed → same sequence), range [0,1), distinct seeds diverge. |

**Modified files:**

| Path | Change |
|------|--------|
| `include/yaoray/render/bsdf.hpp` | `#include <yaoray/core/rng.hpp>`; add trailing `Rng& rng` to `EvaluateBsdf`, `PdfBsdf`, `SampleBsdf`. `IsDeltaBsdf` unchanged (pure query). |
| `src/render/bsdf.cpp` | Update the three function definitions; mark `rng` `[[maybe_unused]]`; internal switch branches don't touch it. |
| `src/backends/cpu/cpu_path_tracer.cpp` | Thread `Rng& rng` through `TracePath`, `EstimateDirectLight`, `EstimateDirectEnvironmentLight`; construct the per-pixel-sample `Rng`; pass it to all 6 BSDF call sites. |
| `tests/bsdf_tests.cpp` | Add a seeded `yr::Rng` in each test; pass it to all 29 BSDF call sites. |
| `CMakeLists.txt` | Register `tests/rng_tests.cpp` in the `yaoray_tests` source list. |

**Confirmed NOT a call site:** `src/backends/cpu/cpu_debug_renderer.cpp` does not call `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf` (grep returns nothing). Task 2 re-confirms before finishing.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `fec107b`, post-M3-spec commit). Use the harness-native `EnterWorktree` tool with name `m3-slice1-rng-threading`.

Verify the baseline before any change:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/yaoray_tests.exe        # expect 202/202 PASS (or build/Release/yaoray_tests.exe on multi-config)
cd build && ctest --output-on-failure -C Release   # expect 8/8 PASS
cd ..
```

Note: this project uses a single-config generator (Ninja) on the dev sandbox, so the binary is at `build/yaoray_tests.exe` and `build/yaoray.exe`. If a multi-config generator is in use, it's `build/Release/...`. Use whichever exists.

All commits land on the worktree branch.

---

## Task 1: Core `Rng` type + unit tests

**Files:**
- Create: `include/yaoray/core/rng.hpp`
- Create: `tests/rng_tests.cpp`
- Modify: `CMakeLists.txt` (register the test)

A minimal PCG32. Header-only, no vtable (the layered walk in Slice 2 calls it in a hot loop), depends only on `<cstdint>` and `core/vec.hpp`.

- [ ] **Step 1: Write the failing tests**

Create `tests/rng_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/core/rng.hpp>

YR_TEST(rng_same_seed_produces_same_sequence) {
    yr::Rng a{42u};
    yr::Rng b{42u};
    for (int i = 0; i < 16; ++i) {
        YR_EXPECT_EQ(a.NextFloat(), b.NextFloat());
    }
}

YR_TEST(rng_values_lie_in_unit_interval) {
    yr::Rng rng{12345u};
    for (int i = 0; i < 1000; ++i) {
        const float v = rng.NextFloat();
        YR_EXPECT_TRUE(v >= 0.0f);
        YR_EXPECT_TRUE(v < 1.0f);
    }
}

YR_TEST(rng_distinct_seeds_diverge) {
    yr::Rng a{1u};
    yr::Rng b{2u};
    bool diverged = false;
    for (int i = 0; i < 8; ++i) {
        if (a.NextFloat() != b.NextFloat()) {
            diverged = true;
            break;
        }
    }
    YR_EXPECT_TRUE(diverged);
}

YR_TEST(rng_next_float2_advances_stream) {
    // NextFloat2 must draw two fresh values, equal to two NextFloat calls
    // on an identically-seeded generator.
    yr::Rng pair_rng{777u};
    yr::Rng scalar_rng{777u};
    const yr::Vec2f pair = pair_rng.NextFloat2();
    const float x = scalar_rng.NextFloat();
    const float y = scalar_rng.NextFloat();
    YR_EXPECT_EQ(pair.x, x);
    YR_EXPECT_EQ(pair.y, y);
}
```

- [ ] **Step 2: Run to verify it fails (header doesn't exist yet)**

```bash
cmake -S . -B build
cmake --build build --config Release
```

Expected: compile error — `yaoray/core/rng.hpp` not found / `yr::Rng` undeclared. (Also requires Step 3's CMake registration to even attempt compiling the new test; if the test isn't registered yet the failure is "test not run" — either way it's not passing.)

- [ ] **Step 3: Create the `Rng` header**

Create `include/yaoray/core/rng.hpp`:

```cpp
#pragma once

#include <cstdint>

#include <yaoray/core/vec.hpp>

namespace yr {

// Minimal PCG32 random-number generator. Backend-agnostic, header-only,
// no virtual dispatch (stochastic BSDF evaluation calls it in a hot loop).
// Supplies uniform random numbers to stochastic materials (the M3
// LayeredBxDF). Non-layered materials ignore it. Reference: O'Neill 2014,
// "PCG: A Family of Simple Fast Space-Efficient Statistically Good
// Algorithms for Random Number Generation."
class Rng {
public:
    explicit Rng(std::uint64_t seed) {
        state_ = 0u;
        inc_ = (seed << 1u) | 1u;
        Advance();
        state_ += seed;
        Advance();
    }

    // Uniform float in [0, 1) using the top 24 bits (float mantissa width).
    float NextFloat() {
        return static_cast<float>(Advance() >> 8) * (1.0f / 16777216.0f);
    }

    Vec2f NextFloat2() {
        const float x = NextFloat();
        const float y = NextFloat();
        return Vec2f{x, y};
    }

private:
    std::uint32_t Advance() {
        const std::uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        const std::uint32_t xorshifted =
            static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        const std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    std::uint64_t state_ = 0u;
    std::uint64_t inc_ = 1u;
};

} // namespace yr
```

- [ ] **Step 4: Register the test in CMakeLists.txt**

Find the `yaoray_tests` source list (search for an existing entry like `tests/bsdf_tests.cpp`). Add `tests/rng_tests.cpp` alongside it.

- [ ] **Step 5: Build and run the new tests**

```bash
cmake --build build --config Release
./build/yaoray_tests.exe --filter=rng
```

Expected: 4 `rng_*` tests PASS. Then run the full suite to confirm nothing else broke:

```bash
./build/yaoray_tests.exe
```

Expected: 206/206 PASS (202 pre-existing + 4 new).

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/core/rng.hpp tests/rng_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): minimal PCG32 Rng for stochastic BSDF evaluation

Adds a header-only, backend-agnostic Rng (PCG32) in the core layer.
M3's LayeredBxDF (Slice 2) needs an uncorrelated uniform random
stream inside BSDF evaluation, and the walk length is unbounded so
pre-drawn fixed samples can't serve it. The only existing RNG
(CpuSampler) lives in the CPU backend layer and can't be referenced
from the render-layer BSDF without a circular dependency, so the
core layer gets its own tiny generator.

No consumer yet; this commit only adds the type + unit tests
(determinism, [0,1) range, distinct-seed divergence, NextFloat2).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Thread `Rng&` through the BSDF interface + all call sites

**Files:**
- Modify: `include/yaoray/render/bsdf.hpp`
- Modify: `src/render/bsdf.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/bsdf_tests.cpp`

This is one **atomic** change: the signature change forces every caller to update in the same commit (intermediate states don't compile). Zero behavior change — no branch reads `rng`.

- [ ] **Step 1: Update the three signatures in `bsdf.hpp`**

In `include/yaoray/render/bsdf.hpp`, add the include and the trailing `Rng& rng` parameter:

```cpp
#pragma once

#include <yaoray/core/rng.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct BsdfSample {
    Vec3f wi;
    Color3f weight;
    float pdf = 0.0f;
    bool valid = false;
    bool specular = false;
};

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, Rng& rng);

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, Rng& rng);

bool IsDeltaBsdf(const RenderMaterial& material);

} // namespace yr
```

`IsDeltaBsdf` stays unchanged — it's a pure property query, no randomness.

- [ ] **Step 2: Update the three definitions in `bsdf.cpp`**

Change the three function definitions to match the new signatures. The body logic is unchanged; mark the new parameter unused so the compiler doesn't warn:

```cpp
Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, [[maybe_unused]] Rng& rng) {
    // ... existing body unchanged ...
}

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal, [[maybe_unused]] Rng& rng) {
    // ... existing body unchanged ...
}

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample, [[maybe_unused]] Rng& rng) {
    // ... existing body unchanged ...
}
```

Do not change any internal logic, any switch branch, or the CoatedDiffuse/CoatedConductor/Mix aliasing — that all stays exactly as-is in Slice 1.

- [ ] **Step 3: Thread `Rng&` through the path tracer functions**

In `src/backends/cpu/cpu_path_tracer.cpp`:

Add `#include <yaoray/core/rng.hpp>` near the existing includes (it's transitively available through `bsdf.hpp`, but include it directly for clarity).

Add a trailing `Rng& rng` parameter to the three functions that reach BSDF calls:

- `EstimateDirectEnvironmentLight(...)` (declared ~line 342, signature includes `CpuSampler& sampler`) — add `, Rng& rng` after `sampler`.
- `EstimateDirectLight(...)` (declared ~line 394, includes `CpuSampler& sampler`) — add `, Rng& rng` after `sampler`.
- `TracePath(const CpuPreparedScene& prepared_scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats)` (~line 511) — add `, Rng& rng` after `sampler`:
  `Color3f TracePath(const CpuPreparedScene& prepared_scene, Ray3f ray, CpuSampler& sampler, Rng& rng, CpuPathTraceStats& stats)`.

Update the internal calls so the `rng` flows down:
- Inside `TracePath`, the call to `EstimateDirectLight(prepared_scene, material, hit_point, normal, wo, sampler, stats)` (~line 570) becomes `EstimateDirectLight(prepared_scene, material, hit_point, normal, wo, sampler, rng, stats)`.
- Inside `EstimateDirectLight`, the call to `EstimateDirectEnvironmentLight(prepared_scene, material, hit_point, normal, wo, sampler, stats)` (~line 507) becomes `... sampler, rng, stats)`.

Update the 6 BSDF call sites to pass `rng` as the trailing argument:
- Line ~371: `EvaluateBsdf(material, wo, wi, normal)` → `EvaluateBsdf(material, wo, wi, normal, rng)`
- Line ~385: `PdfBsdf(material, wo, wi, normal)` → `PdfBsdf(material, wo, wi, normal, rng)`
- Line ~455: `EvaluateBsdf(material, wo, wi, normal)` → `... , rng)`
- Line ~460: `PdfBsdf(material, wo, wi, normal)` → `... , rng)`
- Line ~487: `EvaluateBsdf(material, wo, sample.wi, normal)` → `... , rng)`
- Line ~577: `SampleBsdf(material, wo, normal, sampler.Next2D())` → `SampleBsdf(material, wo, normal, sampler.Next2D(), rng)`

(Verify exact line numbers by grepping `EvaluateBsdf\|PdfBsdf\|SampleBsdf` in the file — they may shift slightly.)

- [ ] **Step 4: Construct the per-pixel-sample `Rng` and pass it to `TracePath`**

At the per-pixel-sample loop (~line 643, where `CpuSampler sampler{...}` is constructed), construct a distinct `Rng` stream right after the sampler, then pass it to `TracePath`:

```cpp
                    CpuSampler sampler{
                        scene.sampler,
                        SeedForPixelSample(scene.seed, x, y, sample),
                        sample,
                        samples_per_pixel,
                        DirectLightSampleCount(scene)
                    };
                    // Distinct RNG stream for stochastic BSDF evaluation
                    // (M3 LayeredBxDF). Salted so it does not correlate with
                    // CpuSampler's stratified dimensions. No consumer in
                    // Slice 1; load-bearing from Slice 2 on.
                    Rng bsdf_rng{SeedForPixelSample(scene.seed, x, y, sample) ^ 0x9E3779B97F4A7C15ULL};
                    const Vec2f pixel_sample = sampler.NextPixel2D();
                    const Ray3f ray = MakeCameraRay(scene, x, y, pixel_sample.x, pixel_sample.y);
                    Color3f sample_radiance = TracePath(prepared_scene, ray, sampler, bsdf_rng, stats);
```

The `0x9E3779B97F4A7C15` constant is the 64-bit golden-ratio odd constant (a standard hashing salt); it only needs to be a fixed nonzero value to make `bsdf_rng` a distinct, deterministic stream.

- [ ] **Step 5: Update the BSDF unit test call sites**

In `tests/bsdf_tests.cpp` there are 29 call sites across the test functions. For each `YR_TEST` that calls `EvaluateBsdf` / `PdfBsdf` / `SampleBsdf`, declare a seeded `Rng` once at the top of that test body and pass it as the trailing argument. Apply this mechanical transformation:

- `yr::EvaluateBsdf(material, wo, wi, normal)` → `yr::EvaluateBsdf(material, wo, wi, normal, rng)`
- `yr::PdfBsdf(material, wo, wi, normal)` → `yr::PdfBsdf(material, wo, wi, normal, rng)`
- `yr::SampleBsdf(material, wo, normal, SAMPLE)` → `yr::SampleBsdf(material, wo, normal, SAMPLE, rng)`

with a `yr::Rng rng{1u};` declared near the start of each affected test. Concrete examples (covering all three function shapes):

```cpp
// Before:
YR_TEST(diffuse_evaluate_is_lambertian) {
    // ... setup wo, wi, normal, material ...
    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);
    // ... asserts ...
}

// After:
YR_TEST(diffuse_evaluate_is_lambertian) {
    yr::Rng rng{1u};
    // ... setup wo, wi, normal, material ...
    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal, rng);
    // ... asserts ...
}
```

```cpp
// Before:  const float pdf = yr::PdfBsdf(material, wo, wi, normal);
// After:   const float pdf = yr::PdfBsdf(material, wo, wi, normal, rng);
```

```cpp
// Before:  const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});
// After:   const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f}, rng);
```

Add `#include <yaoray/core/rng.hpp>` to the top of `tests/bsdf_tests.cpp` if not already pulled in transitively. The seed value (`1u`) is arbitrary — no test reads the `rng`, so results are unchanged. If a single test calls the BSDF functions multiple times, one `rng` declared at the top of that test is reused across all its calls (fine — the calls don't consume it).

Use grep to enumerate every call site and confirm none are missed:

```bash
grep -n "EvaluateBsdf\|PdfBsdf\|SampleBsdf" tests/bsdf_tests.cpp
```

All 29 must end with `, rng)` (or `, rng);`) after the edit.

- [ ] **Step 6: Confirm `cpu_debug_renderer.cpp` needs no change**

```bash
grep -n "EvaluateBsdf\|PdfBsdf\|SampleBsdf" src/backends/cpu/cpu_debug_renderer.cpp
```

Expected: no matches. If there ARE matches (unexpected), thread an `Rng` through that renderer the same way as the path tracer.

- [ ] **Step 7: Build and run the full suite + CTest**

```bash
cmake --build build --config Release
./build/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **206/206 unit tests PASS** (202 prior + 4 from Task 1) and **8/8 CTest PASS**.

The CTest entries render committed scenes and pixel-compare against references. Since no BSDF branch reads `rng`, the rendered output must be **byte-identical** to before — CTest staying 8/8 green is the zero-behavior-change contract. If any CTest scene fails, the threading accidentally perturbed something (e.g., a stray `sampler.Next2D()` reordering); investigate before continuing.

If a compile error mentions a missed call site, grep again — every `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf` in the tree must now have the trailing `rng` argument.

- [ ] **Step 8: Commit**

```bash
git add include/yaoray/render/bsdf.hpp src/render/bsdf.cpp src/backends/cpu/cpu_path_tracer.cpp tests/bsdf_tests.cpp
git commit -m "$(cat <<'EOF'
refactor(bsdf): thread Rng& through EvaluateBsdf / PdfBsdf / SampleBsdf

Adds a trailing Rng& parameter to the three BSDF entry points and
updates every call site. The render-layer BSDF now carries the
core-layer Rng (added in the previous commit) so M3 Slice 2's
stochastic LayeredBxDF has a random stream inside BSDF evaluation.

Zero behavior change: no material branch reads rng yet (marked
[[maybe_unused]]). The CPU path tracer constructs a per-pixel-sample
Rng stream (salted off the existing per-sample seed, distinct from
CpuSampler's stratified dimensions) and threads it through TracePath
-> EstimateDirectLight / EstimateDirectEnvironmentLight -> the BSDF
calls. IsDeltaBsdf is unchanged (pure query).

All 206 unit tests + 8 CTest entries pass with identical render
output.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline fec107b..HEAD
```

Expected two commits:
1. `feat(core): minimal PCG32 Rng for stochastic BSDF evaluation`
2. `refactor(bsdf): thread Rng& through EvaluateBsdf / PdfBsdf / SampleBsdf`

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin m3-slice1-rng-threading
gh pr create --title "feat(bsdf): M3 Slice 1 - thread Rng into BSDF interface (no behavior change)" --body "$(cat <<'EOF'
## Summary

- Add a minimal header-only `Rng` (PCG32) in the core layer (`include/yaoray/core/rng.hpp`).
- Thread `Rng&` through the three BSDF entry points (`EvaluateBsdf`, `PdfBsdf`, `SampleBsdf`) and every call site.
- Zero behavior change: no material reads the `Rng` yet; render output is byte-identical.

This is M3 Slice 1 (plumbing). It sets up Slice 2's stochastic `LayeredBxDF`, which needs an uncorrelated uniform random stream inside BSDF evaluation.

## Why a new core-layer Rng

The only existing RNG (`CpuSampler`) lives in the CPU backend layer and depends on the render layer. Passing it into the render-layer BSDF would create a circular dependency (render → backend → render). A tiny dependency-free `Rng` in the core layer (which both render and backend already depend on) avoids that. `CpuSampler` keeps its stratified dimensions; the path tracer constructs a distinct per-pixel-sample `Rng` stream for BSDF-internal randomness.

## Test plan

- [x] `yaoray_tests` — 206/206 PASS (202 pre-existing + 4 new Rng tests)
- [x] `ctest --output-on-failure -C Release` — 8/8 PASS (render output byte-identical; the zero-behavior-change contract)

## Out of scope (subsequent slices)

- The `LayeredBxDF` stochastic walk + real `coateddiffuse` / `coatedconductor` (Slice 2)
- killeroo-coated integration (Slice 3)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Address review feedback**

If review surfaces issues, fix on the worktree branch with new commits (no force-push, no amend). Re-run `yaoray_tests` + `ctest` after each fix.

- [ ] **Step 4: Merge**

When the PR is approved and the operator confirms, merge via the GitHub UI. Then locally:

```bash
git checkout main
git pull origin main
git worktree remove .worktrees/m3-slice1-rng-threading
git branch -D m3-slice1-rng-threading
```

Slice 1 is in `main`. Slice 2 (`LayeredBxDF` + coated wiring) can be planned next.

---

## Self-Review Notes

- **Spec coverage:** Slice 1's sole deliverable per the M3 spec ("add a `RNG&` parameter to the three BSDF entry points... every existing material ignores the new parameter... all 202 unit tests + 8 CTest entries stay green") is covered by Tasks 1–2. The spec's "don't introduce a new RNG type" note is explicitly corrected up front (layering would be violated); the deviation is justified and documented.
- **Placeholder scan:** No TBD/TODO. The 29 test-call-site edits are specified as a mechanical transformation with all three function-shape examples shown and a grep to confirm completeness — this is repetition of a fully-specified edit, not a vague reference.
- **Type consistency:** `Rng` (core), `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf` (the real names from `bsdf.hpp`, not the spec's tentative `BsdfPdf`), `CpuSampler`, `TracePath`/`EstimateDirectLight`/`EstimateDirectEnvironmentLight` — all match the actual codebase signatures read during planning. `Rng::NextFloat` / `NextFloat2` names are consistent between the header (Task 1 Step 3) and tests (Task 1 Step 1).
- **Zero-behavior-change guard:** the contract is "CTest 8/8 still green" (pixel-compare against references) plus "206/206 unit tests." Both are explicit in Task 2 Step 7.
- **Worktree branch name** `m3-slice1-rng-threading` consistent across Setup, Task 3 Step 2 push, and Task 3 Step 4 cleanup.
