# YaoRay MIS Architecture v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add render-level MIS and area-light sampling helpers, then use them in the CPU path tracer for direct-light MIS.

**Architecture:** Keep the path tracing loop in `src/backends/cpu/cpu_path_tracer.cpp`, but move MIS weighting and area-light sampling math into focused `yaoray_render` helpers. The CPU backend will own random sample generation, shadow visibility, path state, and stats; `yaoray_render` will own pure PDF and weighting math over `RenderScene` data.

**Tech Stack:** C++20, CMake/CTest, in-repo `yr_test`, `yaoray_render`, CPU path tracer, PNG manual render.

---

## Scope

This plan implements:

- `docs/superpowers/specs/2026-05-17-yaoray-mis-architecture-v1-design.md`

It does not implement a full Integrator API refactor, new scene schema, new materials, Russian roulette, environment MIS, one-sample light selection, arbitrary oriented lights, emissive mesh-light extraction, CUDA path tracing, denoising, adaptive sampling, or sampler sequence changes.

## File Structure

- Create `include/yaoray/render/mis.hpp`
  - Declares the generic power heuristic helper.
- Create `src/render/mis.cpp`
  - Implements robust MIS PDF weighting.
- Create `tests/mis_tests.cpp`
  - Covers balanced, sample-count-biased, PDF-biased, and invalid-PDF cases.
- Create `include/yaoray/render/light_sampling.hpp`
  - Declares area-light sample records and area-light PDF helpers.
- Create `src/render/light_sampling.cpp`
  - Implements XZ-rectangle area-light sampling and solid-angle PDF math.
- Create `tests/light_sampling_tests.cpp`
  - Covers current area-light geometry, invalid lights, solid-angle PDF conversion, and scene-level light PDF lookup.
- Modify `CMakeLists.txt`
  - Adds new render sources and tests.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Uses render-level light sampling helpers.
  - MIS-weights explicit light samples against `PdfBsdf()`.
  - Tracks previous non-delta BSDF samples to MIS-weight emissive hits.
- Modify `tests/cpu_path_tracer_tests.cpp`
  - Adds behavior coverage for light-sampled MIS and BSDF-sampled emissive MIS.
- Modify `README.md`
  - Documents CPU direct-light MIS and remaining non-goals.
- Modify `docs/architecture/overview.md`
  - Documents render-level MIS/light sampling helper boundaries.

## Task 1: Add Render-Level MIS Helper

**Files:**
- Modify: `CMakeLists.txt`
- Create: `include/yaoray/render/mis.hpp`
- Create: `src/render/mis.cpp`
- Create: `tests/mis_tests.cpp`

- [ ] **Step 1: Add failing MIS tests and CMake entries**

In `CMakeLists.txt`, add `src/render/mis.cpp` to `yaoray_render`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
)
```

Add `tests/mis_tests.cpp` to `yaoray_tests` after `tests/bsdf_tests.cpp`:

```cmake
    tests/bsdf_tests.cpp
    tests/mis_tests.cpp
    tests/backend_tests.cpp
```

Create `tests/mis_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/render/mis.hpp>

YR_TEST(mis_power_heuristic_balances_equal_estimators) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, 1.0f), 0.5, 1e-6);
}

YR_TEST(mis_power_heuristic_favors_larger_pdf) {
    const float weight = yr::PowerHeuristic(1, 4.0f, 1, 1.0f);

    YR_EXPECT_NEAR(weight, 16.0 / 17.0, 1e-6);
}

YR_TEST(mis_power_heuristic_accounts_for_sample_counts) {
    const float weight = yr::PowerHeuristic(4, 1.0f, 1, 1.0f);

    YR_EXPECT_NEAR(weight, 16.0 / 17.0, 1e-6);
}

YR_TEST(mis_power_heuristic_rejects_invalid_a_estimator) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(0, 1.0f, 1, 1.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 0.0f, 1, 1.0f), 0.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, -1.0f, 1, 1.0f), 0.0, 1e-6);
}

YR_TEST(mis_power_heuristic_uses_full_weight_when_b_cannot_compete) {
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 0, 1.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, 0.0f), 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::PowerHeuristic(1, 1.0f, 1, -1.0f), 1.0, 1e-6);
}
```

- [ ] **Step 2: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected:

- Build fails because `include/yaoray/render/mis.hpp` and `src/render/mis.cpp` do not exist.

- [ ] **Step 3: Add MIS helper header**

Create `include/yaoray/render/mis.hpp`:

```cpp
#pragma once

namespace yr {

float PowerHeuristic(int sample_count_a, float pdf_a, int sample_count_b, float pdf_b);

} // namespace yr
```

- [ ] **Step 4: Add MIS helper implementation**

Create `src/render/mis.cpp`:

```cpp
#include <yaoray/render/mis.hpp>

#include <cmath>

namespace yr {
namespace {

bool ValidEstimator(int sample_count, float pdf) {
    return sample_count > 0 && pdf > 0.0f && std::isfinite(pdf);
}

} // namespace

float PowerHeuristic(int sample_count_a, float pdf_a, int sample_count_b, float pdf_b) {
    if (!ValidEstimator(sample_count_a, pdf_a)) {
        return 0.0f;
    }
    if (!ValidEstimator(sample_count_b, pdf_b)) {
        return 1.0f;
    }

    const float a = static_cast<float>(sample_count_a) * pdf_a;
    const float b = static_cast<float>(sample_count_b) * pdf_b;
    const float a2 = a * a;
    const float b2 = b * b;
    const float denom = a2 + b2;
    if (!std::isfinite(denom) || denom <= 0.0f) {
        return 0.0f;
    }
    return a2 / denom;
}

} // namespace yr
```

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `mis_*` tests pass.
- Existing tests still pass.

- [ ] **Step 6: Commit MIS helper**

Run:

```powershell
git add CMakeLists.txt include\yaoray\render\mis.hpp src\render\mis.cpp tests\mis_tests.cpp
git commit -m "feat: add mis heuristic helper"
```

## Task 2: Add Render-Level Area-Light Sampling Helper

**Files:**
- Modify: `CMakeLists.txt`
- Create: `include/yaoray/render/light_sampling.hpp`
- Create: `src/render/light_sampling.cpp`
- Create: `tests/light_sampling_tests.cpp`

- [ ] **Step 1: Add failing area-light sampling tests and CMake entries**

In `CMakeLists.txt`, add `src/render/light_sampling.cpp` to `yaoray_render`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
)
```

Add `tests/light_sampling_tests.cpp` to `yaoray_tests` after `tests/mis_tests.cpp`:

```cmake
    tests/bsdf_tests.cpp
    tests/mis_tests.cpp
    tests/light_sampling_tests.cpp
    tests/backend_tests.cpp
```

Create `tests/light_sampling_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <optional>

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderAreaLight MakeAreaLight() {
    return yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        4.0f,
        2.0f,
        yr::Color3f{3.0f, 2.0f, 1.0f}
    };
}

} // namespace

YR_TEST(light_sampling_samples_current_xz_rectangle_geometry) {
    const yr::RenderAreaLight light = MakeAreaLight();

    const std::optional<yr::AreaLightSample> center = yr::SampleAreaLight(light, yr::Vec2f{0.5f, 0.5f});
    const std::optional<yr::AreaLightSample> corner = yr::SampleAreaLight(light, yr::Vec2f{1.0f, 0.0f});

    YR_EXPECT_TRUE(center.has_value());
    YR_EXPECT_NEAR(center->point.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->point.y, 2.0, 1e-6);
    YR_EXPECT_NEAR(center->point.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.y, -1.0, 1e-6);
    YR_EXPECT_NEAR(center->normal.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(center->radiance.x, 3.0, 1e-6);
    YR_EXPECT_NEAR(center->area, 8.0, 1e-6);
    YR_EXPECT_NEAR(center->pdf_area, 0.125, 1e-6);

    YR_EXPECT_TRUE(corner.has_value());
    YR_EXPECT_NEAR(corner->point.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(corner->point.y, 2.0, 1e-6);
    YR_EXPECT_NEAR(corner->point.z, -1.0, 1e-6);
}

YR_TEST(light_sampling_rejects_invalid_area) {
    yr::RenderAreaLight light = MakeAreaLight();
    light.width = 0.0f;

    const std::optional<yr::AreaLightSample> sample = yr::SampleAreaLight(light, yr::Vec2f{0.5f, 0.5f});

    YR_EXPECT_TRUE(!sample.has_value());
}

YR_TEST(light_sampling_converts_area_pdf_to_solid_angle_pdf) {
    const yr::RenderAreaLight light = MakeAreaLight();
    const float pdf = yr::PdfAreaLightSampleSolidAngle(
        light,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f}
    );

    YR_EXPECT_NEAR(pdf, 0.5, 1e-6);
}

YR_TEST(light_sampling_returns_zero_pdf_for_invalid_solid_angle_cases) {
    const yr::RenderAreaLight light = MakeAreaLight();

    YR_EXPECT_NEAR(
        yr::PdfAreaLightSampleSolidAngle(light, yr::Point3f{0.0f, 3.0f, 0.0f}, yr::Point3f{0.0f, 2.0f, 0.0f}),
        0.0,
        1e-6
    );
    YR_EXPECT_NEAR(
        yr::PdfAreaLightSampleSolidAngle(light, yr::Point3f{0.0f, 2.0f, 0.0f}, yr::Point3f{0.0f, 2.0f, 0.0f}),
        0.0,
        1e-6
    );
}

YR_TEST(light_sampling_sums_scene_light_pdf_for_points_on_area_lights) {
    yr::RenderScene scene;
    scene.area_lights.push_back(MakeAreaLight());

    const float on_light_pdf = yr::PdfAreaLightsForPointSolidAngle(
        scene,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f}
    );
    const float outside_light_pdf = yr::PdfAreaLightsForPointSolidAngle(
        scene,
        yr::Point3f{0.0f, 0.0f, 0.0f},
        yr::Point3f{3.0f, 2.0f, 0.0f}
    );

    YR_EXPECT_NEAR(on_light_pdf, 0.5, 1e-6);
    YR_EXPECT_NEAR(outside_light_pdf, 0.0, 1e-6);
}
```

- [ ] **Step 2: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected:

- Build fails because `include/yaoray/render/light_sampling.hpp` and `src/render/light_sampling.cpp` do not exist.

- [ ] **Step 3: Add light sampling header**

Create `include/yaoray/render/light_sampling.hpp`:

```cpp
#pragma once

#include <optional>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct AreaLightSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float area = 0.0f;
    float pdf_area = 0.0f;
};

std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Vec2f uv);

float PdfAreaLightSampleSolidAngle(
    const RenderAreaLight& light,
    Point3f shading_point,
    Point3f light_point
);

float PdfAreaLightsForPointSolidAngle(
    const RenderScene& scene,
    Point3f shading_point,
    Point3f light_point
);

} // namespace yr
```

- [ ] **Step 4: Add light sampling implementation**

Create `src/render/light_sampling.cpp`:

```cpp
#include <yaoray/render/light_sampling.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

constexpr float MinPdfDistanceSquared = 1.0e-12f;
constexpr float AreaLightPointTolerance = 1.0e-3f;

float Area(const RenderAreaLight& light) {
    return light.width * light.height;
}

Vec3f AreaLightNormal() {
    return Vec3f{0.0f, -1.0f, 0.0f};
}

bool IsPointOnCurrentAreaLightRectangle(const RenderAreaLight& light, Point3f point) {
    const float half_width = light.width * 0.5f;
    const float half_height = light.height * 0.5f;
    return std::fabs(point.y - light.position.y) <= AreaLightPointTolerance &&
           point.x >= light.position.x - half_width - AreaLightPointTolerance &&
           point.x <= light.position.x + half_width + AreaLightPointTolerance &&
           point.z >= light.position.z - half_height - AreaLightPointTolerance &&
           point.z <= light.position.z + half_height + AreaLightPointTolerance;
}

} // namespace

std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Vec2f uv) {
    const float area = Area(light);
    if (area <= 0.0f) {
        return std::nullopt;
    }

    const float u = std::clamp(uv.x, 0.0f, 1.0f);
    const float v = std::clamp(uv.y, 0.0f, 1.0f);
    const float offset_x = (u - 0.5f) * light.width;
    const float offset_z = (v - 0.5f) * light.height;

    return AreaLightSample{
        light.position + Vec3f{offset_x, 0.0f, offset_z},
        AreaLightNormal(),
        light.radiance,
        area,
        1.0f / area
    };
}

float PdfAreaLightSampleSolidAngle(
    const RenderAreaLight& light,
    Point3f shading_point,
    Point3f light_point
) {
    const float area = Area(light);
    if (area <= 0.0f) {
        return 0.0f;
    }

    const Vec3f to_light = light_point - shading_point;
    const float distance_squared = LengthSquared(to_light);
    if (distance_squared <= MinPdfDistanceSquared) {
        return 0.0f;
    }

    const Vec3f wi = to_light / std::sqrt(distance_squared);
    const float cos_light = std::max(0.0f, Dot(AreaLightNormal(), -wi));
    if (cos_light <= 0.0f) {
        return 0.0f;
    }

    return distance_squared / (cos_light * area);
}

float PdfAreaLightsForPointSolidAngle(
    const RenderScene& scene,
    Point3f shading_point,
    Point3f light_point
) {
    float pdf = 0.0f;
    for (const RenderAreaLight& light : scene.area_lights) {
        if (!IsPointOnCurrentAreaLightRectangle(light, light_point)) {
            continue;
        }
        pdf += PdfAreaLightSampleSolidAngle(light, shading_point, light_point);
    }
    return pdf;
}

} // namespace yr
```

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `light_sampling_*` tests pass.
- Task 1 MIS tests still pass.
- Existing render tests still pass.

- [ ] **Step 6: Commit light sampling helper**

Run:

```powershell
git add CMakeLists.txt include\yaoray\render\light_sampling.hpp src\render\light_sampling.cpp tests\light_sampling_tests.cpp
git commit -m "feat: add area light sampling helpers"
```

## Task 3: Refactor CPU Direct Lighting To Use Render Helpers And Light-Sampled MIS

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add failing CPU direct-light MIS test**

In `tests/cpu_path_tracer_tests.cpp`, add this test after `cpu_path_tracer_direct_light_uses_diffuse_brdf_weight`:

```cpp
YR_TEST(cpu_path_tracer_direct_light_mis_downweights_large_light_against_diffuse_bsdf) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.max_depth = 1;
    scene.spp = 1;
    scene.light_samples = 8;
    scene.area_lights[0].width = 50.0f;
    scene.area_lights[0].height = 50.0f;
    scene.area_lights[0].radiance = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(center.x < 0.9f);
    YR_EXPECT_TRUE(center.y < 0.9f);
    YR_EXPECT_TRUE(center.z < 0.9f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
}
```

- [ ] **Step 2: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- The new `cpu_path_tracer_direct_light_mis_downweights_large_light_against_diffuse_bsdf` test fails because explicit light samples currently use full weight instead of the `PowerHeuristic()` helper.
- Existing tests continue to build.

- [ ] **Step 3: Include render helper headers in CPU path tracer**

In `src/backends/cpu/cpu_path_tracer.cpp`, add these includes near the existing render includes:

```cpp
#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/mis.hpp>
```

- [ ] **Step 4: Remove local area-light sample type and sampler**

In `src/backends/cpu/cpu_path_tracer.cpp`, delete this local struct:

```cpp
struct AreaLightSample {
    Point3f point;
    Vec3f normal{0.0f, -1.0f, 0.0f};
    Color3f radiance;
    float area = 0.0f;
    float pdf_area = 0.0f;
};
```

Delete the local `SampleAreaLight(const RenderAreaLight& light, CpuSampler& sampler, int light_sample_index)` function. The CPU path tracer will call the render-level `SampleAreaLight(light, uv)` helper instead.

- [ ] **Step 5: Update explicit direct-light estimator**

In `src/backends/cpu/cpu_path_tracer.cpp`, replace the full `EstimateDirectLight` function with this implementation:

```cpp
Color3f EstimateDirectLight(
    const RenderScene& scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
) {
    Color3f radiance;
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);

    for (const RenderAreaLight& light : scene.area_lights) {
        Color3f light_radiance;
        for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
            const std::optional<AreaLightSample> sample = SampleAreaLight(light, sampler.NextLight2D(sample_index));
            if (!sample.has_value()) {
                continue;
            }

            const Vec3f to_light = sample->point - hit_point;
            const float distance_squared = LengthSquared(to_light);
            if (distance_squared <= MinShadowBias * MinShadowBias) {
                continue;
            }

            const float distance = std::sqrt(distance_squared);
            const float shadow_bias = ShadowBias(hit_point, sample->point, distance);
            if (distance <= shadow_bias) {
                continue;
            }

            const Point3f shadow_origin = hit_point + normal * shadow_bias;
            const Vec3f shadow_to_light = sample->point - shadow_origin;
            const float shadow_distance_squared = LengthSquared(shadow_to_light);
            if (shadow_distance_squared <= MinShadowBias * MinShadowBias) {
                continue;
            }

            const float shadow_distance = std::sqrt(shadow_distance_squared);
            const Vec3f wi = shadow_to_light / shadow_distance;
            const float cos_surface = std::max(0.0f, Dot(normal, wi));
            if (cos_surface <= 0.0f) {
                continue;
            }

            const float pdf_light = PdfAreaLightSampleSolidAngle(light, hit_point, sample->point);
            if (pdf_light <= 0.0f) {
                continue;
            }

            ++stats.shadow_rays;
            BvhTraceStats shadow_trace;
            const Ray3f shadow_ray{shadow_origin, wi};
            const BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_trace);
            AccumulateTraceStats(stats, shadow_trace);
            if (shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias) {
                ++stats.occluded_shadow_rays;
                continue;
            }

            const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
            if (IsNearBlack(bsdf)) {
                continue;
            }

            const float pdf_bsdf = PdfBsdf(material, wo, wi, normal);
            const float mis_weight = PowerHeuristic(light_sample_count, pdf_light, 1, pdf_bsdf);
            light_radiance = light_radiance + Multiply(bsdf, sample->radiance) * (cos_surface * mis_weight / pdf_light);
        }

        radiance = radiance + light_radiance * inverse_light_sample_count;
    }
    return radiance;
}
```

- [ ] **Step 6: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- New direct-light MIS test passes.
- Existing direct-light tests still pass.
- `cpu_path_tracer_light_samples_increase_shadow_rays` still passes because `render.light_samples` still controls explicit light visibility samples.

- [ ] **Step 7: Commit explicit direct-light MIS**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: apply mis to explicit direct lighting"
```

## Task 4: MIS-Weight BSDF-Sampled Emissive Hits

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add helper scene for BSDF-sampled emissive hits**

In `tests/cpu_path_tracer_tests.cpp`, add this helper after `MakeBlockedDiffuseFloorScene()`:

```cpp
yr::RenderScene MakeDiffuseToExplicitEmitterScene(bool add_explicit_area_light) {
    yr::RenderScene scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 32;
    scene.max_depth = 2;
    scene.seed = 21;
    scene.light_samples = 128;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    });
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, 25.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, 2.0f, -25.0f},
        yr::Point3f{25.0f, 2.0f, 25.0f},
        yr::Point3f{-25.0f, 2.0f, 25.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        1
    });

    if (add_explicit_area_light) {
        scene.area_lights.push_back(yr::RenderAreaLight{
            yr::Point3f{0.0f, 2.0f, 0.0f},
            50.0f,
            50.0f,
            yr::Color3f{}
        });
    }

    RebuildBvh(scene);
    return scene;
}
```

This helper uses a zero-radiance explicit area light so direct light sampling contributes no radiance, while `PdfAreaLightsForPointSolidAngle()` still provides the competing light PDF for MIS on BSDF-sampled emissive hits.

- [ ] **Step 2: Add failing BSDF-sampled emissive MIS tests**

In `tests/cpu_path_tracer_tests.cpp`, add these tests after `cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce`:

```cpp
YR_TEST(cpu_path_tracer_mis_weights_bsdf_sampled_explicit_emitter_hits) {
    const yr::CpuPathTraceResult without_explicit_light =
        yr::RenderCpuPathTrace(MakeDiffuseToExplicitEmitterScene(false));
    const yr::CpuPathTraceResult with_explicit_light =
        yr::RenderCpuPathTrace(MakeDiffuseToExplicitEmitterScene(true));

    const float without_luminance = Luminance(without_explicit_light.film.LinearPixel(0, 0));
    const float with_luminance = Luminance(with_explicit_light.film.LinearPixel(0, 0));

    YR_EXPECT_TRUE(without_luminance > 0.0f);
    YR_EXPECT_TRUE(with_luminance > 0.0f);
    YR_EXPECT_TRUE(with_luminance < without_luminance);
}

YR_TEST(cpu_path_tracer_delta_bsdf_sampled_emissive_hits_keep_full_weight) {
    yr::RenderScene scene = MakeDiffuseToExplicitEmitterScene(true);
    scene.materials[0] = yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    };
    scene.triangles[0].normal = yr::Vec3f{0.0f, 0.0f, 1.0f};
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.triangles.clear();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-2.0f, -2.0f, 0.0f},
        yr::Point3f{2.0f, -2.0f, 0.0f},
        yr::Point3f{0.0f, 2.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, -25.0f, 8.0f},
        yr::Point3f{25.0f, -25.0f, 8.0f},
        yr::Point3f{25.0f, 25.0f, 8.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        1
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-25.0f, -25.0f, 8.0f},
        yr::Point3f{25.0f, 25.0f, 8.0f},
        yr::Point3f{-25.0f, 25.0f, 8.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        1
    });
    scene.area_lights[0] = yr::RenderAreaLight{
        yr::Point3f{0.0f, 8.0f, 0.0f},
        50.0f,
        50.0f,
        yr::Color3f{}
    };
    RebuildBvh(scene);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(pixel.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(pixel.z, 1.0, 1e-6);
}
```

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `cpu_path_tracer_mis_weights_bsdf_sampled_explicit_emitter_hits` fails because BSDF-sampled emissive hits are currently added with full weight even when the hit point belongs to an explicit area light.
- The delta test should already pass or remain green after implementation; keep it as regression coverage for full-weight delta emission.

- [ ] **Step 4: Add previous-bounce state to CPU path tracer**

In `src/backends/cpu/cpu_path_tracer.cpp`, add this struct near the other private structs:

```cpp
struct PreviousBounce {
    bool valid = false;
    bool delta = false;
    Point3f origin;
    float bsdf_pdf = 0.0f;
    int light_sample_count = 1;
};
```

Add this helper near `EnvironmentColor`:

```cpp
float EmissiveHitMisWeight(const RenderScene& scene, const PreviousBounce& previous, Point3f hit_point) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfAreaLightsForPointSolidAngle(scene, previous.origin, hit_point);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}
```

- [ ] **Step 5: Apply MIS weight to emissive hits**

In the `TracePath` function, add previous-bounce state after throughput initialization:

```cpp
Color3f throughput{1.0f, 1.0f, 1.0f};
PreviousBounce previous_bounce;
const int max_depth = std::max(1, scene.max_depth);
```

Replace:

```cpp
radiance = radiance + Multiply(throughput, material.emission);
```

with:

```cpp
if (!IsNearBlack(material.emission)) {
    const float emission_weight = EmissiveHitMisWeight(scene, previous_bounce, hit_point);
    radiance = radiance + Multiply(throughput, material.emission) * emission_weight;
}
```

After validating `bsdf_sample` and before updating `throughput`, set the previous-bounce record:

```cpp
previous_bounce = PreviousBounce{
    true,
    bsdf_sample.specular,
    hit_point,
    bsdf_sample.pdf,
    DirectLightSampleCount(scene)
};
throughput = Multiply(throughput, bsdf_sample.weight);
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bsdf_sample.wi};
```

- [ ] **Step 6: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- BSDF-sampled explicit-emitter MIS test passes.
- Delta emissive hit test passes.
- Existing mirror, direct-light, deterministic, and thread-count tests pass.

- [ ] **Step 7: Commit BSDF-sampled emissive MIS**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: weight emissive hits with mis"
```

## Task 5: Update Documentation And Run Full Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README**

In `README.md`, update the render paragraph sentence that describes the CPU path tracer. Replace the phrase:

```markdown
diffuse BRDF/PDF-weighted direct lighting, explicit emissive hits,
```

with:

```markdown
direct-light MIS that weights explicit area-light samples against BSDF PDFs, MIS-weighted BSDF-sampled emissive hits,
```

Update the limitations sentence so it still names future work:

```markdown
The path integrator still does not implement Russian roulette, environment MIS, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, glass refraction, textures, imported materials, CUDA materials, or other advanced material models.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, update the CPU `path` paragraph by replacing:

```markdown
diffuse BRDF/PDF-weighted direct lighting,
```

with:

```markdown
direct-light MIS over explicit area-light samples and BSDF-sampled emissive hits,
```

Add this paragraph after the BSDF API paragraph:

```markdown
Direct-light MIS is split into render-level helpers and CPU path tracer orchestration. `yaoray_render` owns `PowerHeuristic()` plus current XZ-rectangle area-light sampling and solid-angle PDF math. The CPU path tracer owns random sample generation, shadow visibility, previous-bounce state, and path throughput.
```

Update the future-work paragraph to mention that full Integrator API refactoring should wait until MIS, Russian roulette, environment sampling, or CUDA path tracing make the current path loop too crowded.

- [ ] **Step 3: Run docs scope check**

Run:

```powershell
rg -n "MIS|PowerHeuristic|light_sampling|Integrator API|Russian roulette|environment MIS|CUDA" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-mis-architecture-v1-design.md
```

Expected:

- README mentions direct-light MIS.
- Architecture overview mentions render-level helper ownership.
- Future work still excludes Russian roulette, environment MIS, CUDA path tracing, and full Integrator API refactoring.

- [ ] **Step 4: Commit docs**

Run:

```powershell
git add README.md docs\architecture\overview.md
git commit -m "docs: document direct light mis"
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

- [ ] **Step 7: Render manual scenes**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\cornell_box_path.toml --backend cpu
.\build-release\Release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

If the local generator produced a single-config Ninja build instead of Visual Studio, use:

```powershell
.\build-release\yaoray.exe render .\scenes\examples\cornell_box_path.toml --backend cpu
.\build-release\yaoray.exe render .\scenes\examples\material_v2_showcase.toml --backend cpu
```

Expected output for each render contains:

```text
Integrator: path
Threads:
Samples/sec:
Rays/sec:
Shadow rays:
Rendered image:
```

Do not commit generated files under `scenes/examples/out/`.

- [ ] **Step 8: Run final scope checks**

Run:

```powershell
rg -n "PowerHeuristic|SampleAreaLight|PdfAreaLightSampleSolidAngle|PdfAreaLightsForPointSolidAngle|PreviousBounce|Integrator API|environment MIS|Russian roulette|CUDA path" include src tests README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-mis-architecture-v1-design.md
git status --short --branch
git log --oneline --decorate -12
```

Expected:

- MIS and light sampling helpers appear only in render-level helper files, tests, docs, and CPU path tracer call sites.
- Full Integrator API, environment MIS, Russian roulette, and CUDA path tracing appear only in docs/spec limitations or future-refactor wording.
- Working tree is clean except generated ignored output files.
- Recent commits include:
  - `feat: add mis heuristic helper`
  - `feat: add area light sampling helpers`
  - `feat: apply mis to explicit direct lighting`
  - `feat: weight emissive hits with mis`
  - `docs: document direct light mis`

## Self-Review Checklist

- Spec coverage:
  - Render-level `PowerHeuristic()`: Task 1.
  - Render-level area-light sampling and solid-angle PDFs: Task 2.
  - CPU explicit light samples weighted against `PdfBsdf()`: Task 3.
  - BSDF-sampled emissive hits weighted against area-light PDFs: Task 4.
  - Delta paths keep full emission: Task 4.
  - Existing `render.light_samples` meaning preserved: Task 3 and Task 4.
  - No scene schema changes: no parser tasks in this plan.
  - No Integrator API refactor: only docs mention future trigger.
  - Manual render verification: Task 5.
- Type consistency:
  - `AreaLightSample` is in `yr` namespace and exposed through `include/yaoray/render/light_sampling.hpp`.
  - `PowerHeuristic()` takes two sample-count/PDF pairs.
  - CPU path tracer uses `PdfBsdf()`, `PdfAreaLightSampleSolidAngle()`, and `PdfAreaLightsForPointSolidAngle()`.
- Scope boundaries:
  - No environment light sampling.
  - No light selection distribution.
  - No CUDA implementation.
  - No material changes.
