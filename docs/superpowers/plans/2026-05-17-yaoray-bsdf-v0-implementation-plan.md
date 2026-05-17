# YaoRay BSDF v0 Abstraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a small render-level BSDF API for current diffuse and mirror materials, then route the CPU path tracer through it without changing scene syntax or rendered behavior.

**Architecture:** Introduce `include/yaoray/render/bsdf.hpp` and `src/render/bsdf.cpp` in `yaoray_render`. Keep dispatch data-driven through `MaterialKind` switches and plain data structs. The CPU path tracer remains responsible for integration flow, stats, rays, emission, direct-light visibility, and depth control, while BSDF functions own Lambertian and perfect mirror scattering math.

**Tech Stack:** C++20, existing CMake static libraries, in-repo `yr_test` harness, CPU path tracer, existing `CpuSampler`, PNG manual render.

---

## Scope

This plan implements:

- `docs/superpowers/specs/2026-05-17-yaoray-bsdf-v0-design.md`

It does not add new material kinds, scene fields, glass, dielectric refraction, rough metal, GGX, Fresnel, textures, MIS, Russian roulette, denoising, CUDA implementation, or a broad material module refactor.

## File Structure

- Create `include/yaoray/render/bsdf.hpp`
  - Public renderer-level BSDF types and functions.
- Create `src/render/bsdf.cpp`
  - Data-driven switch implementation for diffuse and mirror BSDF behavior.
- Create `tests/bsdf_tests.cpp`
  - Focused unit tests for diffuse, mirror, and unknown material behavior.
- Modify `CMakeLists.txt`
  - Add `src/render/bsdf.cpp` to `yaoray_render`.
  - Add `tests/bsdf_tests.cpp` to `yaoray_tests`.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Replace local diffuse/mirror scattering formulas with calls to the BSDF API.
  - Keep path control, emission, shadow rays, stats, and environment handling local.
- Modify `docs/architecture/overview.md`
  - Document that material scattering now routes through a render-level BSDF API.
- Optionally modify `README.md`
  - Add one concise sentence only if the current user-facing material paragraph benefits from it.

## Task 1: Add BSDF API Tests And Build Wiring

**Files:**
- Create: `tests/bsdf_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing BSDF unit tests**

Create `tests/bsdf_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cmath>

#include <yaoray/render/bsdf.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

constexpr float Pi = 3.14159265358979323846f;

bool IsBlack(yr::Color3f color) {
    return color.x == 0.0f && color.y == 0.0f && color.z == 0.0f;
}

yr::RenderMaterial DiffuseMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{0.6f, 0.3f, 0.15f},
        yr::Color3f{}
    };
}

yr::RenderMaterial MirrorMaterial() {
    return yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.9f, 0.8f, 0.7f},
        yr::Color3f{}
    };
}

yr::RenderMaterial UnknownMaterial() {
    return yr::RenderMaterial{
        static_cast<yr::MaterialKind>(999),
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    };
}

} // namespace

YR_TEST(bsdf_diffuse_evaluate_returns_lambertian_brdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{0.0f, 0.25f, 1.0f});
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.5f, 0.0f, 1.0f});

    const yr::Color3f value = yr::EvaluateBsdf(material, wo, wi, normal);

    YR_EXPECT_NEAR(value.x, 0.6 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.y, 0.3 / Pi, 1e-6);
    YR_EXPECT_NEAR(value.z, 0.15 / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_evaluate_rejects_below_surface_directions) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_below{0.0f, 0.0f, -1.0f};
    const yr::Vec3f wi_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo_above{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi_below{0.0f, 0.0f, -1.0f};

    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_below, wi_above, normal)));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo_above, wi_below, normal)));
}

YR_TEST(bsdf_diffuse_pdf_uses_cosine_weighted_hemisphere_density) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi = yr::Normalize(yr::Vec3f{0.0f, 1.0f, 1.0f});

    const float pdf = yr::PdfBsdf(material, wo, wi, normal);

    YR_EXPECT_NEAR(pdf, yr::Dot(normal, wi) / Pi, 1e-6);
}

YR_TEST(bsdf_diffuse_sample_returns_albedo_weight_and_positive_pdf) {
    const yr::RenderMaterial material = DiffuseMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.25f, 0.5f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(!sample.specular);
    YR_EXPECT_TRUE(yr::Dot(sample.wi, normal) > 0.0f);
    YR_EXPECT_TRUE(sample.pdf > 0.0f);
    YR_EXPECT_NEAR(sample.weight.x, 0.6, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.15, 1e-6);
}

YR_TEST(bsdf_mirror_sample_reflects_incident_direction) {
    const yr::RenderMaterial material = MirrorMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo = yr::Normalize(yr::Vec3f{-0.25f, 0.0f, 1.0f});

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.0f, 0.0f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.specular);
    YR_EXPECT_NEAR(sample.wi.x, 0.24253563, 1e-6);
    YR_EXPECT_NEAR(sample.wi.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(sample.wi.z, 0.9701425, 1e-6);
    YR_EXPECT_NEAR(sample.weight.x, 0.9, 1e-6);
    YR_EXPECT_NEAR(sample.weight.y, 0.8, 1e-6);
    YR_EXPECT_NEAR(sample.weight.z, 0.7, 1e-6);
    YR_EXPECT_NEAR(sample.pdf, 1.0, 1e-6);
}

YR_TEST(bsdf_mirror_is_delta_and_has_no_finite_brdf_pdf) {
    const yr::RenderMaterial material = MirrorMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};

    YR_EXPECT_TRUE(yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal), 0.0, 1e-6);
}

YR_TEST(bsdf_unknown_material_fails_closed) {
    const yr::RenderMaterial material = UnknownMaterial();
    const yr::Vec3f normal{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wo{0.0f, 0.0f, 1.0f};
    const yr::Vec3f wi{0.0f, 0.0f, 1.0f};

    const yr::BsdfSample sample = yr::SampleBsdf(material, wo, normal, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_TRUE(!yr::IsDeltaBsdf(material));
    YR_EXPECT_TRUE(IsBlack(yr::EvaluateBsdf(material, wo, wi, normal)));
    YR_EXPECT_NEAR(yr::PdfBsdf(material, wo, wi, normal), 0.0, 1e-6);
    YR_EXPECT_TRUE(!sample.valid);
    YR_EXPECT_NEAR(sample.pdf, 0.0, 1e-6);
}
```

- [ ] **Step 2: Wire the new test file into CMake**

In `CMakeLists.txt`, add `tests/bsdf_tests.cpp` to the `yaoray_tests` executable source list near the render tests:

```cmake
add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
    tests/render_scene_tests.cpp
    tests/bsdf_tests.cpp
    tests/backend_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
    tests/cpu_tile_scheduler_tests.cpp
    tests/cpu_sampler_tests.cpp
    tests/cpu_path_tracer_tests.cpp
)
```

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected:

- Build fails because `<yaoray/render/bsdf.hpp>`, `yr::BsdfSample`, `yr::EvaluateBsdf`, `yr::PdfBsdf`, `yr::SampleBsdf`, and `yr::IsDeltaBsdf` do not exist yet.

- [ ] **Step 4: Commit the red test state**

Do not commit a build-breaking red state. This task is intentionally completed together with Task 2 in one implementation commit after the tests pass.

## Task 2: Implement Render-Level BSDF API

**Files:**
- Create: `include/yaoray/render/bsdf.hpp`
- Create: `src/render/bsdf.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/bsdf_tests.cpp`

- [ ] **Step 1: Add the BSDF public header**

Create `include/yaoray/render/bsdf.hpp`:

```cpp
#pragma once

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

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal);

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample);

bool IsDeltaBsdf(const RenderMaterial& material);

} // namespace yr
```

- [ ] **Step 2: Implement the BSDF source**

Create `src/render/bsdf.cpp`:

```cpp
#include <yaoray/render/bsdf.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

bool IsAboveSurface(Vec3f direction, Vec3f normal) {
    return Dot(direction, normal) > 0.0f;
}

Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}

Vec3f SampleCosineHemisphere(Vec3f normal, Vec2f sample) {
    const float u1 = std::clamp(sample.x, 0.0f, 1.0f);
    const float u2 = std::clamp(sample.y, 0.0f, 1.0f);
    const float radius = std::sqrt(u1);
    const float theta = 2.0f * Pi * u2;
    const float local_x = radius * std::cos(theta);
    const float local_y = radius * std::sin(theta);
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - u1));

    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

Color3f LambertianBrdf(Color3f albedo) {
    return albedo / Pi;
}

bool IsBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

} // namespace

Color3f EvaluateBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return Color3f{};
            }
            return LambertianBrdf(material.albedo);
        case MaterialKind::Mirror:
            return Color3f{};
    }
    return Color3f{};
}

float PdfBsdf(const RenderMaterial& material, Vec3f wo, Vec3f wi, Vec3f normal) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            if (!IsAboveSurface(wo, normal) || !IsAboveSurface(wi, normal)) {
                return 0.0f;
            }
            return std::max(0.0f, Dot(normal, wi)) / Pi;
        case MaterialKind::Mirror:
            return 0.0f;
    }
    return 0.0f;
}

BsdfSample SampleBsdf(const RenderMaterial& material, Vec3f wo, Vec3f normal, Vec2f sample) {
    switch (material.type) {
        case MaterialKind::Diffuse: {
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            const Vec3f wi = SampleCosineHemisphere(normal, sample);
            return BsdfSample{
                wi,
                material.albedo,
                PdfBsdf(material, wo, wi, normal),
                true,
                false
            };
        }
        case MaterialKind::Mirror:
            if (!IsAboveSurface(wo, normal) || IsBlack(material.albedo)) {
                return BsdfSample{};
            }
            return BsdfSample{
                Reflect(-wo, normal),
                material.albedo,
                1.0f,
                true,
                true
            };
    }
    return BsdfSample{};
}

bool IsDeltaBsdf(const RenderMaterial& material) {
    switch (material.type) {
        case MaterialKind::Diffuse:
            return false;
        case MaterialKind::Mirror:
            return true;
    }
    return false;
}

} // namespace yr
```

- [ ] **Step 3: Add BSDF source to `yaoray_render`**

In `CMakeLists.txt`, update the `yaoray_render` library:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/scene_compiler.cpp
)
```

- [ ] **Step 4: Build and run focused tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- `yaoray_tests` passes, including all new `bsdf_*` tests.
- Existing path tracer tests still pass because the path tracer has not been changed yet.

- [ ] **Step 5: Commit BSDF API and unit tests**

Run:

```powershell
git add CMakeLists.txt include\yaoray\render\bsdf.hpp src\render\bsdf.cpp tests\bsdf_tests.cpp
git commit -m "feat: add render bsdf api"
```

## Task 3: Route CPU Path Tracer Through BSDF API

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Test: `tests/cpu_path_tracer_tests.cpp`
- Test: `tests/bsdf_tests.cpp`

- [ ] **Step 1: Include the BSDF header**

In `src/backends/cpu/cpu_path_tracer.cpp`, add:

```cpp
#include <yaoray/render/bsdf.hpp>
```

near the other YaoRay includes.

- [ ] **Step 2: Remove local scattering helpers from the CPU path tracer**

Delete these local helpers from `src/backends/cpu/cpu_path_tracer.cpp`:

```cpp
Vec3f Reflect(Vec3f direction, Vec3f normal) {
    return Normalize(direction - normal * (2.0f * Dot(direction, normal)));
}
```

```cpp
Vec3f SampleCosineHemisphere(Vec3f normal, CpuSampler& sampler) {
    const Vec2f sample = sampler.Next2D();
    const float u1 = sample.x;
    const float u2 = sample.y;
    const float radius = std::sqrt(u1);
    const float theta = 2.0f * Pi * u2;
    const float local_x = radius * std::cos(theta);
    const float local_y = radius * std::sin(theta);
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - u1));

    const Vec3f helper = std::fabs(normal.z) < 0.999f ? Vec3f{0.0f, 0.0f, 1.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}
```

```cpp
Color3f EvaluateDiffuseBrdf(Color3f albedo) {
    return albedo / Pi;
}
```

After removing these helpers, if `Pi` is no longer used in `cpu_path_tracer.cpp`, remove:

```cpp
constexpr float Pi = 3.14159265358979323846f;
```

- [ ] **Step 3: Change direct-light estimation to use `EvaluateBsdf()`**

Replace the signature of `EstimateDirectLight` from:

```cpp
Color3f EstimateDirectLight(
    const RenderScene& scene,
    Point3f hit_point,
    Vec3f normal,
    Color3f albedo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
)
```

to:

```cpp
Color3f EstimateDirectLight(
    const RenderScene& scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
)
```

Inside `EstimateDirectLight`, replace:

```cpp
const Color3f brdf = EvaluateDiffuseBrdf(albedo);
const float geometry = cos_surface * cos_light / distance_squared;
const float weight = geometry / sample->pdf_area;
light_radiance = light_radiance + Multiply(brdf, sample->radiance) * weight;
```

with:

```cpp
const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
if (IsNearBlack(bsdf)) {
    continue;
}

const float geometry = cos_surface * cos_light / distance_squared;
const float weight = geometry / sample->pdf_area;
light_radiance = light_radiance + Multiply(bsdf, sample->radiance) * weight;
```

This preserves the current direct-lighting equation for diffuse materials because `EvaluateBsdf()` returns `albedo / pi`.

- [ ] **Step 4: Replace material scattering in `TracePath()`**

In `TracePath`, after computing `normal`, add:

```cpp
const Vec3f wo = -ray.direction;
```

Replace the direct-light branch:

```cpp
if (material.type == MaterialKind::Diffuse) {
    radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, sampler, stats));
}
```

with:

```cpp
if (!IsDeltaBsdf(material)) {
    radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, material, hit_point, normal, wo, sampler, stats));
}
```

Replace the bounce/scattering block:

```cpp
if (depth + 1 >= max_depth || IsNearBlack(material.albedo)) {
    break;
}

throughput = Multiply(throughput, material.albedo);
if (material.type == MaterialKind::Mirror) {
    const Vec3f reflected_direction = Reflect(ray.direction, normal);
    ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), reflected_direction};
    continue;
}

const Vec3f bounce_direction = SampleCosineHemisphere(normal, sampler);
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bounce_direction};
```

with:

```cpp
if (depth + 1 >= max_depth) {
    break;
}

const BsdfSample bsdf_sample = SampleBsdf(material, wo, normal, sampler.Next2D());
if (!bsdf_sample.valid || IsNearBlack(bsdf_sample.weight)) {
    break;
}

throughput = Multiply(throughput, bsdf_sample.weight);
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bsdf_sample.wi};
```

- [ ] **Step 5: Run focused path tracer tests**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing `cpu_path_tracer_mirror_reflects_environment` still passes.
- Existing `cpu_path_tracer_mirror_skips_diffuse_direct_lighting` still proves mirror skips direct-light shadow rays.
- Existing diffuse direct-light tests still pass because direct lighting now uses `EvaluateBsdf()`.
- Threaded determinism still passes because each pixel/sample still owns sampler state.

- [ ] **Step 6: Verify path tracer no longer switches on material kind for scattering**

Run:

```powershell
rg -n "MaterialKind::Diffuse|MaterialKind::Mirror|Reflect\\(|SampleCosineHemisphere|EvaluateDiffuseBrdf" src\backends\cpu\cpu_path_tracer.cpp src\render\bsdf.cpp include\yaoray\render\bsdf.hpp
```

Expected:

- `MaterialKind::Diffuse` and `MaterialKind::Mirror` appear in `src/render/bsdf.cpp`.
- `Reflect(` and `SampleCosineHemisphere` appear in `src/render/bsdf.cpp`.
- `EvaluateDiffuseBrdf` does not appear.
- `src/backends/cpu/cpu_path_tracer.cpp` does not contain `MaterialKind::Diffuse`, `MaterialKind::Mirror`, `Reflect(`, `SampleCosineHemisphere`, or `EvaluateDiffuseBrdf`.

- [ ] **Step 7: Commit CPU path tracer BSDF integration**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp
git commit -m "refactor: route cpu path tracer through bsdf"
```

## Task 4: Update Documentation And Run Full Verification

**Files:**
- Modify: `docs/architecture/overview.md`
- Optional modify: `README.md`
- No generated image files should be committed.

- [ ] **Step 1: Update architecture overview**

In `docs/architecture/overview.md`, update the CPU backend paragraph so it includes this sentence:

```markdown
Material scattering for `path` is routed through a small render-level BSDF API that currently implements Lambertian diffuse and perfect mirror behavior with data-driven `MaterialKind` dispatch.
```

Keep the existing limitation wording that glass, roughness, texture maps, MIS, and CUDA material evaluation remain future work.

- [ ] **Step 2: Optionally update README**

If the current README render paragraph feels too implementation-heavy after the architecture update, leave it unchanged.

If adding one user-facing sentence helps, add only this concise sentence near the existing material explanation:

```markdown
Internally, diffuse and mirror scattering are evaluated through a small BSDF layer so future material models can share the same path-tracing boundary.
```

Do not add a long explanation of BxDF/BRDF/BTDF to README.

- [ ] **Step 3: Run documentation scope check**

Run:

```powershell
rg -n "BSDF|Lambertian|perfect mirror|glass|roughness|texture|MIS|CUDA material" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-bsdf-v0-design.md
```

Expected:

- Architecture docs mention the render-level BSDF API.
- Limitations still mention glass, roughness, textures, MIS, and CUDA material evaluation as future work.
- README either remains concise or has only one short BSDF sentence.

- [ ] **Step 4: Commit documentation updates**

Run:

```powershell
git add docs\architecture\overview.md README.md
git commit -m "docs: document bsdf layer"
```

If README is unchanged, run:

```powershell
git add docs\architecture\overview.md
git commit -m "docs: document bsdf layer"
```

- [ ] **Step 5: Run full Debug configure, build, and tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

- Configure succeeds.
- Build succeeds.
- All CTest tests pass.

- [ ] **Step 6: Run Release build**

Run:

```powershell
cmake --build build-release --config Release
```

If `build-release` does not exist, run:

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build-release --config Release
```

Expected:

- Release `yaoray.exe` builds successfully.
- If MSBuild cannot access the Windows SDK under sandboxing, rerun the same command with escalated permissions.

- [ ] **Step 7: Render the material showcase**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\material_showcase.toml --backend cpu
```

If the local generator produced a single-config Ninja build instead of Visual Studio, use:

```powershell
.\build-release\yaoray.exe render .\scenes\examples\material_showcase.toml --backend cpu
```

Expected output contains:

```text
Integrator: path
Threads:
Samples/sec:
Rays/sec:
Shadow rays:
Rendered image: scenes/examples/out/material_showcase.png
```

Do not commit `scenes/examples/out/material_showcase.png`.

- [ ] **Step 8: Final scope and git checks**

Run:

```powershell
rg -n "virtual|new |delete |MaterialKind::Diffuse|MaterialKind::Mirror|SampleBsdf|EvaluateBsdf|PdfBsdf|IsDeltaBsdf|glass|roughness|GGX|Fresnel|MIS|Russian roulette|CUDA" include\yaoray\render src\render src\backends\cpu tests README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-bsdf-v0-design.md
git status --short --branch
git log --oneline --decorate -8
```

Expected:

- No new material kinds were added.
- No `virtual` BSDF hierarchy was introduced.
- `MaterialKind` dispatch is concentrated in `src/render/bsdf.cpp`.
- Future-work terms only appear in docs/specs or pre-existing CUDA stub wording.
- Working tree is clean except for pre-existing user edits or generated output that should not be committed.
- Recent commits include:
  - `feat: add render bsdf api`
  - `refactor: route cpu path tracer through bsdf`
  - `docs: document bsdf layer`

## Self-Review Checklist

- Spec coverage:
  - Render-level BSDF API: Task 2.
  - Data-driven switch, no virtual classes: Task 2 and Task 4 scope check.
  - CPU sampler independence through raw `Vec2f`: Task 2 and Task 3.
  - Diffuse Lambertian behavior: Task 1 tests and Task 2 implementation.
  - Mirror delta behavior: Task 1 tests and Task 2 implementation.
  - CPU path tracer no longer owns scattering formulas: Task 3.
  - Direct lighting uses `EvaluateBsdf()`: Task 3.
  - Existing scene syntax preserved: Task 4 full tests and render.
  - No glass/roughness/GGX/MIS/CUDA implementation: Task 4 scope check.
- Placeholder scan:
  - Every task names concrete files, code snippets, commands, and expected results.
- Type consistency:
  - `BsdfSample`, `EvaluateBsdf`, `PdfBsdf`, `SampleBsdf`, and `IsDeltaBsdf` names match between tests, header, implementation, and integration steps.
  - `wo` and `wi` conventions match the spec: both point away from the surface.
