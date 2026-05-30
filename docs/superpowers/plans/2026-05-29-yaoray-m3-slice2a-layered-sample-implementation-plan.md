# YaoRay M3 Slice 2a — LayeredBxDF Sample (stochastic walk) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **Task 2 is BSDF math — dispatch it to the most capable available model.**

**Goal:** Make `coateddiffuse` and `coatedconductor` perform real two-layer **stochastic BSDF sampling** (a position-free Monte Carlo walk: clearcoat dielectric interface + Beer-Lambert medium + diffuse/conductor base), replacing the current alias-to-base in `SampleBsdf`. Validated by energy-conservation, coat-changes-appearance, and determinism unit tests plus a committed synthetic scene.

**Architecture:** The walk lives **inside `src/render/bsdf.cpp`** so it can reuse the existing anonymous-namespace helpers (`FresnelDielectric`, `Refract`, `Reflect`, `LambertianBrdf`, conductor GGX) without a risky extraction. It composes sub-materials (a temporary `Dielectric` view for the coat is NOT used — the coat is handled with explicit smooth-Fresnel math; the base is handled by recursively calling the existing `Diffuse`/`Conductor` `SampleBsdf`). `SampleBsdf`'s `CoatedDiffuse`/`CoatedConductor` branches delegate to `SampleLayered`. `EvaluateBsdf`/`PdfBsdf` for coated kinds stay base-aliased in 2a (a documented MIS-consistency gap closed by Slice 2b).

**Tech Stack:** C++20, CMake 3.24, `yr_test.hpp`, CTest. Uses the `yr::Rng` from M3 Slice 1.

---

## Scope (what 2a is and is NOT)

**2a delivers:**
- A stochastic layered **`SampleBsdf`** for `CoatedDiffuse` / `CoatedConductor`: clearcoat reflect/transmit (Russian-roulette by Fresnel), descend through a Beer-Lambert absorbing medium, reflect off the base, ascend, exit-or-internally-reflect at the coat, looping to `maxdepth`.
- Coat + medium parameters compiled onto `RenderMaterial`.
- Energy-conservation (white furnace), coat-changes-appearance, determinism unit tests.
- A committed `coated_showcase` scene + CTest entry.

**2a explicitly does NOT do (deferred):**
- **`EvaluateBsdf` / `PdfBsdf` for coated kinds** — they remain aliased to the base (Diffuse/Conductor) in 2a. This means the path tracer's **light-sampling MIS branch** uses the base f/pdf while the **BSDF-sampling branch** uses the real walk — a documented, bounded inconsistency. **Slice 2b** adds the stochastic f/pdf estimators to close it.
- **Rough (microfacet GGX) coat interfaces** — 2a uses a SMOOTH (perfect-Fresnel) clearcoat. `coating_roughness` is compiled but not yet used by the walk. A rough-coat refinement is a later slice. (The base may be rough; only the coat is smooth in 2a.)
- In-medium phase-function scattering (`g`); N-layer nesting; `mix`/`subsurface`/`measured`.

---

## File Structure

**Modified files:**

| Path | Change |
|------|--------|
| `include/yaoray/render/render_scene.hpp` | Add `coat_thickness` (float, default 0.01), `coat_absorption` (Color3f, default {0,0,0}), `coat_maxdepth` (int, default 10) to `RenderMaterial`. |
| `src/render/scene_compiler.cpp` | `coateddiffuse`/`coatedconductor` branches: read `thickness`/`maxdepth`; derive the conductor base `reflectance` (f0) from `eta`/`k` for coatedconductor. |
| `src/render/bsdf.cpp` | Add `ApplyBeerLambert` + `SampleLayered` in the anonymous namespace; wire `CoatedDiffuse`/`CoatedConductor` in `SampleBsdf` to call `SampleLayered`. `EvaluateBsdf`/`PdfBsdf` coated branches UNCHANGED (stay base-aliased). |
| `tests/layered_bsdf_tests.cpp` (new) | Energy conservation, coat-changes-appearance, determinism. |
| `scenes/pbrt/coated_showcase/coated_showcase.pbrt` (new) | Committed synthetic scene. |
| `scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt` (new) | Low-res CTest variant (mirrors `material_studio_smoke.pbrt`). |
| `CMakeLists.txt` | Register `tests/layered_bsdf_tests.cpp`; add the `yaoray_cli_render_pbrt_coated_showcase` CTest entry. |

**No new .cpp/.hpp for the walk** — it lives in `bsdf.cpp` (rationale: the reusable Fresnel/GGX/Lambertian helpers are in `bsdf.cpp`'s anonymous namespace; extracting them to a shared header is a large, risky refactor of well-tested code, deferred to a dedicated cleanup if `bsdf.cpp` ever needs splitting).

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `d3394d7`, post-PR-#10 merge). Use the harness-native `EnterWorktree` tool with name `m3-slice2-layered-bxdf`.

Baseline check (MSVC multi-config on this machine → `build/Release/`):

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe        # expect 206/206 PASS
cd build && ctest --output-on-failure -C Release   # expect 8/8 PASS
cd ..
```

Note: `yaoray_tests.exe` ignores `--filter`; run the full binary and grep its output.

---

## Task 1: Coat parameters on `RenderMaterial` + compiler wiring

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp` (add fields)
- Modify: `src/render/scene_compiler.cpp` (read params + derive conductor f0)
- Modify: `tests/scene_compiler_*` or a new small test for the compile behavior

The `RenderMaterial` already has `coating_ior` (default 1.5) and `coating_roughness`. Add the medium + walk-control fields. For `coatedconductor`, YaoRay's conductor BSDF uses `reflectance` as the Schlick f0 (not `eta`/`k` directly), so the compiler must derive f0 from `eta`/`k`.

- [ ] **Step 1: Add the fields to `RenderMaterial`**

In `include/yaoray/render/render_scene.hpp`, in `struct RenderMaterial` (after the existing `coating_ior` / `coating_roughness` lines, ~line 104), add:

```cpp
    // Layered (coated*) medium between coat and base. Beer-Lambert absorption
    // only (no in-medium scattering). Defaults: thin clear coat.
    float coat_thickness = 0.01f;
    Color3f coat_absorption{0.0f, 0.0f, 0.0f};   // per-channel; 0 = clear
    int coat_maxdepth = 10;                       // walk bounce cap
```

- [ ] **Step 2: Write a failing compile test**

Create `tests/scene_compiler_coated_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>

namespace {

yr::PbrtScene MakeSceneWithCoatedConductor() {
    yr::PbrtScene pbrt;
    pbrt.source_path = "coated.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A sphere using an inline coatedconductor material.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    shape.material.type = "coatedconductor";
    shape.material.params.push_back(yr::PbrtParam{"rgb", "conductor.eta", {0.2f, 0.92f, 1.1f}, {}, {}, {}});
    shape.material.params.push_back(yr::PbrtParam{"rgb", "conductor.k", {3.9f, 2.45f, 2.14f}, {}, {}, {}});
    shape.material.params.push_back(yr::PbrtParam{"float", "thickness", {0.02f}, {}, {}, {}});
    pbrt.shapes.push_back(shape);

    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_coatedconductor_reads_thickness) {
    const yr::PbrtScene pbrt = MakeSceneWithCoatedConductor();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!result.scene->materials.empty());
    const yr::RenderMaterial& m = result.scene->materials.front();
    YR_EXPECT_EQ(m.kind, yr::RenderMaterialKind::CoatedConductor);
    YR_EXPECT_NEAR(m.coat_thickness, 0.02f, 1e-6f);
}

YR_TEST(scene_compiler_coatedconductor_derives_f0_from_eta_k) {
    // Schlick f0 = ((eta-1)^2 + k^2) / ((eta+1)^2 + k^2) per channel.
    const yr::PbrtScene pbrt = MakeSceneWithCoatedConductor();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials.front();
    auto f0 = [](float eta, float k) {
        const float num = (eta - 1.0f) * (eta - 1.0f) + k * k;
        const float den = (eta + 1.0f) * (eta + 1.0f) + k * k;
        return num / den;
    };
    YR_EXPECT_NEAR(m.reflectance.value.x, f0(0.2f, 3.9f), 1e-4f);
    YR_EXPECT_NEAR(m.reflectance.value.y, f0(0.92f, 2.45f), 1e-4f);
    YR_EXPECT_NEAR(m.reflectance.value.z, f0(1.1f, 2.14f), 1e-4f);
}
```

Register `tests/scene_compiler_coated_tests.cpp` in `CMakeLists.txt`.

**Note:** verify `PbrtShapeRecord`'s inline-material field name (`shape.material`) against the codebase — adapt if the field differs (e.g., `shape.material` vs an attached-material mechanism). If inline shape materials aren't supported in the test fixture, use a `MakeNamedMaterial` + named reference instead, matching how `tests/scene_compiler_*_tests.cpp` build materials.

- [ ] **Step 3: Run to verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -E "coatedconductor|FAIL"
```

Expected: the two new tests FAIL (`coat_thickness` is default 0.01 not 0.02; `reflectance` is default 0.5 not the derived f0).

- [ ] **Step 4: Extend the compiler branches**

In `src/render/scene_compiler.cpp`, the `coateddiffuse` branch (~line 601): add reading of `thickness` and `maxdepth`:

```cpp
    } else if (type == "coateddiffuse") {
        material.kind = RenderMaterialKind::CoatedDiffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
        material.coat_thickness = FloatParam(FindParam(params, "thickness"), 0.01f);
        material.coat_maxdepth = static_cast<int>(FloatParam(FindParam(params, "maxdepth"), 10.0f));
    } else if (type == "coatedconductor") {
        material.kind = RenderMaterialKind::CoatedConductor;
        const Color3f cond_eta = RgbParam(FindParam(params, "conductor.eta"), Color3f{0.2f, 0.92f, 1.1f});
        const Color3f cond_k = RgbParam(FindParam(params, "conductor.k"), Color3f{3.9f, 2.45f, 2.14f});
        material.eta.value = cond_eta;
        material.k.value = cond_k;
        // YaoRay's conductor BSDF uses reflectance as the Schlick f0. Derive it
        // from eta/k: f0 = ((eta-1)^2 + k^2) / ((eta+1)^2 + k^2) per channel.
        auto schlick_f0 = [](float eta, float k) {
            const float num = (eta - 1.0f) * (eta - 1.0f) + k * k;
            const float den = (eta + 1.0f) * (eta + 1.0f) + k * k;
            return den > 0.0f ? num / den : 1.0f;
        };
        material.reflectance.value = Color3f{
            schlick_f0(cond_eta.x, cond_k.x),
            schlick_f0(cond_eta.y, cond_k.y),
            schlick_f0(cond_eta.z, cond_k.z),
        };
        material.uroughness = TexParam1fFromParams(params, "conductor.roughness",
            0.0f, bindings, scene, diagnostics);
        material.vroughness = TexParam1fFromParams(params, "conductor.roughness",
            material.uroughness.value, bindings, scene, diagnostics);
        material.coating_ior = FloatParam(FindParam(params, "eta"), 1.5f);
        material.coating_roughness = TexParam1fFromParams(params, "roughness",
            0.0f, bindings, scene, diagnostics);
        material.coat_thickness = FloatParam(FindParam(params, "thickness"), 0.01f);
        material.coat_maxdepth = static_cast<int>(FloatParam(FindParam(params, "maxdepth"), 10.0f));
    }
```

Verify `RgbParam` exists (it's used elsewhere in scene_compiler.cpp for `LightSource` L); if the helper is named differently, use the existing RGB-param reader.

- [ ] **Step 5: Run to verify pass + full suite**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | tail -5
cd build && ctest --output-on-failure -C Release 2>&1 | tail -5
cd ..
```

Expected: the 2 new coated compile tests PASS; full suite 208/208 (206 + 2); CTest 8/8 (no render behavior changed yet — `SampleBsdf` still aliases coated to base until Task 2).

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp tests/scene_compiler_coated_tests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(material): coat medium params + conductor f0 derivation for coated*

Adds coat_thickness, coat_absorption, coat_maxdepth to RenderMaterial
(Beer-Lambert medium + walk control for the M3 LayeredBxDF). Extends
the coateddiffuse / coatedconductor compiler branches to read
thickness/maxdepth, and derives the conductor base's Schlick f0 from
conductor.eta/conductor.k (YaoRay's conductor BSDF uses reflectance
as f0). No render behavior change yet — SampleBsdf still aliases
coated kinds to their base until the walk lands in the next commit.

All 208 unit tests + 8 CTest entries pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: The stochastic layered walk in `SampleBsdf` (BSDF MATH — use the strongest model)

**Files:**
- Modify: `src/render/bsdf.cpp` (add `ApplyBeerLambert` + `SampleLayered`; wire the two coated branches in `SampleBsdf`)
- Create: `tests/layered_bsdf_tests.cpp`
- Modify: `CMakeLists.txt` (register the test)

**Geometry conventions (verified — implement to these; the tests pin them):**
- World `normal` `n` points to the exterior (air) side. A direction `d` is "above/up/exterior" when `Dot(d, n) > 0`.
- `wo` (input) is above. The coat is a SMOOTH dielectric interface, IOR `coating_ior` (air=1 outside, `ce`=coating_ior in medium).
- The existing `Reflect(d, n)` computes the mirror `d - 2·Dot(d,n)·n`. The existing `Refract(wo, normal, eta, wi)` uses `eta = eta_i/eta_t` and returns false on TIR.
- Direction signs to assert in tests:
  - Coat specular exit reflection of `wo`: `Reflect(-wo, n)` → up (Dot>0).
  - Entry refraction air→medium: `Refract(wo, n, 1.0/ce, w)` → `w` down (Dot<0).
  - Base reflection: call `SampleBsdf(base, -w, n, ...)` with the down-going `w` reversed to the up `-w`; returned `wi` is up (Dot>0).
  - Internal reflection at coat-from-below of an up-going `w`: `Reflect(w, n)` → down (Dot<0). (NOT `Reflect(-w, n)`.)
  - Exit refraction medium→air of up-going `w`: `Refract(w, n, ce/1.0, wexit)` → `wexit` up (Dot>0); may TIR.

**Russian-roulette / energy:** at each coat interaction, draw `rng.NextFloat()`; reflect with probability = Fresnel `F`, transmit with `1-F`. On either branch multiply throughput by `chosen_reflectance / chosen_prob` = 1 (Fresnel cancels), so throughput stays unbiased and energy-conserving by construction.

- [ ] **Step 1: Write the failing tests**

Create `tests/layered_bsdf_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/core/rng.hpp>
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

#include <cmath>

namespace {

yr::Vec3f Up() { return yr::Vec3f{0.0f, 0.0f, 1.0f}; }

yr::RenderMaterial MakeCoatedDiffuse(yr::Color3f base_reflectance) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::CoatedDiffuse;
    m.reflectance.value = base_reflectance;
    m.coating_ior = 1.5f;
    m.coating_roughness.value = 0.0f;
    m.coat_thickness = 0.01f;
    m.coat_absorption = yr::Color3f{0.0f, 0.0f, 0.0f};
    m.coat_maxdepth = 10;
    return m;
}

bool IsFiniteColor(yr::Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

float MaxComp(yr::Color3f c) { return std::max(c.x, std::max(c.y, c.z)); }

} // namespace

YR_TEST(layered_sample_is_energy_conserving_white_furnace) {
    // Coated diffuse, white base (reflectance 1), zero absorption. Average
    // reflected throughput over many samples must not exceed 1 (no energy
    // gain). Bounded loss is fine.
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{1.0f, 1.0f, 1.0f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.3f, 0.0f, 1.0f});

    yr::Rng rng{2024u};
    const int N = 20000;
    yr::Color3f sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (s.valid) {
            YR_EXPECT_TRUE(IsFiniteColor(s.weight));
            sum = sum + s.weight;
        }
    }
    const yr::Color3f mean = sum / static_cast<float>(N);
    // Allow a small Monte-Carlo margin above 1.0.
    YR_EXPECT_TRUE(MaxComp(mean) <= 1.05f);
}

YR_TEST(layered_sample_differs_from_bare_base) {
    // The coat must change appearance: a coated-diffuse sample distribution
    // is not identical to a bare diffuse. We compare mean reflected weight;
    // the coat's Fresnel reflection + absorption-free transport yields a
    // different mean than a bare Lambertian (whose sample weight is exactly
    // the albedo every time).
    yr::RenderMaterial coated = MakeCoatedDiffuse(yr::Color3f{0.5f, 0.5f, 0.5f});
    yr::RenderMaterial bare;
    bare.kind = yr::RenderMaterialKind::Diffuse;
    bare.reflectance.value = yr::Color3f{0.5f, 0.5f, 0.5f};

    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.0f, 1.0f});

    yr::Rng rng_a{7u};
    yr::Rng rng_b{7u};
    const int N = 20000;
    yr::Color3f coated_sum{0, 0, 0};
    yr::Color3f bare_sum{0, 0, 0};
    for (int i = 0; i < N; ++i) {
        const yr::BsdfSample sc = yr::SampleBsdf(coated, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample sb = yr::SampleBsdf(bare, wo, n, rng_b.NextFloat2(), rng_b);
        if (sc.valid) coated_sum = coated_sum + sc.weight;
        if (sb.valid) bare_sum = bare_sum + sb.weight;
    }
    // Means must differ measurably (coat adds a specular reflection lobe).
    const float diff = std::fabs(MaxComp(coated_sum) - MaxComp(bare_sum)) / static_cast<float>(N);
    YR_EXPECT_TRUE(diff > 0.01f);
}

YR_TEST(layered_sample_is_deterministic_under_fixed_seed) {
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.6f, 0.4f, 0.2f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.2f, 0.1f, 1.0f});

    yr::Rng rng_a{999u};
    yr::Rng rng_b{999u};
    for (int i = 0; i < 50; ++i) {
        const yr::BsdfSample a = yr::SampleBsdf(m, wo, n, rng_a.NextFloat2(), rng_a);
        const yr::BsdfSample b = yr::SampleBsdf(m, wo, n, rng_b.NextFloat2(), rng_b);
        YR_EXPECT_EQ(a.valid, b.valid);
        if (a.valid && b.valid) {
            YR_EXPECT_EQ(a.wi.x, b.wi.x);
            YR_EXPECT_EQ(a.wi.y, b.wi.y);
            YR_EXPECT_EQ(a.wi.z, b.wi.z);
            YR_EXPECT_EQ(a.weight.x, b.weight.x);
        }
    }
}

YR_TEST(layered_sample_directions_are_valid) {
    // Every valid sample must return a finite, normalized-ish wi in the
    // upper hemisphere (coated materials are reflective; exit is above).
    yr::RenderMaterial m = MakeCoatedDiffuse(yr::Color3f{0.7f, 0.7f, 0.7f});
    const yr::Vec3f n = Up();
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.4f, 0.2f, 1.0f});
    yr::Rng rng{55u};
    for (int i = 0; i < 5000; ++i) {
        const yr::BsdfSample s = yr::SampleBsdf(m, wo, n, rng.NextFloat2(), rng);
        if (!s.valid) continue;
        YR_EXPECT_TRUE(std::isfinite(s.wi.x) && std::isfinite(s.wi.y) && std::isfinite(s.wi.z));
        YR_EXPECT_TRUE(yr::Dot(s.wi, n) > -1e-4f);   // exits above (or grazing)
        YR_EXPECT_TRUE(s.pdf > 0.0f);
    }
}
```

Register `tests/layered_bsdf_tests.cpp` in `CMakeLists.txt`.

- [ ] **Step 2: Run to verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -E "layered_sample|FAIL"
```

Expected: the layered tests FAIL or behave like a bare base (since `CoatedDiffuse` currently aliases `Diffuse` in `SampleBsdf`). Specifically `layered_sample_differs_from_bare_base` FAILS (coated == bare today).

- [ ] **Step 3: Implement `ApplyBeerLambert` + `SampleLayered` in `bsdf.cpp`**

In `src/render/bsdf.cpp`, inside the anonymous namespace (after `SampleGgxReflection`, before the closing `}` at line 353), add:

```cpp
Color3f ApplyBeerLambert(Color3f throughput, Color3f absorption, float thickness, Vec3f w, Vec3f normal) {
    const float cos = std::max(1.0e-4f, std::fabs(Dot(w, normal)));
    const float dist = thickness / cos;
    return Color3f{
        throughput.x * std::exp(-absorption.x * dist),
        throughput.y * std::exp(-absorption.y * dist),
        throughput.z * std::exp(-absorption.z * dist),
    };
}

// Stochastic two-layer walk for coated* materials (M3 Slice 2a): a SMOOTH
// dielectric clearcoat over a (possibly rough) diffuse/conductor base, with a
// Beer-Lambert absorbing medium between them. Russian-roulette reflect/transmit
// at the coat keeps throughput unbiased. Returns the exit sample. The pdf is a
// proxy (1.0) in 2a — light-sampling MIS for coated kinds is handled in 2b.
BsdfSample SampleLayered(const RenderMaterial& material, Vec3f wo, Vec3f normal, Rng& rng,
                         bool conductor_base) {
    if (!IsAboveSurface(wo, normal)) {
        return BsdfSample{};
    }

    const float ce = std::max(1.0f, material.coating_ior);

    // Build the base sub-material (reuses the existing Diffuse/Conductor BSDF).
    RenderMaterial base;
    if (conductor_base) {
        base.kind = RenderMaterialKind::Conductor;
        base.reflectance = material.reflectance;        // f0 (compiler-derived)
        base.uroughness = material.uroughness;
        base.vroughness = material.vroughness;
    } else {
        base.kind = RenderMaterialKind::Diffuse;
        base.reflectance = material.reflectance;
    }

    // --- Top interface, entering from air (smooth Fresnel) ---
    const float cos_o = std::max(0.0f, Dot(wo, normal));
    const float f_enter = FresnelDielectric(cos_o, 1.0f, ce);
    if (rng.NextFloat() < f_enter) {
        // Coat specular reflection back to exterior.
        const Vec3f wr = Reflect(-wo, normal);
        if (!IsAboveSurface(wr, normal)) {
            return BsdfSample{};
        }
        return BsdfSample{wr, Color3f{1.0f, 1.0f, 1.0f}, 1.0f, true, true};
    }

    // Refract into the medium (downward).
    Vec3f w;
    if (!Refract(wo, normal, 1.0f / ce, w)) {
        // Should not TIR air->denser; guard by reflecting.
        const Vec3f wr = Reflect(-wo, normal);
        return IsAboveSurface(wr, normal)
            ? BsdfSample{wr, Color3f{1.0f, 1.0f, 1.0f}, 1.0f, true, true}
            : BsdfSample{};
    }

    Color3f throughput{1.0f, 1.0f, 1.0f};

    for (int depth = 0; depth < material.coat_maxdepth; ++depth) {
        // Descend through the medium.
        throughput = ApplyBeerLambert(throughput, material.coat_absorption, material.coat_thickness, w, normal);

        // Reflect off the base: down-going w reversed to up-going -w as the
        // base BSDF's "wo"; the base returns an up-going wi.
        const BsdfSample base_sample = SampleBsdf(base, -w, normal, rng.NextFloat2(), rng);
        if (!base_sample.valid || IsBlack(base_sample.weight)) {
            return BsdfSample{};
        }
        throughput = throughput * base_sample.weight;
        w = base_sample.wi;
        if (!IsAboveSurface(w, normal)) {
            return BsdfSample{};   // opaque base must reflect upward
        }

        // Ascend through the medium.
        throughput = ApplyBeerLambert(throughput, material.coat_absorption, material.coat_thickness, w, normal);

        // Coat interface from below (medium -> air). w is up-going.
        const float cos_t = std::max(0.0f, Dot(w, normal));
        const float f_back = FresnelDielectric(cos_t, ce, 1.0f);
        if (rng.NextFloat() < f_back) {
            // Internal reflection: send w back down and continue.
            w = Reflect(w, normal);
            if (IsAboveSurface(w, normal)) {
                return BsdfSample{};   // must go down after internal reflection
            }
            continue;
        }

        // Transmit out to the exterior.
        Vec3f wexit;
        if (!Refract(w, normal, ce, wexit)) {
            // Numerical TIR: reflect back down and continue.
            w = Reflect(w, normal);
            continue;
        }
        if (!IsAboveSurface(wexit, normal)) {
            return BsdfSample{};
        }
        return BsdfSample{wexit, throughput, 1.0f, true, false};
    }

    return BsdfSample{};   // absorbed within maxdepth
}
```

**IMPLEMENTER NOTE:** This reference is written to the verified sign conventions above, but BSDF geometry is pitfall-prone. Treat the four `layered_*` unit tests as the contract. If `layered_sample_directions_are_valid` shows `wi` going below the surface, or energy conservation fails, the likely culprits are: (a) the `Reflect(w, normal)` vs `Reflect(-w, normal)` choice at internal reflection, (b) the `Refract` `eta` argument direction (`1/ce` entering vs `ce` exiting), or (c) the base call using `-w` vs `w`. Adjust to satisfy the tests; do not weaken the tests.

- [ ] **Step 4: Wire the coated branches in `SampleBsdf`**

In `SampleBsdf` (line 428), the `CoatedDiffuse`/`CoatedConductor` cases currently fall through to the Diffuse/Conductor aliases. Split them out to call `SampleLayered`. Change the dispatch so:

```cpp
        case RenderMaterialKind::CoatedDiffuse:
            return SampleLayered(material, wo, normal, rng, /*conductor_base=*/false);
        case RenderMaterialKind::CoatedConductor:
            return SampleLayered(material, wo, normal, rng, /*conductor_base=*/true);
```

Remove `CoatedDiffuse` from the `Diffuse`/`DiffuseTransmission`/`Mix` fall-through group and `CoatedConductor` from the `Conductor` group **in `SampleBsdf` only**. Leave `EvaluateBsdf` and `PdfBsdf` unchanged (coated kinds stay grouped with their base there — the documented 2a MIS gap).

- [ ] **Step 5: Run the layered tests, then the full suite + CTest**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe 2>&1 | grep -E "layered_sample"
./build/Release/yaoray_tests.exe 2>&1 | tail -5
cd build && ctest --output-on-failure -C Release 2>&1 | tail -5
cd ..
```

Expected: all 4 `layered_sample_*` tests PASS; full suite 212/212 (208 + 4); CTest 8/8.

The CTest scenes (`material_studio` uses `coateddiffuse`/`coatedconductor`!) now exercise the real walk. They render via the committed reference compare — the coated spheres will look DIFFERENT from before (real coat instead of alias). If a CTest scene that uses coated materials fails its pixel-compare, that is EXPECTED (the reference was captured with the fake alias). Update that scene's reference image as part of this task: re-render it, eyeball that the coated spheres now show a clearcoat highlight, and refresh the committed reference. Document this in the commit message. (Confirm which CTest scenes use coated materials by grepping the scene files; `material_studio_smoke.pbrt` is the likely one.)

- [ ] **Step 6: Commit**

```bash
git add src/render/bsdf.cpp tests/layered_bsdf_tests.cpp CMakeLists.txt
# plus any refreshed reference image(s):
# git add scenes/pbrt/material_studio/...reference...
git commit -m "$(cat <<'EOF'
feat(bsdf): stochastic layered SampleBsdf for coated* materials

Implements the M3 position-free Monte Carlo walk for CoatedDiffuse /
CoatedConductor in SampleBsdf: a smooth dielectric clearcoat (Fresnel
reflect/transmit via Russian roulette) over a diffuse/conductor base,
with a Beer-Lambert absorbing medium between them and up to maxdepth
internal bounces. Reuses the existing Fresnel/Refract/Reflect helpers
and the base Diffuse/Conductor SampleBsdf; lives in bsdf.cpp to keep
those anonymous-namespace helpers reachable.

coateddiffuse / coatedconductor now do real two-layer evaluation under
BSDF sampling instead of aliasing to their base. Validated by:
- white-furnace energy conservation (mean throughput <= 1)
- coat-changes-appearance (coated != bare base)
- determinism under fixed seed
- valid upper-hemisphere exit directions

Scope (2a): coat is SMOOTH (clearcoat); coat microfacet roughness is
deferred. EvaluateBsdf / PdfBsdf for coated kinds still alias to base
(documented light-sampling MIS gap closed in Slice 2b).

<If a committed scene reference was refreshed:> Refreshes
material_studio's reference image, whose coated spheres now show a real
clearcoat highlight rather than the prior alias-to-base look.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `coated_showcase` synthetic scene + CTest

**Files:**
- Create: `scenes/pbrt/coated_showcase/coated_showcase.pbrt`
- Create: `scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt`
- Modify: `CMakeLists.txt` (CTest entry)

Mirror `scenes/pbrt/material_studio/`. A row of spheres on a diffuse ground under an HDRI (or a couple of analytic lights to avoid an asset dependency — prefer analytic point/distant lights so the scene is self-contained and committable without an env map). At least: one `coateddiffuse` sphere, one `coatedconductor` sphere, and a bare `diffuse` + bare `conductor` sphere beside them for visual comparison.

- [ ] **Step 1: Write the full-quality scene**

Create `scenes/pbrt/coated_showcase/coated_showcase.pbrt`. Use analytic lighting (no external asset) so the scene commits cleanly:

```
# YaoRay M3 Slice 2a — coated material showcase.
# Four spheres: bare diffuse, coateddiffuse, bare conductor, coatedconductor.
# Lit by a distant key + a dim fill, on a diffuse ground. Exercises the
# real two-layer LayeredBxDF (coateddiffuse / coatedconductor).

LookAt 0 1.2 5  0 0.3 0  0 1 0
Camera "perspective" "float fov" [35]
Sampler "independent" "integer pixelsamples" [256]
Integrator "path" "integer maxdepth" [12]
Film "rgb"
    "integer xresolution" [640]
    "integer yresolution" [240]
    "string filename" ["out/coated_showcase.png"]

WorldBegin

LightSource "distant"
    "point3 from" [-3 5 4] "point3 to" [0 0 0]
    "rgb L" [3 3 3]
LightSource "distant"
    "point3 from" [4 2 3] "point3 to" [0 0 0]
    "rgb L" [0.6 0.7 0.9]

# ground
MakeNamedMaterial "ground" "string type" ["diffuse"] "rgb reflectance" [0.5 0.5 0.5]
AttributeBegin
  NamedMaterial "ground"
  Shape "trianglemesh"
    "point3 P" [-10 0 -10  10 0 -10  10 0 10  -10 0 10]
    "integer indices" [0 1 2 0 2 3]
AttributeEnd

# bare diffuse
AttributeBegin
  Material "diffuse" "rgb reflectance" [0.2 0.5 0.8]
  Translate -2.4 0.5 0
  Shape "sphere" "float radius" [0.5]
AttributeEnd

# coateddiffuse (same base color, with a clearcoat)
AttributeBegin
  Material "coateddiffuse" "rgb reflectance" [0.2 0.5 0.8] "float roughness" [0.0] "float eta" [1.5]
  Translate -0.8 0.5 0
  Shape "sphere" "float radius" [0.5]
AttributeEnd

# bare conductor (gold-ish)
AttributeBegin
  Material "conductor" "rgb reflectance" [1.0 0.78 0.34] "float roughness" [0.1]
  Translate 0.8 0.5 0
  Shape "sphere" "float radius" [0.5]
AttributeEnd

# coatedconductor (gold base + clearcoat)
AttributeBegin
  Material "coatedconductor"
    "rgb conductor.eta" [0.18 0.42 1.37] "rgb conductor.k" [3.42 2.35 1.77]
    "float conductor.roughness" [0.1] "float eta" [1.5] "float thickness" [0.02]
  Translate 2.4 0.5 0
  Shape "sphere" "float radius" [0.5]
AttributeEnd
```

(Verify the analytic `distant` light param names against YaoRay's parser — M1 Slice 4 added `distant`; match its exact `from`/`to`/`L` param spelling. If `conductor` expects `eta`/`k` rather than `reflectance`, adjust the bare-conductor sphere to match what the existing `material_studio` conductor uses.)

- [ ] **Step 2: Write the smoke variant**

Create `scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt` — identical but low-res / low-spp for CTest:

```
# Low-resolution smoke variant of coated_showcase for CTest.
```
…with `"integer xresolution" [64] "integer yresolution" [24]`, `"integer pixelsamples" [4]`, and `"string filename" ["out/coated_showcase_smoke.png"]`. Copy the rest verbatim from the full scene.

- [ ] **Step 3: Render the full scene and eyeball it**

```bash
./build/Release/yaoray.exe render scenes/pbrt/coated_showcase/coated_showcase.pbrt --backend cpu 2>&1 | tail -8
```

Open the output PNG. Confirm: four spheres; the coateddiffuse sphere shows a glossy clearcoat highlight on top of its blue base (vs. the flat bare-diffuse sphere beside it); the coatedconductor shows a clearcoat sheen over gold. No NaN/Inf speckle (check the renderer reports no Errors).

- [ ] **Step 4: Register the CTest entry**

In `CMakeLists.txt`, near the other `add_yaoray_cli_render_test(...)` calls (~line 206), add:

```cmake
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_coated_showcase
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/coated_showcase/coated_showcase_smoke.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/coated_showcase/out/coated_showcase_smoke.png")
```

(Match the exact macro signature used by the neighboring entries — verify whether they pass a reference image for pixel-compare or just assert a successful render. Mirror that pattern.)

- [ ] **Step 5: Run CTest**

```bash
cmake -S . -B build
cmake --build build --config Release
cd build && ctest --output-on-failure -C Release 2>&1 | tail -8
cd ..
```

Expected: 9/9 CTest (8 prior + the new coated_showcase). Full unit suite still 212/212.

- [ ] **Step 6: Commit**

```bash
git add scenes/pbrt/coated_showcase/ CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(scenes): coated_showcase synthetic scene + CTest

A committed, self-contained (analytic-lit) scene with a row of
spheres — bare diffuse, coateddiffuse, bare conductor, coatedconductor
— so the real two-layer LayeredBxDF is exercised by CTest and visually
comparable against the bare base materials. Mirrors the material_studio
committed-scene pattern; adds the yaoray_cli_render_pbrt_coated_showcase
CTest entry on the low-res smoke variant.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline d3394d7..HEAD
```

Expected three commits (Task 1 params, Task 2 walk, Task 3 scene).

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin m3-slice2-layered-bxdf
gh pr create --title "feat(bsdf): M3 Slice 2a — stochastic layered SampleBsdf (real coated materials)" --body "$(cat <<'EOF'
## Summary

- `coateddiffuse` / `coatedconductor` now do **real two-layer stochastic BSDF sampling** instead of aliasing to their base BSDF.
- Implements a position-free Monte Carlo walk in `SampleBsdf`: smooth dielectric clearcoat (Russian-roulette Fresnel reflect/transmit) → Beer-Lambert absorbing medium → diffuse/conductor base → ascend → exit-or-internal-reflect, looping to `maxdepth`.
- Reuses the existing Fresnel/Refract/Reflect + base BSDFs; lives in `bsdf.cpp` to keep those anonymous-namespace helpers reachable.
- New coat params on `RenderMaterial` (`coat_thickness`, `coat_absorption`, `coat_maxdepth`); conductor base f0 derived from `conductor.eta`/`conductor.k`.
- New committed `coated_showcase` scene (+ CTest) and `layered_bsdf_tests`.

## Scope (2a)

- **Coat is SMOOTH** (clearcoat). Coat microfacet roughness (`coating_roughness`) is compiled but not yet used by the walk — a later refinement.
- **`EvaluateBsdf` / `PdfBsdf` for coated kinds still alias to base** — the path tracer's light-sampling MIS branch uses the base f/pdf while BSDF-sampling uses the real walk. This is a documented, bounded MIS inconsistency closed by **Slice 2b** (stochastic f/pdf estimators).

## Validation

- White-furnace energy conservation (mean reflected throughput ≤ 1).
- Coat-changes-appearance (coated ≠ bare base).
- Determinism under fixed seed.
- Valid upper-hemisphere exit directions, finite weights.

## Test plan

- [x] `yaoray_tests` — 212/212 PASS (206 + 2 compile + 4 layered)
- [x] `ctest` — 9/9 PASS (8 prior + coated_showcase)
- [x] `coated_showcase` renders; coated spheres show a clearcoat highlight; no NaN/Inf
- [x] material_studio reference refreshed if its coated spheres changed

## Out of scope (subsequent slices)

- Slice 2b: stochastic `f`/`pdf` estimators for coated (light-sampling MIS consistency)
- killeroo-coated integration (Slice 3)
- Rough-coat GGX interfaces; in-medium scattering

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Address review feedback**; re-run tests after each fix.

- [ ] **Step 4: Merge** (operator-gated), then:

```bash
git checkout main
git pull origin main
git worktree remove .worktrees/m3-slice2-layered-bxdf
git branch -D m3-slice2-layered-bxdf
```

---

## Self-Review Notes

- **Spec coverage:** Slice 2 spec items mapped — LayeredBxDF walk (Task 2), coated\* wiring (Task 2 Step 4), coat params + compiler (Task 1), white-furnace/coat-changes/determinism tests (Task 2 Step 1), synthetic scene + CTest (Task 3). The spec's full `f`/`pdf` estimators are deliberately split to **Slice 2b** (user-approved 2a/2b decomposition); 2a documents the interim MIS gap.
- **Deviations from spec, documented:** (1) walk lives in `bsdf.cpp` not a new file (anonymous-namespace helper reuse); (2) coat is smooth in 2a (rough-coat deferred); (3) f/pdf deferred to 2b. All three are called out in the plan's Scope section and the PR body.
- **Placeholder scan:** the walk reference code is complete and written to verified sign conventions; the "implementer note" frames it as test-driven (adjust signs to pass the fully-written tests), not a placeholder. Scene param names carry explicit "verify against the parser" notes because the exact PBRT param spellings (distant light `from`/`to`, conductor `reflectance` vs `eta`/`k`) must match YaoRay's existing usage — the implementer confirms by reading `material_studio.pbrt` and the M1 Slice 4 distant-light support.
- **Type consistency:** `SampleLayered(material, wo, normal, rng, conductor_base)`, `ApplyBeerLambert(...)`, `RenderMaterial::{coat_thickness, coat_absorption, coat_maxdepth, coating_ior, coating_roughness, reflectance, eta, k, uroughness, vroughness}` — all match the actual struct + the bsdf.cpp helper names read during planning. `yr::Rng::NextFloat`/`NextFloat2` match Slice 1.
- **Energy-conservation argument:** Russian-roulette reflect/transmit with probability = Fresnel and weight ×= reflectance/prob = 1 keeps the estimator unbiased; base weight ≤ 1; geometric internal-bounce series bounded → mean throughput ≤ 1. The white-furnace test is the empirical guard.
- **Worktree branch name** `m3-slice2-layered-bxdf` consistent across Setup and Task 4.
