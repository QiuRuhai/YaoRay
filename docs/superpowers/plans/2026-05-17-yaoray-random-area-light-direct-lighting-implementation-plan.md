# YaoRay Random Area Light Direct Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace center-sampled path direct lighting with deterministic random area-light surface sampling and physically weighted diffuse direct-light estimation.

**Architecture:** Keep `debug_direct` unchanged. Update only the CPU path integrator in `src/backends/cpu/cpu_path_tracer.cpp`, adding local helpers for area-light sampling, diffuse BRDF evaluation, and visibility-weighted direct lighting. Tests exercise behavior only through `RenderCpuPathTrace`, preserving private helper freedom.

**Tech Stack:** C++20, CMake/CTest, current in-repo `yr_test` harness, existing CPU path tracer, BVH shadow traversal, TOML scene examples.

---

## Scope

This plan implements the approved spec:

- `docs/superpowers/specs/2026-05-17-yaoray-random-area-light-direct-lighting-design.md`

It does not implement MIS, Russian roulette, `render.light_samples`, new material types, texture sampling, arbitrary area-light orientation, or CUDA.

## File Structure

- Modify `tests/cpu_path_tracer_tests.cpp`
  - Replace the direct-light-specific test scenes with horizontal diffuse floor scenes that match the new XZ-plane area-light convention.
  - Add tests that fail against center-sampled direct lighting and pass after random area-light sampling and diffuse BRDF weighting.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Include `<optional>`.
  - Add `AreaLightSample`, `SampleAreaLight`, and `EvaluateDiffuseBrdf`.
  - Update `EstimateDirectLight` to take `Rng&`, sample the light surface, apply `albedo / pi`, `cos_surface`, `cos_light`, inverse-square distance, and area-PDF weighting.
  - Update the `TracePath` call site to pass the path RNG into direct lighting.
- Modify `README.md`
  - Document that the path integrator uses random area-light sampling and diffuse BRDF/PDF direct-light weighting.
- Modify `docs/architecture/overview.md`
  - Mirror the README architecture statement and keep current limitations explicit.
- Optional manual output file under `scenes/examples/out/`
  - Generated render outputs remain ignored by existing project conventions and must not be committed.

## Task 1: Add Failing Area-Light Direct-Lighting Tests

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add a floor-scene helper that matches the path area-light convention**

In `tests/cpu_path_tracer_tests.cpp`, after `MakeStochasticEdgeScene`, add these helpers:

```cpp
yr::RenderScene MakeDiffuseFloorScene(std::uint64_t seed = 7) {
    yr::RenderScene scene;
    scene.width = 3;
    scene.height = 3;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = seed;
    scene.camera.origin = yr::Point3f{0.0f, 0.5f, 4.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.5f, -4.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.7f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 0.0f, -3.0f},
        yr::Point3f{0.0f, 0.0f, 3.0f},
        yr::Point3f{3.0f, 0.0f, -3.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    scene.area_lights.push_back(yr::RenderAreaLight{
        yr::Point3f{0.0f, 2.0f, 0.0f},
        2.0f,
        2.0f,
        yr::Color3f{4.0f, 4.0f, 4.0f}
    });
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeBlockedDiffuseFloorScene() {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-3.0f, 1.0f, -3.0f},
        yr::Point3f{0.0f, 1.0f, 3.0f},
        yr::Point3f{3.0f, 1.0f, -3.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        0
    });
    RebuildBvh(scene);
    return scene;
}
```

If this does not compile because `Normalize` is not visible through the current includes, use:

```cpp
scene.camera.forward = yr::Vec3f{0.0f, -0.124034733f, -0.992277861f};
```

- [ ] **Step 2: Update existing direct-light tests to use floor scenes**

Replace `MakeDirectLightScene` with:

```cpp
yr::RenderScene MakeDirectLightScene() {
    return MakeDiffuseFloorScene();
}
```

Replace `MakeShadowedDirectLightScene` with:

```cpp
yr::RenderScene MakeShadowedDirectLightScene() {
    return MakeBlockedDiffuseFloorScene();
}
```

Update `cpu_path_tracer_counts_shadow_occlusion_and_dims_direct_light` to avoid moving the light sideways. Use:

```cpp
YR_TEST(cpu_path_tracer_counts_shadow_occlusion_and_dims_direct_light) {
    const yr::RenderScene unblocked = MakeDiffuseFloorScene();
    const yr::RenderScene blocked = MakeBlockedDiffuseFloorScene();

    const yr::CpuPathTraceResult unblocked_result = yr::RenderCpuPathTrace(unblocked);
    const yr::CpuPathTraceResult blocked_result = yr::RenderCpuPathTrace(blocked);

    YR_EXPECT_TRUE(unblocked_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(unblocked_result.stats.occluded_shadow_rays, std::uint64_t{0});
    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
    YR_EXPECT_TRUE(Luminance(blocked_result.film.LinearPixel(1, 1)) < Luminance(unblocked_result.film.LinearPixel(1, 1)));
}
```

- [ ] **Step 3: Add tests that prove the new estimator behavior**

Add these tests before `cpu_path_tracer_reports_single_thread_when_requested`:

```cpp
YR_TEST(cpu_path_tracer_direct_light_uses_diffuse_brdf_weight) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.area_lights[0].width = 2.0f;
    scene.area_lights[0].height = 2.0f;
    scene.area_lights[0].radiance = yr::Color3f{4.0f, 4.0f, 4.0f};
    RebuildBvh(scene);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
    YR_EXPECT_TRUE(center.x < 2.0f);
    YR_EXPECT_TRUE(center.y < 2.0f);
    YR_EXPECT_TRUE(center.z < 2.0f);
}

YR_TEST(cpu_path_tracer_area_light_sampling_changes_with_seed) {
    yr::RenderScene first_scene = MakeDiffuseFloorScene(11);
    yr::RenderScene second_scene = MakeDiffuseFloorScene(12);
    first_scene.spp = 1;
    second_scene.spp = 1;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(first_scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(second_scene);

    YR_EXPECT_TRUE(!ColorEqual(first.film.LinearPixel(1, 1), second.film.LinearPixel(1, 1)));
}

YR_TEST(cpu_path_tracer_ignores_invalid_area_light_size) {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.area_lights[0].width = 0.0f;
    scene.area_lights[0].height = 2.0f;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}

YR_TEST(cpu_path_tracer_ignores_area_light_behind_surface) {
    yr::RenderScene scene = MakeDiffuseFloorScene();
    scene.area_lights[0].position = yr::Point3f{0.0f, -2.0f, 0.0f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
}
```

Why these fail now:

- `cpu_path_tracer_direct_light_uses_diffuse_brdf_weight` fails because the current estimator omits `1 / pi` and `cos_light`, making the center sample too bright.
- `cpu_path_tracer_area_light_sampling_changes_with_seed` fails because the current direct-light estimator does not consume RNG and always samples the center.

- [ ] **Step 4: Run the focused test binary and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- `yaoray_tests` fails at least on:
  - `cpu_path_tracer_direct_light_uses_diffuse_brdf_weight`
  - `cpu_path_tracer_area_light_sampling_changes_with_seed`

If the floor-scene camera helper does not hit the floor center, fix the helper before implementing production code. Do not weaken assertions.

## Task 2: Implement Random Area-Light Sampling And Diffuse Weighting

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add required include**

Add:

```cpp
#include <optional>
```

near the other standard library includes in `src/backends/cpu/cpu_path_tracer.cpp`.

- [ ] **Step 2: Add helper struct and area-light sampling**

After `struct Rng`, add:

```cpp
struct AreaLightSample {
    Point3f point;
    Vec3f normal{0.0f, -1.0f, 0.0f};
    Color3f radiance;
    float area = 0.0f;
    float pdf_area = 0.0f;
};
```

After `IsValidMaterialIndex`, add:

```cpp
std::optional<AreaLightSample> SampleAreaLight(const RenderAreaLight& light, Rng& rng) {
    const float area = light.width * light.height;
    if (area <= 0.0f) {
        return std::nullopt;
    }

    const float u = rng.NextFloat();
    const float v = rng.NextFloat();
    const float offset_x = (u - 0.5f) * light.width;
    const float offset_z = (v - 0.5f) * light.height;

    return AreaLightSample{
        light.position + Vec3f{offset_x, 0.0f, offset_z},
        Vec3f{0.0f, -1.0f, 0.0f},
        light.radiance,
        area,
        1.0f / area
    };
}

Color3f EvaluateDiffuseBrdf(Color3f albedo) {
    return albedo / Pi;
}
```

- [ ] **Step 3: Replace the direct-light estimator**

Replace the existing `EstimateDirectLight` with:

```cpp
Color3f EstimateDirectLight(
    const RenderScene& scene,
    Point3f hit_point,
    Vec3f normal,
    Color3f albedo,
    Rng& rng,
    CpuPathTraceStats& stats
) {
    Color3f radiance;
    for (const RenderAreaLight& light : scene.area_lights) {
        const std::optional<AreaLightSample> sample = SampleAreaLight(light, rng);
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

        const float cos_light = std::max(0.0f, Dot(sample->normal, -wi));
        if (cos_light <= 0.0f) {
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

        const Color3f brdf = EvaluateDiffuseBrdf(albedo);
        const float geometry = cos_surface * cos_light / distance_squared;
        const float weight = geometry / sample->pdf_area;
        radiance = radiance + Multiply(brdf, sample->radiance) * weight;
    }
    return radiance;
}
```

- [ ] **Step 4: Pass the path RNG into direct lighting**

In `TracePath`, replace:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, stats));
```

with:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, rng, stats));
```

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing thread determinism test still passes.

- [ ] **Step 6: Commit implementation and tests**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: sample area lights in path direct lighting"
```

## Task 3: Update Documentation For New Path Direct Lighting

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README render paragraph**

In `README.md`, replace the last paragraph with:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the CPU path tracer with diffuse bounce, deterministic sampling, random area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, explicit emissive hits, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). The CLI reports actual threads plus samples/sec and rays/sec. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render. The path integrator still does not implement MIS, Russian roulette, configurable light sample counts, arbitrary oriented area lights, or advanced material models.
```

- [ ] **Step 2: Update architecture overview CPU backend paragraph**

In `docs/architecture/overview.md`, replace the CPU backend paragraph with:

```markdown
The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output; it remains single-threaded for reference debugging. `path` is the CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, random XZ-rectangle area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, and row-major tile threading controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). It is still a v0 integrator without MIS, Russian roulette, spectral rendering, random environment sampling, configurable light sample counts, arbitrary oriented area lights, or final-quality material models.
```

- [ ] **Step 3: Run docs-safe verification**

Run:

```powershell
rg -n "random area-light|MIS|Russian roulette|light sample|debug_direct|render.threads" README.md docs\architecture\overview.md
```

Expected:

- README mentions random area-light surface sampling.
- Architecture overview mentions `debug_direct` remains unchanged.
- Limitations mention no MIS, no Russian roulette, no configurable light sample counts.

- [ ] **Step 4: Commit docs**

Run:

```powershell
git add README.md docs\architecture\overview.md
git commit -m "docs: document random area light direct lighting"
```

## Task 4: Full Verification And Manual Cornell Render

**Files:**
- No source edits expected.
- Do not commit generated files under `scenes/examples/out/`.

- [ ] **Step 1: Run full Debug configure, build, and tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

- Configure succeeds.
- Build succeeds.
- `12/12` CTest tests pass.

- [ ] **Step 2: Run Release build**

Run:

```powershell
cmake --build build-release --config Release
```

If `build-release` does not exist, create it first:

```powershell
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build-release --config Release
```

Expected:

- Release `yaoray.exe` builds successfully.

- [ ] **Step 3: Render the threaded Cornell example**

Run:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\cornell_box_path_threaded.toml --backend cpu
```

If the local generator produced a single-config Ninja build instead of Visual Studio, use:

```powershell
.\build-release\yaoray.exe render .\scenes\examples\cornell_box_path_threaded.toml --backend cpu
```

Expected output contains:

```text
Integrator: path
Threads:
Samples/sec:
Rays/sec:
Rendered image: scenes/examples/out/cornell_box_path_threaded.png
```

The image should render successfully. With the same scene settings, the shadow from the ceiling area light should be softer/noisier than the old center-sampled result at low `spp`, and should converge as `spp` increases.

- [ ] **Step 4: Check scope**

Run:

```powershell
rg -n "SampleAreaLight|EvaluateDiffuseBrdf|AreaLightSample|light_samples|MIS|Russian roulette|debug_direct|Cuda|CUDA|RenderAreaLight" src include tests README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-random-area-light-direct-lighting-design.md docs\superpowers\plans\2026-05-17-yaoray-random-area-light-direct-lighting-implementation-plan.md
```

Expected:

- New sampling helpers appear only in path tracer implementation/tests or docs.
- `debug_direct` is documented as unchanged and has no implementation diff.
- `light_samples`, MIS, and Russian roulette appear only as non-goals/limitations.
- CUDA mentions remain existing backend/future-target text only.

- [ ] **Step 5: Confirm clean git state**

Run:

```powershell
git status --short --branch
git log --oneline --decorate -5
```

Expected:

- Working tree is clean.
- Recent commits include:
  - `feat: sample area lights in path direct lighting`
  - `docs: document random area light direct lighting`

## Self-Review Checklist

- Spec coverage:
  - Random area-light surface sampling: Task 2.
  - Diffuse `albedo / pi` direct-light weight: Task 2.
  - `cos_surface`, `cos_light`, inverse-square distance, area PDF: Task 2.
  - Determinism and thread independence: Task 1 tests, Task 4 full tests.
  - `debug_direct` unchanged: Task 3 docs, Task 4 scope check.
  - No MIS/Russian roulette/light_samples: Task 3 docs, Task 4 scope check.
- Placeholder scan:
  - No task uses "TBD", "TODO", or unspecified tests.
- Type consistency:
  - `AreaLightSample`, `SampleAreaLight`, `EvaluateDiffuseBrdf`, and `EstimateDirectLight` signatures are defined before use.
  - Existing public API remains unchanged.
