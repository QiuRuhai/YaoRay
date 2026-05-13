# YaoRay CPU Direct Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the CPU debug renderer's fake normal-facing hit shading with deterministic direct lighting from scene area lights.

**Architecture:** Keep this slice inside `yaoray_backends`, using the existing `RenderScene`, `RenderMaterial`, `RenderAreaLight`, and `IntersectBvh()` APIs. Extend backend stats with explicit shadow-ray counts, then update `RenderCpuDebug()` so camera hits accumulate material emission plus Lambert direct light from center-sampled area lights with BVH shadow rays.

**Tech Stack:** C++20, CMake 3.24+, CTest, existing YaoRay `core`, `render`, `backends`, `film`, and custom `yr_test` harness.

---

## Scope Check

This plan implements only the approved CPU direct lighting design:

- shadow-ray stats in CPU/backend result types
- CLI output for shadow-ray stats
- deterministic center-sampled area-light direct lighting
- Lambert diffuse using `RenderMaterial::albedo`
- direct material emission
- BVH shadow rays
- tests for emission, lighting, light-behind-surface, shadows, and stats
- example scenes with area lights
- docs updates

It does not implement path tracing, secondary bounces, random sampling, soft shadows, multiple importance sampling, material TOML syntax, texture import, OBJ `.mtl`, light orientation, a public Integrator abstraction, or CUDA rendering.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/backends/backend.hpp
include/yaoray/backends/cpu/cpu_debug_renderer.hpp
src/app/main.cpp
src/backends/cpu/cpu_debug_backend.cpp
src/backends/cpu/cpu_debug_renderer.cpp
tests/backend_tests.cpp
tests/cpu_debug_renderer_tests.cpp
tests/fixtures/scene/builtin_triangle.toml
tests/fixtures/scene/obj_quad.toml
scenes/examples/minimal.toml
scenes/examples/obj_pyramid.toml
```

Responsibilities:

- `include/yaoray/backends/backend.hpp`: public render stats exposed to app/CLI.
- `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`: CPU debug render stats.
- `src/backends/cpu/cpu_debug_backend.cpp`: maps CPU stats to backend stats.
- `src/backends/cpu/cpu_debug_renderer.cpp`: direct lighting, emission, face-forward normals, shadow rays, and shadow stats.
- `tests/cpu_debug_renderer_tests.cpp`: deterministic renderer tests for the new lighting behavior.
- `tests/backend_tests.cpp`: backend stat mapping coverage.
- `src/app/main.cpp`: prints shadow-ray stats.
- `CMakeLists.txt`: CLI tests check shadow-ray output.
- Scene files: examples and CLI fixtures include area lights.
- Docs: describe direct lighting as implemented and keep path tracing/materials in future work.

## Task 1: Add Shadow-Ray Stats Plumbing

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `tests/cpu_debug_renderer_tests.cpp`

- [ ] **Step 1: Add failing stat expectations**

In `tests/cpu_debug_renderer_tests.cpp`, in `cpu_debug_renderer_traces_one_ray_per_pixel`, add these assertions after `YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});`:

```cpp
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
```

In `tests/backend_tests.cpp`, in `cpu_backend_renders_film_and_stats`, add these assertions after `YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});`:

```cpp
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `shadow_rays` and `occluded_shadow_rays` do not exist on `CpuDebugRenderStats` or `RenderStats`.

Do not commit this failing state.

- [ ] **Step 3: Add shadow-ray fields to stats types**

In `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`, add these fields after `rays_traced`:

```cpp
    std::uint64_t shadow_rays = 0;
    std::uint64_t occluded_shadow_rays = 0;
```

In `include/yaoray/backends/backend.hpp`, add these fields after `rays_traced`:

```cpp
    std::uint64_t shadow_rays = 0;
    std::uint64_t occluded_shadow_rays = 0;
```

- [ ] **Step 4: Map CPU stats to backend stats**

In `src/backends/cpu/cpu_debug_backend.cpp`, update `ToRenderStats()` so the block after `result.rays_traced = stats.rays_traced;` becomes:

```cpp
    result.rays_traced = stats.rays_traced;
    result.shadow_rays = stats.shadow_rays;
    result.occluded_shadow_rays = stats.occluded_shadow_rays;
    result.triangle_tests = stats.triangle_tests;
```

- [ ] **Step 5: Run backend and renderer tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_debug_renderer.hpp src/backends/cpu/cpu_debug_backend.cpp tests/backend_tests.cpp tests/cpu_debug_renderer_tests.cpp
git commit -m "feat: add shadow ray render stats"
```

## Task 2: Implement Direct Lighting In The CPU Renderer

**Files:**
- Modify: `src/backends/cpu/cpu_debug_renderer.cpp`
- Modify: `tests/cpu_debug_renderer_tests.cpp`

- [ ] **Step 1: Add test helpers for direct lighting cases**

In `tests/cpu_debug_renderer_tests.cpp`, inside the anonymous namespace after `MakeDebugTriangleScene(...)`, add:

```cpp
void RebuildBvh(yr::RenderScene& scene) {
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
}

void AddCenterLight(yr::RenderScene& scene, yr::Point3f position, yr::Color3f radiance) {
    scene.area_lights.push_back(yr::RenderAreaLight{
        position,
        1.0f,
        1.0f,
        radiance
    });
}
```

Then simplify `MakeDebugTriangleScene(...)` by replacing its local BVH build block:

```cpp
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
```

with:

```cpp
    RebuildBvh(scene);
```

- [ ] **Step 2: Add failing direct-lighting tests**

Append these tests to `tests/cpu_debug_renderer_tests.cpp`:

```cpp
YR_TEST(cpu_debug_renderer_adds_material_emission_on_hits) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_lights_front_facing_diffuse_hits) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    AddCenterLight(scene, yr::Point3f{0.0f, 0.0f, 2.0f}, yr::Color3f{4.0f, 4.0f, 4.0f});

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(center.y, 0.2, 1e-5);
    YR_EXPECT_NEAR(center.z, 0.1, 1e-5);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_ignores_lights_behind_surface) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    AddCenterLight(scene, yr::Point3f{0.0f, 0.0f, -2.0f}, yr::Color3f{10.0f, 10.0f, 10.0f});

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.0, 1e-6);
    YR_EXPECT_EQ(result.stats.shadow_rays, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_debug_renderer_shadow_ray_blocks_direct_light) {
    yr::RenderScene unblocked = MakeDebugTriangleScene(3, 3);
    AddCenterLight(unblocked, yr::Point3f{0.0f, 2.0f, 2.0f}, yr::Color3f{8.0f, 8.0f, 8.0f});

    yr::RenderScene blocked = unblocked;
    blocked.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.75f, 1.0f, 0.25f},
        yr::Point3f{0.75f, 1.0f, 0.25f},
        yr::Point3f{0.0f, 1.0f, 1.75f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        0
    });
    RebuildBvh(blocked);

    const yr::CpuDebugRenderResult unblocked_result = yr::RenderCpuDebug(unblocked);
    const yr::CpuDebugRenderResult blocked_result = yr::RenderCpuDebug(blocked);
    const yr::Color3f unblocked_center = unblocked_result.film.LinearPixel(1, 1);
    const yr::Color3f blocked_center = blocked_result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(unblocked_center.x > 0.5f);
    YR_EXPECT_TRUE(blocked_center.x < unblocked_center.x * 0.1f);
    YR_EXPECT_TRUE(blocked_result.stats.shadow_rays > 0);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}
```

- [ ] **Step 3: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `yaoray_tests` fails. The new emission and direct-lighting tests fail because the current renderer still uses debug normal-facing shading and never casts shadow rays.

Do not commit this failing state.

- [ ] **Step 4: Replace debug hit shading with direct lighting**

In `src/backends/cpu/cpu_debug_renderer.cpp`, add these constants and helpers inside the anonymous namespace after `EnvironmentColor(...)`:

```cpp
constexpr float ShadowEpsilon = 1.0e-4f;

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

bool IsValidMaterialIndex(const RenderScene& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(CpuDebugRenderStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
}
```

Replace the existing `ShadeHit(...)` function:

```cpp
Color3f ShadeHit(const RenderScene& scene, const Ray3f& ray, const RenderTriangle& triangle) {
    if (triangle.material_index < 0 ||
        static_cast<std::size_t>(triangle.material_index) >= scene.materials.size()) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const float normal_lighting = std::max(0.15f, std::fabs(Dot(triangle.normal, -ray.direction)));
    return material.albedo * normal_lighting + material.emission;
}
```

with:

```cpp
Color3f ShadeHit(
    const RenderScene& scene,
    const Ray3f& ray,
    const BvhHit& hit,
    CpuDebugRenderStats& stats
) {
    if (hit.triangle == nullptr || !IsValidMaterialIndex(scene, hit.triangle->material_index)) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderTriangle& triangle = *hit.triangle;
    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const Point3f hit_point = ray.origin + ray.direction * hit.t;
    const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);

    Color3f radiance = material.emission;
    for (const RenderAreaLight& light : scene.area_lights) {
        const float area = light.width * light.height;
        if (area <= 0.0f) {
            continue;
        }

        const Vec3f to_light = light.position - hit_point;
        const float distance_squared = LengthSquared(to_light);
        if (distance_squared <= ShadowEpsilon * ShadowEpsilon) {
            continue;
        }

        const float distance = std::sqrt(distance_squared);
        const Vec3f wi = to_light / distance;
        const float n_dot_l = std::max(0.0f, Dot(normal, wi));
        if (n_dot_l <= 0.0f) {
            continue;
        }

        ++stats.shadow_rays;
        BvhTraceStats shadow_trace;
        const Ray3f shadow_ray{hit_point + normal * ShadowEpsilon, wi};
        const BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_trace);
        AccumulateTraceStats(stats, shadow_trace);
        if (shadow_hit.hit && shadow_hit.t < distance - ShadowEpsilon) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float scale = area * n_dot_l / distance_squared;
        radiance = radiance + Multiply(material.albedo, light.radiance) * scale;
    }

    return radiance;
}
```

In `RenderCpuDebug(...)`, replace:

```cpp
            result.stats.bvh_node_tests += trace_stats.node_tests;
            result.stats.triangle_tests += trace_stats.triangle_tests;
            if (hit.hit && hit.triangle != nullptr) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(scene, ray, *hit.triangle));
```

with:

```cpp
            AccumulateTraceStats(result.stats, trace_stats);
            if (hit.hit && hit.triangle != nullptr) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(scene, ray, hit, result.stats));
```

- [ ] **Step 5: Run renderer tests and full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

Run:

```powershell
git add src/backends/cpu/cpu_debug_renderer.cpp tests/cpu_debug_renderer_tests.cpp
git commit -m "feat: add cpu direct lighting"
```

## Task 3: Expose Shadow Stats In CLI And CLI Tests

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing CLI expectations**

In `CMakeLists.txt`, update `yaoray_cli_render_cpu` so the PowerShell command also checks:

```powershell
if ($out -notmatch 'Shadow rays:') { exit 1 }; if ($out -notmatch 'Occluded shadow rays:') { exit 1 };
```

The full `yaoray_cli_render_cpu` command becomes:

```cmake
    add_test(NAME yaoray_cli_render_cpu
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if ($out -notmatch 'Occluded shadow rays:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

Update `yaoray_cli_render_obj` similarly:

```cmake
    add_test(NAME yaoray_cli_render_obj
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if ($out -notmatch 'Occluded shadow rays:') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 2: Run CTest to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CLI render tests fail because `yaoray render` does not print shadow-ray stats yet.

Do not commit this failing state.

- [ ] **Step 3: Print shadow-ray stats in the CLI**

In `src/app/main.cpp`, after:

```cpp
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
```

add:

```cpp
    std::cout << "Shadow rays: " << render_result.stats.shadow_rays << '\n';
    std::cout << "Occluded shadow rays: " << render_result.stats.occluded_shadow_rays << '\n';
```

- [ ] **Step 4: Run full tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 5: Commit**

Run:

```powershell
git add CMakeLists.txt src/app/main.cpp
git commit -m "feat: expose direct lighting stats"
```

## Task 4: Add Area Lights To Renderable Scenes And Update Docs

**Files:**
- Modify: `tests/fixtures/scene/builtin_triangle.toml`
- Modify: `tests/fixtures/scene/obj_quad.toml`
- Modify: `scenes/examples/minimal.toml`
- Modify: `scenes/examples/obj_pyramid.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add area lights to CLI fixtures**

In `tests/fixtures/scene/builtin_triangle.toml`, add this block before `[environment]`:

```toml
[[lights]]
type = "area"
position = [0, 0, 2]
size = [1, 1]
radiance = [4, 4, 4]
```

In `tests/fixtures/scene/obj_quad.toml`, add this block before `[environment]`:

```toml
[[lights]]
type = "area"
position = [0, 1.5, 2]
size = [1, 1]
radiance = [6, 6, 6]
```

- [ ] **Step 2: Add area lights to human-facing examples**

In `scenes/examples/minimal.toml`, add this block before `[environment]`:

```toml
[[lights]]
type = "area"
position = [0, 0, 2]
size = [2, 2]
radiance = [4, 4, 4]
```

In `scenes/examples/obj_pyramid.toml`, add this block before `[environment]`:

```toml
[[lights]]
type = "area"
position = [0, 2.5, 3]
size = [2, 2]
radiance = [8, 8, 8]
```

- [ ] **Step 3: Update README**

In `README.md`, replace this status bullet:

```markdown
- CPU debug rendering of compiled triangle scenes to ASCII PPM
```

with:

```markdown
- CPU rendering with deterministic area-light direct lighting and BVH shadow rays
```

Replace this future-work sentence:

```markdown
Final path tracing quality, material and texture import, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

with:

```markdown
Final path tracing quality, material and texture import, soft shadows, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

Replace this run description:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders CPU debug images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

with:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU direct-lighting images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default and include simple center-sampled area lights. This is still a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 4: Update architecture overview**

In `docs/architecture/overview.md`, replace this implemented slice bullet:

```markdown
- CPU debug rendering to PPM for the first image-output loop
```

with:

```markdown
- CPU rendering with deterministic area-light direct lighting and BVH shadow rays
```

Replace this paragraph:

```markdown
The CPU debug renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, Film accumulation, tone mapping, and PNG/PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

with:

```markdown
The CPU renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

Replace this future-work sentence:

```markdown
Material and texture import, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

with:

```markdown
Material and texture import, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 5: Run docs smoke check and full tests**

Run:

```powershell
rg -n "direct lighting|area-light|area light|shadow rays|Shadow rays|soft shadows|path tracer" README.md docs/architecture/overview.md CMakeLists.txt src/app/main.cpp scenes/examples tests/fixtures/scene
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Manually render examples**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
```

Expected output includes:

```text
Shadow rays:
Occluded shadow rays:
Rendered image: scenes/examples/out/minimal.png
Rendered image: scenes/examples/out/obj_pyramid.png
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add README.md docs/architecture/overview.md tests/fixtures/scene/builtin_triangle.toml tests/fixtures/scene/obj_quad.toml scenes/examples/minimal.toml scenes/examples/obj_pyramid.toml
git commit -m "docs: describe cpu direct lighting"
```

## Task 5: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm implementation ownership**

Run:

```powershell
rg -n "ShadowEpsilon|ShadeHit|shadow_rays|occluded_shadow_rays|FaceForward|Multiply\\(|IntersectBvh" include src tests CMakeLists.txt
```

Expected:

- `shadow_rays` and `occluded_shadow_rays` appear in backend stats headers, CPU backend mapping, app output, and tests.
- `ShadeHit`, `ShadowEpsilon`, `FaceForward`, and local color multiplication appear only in `src/backends/cpu/cpu_debug_renderer.cpp`.
- `IntersectBvh` remains owned by `yaoray_render` and is consumed by the CPU renderer.

- [ ] **Step 2: Confirm non-goals are not implemented**

Run:

```powershell
rg -n "Integrator|BSDF|MIS|random|mtl|material_id|texture|soft shadow|path tracer" include src tests scenes README.md docs/architecture/overview.md
```

Expected:

- Matches may appear in docs as future work or non-goals.
- There are no new production `Integrator`, BSDF, random sampling, material TOML, texture import, `.mtl`, or path tracer implementations.

- [ ] **Step 3: Run full Debug verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Verify built-in PNG output and stats**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\minimal.png) { Remove-Item -LiteralPath scenes\examples\out\minimal.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\minimal.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Compiled triangles: 1
Shadow rays:
Occluded shadow rays:
Rendered image: scenes/examples/out/minimal.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
```

- [ ] **Step 5: Verify OBJ PNG output and stats**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\obj_pyramid.png) { Remove-Item -LiteralPath scenes\examples\out\obj_pyramid.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\obj_pyramid.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Compiled triangles: 6
Shadow rays:
Occluded shadow rays:
Rendered image: scenes/examples/out/obj_pyramid.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
```

- [ ] **Step 6: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- Replace debug normal hit shading: Task 2.
- Use `RenderMaterial::albedo` as Lambert diffuse: Task 2.
- Add `RenderMaterial::emission` directly: Task 2 tests and implementation.
- Use every `RenderAreaLight` as a center sample: Task 2.
- Cast BVH shadow rays: Task 2.
- Keep environment misses: Task 2 preserves existing test.
- Accumulate primary and shadow BVH stats: Tasks 1 and 2.
- Add explicit shadow stats and CLI output: Tasks 1 and 3.
- Update examples with area lights: Task 4.
- Docs: Task 4.

Type consistency:

- CPU-local stats type is `CpuDebugRenderStats`.
- Backend-facing stats type is `RenderStats`.
- Shadow fields are `shadow_rays` and `occluded_shadow_rays` in both structs.
- App output labels are `Shadow rays:` and `Occluded shadow rays:`.
- Direct lighting stays in `src/backends/cpu/cpu_debug_renderer.cpp`.

Implementation guardrails:

- Do not add an Integrator abstraction.
- Do not change scene parser material syntax.
- Do not add random sampling or soft shadows.
- Do not change `IntersectBvh()` API.
- Do not make render modules depend on backend modules.
