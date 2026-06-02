# M4 Subsurface Slice 4 — Integrator Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the BSSRDF into the CPU path tracer so `subsurface` materials render real separable subsurface scattering — medium params + table ownership + the `subsurface` compiler branch, a `SubsurfaceExit` Sw-lobe BSDF, the entry/exit/continue integrator path, and white-furnace energy validation.

**Architecture:** A `subsurface` hit is handled as a smooth-dielectric *entry interface* plus a teleport to a BSSRDF-sampled *exit point*. The integrator reflects at the interface with probability `Fr` (the `(1−Fr)` entry-transmission factor is consumed by this probabilistic split, so it is **not** double-counted against `S`'s own `(1−Fr)`); otherwise it enters, pays `β *= Sp/pdf_sp` via `SampleBssrdfProbe`, and treats the exit point `pi` as an ordinary diffuse-like vertex whose BSDF is `SubsurfaceExit` (`f = Sw(wi)`, cosine-sampled). That reuses the existing `EstimateDirectLight` + continue logic unchanged. The entry/exit `1/eta²` radiance factors cancel for the air→medium→air round trip and are dropped.

**Tech Stack:** C++20, `yr_test.hpp`, CMake + MSVC, CTest.

**Base branch:** local `main` (now at `b23cb0c`, after Slice 3 merged). **Worktree:** `m4-subsurface-slice4`.

**Build & test (from worktree root):**
```
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```
After Slice 3 the suite has **330 tests**. clangd "stale index" errors are FALSE POSITIVES — only MSVC build + ctest are authoritative.

---

## Context the implementer needs

- **BSSRDF surface** (`include/yaoray/render/bssrdf.hpp`): `struct TabulatedBSSRDF { TabulatedBSSRDF(const Color3f& sigma_a, const Color3f& sigma_s, float eta, const BSSRDFTable& table); Color3f Sr/Sp(float); float Sw(float cos); Color3f S(...); float Sample_Sr(int,float); float Pdf_Sr(int,float); float Pdf_Sp(...); };`. Free functions `float FrDielectric(float cos, float eta)`, `float FresnelMoment1(float eta)`. `struct BSSRDFTable { BSSRDFTable(int n_rho, int n_radius); ... };`, `void ComputeBeamDiffusionBSSRDF(float g, float eta, BSSRDFTable&)`. `struct BssrdfProbeSample { bool hit; Point3f pi; Vec3f ni; int primitive_index, triangle_index, sphere_index; float bary_u, bary_v; Color3f sp; float pdf; };`, `BssrdfProbeSample SampleBssrdfProbe(const TabulatedBSSRDF&, const RenderSceneIR&, const RenderBvh&, const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns, int target_primitive_index, int target_sphere_index, float u1, const Vec2f& u2);`.
- **Material/IR** (`include/yaoray/render/render_scene.hpp`): `enum class RenderMaterialKind { Diffuse, Conductor, Dielectric, ThinDielectric, CoatedDiffuse, CoatedConductor, DiffuseTransmission, Mix, Measured };`. The measured material owns heap data via, on `RenderMaterial`: `int measured_index = -1; const MeasuredBrdf* measured_brdf = nullptr;` and, on `RenderSceneIR`: `std::vector<std::unique_ptr<MeasuredBrdf>> measured_brdfs;` (with `MeasuredBrdf` forward-declared / included exactly as the existing code does — mirror it for `BSSRDFTable`).
- **BSDF dispatch** (`src/render/bsdf.cpp`): `EvaluateBsdf`/`PdfBsdf`/`SampleBsdf`/`IsDeltaBsdf` are `switch (material.kind)`. The `Diffuse` case is the template for the new `SubsurfaceExit` lobe: Evaluate returns `LambertianBrdf(reflectance)` guarded by `IsAboveSurface(wo,normal)&&IsAboveSurface(wi,normal)`; Pdf returns `max(0,Dot(normal,wi))/Pi`; Sample uses `SampleCosineHemisphere(normal, sample)` and returns `BsdfSample{wi, weight, pdf, valid=true, specular=false}`. `BsdfSample{ Vec3f wi; Color3f weight; float pdf; bool valid; bool specular; }`. File-local helpers available: `Pi`, `SampleCosineHemisphere(normal, sample)`, `IsAboveSurface(w, n)`, `IsBlack(color)`, `LambertianBrdf`. `bsdf.cpp` currently does NOT include `bssrdf.hpp`.
- **scene_compiler** (`src/render/scene_compiler.cpp`): the `subsurface` branch (~line 956) currently degrades to diffuse. The `measured` branch (~line 961) is the ownership template: `ir.measured_brdfs.push_back(std::make_unique<MeasuredBrdf>(...)); material.kind = …; material.measured_index = idx; material.measured_brdf = ir.measured_brdfs[idx].get();`. Param helpers: `FindParam(params,"name")`, `FloatParam(ptr, fallback)`. For RGB spectra use the existing color param reader the `measured`/`coated` branches use (search the file for how a 3-float param like `eta`/`k` is read into a `Color3f` — e.g. `TexParam3fFromParams(params, "name", default, bindings, scene, diagnostics).value`); use `.value` since subsurface coefficients are not textured.
- **Path tracer** (`src/backends/cpu/cpu_path_tracer.cpp`): `Color3f TracePath(const CpuPreparedScene& prepared_scene, Ray3f ray, CpuSampler& sampler, Rng& rng, CpuPathTraceStats& stats)`. Inside: `const RenderSceneIR& scene = prepared_scene.Scene();`, BVH via `prepared_scene.bvh`. Per-bounce locals: `throughput` (Color3f), `radiance` (Color3f), `hit_point`, `wo`, `material`, `normal` (= `surface_hit.sample.shading_normal`), `surface_hit.geometry_hit.{primitive_index,sphere_index}`. File-local helpers: `Multiply(Color3f,Color3f)`, `IsNearBlack(Color3f)`, `SurfaceBias(Point3f)`, `SurviveRussianRoulette(depth, throughput, sampler)`, `DirectLightSampleCount(scene)`, `struct PreviousBounce`, `EstimateDirectLight(prepared_scene, material, point, normal, wo, sampler, rng, stats)`, `IsDeltaBsdf(material)`. `CpuSampler` exposes `Next1D()`, `Next2D()`. The generic per-bounce tail is: sample BSDF → `throughput = Multiply(throughput, bsdf_sample.weight)` → RR → spawn `ray = Ray3f{hit_point + bias_normal*SurfaceBias(hit_point), bsdf_sample.wi}`.
- **Path-tracer test harness** (`tests/cpu_path_tracer_tests.cpp`): `PreparePathScene(RenderSceneIR) → CpuPreparedScene` (via `PrepareCpuScene`), `RunPathTrace(RenderSceneIR[, RenderRequest]) → CpuPathTraceResult`; read pixels via `result.film.LinearPixel(x, y)`. Scenes are built by hand on the IR (materials, vertices/indices/primitives or spheres, `width/height/spp/max_depth/seed/camera`, `environment`).
- **Faithfulness:** the entry split consumes `(1−Fr)` exactly once; `Sw` uses `c = 1 − 2·FresnelMoment1(1/eta)`; the exit lobe weight is `Sw(cosθ)·π = (1−Fr(cosθ))/c`. Keep these exact so Slice 5 matches the pbrt reference.

---

## File Structure

| File | Change |
|---|---|
| `include/yaoray/render/render_scene.hpp` | `RenderMaterialKind::Subsurface` + `::SubsurfaceExit`; `RenderMaterial` fields `sigma_a`, `sigma_s`, `bssrdf_eta`, `bssrdf_index`, `bssrdf_table`; `RenderSceneIR::bssrdf_tables`; forward-decl `struct BSSRDFTable;` (mirror `MeasuredBrdf`). |
| `src/render/bsdf.cpp` | `#include <yaoray/render/bssrdf.hpp>`; add `SubsurfaceExit` cases (Sw lobe) and `Subsurface` cases (inert entry) to Evaluate/Pdf/Sample/IsDelta. |
| `src/render/scene_compiler.cpp` | `#include <yaoray/render/bssrdf.hpp>`; rewrite the `subsurface` branch to build a `BSSRDFTable` + store coefficients (degrade→diffuse only on bad params). |
| `src/backends/cpu/cpu_path_tracer.cpp` | `#include <yaoray/render/bssrdf.hpp>`; add the `Subsurface` entry/exit/continue branch. |
| `tests/scene_compiler_subsurface_tests.cpp` | New (Task 1). |
| `tests/bssrdf_exit_lobe_tests.cpp` | New (Task 1). |
| `tests/cpu_subsurface_tests.cpp` | New (Tasks 2 & 3). |
| `CMakeLists.txt` | Register the three new test files. |

---

### Task 1: Medium params + compiler + SubsurfaceExit lobe

**Files:** `include/yaoray/render/render_scene.hpp`, `src/render/bsdf.cpp`, `src/render/scene_compiler.cpp`, `tests/scene_compiler_subsurface_tests.cpp`, `tests/bssrdf_exit_lobe_tests.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Extend `render_scene.hpp`**

(a) Add two enumerators to `RenderMaterialKind` (after `Measured`):
```cpp
    Subsurface,
    SubsurfaceExit,
```
(b) On `RenderMaterial`, after the `measured_brdf` pointer line, add:
```cpp
    // Subsurface (BSSRDF) medium coefficients + the precomputed diffusion table.
    Color3f sigma_a{0.0f, 0.0f, 0.0f};   // absorption per RGB channel
    Color3f sigma_s{0.0f, 0.0f, 0.0f};   // scattering per RGB channel
    float bssrdf_eta = 1.33f;            // relative IOR of the interface
    int bssrdf_index = -1;               // index into RenderSceneIR::bssrdf_tables
    const BSSRDFTable* bssrdf_table = nullptr;  // stable raw pointer (nullptr unless Subsurface)
```
(c) Add a forward declaration `struct BSSRDFTable;` next to the existing `struct MeasuredBrdf;` forward declaration (find it near the top of the file).
(d) On `RenderSceneIR`, next to `std::vector<std::unique_ptr<MeasuredBrdf>> measured_brdfs;`, add:
```cpp
    // Heap-allocated so each BSSRDFTable address stays stable across vector growth
    // and IR moves; RenderMaterial::bssrdf_table points at one.
    std::vector<std::unique_ptr<BSSRDFTable>> bssrdf_tables;
```
If `render_scene.cpp` (or wherever `RenderSceneIR`'s destructor is emitted) requires the complete `MeasuredBrdf` type via an include, add the analogous `#include <yaoray/render/bssrdf.hpp>` there so `std::unique_ptr<BSSRDFTable>` can be destroyed. (Mirror exactly how `measured_brdfs` is handled — check `git grep "measured_brdf" src include`.)

- [ ] **Step 2: Write the failing exit-lobe tests — create `tests/bssrdf_exit_lobe_tests.cpp`**

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/bssrdf.hpp>
#include <yaoray/core/rng.hpp>
#include <cmath>

constexpr float kPi = 3.14159265358979323846f;

static yr::RenderMaterial ExitMaterial(float eta) {
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::SubsurfaceExit;
    m.ior = eta;
    return m;
}

// The exit lobe is a normalized Fresnel-weighted cosine lobe: f = Sw(cos) > 0
// above the surface, pdf = cos/pi, and the sample weight is finite and ~O(1).
YR_TEST(subsurface_exit_lobe_basic) {
    yr::Rng rng{123};
    yr::RenderMaterial m = ExitMaterial(1.33f);
    yr::Vec3f n{0, 0, 1};
    yr::Vec3f wo{0, 0, 1};
    yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.3f, 0.2f, 0.9f});
    yr::Color3f f = yr::EvaluateBsdf(m, wo, wi, n, rng);
    float pdf = yr::PdfBsdf(m, wo, wi, n, rng);
    YR_EXPECT_TRUE(f.x > 0.0f && std::isfinite(f.x));
    YR_EXPECT_NEAR(pdf, std::max(0.0f, wi.z) / kPi, 1e-5f);
}

// SampleBsdf cosine-samples and returns a valid, finite, non-negative weight.
YR_TEST(subsurface_exit_lobe_sample_valid) {
    yr::Rng rng{7};
    yr::RenderMaterial m = ExitMaterial(1.33f);
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.4f, 0.6f}, rng);
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(!s.specular);
    YR_EXPECT_TRUE(s.weight.x >= 0.0f && std::isfinite(s.weight.x));
    YR_EXPECT_TRUE(s.pdf > 0.0f);
    YR_EXPECT_TRUE(s.wi.z > 0.0f);  // sampled into the upper hemisphere
}

// At eta = 1 there is no Fresnel loss, so the exit lobe is ~white: the cosine-
// sampled weight (= Sw*pi = (1-Fr)/c) is approximately 1 for any direction.
YR_TEST(subsurface_exit_lobe_white_at_eta_one) {
    yr::Rng rng{99};
    yr::RenderMaterial m = ExitMaterial(1.0f);
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.5f, 0.5f}, rng);
    YR_EXPECT_NEAR(s.weight.x, 1.0f, 0.05f);
}

// The entry kind is an inert specular interface for the generic BSDF API: delta,
// no NEE, no usable sample (the integrator handles it specially).
YR_TEST(subsurface_entry_is_delta_and_inert) {
    yr::Rng rng{1};
    yr::RenderMaterial m;
    m.kind = yr::RenderMaterialKind::Subsurface;
    YR_EXPECT_TRUE(yr::IsDeltaBsdf(m));
    yr::Vec3f n{0, 0, 1};
    yr::BsdfSample s = yr::SampleBsdf(m, n, n, yr::Vec2f{0.5f, 0.5f}, rng);
    YR_EXPECT_TRUE(!s.valid);
}
```

- [ ] **Step 3: Write the failing compiler test — create `tests/scene_compiler_subsurface_tests.cpp`**

Mirror an existing `scene_compiler_*` test's structure (open `tests/scene_compiler_measured_tests.cpp` for the exact harness — how it builds a `PbrtScene`/parses a snippet and calls the compiler, and how it asserts on `ir.materials[...]`). Write one test that compiles a `subsurface` material with explicit `sigma_a`/`sigma_s`/`eta` and asserts:
```cpp
// (Adapt the harness to this file's existing pattern; the assertions are:)
//   - the compiled material.kind == yr::RenderMaterialKind::Subsurface
//   - material.bssrdf_table != nullptr
//   - material.sigma_s.x > 0.0f and material.bssrdf_eta == the parsed eta
//   - ir.bssrdf_tables.size() == 1
// Use sigma_a "0.0011 0.0024 0.014", sigma_s "2.55 3.21 3.77", eta 1.33.
```
If matching the exact compiler-test harness is unclear, STOP and ask the controller for the `scene_compiler_measured_tests.cpp` pattern before writing this test.

- [ ] **Step 4: Register all three? (only two new test files in Task 1)** — add `tests/bssrdf_exit_lobe_tests.cpp` and `tests/scene_compiler_subsurface_tests.cpp` to `add_executable(yaoray_tests …)` (after `tests/bssrdf_probe_tests.cpp`). Build; confirm RED (new enum cases / fields / branch missing).

- [ ] **Step 5: Implement the `SubsurfaceExit` + `Subsurface` BSDF cases in `src/render/bsdf.cpp`**

Add `#include <yaoray/render/bssrdf.hpp>` to the include block. Add a file-local helper in the anonymous namespace (near `LambertianBrdf`):
```cpp
// Exit-interface directional term Sw(cos) for a subsurface boundary of relative
// IOR eta: a normalized Fresnel-weighted cosine lobe. Sw(cos) = (1 - Fr(cos)) / (c*Pi),
// c = 1 - 2*FresnelMoment1(1/eta).
float SubsurfaceSw(float cos_theta, float eta) {
    const float c = 1.0f - 2.0f * FresnelMoment1(1.0f / eta);
    return (1.0f - FrDielectric(cos_theta, eta)) / (c * Pi);
}
```
In `EvaluateBsdf`'s switch, add before the final `return Color3f{};`:
```cpp
        case RenderMaterialKind::SubsurfaceExit: {
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            const float sw = SubsurfaceSw(std::max(0.0f, Dot(normal, wi)), material.ior);
            return Color3f{sw, sw, sw};
        }
        case RenderMaterialKind::Subsurface:
            return Color3f{};  // entry interface handled by the integrator
```
In `PdfBsdf`'s switch:
```cpp
        case RenderMaterialKind::SubsurfaceExit:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case RenderMaterialKind::Subsurface:
            return 0.0f;
```
In `SampleBsdf`'s switch:
```cpp
        case RenderMaterialKind::SubsurfaceExit: {
            if (!IsAboveSurface(wo, normal)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            const float pdf = std::max(0.0f, Dot(normal, wi)) / Pi;
            if (pdf <= 0.0f) {
                return BsdfSample{};
            }
            // weight = f * cos / pdf = Sw(cos) * pi.
            const float w = SubsurfaceSw(std::max(0.0f, Dot(normal, wi)), material.ior) * Pi;
            return BsdfSample{wi, Color3f{w, w, w}, pdf, true, false};
        }
        case RenderMaterialKind::Subsurface:
            return BsdfSample{};  // entry interface handled by the integrator
```
In `IsDeltaBsdf`'s switch, add `case RenderMaterialKind::Subsurface:` alongside the other delta kinds (returns `true`). `SubsurfaceExit` must NOT be listed there (it is non-delta; the default tail returns `false`).

- [ ] **Step 6: Implement the `subsurface` compiler branch in `src/render/scene_compiler.cpp`**

Add `#include <yaoray/render/bssrdf.hpp>`. Replace the existing `} else if (type == "subsurface") { … diffuse degrade … }` block with:
```cpp
    } else if (type == "subsurface") {
        // PBRT v4 subsurface: sigma_a / sigma_s spectra (mm^-1), eta (default 1.33).
        const Color3f sigma_a = TexParam3fFromParams(params, "sigma_a",
            Color3f{0.0011f, 0.0024f, 0.014f}, bindings, scene, diagnostics).value;
        const Color3f sigma_s = TexParam3fFromParams(params, "sigma_s",
            Color3f{2.55f, 3.21f, 3.77f}, bindings, scene, diagnostics).value;
        const float eta = FloatParam(FindParam(params, "eta"), 1.33f);

        const bool scattering = sigma_s.x > 0.0f || sigma_s.y > 0.0f || sigma_s.z > 0.0f;
        if (!scattering) {
            diagnostics.push_back(MaterialFallbackWarning(scene, type));
            material.kind = RenderMaterialKind::Diffuse;
            material.reflectance.value = Color3f{0.5f, 0.5f, 0.5f};
        } else {
            ir.bssrdf_tables.push_back(std::make_unique<BSSRDFTable>(100, 64));
            ComputeBeamDiffusionBSSRDF(/*g=*/0.0f, eta, *ir.bssrdf_tables.back());
            const int idx = static_cast<int>(ir.bssrdf_tables.size()) - 1;
            material.kind = RenderMaterialKind::Subsurface;
            material.sigma_a = sigma_a;
            material.sigma_s = sigma_s;
            material.bssrdf_eta = eta;
            material.bssrdf_index = idx;
            material.bssrdf_table = ir.bssrdf_tables[idx].get();
        }
```
(Confirm the IR variable name in this function is `ir` — match the `measured` branch's usage; if it differs, use the same name as the `measured` branch.)

- [ ] **Step 7: Build + run; confirm the new Task-1 tests PASS and the full suite is green.** (Subsurface materials now compile and have an inert/exit BSDF, but the integrator does not yet enter them — that's Task 2.)

- [ ] **Step 8: Commit**
```bash
git add include/yaoray/render/render_scene.hpp src/render/bsdf.cpp src/render/scene_compiler.cpp tests/bssrdf_exit_lobe_tests.cpp tests/scene_compiler_subsurface_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): subsurface medium params + compiler + SubsurfaceExit lobe (M4 slice 4)"
```

---

### Task 2: Integrator SSS path

**Files:** `src/backends/cpu/cpu_path_tracer.cpp`, `tests/cpu_subsurface_tests.cpp`, `CMakeLists.txt`

- [ ] **Step 1: Write the failing integration test — create `tests/cpu_subsurface_tests.cpp`**

Mirror `tests/cpu_path_tracer_tests.cpp`'s harness (`PreparePathScene`, `RunPathTrace`, `result.film.LinearPixel`). Build a scene with ONE sphere whose material is `Subsurface`, owning its own table:

```cpp
#include "yr_test.hpp"
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/bssrdf.hpp>
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <cmath>
#include <memory>

// (If the include paths above differ from cpu_path_tracer_tests.cpp, copy that
//  file's exact includes and helper definitions PreparePathScene/RunPathTrace.)

namespace {

yr::RenderSceneIR MakeSubsurfaceSphereScene(float sigma_a_value, float eta, bool subsurface) {
    yr::RenderSceneIR scene;
    scene.width = 16; scene.height = 16; scene.spp = 16; scene.max_depth = 8; scene.seed = 1;
    // (Set scene.camera exactly as cpu_path_tracer_tests.cpp's MakeBaseScene does —
    //  copy that camera setup so a centered sphere is in view.)
    // Uniform white environment so every escaping ray sees radiance 1.
    scene.environment.active = true;
    scene.environment.radiance = yr::Color3f{1.0f, 1.0f, 1.0f};

    yr::RenderMaterial m;
    if (subsurface) {
        scene.bssrdf_tables.push_back(std::make_unique<yr::BSSRDFTable>(100, 64));
        yr::ComputeBeamDiffusionBSSRDF(0.0f, eta, *scene.bssrdf_tables.back());
        m.kind = yr::RenderMaterialKind::Subsurface;
        m.sigma_a = yr::Color3f{sigma_a_value, sigma_a_value, sigma_a_value};
        m.sigma_s = yr::Color3f{1.0f, 1.0f, 1.0f};
        m.bssrdf_eta = eta;
        m.bssrdf_table = scene.bssrdf_tables.back().get();
    } else {
        m.kind = yr::RenderMaterialKind::Diffuse;
        m.reflectance.value = yr::Color3f{0.8f, 0.8f, 0.8f};
    }
    scene.materials.push_back(m);
    scene.spheres.push_back(yr::RenderSphere{yr::Point3f{0, 0, 0}, 1.0f, 0, -1, false});
    return scene;
}

}  // namespace

// A subsurface sphere under a white environment renders to a finite, non-black,
// no-energy-gain center pixel (the integrator enters and exits the medium).
YR_TEST(cpu_subsurface_renders_finite_nonblack) {
    auto result = RunPathTrace(MakeSubsurfaceSphereScene(/*sigma_a=*/0.01f, /*eta=*/1.33f, /*subsurface=*/true));
    yr::Color3f c = result.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z));
    YR_EXPECT_TRUE(c.x > 0.0f);
    YR_EXPECT_TRUE(c.x <= 1.5f);  // no gross energy gain
}

// Determinism: same seed -> identical center pixel.
YR_TEST(cpu_subsurface_deterministic) {
    auto a = RunPathTrace(MakeSubsurfaceSphereScene(0.01f, 1.33f, true));
    auto b = RunPathTrace(MakeSubsurfaceSphereScene(0.01f, 1.33f, true));
    yr::Color3f ca = a.film.LinearPixel(8, 8), cb = b.film.LinearPixel(8, 8);
    YR_EXPECT_NEAR(ca.x, cb.x, 1e-5f);
}
```
NOTE to implementer: open `tests/cpu_path_tracer_tests.cpp` and copy its exact include set, its `PreparePathScene`/`RunPathTrace` helpers, and its camera setup into this file (or factor a shared helper if the file already exposes one). The scene must put the sphere in front of the camera so pixel (8,8) hits it. If the camera/scene setup is ambiguous, STOP and ask the controller.

- [ ] **Step 2: Register `tests/cpu_subsurface_tests.cpp`** in `CMakeLists.txt` (after the Task-1 test files); build; confirm RED (`cpu_subsurface_renders_finite_nonblack` should currently fail — the subsurface sphere renders black/garbage because the integrator does not enter it yet, OR it asserts because nothing handles the kind).

- [ ] **Step 3: Implement the integrator branch in `src/backends/cpu/cpu_path_tracer.cpp`**

Add `#include <yaoray/render/bssrdf.hpp>` to the include block. In `TracePath`, locate the point AFTER the direct-lighting block and the `if (depth + 1 >= max_depth) break;` guard, and BEFORE the generic `const BsdfSample bsdf_sample = SampleBsdf(...)` line. Insert:

```cpp
        // --- Subsurface (BSSRDF) entry / exit / continue ---
        if (material.kind == RenderMaterialKind::Subsurface && material.bssrdf_table != nullptr) {
            const Vec3f ns = normal;
            // Build an orthonormal frame (ss, ts) about ns.
            Vec3f ss = (std::fabs(ns.x) > 0.9f)
                           ? Normalize(Cross(ns, Vec3f{0.0f, 1.0f, 0.0f}))
                           : Normalize(Cross(ns, Vec3f{1.0f, 0.0f, 0.0f}));
            const Vec3f ts = Cross(ns, ss);

            const float eta = material.bssrdf_eta;
            const float cos_theta_o = std::fabs(Dot(wo, ns));
            const float fr = FrDielectric(cos_theta_o, eta);

            if (sampler.Next1D() < fr) {
                // Specular reflection at the entry interface (throughput unchanged:
                // the fr selection probability cancels the fr Fresnel factor).
                const Vec3f wr = ns * (2.0f * Dot(wo, ns)) - wo;
                previous_bounce = PreviousBounce{true, true, hit_point, 1.0f, DirectLightSampleCount(scene)};
                const Vec3f bias_n = Dot(wr, ns) >= 0.0f ? ns : -ns;
                ray = Ray3f{hit_point + bias_n * SurfaceBias(hit_point), wr};
                continue;
            }

            // Enter the medium. (1 - fr) is consumed by the split, so it is NOT
            // multiplied again here; S = (1-fr)*Sp*Sw, and Sw is applied at the exit.
            const TabulatedBSSRDF bssrdf(material.sigma_a, material.sigma_s, eta, *material.bssrdf_table);
            const BssrdfProbeSample probe = SampleBssrdfProbe(
                bssrdf, scene, prepared_scene.bvh, hit_point, ss, ts, ns,
                surface_hit.geometry_hit.primitive_index, surface_hit.geometry_hit.sphere_index,
                sampler.Next1D(), sampler.Next2D());
            if (!probe.hit || probe.pdf <= 0.0f) { break; }

            throughput = Multiply(throughput, probe.sp / probe.pdf);
            if (IsNearBlack(throughput)) { break; }

            // Exit interface as a normalized Fresnel-weighted cosine (Sw) lobe.
            RenderMaterial exit_material;
            exit_material.kind = RenderMaterialKind::SubsurfaceExit;
            exit_material.ior = eta;
            const Vec3f ni = probe.ni;

            // Direct lighting at the exit point (reuses the standard NEE path).
            radiance = radiance + Multiply(throughput,
                EstimateDirectLight(prepared_scene, exit_material, probe.pi, ni, ni, sampler, rng, stats));

            // Sample the continuation direction from the exit lobe.
            const BsdfSample exit_sample = SampleBsdf(exit_material, ni, ni, sampler.Next2D(), rng);
            if (!exit_sample.valid || IsNearBlack(exit_sample.weight)) { break; }
            throughput = Multiply(throughput, exit_sample.weight);
            if (!SurviveRussianRoulette(depth, throughput, sampler)) { break; }

            previous_bounce = PreviousBounce{true, false, probe.pi, exit_sample.pdf, DirectLightSampleCount(scene)};
            const Vec3f bias_n = Dot(exit_sample.wi, ni) >= 0.0f ? ni : -ni;
            ray = Ray3f{probe.pi + bias_n * SurfaceBias(probe.pi), exit_sample.wi};
            continue;
        }
```
Verify that `PreviousBounce`'s aggregate initializer matches its real field order/types (open its definition near the top of the file and match it exactly — the fields are roughly `{valid, delta/specular, point, pdf, light_sample_count}`; copy the exact form used at the existing spawn site so this compiles).

- [ ] **Step 4: Build + run; confirm `cpu_subsurface_renders_finite_nonblack` and `cpu_subsurface_deterministic` PASS and the full suite is green. If the center pixel is black or NaN, debug the entry split / probe target indices / frame, and report findings — do not weaken the assertions.**

- [ ] **Step 5: Commit**
```bash
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_subsurface_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): integrate subsurface entry/exit/continue into the path tracer (M4 slice 4)"
```

---

### Task 3: White-furnace energy + appearance validation

**Files:** `tests/cpu_subsurface_tests.cpp` (append), `CMakeLists.txt` (no change)

- [ ] **Step 1: Append the validation tests to `tests/cpu_subsurface_tests.cpp`**

```cpp
// White furnace: a purely scattering medium (sigma_a ~ 0) with a matched interface
// (eta = 1, so no Fresnel reflection or interface loss) under uniform unit
// illumination must conserve energy -- no gain. The diffusion model is approximate,
// so we assert the rendered radiance is bounded (<= 1 + tol) and non-trivial.
YR_TEST(cpu_subsurface_white_furnace_no_gain) {
    auto result = RunPathTrace(MakeSubsurfaceSphereScene(/*sigma_a=*/0.0f, /*eta=*/1.0f, /*subsurface=*/true));
    yr::Color3f c = result.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::isfinite(c.x));
    YR_EXPECT_TRUE(c.x <= 1.05f);   // energy is not created
    YR_EXPECT_TRUE(c.x > 0.1f);     // and the medium does scatter light back out
}

// The coat-changes-appearance analogue: a subsurface sphere does NOT render
// identically to a diffuse sphere under the same lighting (proves SSS is active).
YR_TEST(cpu_subsurface_differs_from_diffuse) {
    auto sss = RunPathTrace(MakeSubsurfaceSphereScene(0.02f, 1.33f, /*subsurface=*/true));
    auto diff = RunPathTrace(MakeSubsurfaceSphereScene(0.02f, 1.33f, /*subsurface=*/false));
    yr::Color3f cs = sss.film.LinearPixel(8, 8);
    yr::Color3f cd = diff.film.LinearPixel(8, 8);
    YR_EXPECT_TRUE(std::fabs(cs.x - cd.x) > 1e-3f);
}
```

- [ ] **Step 2: Build + run; confirm both new tests PASS and the full suite is green. If `cpu_subsurface_white_furnace_no_gain` shows energy GAIN (c.x > 1.05), that is a real bug in the entry/exit accounting — investigate the `(1-Fr)` handling, the `Sp/pdf` factor, and the exit-lobe weight; report the observed value and your diagnosis rather than relaxing the bound.**

- [ ] **Step 3: Commit**
```bash
git add tests/cpu_subsurface_tests.cpp
git commit -m "test(bssrdf): white-furnace energy + appearance validation for subsurface (M4 slice 4)"
```

---

## Self-Review (completed by plan author)

**1. Spec coverage.** Slice 4 of the M4 spec asks for integrator integration (entry/exit/continue), medium params on `RenderMaterial`, the compiler branch, and white-furnace energy validation. Task 1 covers params + ownership + compiler + the exit lobe; Task 2 covers the integrator entry/exit/continue; Task 3 covers white-furnace + appearance. The documented degradation policy is honored (non-scattering params → diffuse + Warning).

**2. Placeholder scan.** The two tests that depend on the existing test harness (the compiler test in Task 1 Step 3, and the camera/helper setup in Task 2 Step 1) explicitly instruct the implementer to copy the exact pattern from a named existing file and to STOP-and-ask if ambiguous — this is deliberate (the harness specifics live in files the implementer must read), not a hidden placeholder. All production code (enum, fields, BSDF cases, compiler branch, integrator branch) is complete and literal.

**3. Type consistency.** New enum names `Subsurface`/`SubsurfaceExit` are used identically in `render_scene.hpp`, `bsdf.cpp`, `scene_compiler.cpp`, `cpu_path_tracer.cpp`, and tests. `RenderMaterial` fields `sigma_a`/`sigma_s`/`bssrdf_eta`/`bssrdf_index`/`bssrdf_table` are used consistently. `SampleBssrdfProbe`/`BssrdfProbeSample`/`TabulatedBSSRDF`/`FrDielectric`/`FresnelMoment1` signatures match Slices 1–3. The exit-lobe weight (`Sw·π`) and `Sw` definition (`(1−Fr)/(c·π)`, `c = 1−2·FresnelMoment1(1/eta)`) are consistent between `EvaluateBsdf` and `SampleBsdf`. The integrator reuses `EstimateDirectLight`/`SampleBsdf`/`PreviousBounce`/`SurviveRussianRoulette`/`SurfaceBias`/`Multiply`/`IsNearBlack` exactly as the existing loop does.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-yaoray-m4-subsurface-slice4-implementation-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks. Use sonnet for Task 1 (data + mechanical BSDF case) and Task 3 (tests); use **opus** for Task 2 (the integrator entry/exit/continue accounting — the highest-risk faithfulness surface, where the white furnace is the safety net).
2. **Inline Execution** — via `superpowers:executing-plans` with checkpoints.
