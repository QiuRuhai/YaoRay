# YaoRay RenderSceneIR CPU Prepared Scene Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split backend-neutral render input from CPU-specific prepared data by introducing `RenderSceneIR` and `CpuPreparedScene`.

**Architecture:** `CompileScene()` should produce backend-neutral render data with no BVH. CPU backend preparation should build and own the CPU `RenderBvh`, then pass a prepared scene to CPU debug and path integrators. This phase preserves current TOML syntax, asset loading behavior, rendered output, and stats.

**Tech Stack:** C++20, CMake, custom `YR_TEST` harness, existing CPU debug renderer, CPU path tracer, BVH utilities, CTest.

---

## Scope Check

This plan implements Phase 1 from the asset resource architecture refactor design:

- Introduce `RenderSceneIR`.
- Remove `RenderBvh` from the shared render compiler output.
- Add `CpuPreparedScene`.
- Make CPU rendering build and consume the prepared BVH internally.

This plan does not introduce `AssetResource`, does not change TOML syntax, and does not change OBJ/glTF loader behavior.

## File Structure

- Modify `include/yaoray/render/render_scene.hpp`: rename the shared render input type to `RenderSceneIR` and remove `RenderBvh bvh`.
- Modify `include/yaoray/render/scene_compiler.hpp`: make `SceneCompileResult` hold `RenderSceneIR`.
- Modify `src/render/scene_compiler.cpp`: compile to `RenderSceneIR` and stop building BVH.
- Modify `include/yaoray/render/bvh.hpp`: make `IntersectBvh()` take explicit `RenderSceneIR` and `RenderBvh` arguments.
- Modify `src/render/bvh.cpp`: remove dependency on `scene.bvh`.
- Create `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`: CPU prepared scene data and prepare API.
- Create `src/backends/cpu/cpu_prepared_scene.cpp`: build CPU BVH from `RenderSceneIR::triangles`.
- Modify `include/yaoray/backends/backend.hpp`: `RenderBackend::Render()` accepts `RenderSceneIR`.
- Modify `include/yaoray/backends/cpu/cpu_debug_backend.hpp`: update backend override signature.
- Modify `src/backends/backend.cpp`: CUDA stub accepts `RenderSceneIR`.
- Modify `src/backends/cpu/cpu_debug_backend.cpp`: prepare CPU scene before dispatching integrators.
- Modify `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`: `RenderCpuDebug()` accepts `CpuPreparedScene`.
- Modify `src/backends/cpu/cpu_debug_renderer.cpp`: read scene data through `CpuPreparedScene` and use explicit BVH.
- Modify `include/yaoray/backends/cpu/cpu_path_tracer.hpp`: `RenderCpuPathTrace()` accepts `CpuPreparedScene`.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: read scene data through `CpuPreparedScene` and use explicit BVH.
- Modify `src/app/main.cpp`: remove pre-render BVH printing from compiler output and rely on render stats.
- Modify `CMakeLists.txt`: add `src/backends/cpu/cpu_prepared_scene.cpp` to `yaoray_backends`.
- Modify `tests/render_scene_tests.cpp`: assert compiler output is backend-neutral and remove BVH expectations.
- Modify `tests/backend_tests.cpp`: add CPU prepare tests and stop manually injecting BVH into render input.
- Modify `tests/bvh_tests.cpp`: update direct BVH intersection helpers to pass explicit BVH.
- Modify `tests/cpu_debug_renderer_tests.cpp`: prepare CPU scenes before direct CPU debug rendering.
- Modify `tests/cpu_path_tracer_tests.cpp`: prepare CPU scenes before direct CPU path tracing.
- Modify `tests/environment_tests.cpp` and `tests/light_sampling_tests.cpp`: rename `RenderScene` fixture types to `RenderSceneIR`; these tests remain IR-only and do not prepare a CPU BVH.
- Modify `README.md` and `docs/architecture/overview.md`: document that BVH is now CPU prepared data, not compiler IR.
- Modify `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-architecture-refactor-design.md`: append Phase 1 implementation status after completion.

## Pre-Flight

- [ ] **Step 1: Confirm branch and dirty state**

Run:

```bash
git status --short --branch --untracked-files=normal
```

Expected: branch is `main`, ahead of origin by the design commits, and only this plan file is dirty before implementation starts.

- [ ] **Step 2: Build current baseline**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build succeeds and `yaoray_tests` passes.

- [ ] **Step 3: Record known full CTest limitation on macOS**

Run:

```bash
ctest --test-dir build --output-on-failure -C Debug
```

Expected on this macOS workspace: `yaoray_tests`, help, version, and render help pass; PowerShell-backed CLI render tests fail to run because `powershell` is not installed. Do not treat the missing PowerShell executable as a renderer regression.

---

### Task 1: Add Failing Contract Tests For Backend-Neutral IR

**Files:**
- Modify: `tests/render_scene_tests.cpp`
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Update render scene default test to use `RenderSceneIR`**

In `tests/render_scene_tests.cpp`, replace `render_scene_defaults_are_backend_friendly` with:

```cpp
YR_TEST(render_scene_ir_defaults_are_backend_friendly) {
    const yr::RenderSceneIR scene;

    YR_EXPECT_EQ(scene.requested_backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.integrator, yr::RenderIntegratorKind::DebugDirect);
    YR_EXPECT_EQ(scene.sampler, yr::RenderSamplerKind::Independent);
    YR_EXPECT_EQ(scene.width, 0);
    YR_EXPECT_EQ(scene.height, 0);
    YR_EXPECT_EQ(scene.spp, 1);
    YR_EXPECT_EQ(scene.max_depth, 5);
    YR_EXPECT_EQ(scene.seed, std::uint64_t{0});
    YR_EXPECT_EQ(scene.threads, 0);
    YR_EXPECT_EQ(scene.light_samples, 1);
    YR_EXPECT_NEAR(scene.radiance_clamp, 0.0, 1e-6);
    YR_EXPECT_TRUE(scene.triangles.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.area_lights.empty());

    const yr::RenderMaterial material;
    YR_EXPECT_EQ(material.type, yr::MaterialKind::Diffuse);
    YR_EXPECT_NEAR(material.roughness, 0.0, 1e-6);
    YR_EXPECT_NEAR(material.specular, 0.04, 1e-6);
    YR_EXPECT_NEAR(material.ior, 1.5, 1e-6);
    YR_EXPECT_TRUE(!material.thin);
    YR_EXPECT_NEAR(material.absorption_color.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_color.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_color.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(material.absorption_distance, 1.0, 1e-6);
}
```

- [ ] **Step 2: Update render settings compiler test to expect `requested_backend`**

In `tests/render_scene_tests.cpp`, inside `scene_compiler_copies_render_settings`, replace:

```cpp
const yr::RenderScene& compiled = result.scene.value();
YR_EXPECT_EQ(compiled.backend, yr::RenderBackendKind::Cuda);
```

with:

```cpp
const yr::RenderSceneIR& compiled = result.scene.value();
YR_EXPECT_EQ(compiled.requested_backend, yr::RenderBackendKind::Cuda);
```

- [ ] **Step 3: Remove compiler BVH output tests**

In `tests/render_scene_tests.cpp`, delete these tests:

```cpp
YR_TEST(scene_compiler_builds_empty_bvh_for_empty_scene) { ... }
YR_TEST(scene_compiler_builds_bvh_for_builtin_triangle) { ... }
YR_TEST(scene_compiler_builds_bvh_for_obj_quad) { ... }
```

Replace them with:

```cpp
YR_TEST(scene_compiler_outputs_backend_neutral_triangles_for_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
}

YR_TEST(scene_compiler_outputs_backend_neutral_triangles_for_obj_quad) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
}
```

- [ ] **Step 4: Add CPU prepare tests**

In `tests/backend_tests.cpp`, add:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Replace `MakeBackendTriangleScene()` with an IR-only helper:

```cpp
yr::RenderSceneIR MakeBackendTriangleScene(int width = 4, int height = 3) {
    yr::RenderSceneIR scene;
    scene.requested_backend = yr::RenderBackendKind::Cpu;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.05f, 0.10f, 0.15f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 0.2f, 0.1f},
        yr::Color3f{}
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}
```

Add this test after `create_render_backend_returns_cpu_backend`:

```cpp
YR_TEST(cpu_prepare_scene_builds_bvh_from_render_scene_ir) {
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);

    const yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);

    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.error.empty());
    YR_EXPECT_TRUE(prepared.scene.has_value());
    YR_EXPECT_TRUE(prepared.scene->render_scene == &scene);
    YR_EXPECT_EQ(prepared.scene->bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(prepared.scene->bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(prepared.scene->bvh.max_depth, 1);
}
```

- [ ] **Step 5: Verify the tests fail**

Run:

```bash
cmake --build build --config Debug
```

Expected: compile fails because `RenderSceneIR`, `requested_backend`, `cpu_prepared_scene.hpp`, `CpuPrepareResult`, and `PrepareCpuScene()` do not exist yet.

---

### Task 2: Introduce RenderSceneIR And Stop Compiler BVH Construction

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `include/yaoray/render/scene_compiler.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `src/app/main.cpp`

- [ ] **Step 1: Replace shared render input type**

In `include/yaoray/render/render_scene.hpp`, remove `#include <yaoray/render/bvh.hpp>` and replace `struct RenderScene` with:

```cpp
struct RenderSceneIR {
    RenderBackendKind requested_backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    float radiance_clamp = 0.0f;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
};
```

- [ ] **Step 2: Update scene compiler result type**

In `include/yaoray/render/scene_compiler.hpp`, replace:

```cpp
std::optional<RenderScene> scene;
```

with:

```cpp
std::optional<RenderSceneIR> scene;
```

- [ ] **Step 3: Rename compiler local type and backend field**

In `src/render/scene_compiler.cpp`, change helper signatures from `RenderScene& compiled` to `RenderSceneIR& compiled`.

In `CompileScene()`, replace:

```cpp
RenderScene compiled;
compiled.backend = scene.render.backend;
```

with:

```cpp
RenderSceneIR compiled;
compiled.requested_backend = scene.render.backend;
```

- [ ] **Step 4: Remove BVH build from compiler**

In `src/render/scene_compiler.cpp`, delete this block near the end of `CompileScene()`:

```cpp
BvhBuildResult bvh_result = BuildBvh(compiled.triangles);
for (const std::string& error : bvh_result.errors) {
    result.diagnostics.push_back(Error(scene, "render.bvh", error));
}
if (HasSceneErrors(result.diagnostics)) {
    return result;
}
compiled.bvh = std::move(bvh_result.bvh);
```

Keep:

```cpp
result.scene = std::move(compiled);
return result;
```

- [ ] **Step 5: Update app render flow**

In `src/app/main.cpp`, replace:

```cpp
const yr::RenderScene& render_scene = compile_result.scene.value();
std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
```

with:

```cpp
const yr::RenderSceneIR& render_scene = compile_result.scene.value();
std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
```

Delete the pre-render BVH prints:

```cpp
std::cout << "BVH nodes: " << render_scene.bvh.nodes.size() << '\n';
std::cout << "BVH max depth: " << render_scene.bvh.max_depth << '\n';
```

Replace backend creation:

```cpp
const auto backend = yr::CreateRenderBackend(render_scene.backend);
```

with:

```cpp
const auto backend = yr::CreateRenderBackend(render_scene.requested_backend);
```

Replace backend error print:

```cpp
std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.backend) << '\n';
```

with:

```cpp
std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.requested_backend) << '\n';
```

- [ ] **Step 6: Verify current compile failures are limited to old type/API references**

Run:

```bash
cmake --build build --config Debug
```

Expected: build still fails in backend, BVH, and tests because they still refer to `RenderScene`, `scene.backend`, or `scene.bvh`.

---

### Task 3: Add CPUPreparedScene And Explicit BVH Intersection

**Files:**
- Create: `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`
- Create: `src/backends/cpu/cpu_prepared_scene.cpp`
- Modify: `include/yaoray/render/bvh.hpp`
- Modify: `src/render/bvh.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/bvh_tests.cpp`

- [ ] **Step 1: Add CPU prepared scene header**

Create `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`:

```cpp
#pragma once

#include <optional>
#include <string>

#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuPreparedScene {
    const RenderSceneIR* render_scene = nullptr;
    RenderBvh bvh;

    const RenderSceneIR& Scene() const {
        return *render_scene;
    }
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
};

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene);

} // namespace yr
```

- [ ] **Step 2: Add CPU prepared scene implementation**

Create `src/backends/cpu/cpu_prepared_scene.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

namespace yr {

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene) {
    CpuPrepareResult result;

    BvhBuildResult build = BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene = CpuPreparedScene{&scene, std::move(build.bvh)};
    return result;
}

} // namespace yr
```

- [ ] **Step 3: Add implementation file to CMake**

In `CMakeLists.txt`, add `src/backends/cpu/cpu_prepared_scene.cpp` to `yaoray_backends`:

```cmake
add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
    src/backends/cpu/cpu_prepared_scene.cpp
    src/backends/cpu/cpu_tile_scheduler.cpp
    src/backends/cpu/cpu_sampler.cpp
    src/backends/cpu/cpu_path_tracer.cpp
)
```

- [ ] **Step 4: Change BVH intersection signature**

In `include/yaoray/render/bvh.hpp`, replace the forward declaration:

```cpp
struct RenderScene;
```

with:

```cpp
struct RenderSceneIR;
```

Replace `IntersectBvh()` declaration with:

```cpp
BvhHit IntersectBvh(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats
);
```

- [ ] **Step 5: Update BVH intersection implementation**

In `src/render/bvh.cpp`, replace `IntersectBvh()` with:

```cpp
BvhHit IntersectBvh(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats
) {
    BvhHit nearest;
    if (bvh.nodes.empty()) {
        return nearest;
    }

    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        const int node_index = stack.back();
        stack.pop_back();
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= bvh.nodes.size()) {
            continue;
        }

        const RenderBvhNode& node = bvh.nodes[static_cast<std::size_t>(node_index)];
        ++stats.node_tests;
        if (!node.bounds.Intersects(ray, MinHitT, nearest.t)) {
            continue;
        }

        if (node.triangle_count > 0) {
            for (int i = 0; i < node.triangle_count; ++i) {
                const int index_position = node.first_triangle + i;
                if (index_position < 0 ||
                    static_cast<std::size_t>(index_position) >= bvh.triangle_indices.size()) {
                    continue;
                }

                const int triangle_index = bvh.triangle_indices[static_cast<std::size_t>(index_position)];
                if (triangle_index < 0 ||
                    static_cast<std::size_t>(triangle_index) >= scene.triangles.size()) {
                    continue;
                }

                ++stats.triangle_tests;
                float t = 0.0f;
                const RenderTriangle& triangle = scene.triangles[static_cast<std::size_t>(triangle_index)];
                if (IntersectTriangle(ray, triangle, t) && t < nearest.t) {
                    nearest.hit = true;
                    nearest.t = t;
                    nearest.triangle = &triangle;
                    nearest.triangle_index = triangle_index;
                }
            }
        } else {
            if (node.right_child >= 0) {
                stack.push_back(node.right_child);
            }
            if (node.left_child >= 0) {
                stack.push_back(node.left_child);
            }
        }
    }

    return nearest;
}
```

- [ ] **Step 6: Update BVH tests**

In `tests/bvh_tests.cpp`, replace the helper:

```cpp
yr::RenderScene MakeBvhScene(std::vector<yr::RenderTriangle> triangles)
```

with:

```cpp
struct TestBvhScene {
    yr::RenderSceneIR scene;
    yr::RenderBvh bvh;
};

TestBvhScene MakeBvhScene(std::vector<yr::RenderTriangle> triangles) {
    TestBvhScene test_scene;
    test_scene.scene.triangles = std::move(triangles);
    const yr::BvhBuildResult build = yr::BuildBvh(test_scene.scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    test_scene.bvh = build.bvh;
    return test_scene;
}
```

Replace each direct BVH trace call:

```cpp
const yr::BvhHit hit = yr::IntersectBvh(scene, ray, stats);
```

with:

```cpp
const yr::BvhHit hit = yr::IntersectBvh(scene.scene, scene.bvh, ray, stats);
```

- [ ] **Step 7: Verify BVH and prepare compile path**

Run:

```bash
cmake --build build --config Debug
```

Expected: compile errors now point mostly to CPU debug/path/backend callers that still pass plain render input instead of `CpuPreparedScene`.

---

### Task 4: Move CPU Debug Renderer To Prepared Scene

**Files:**
- Modify: `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`
- Modify: `src/backends/cpu/cpu_debug_renderer.cpp`
- Modify: `tests/cpu_debug_renderer_tests.cpp`

- [ ] **Step 1: Update debug renderer header**

In `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`, replace:

```cpp
#include <yaoray/render/render_scene.hpp>
```

with:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Replace:

```cpp
CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene);
```

with:

```cpp
CpuDebugRenderResult RenderCpuDebug(const CpuPreparedScene& prepared_scene);
```

- [ ] **Step 2: Update debug renderer implementation signatures**

In `src/backends/cpu/cpu_debug_renderer.cpp`, replace helper signatures that take `const RenderScene&` with `const RenderSceneIR&` unless they need BVH tracing.

Use this pattern at the top of `RenderCpuDebug()`:

```cpp
CpuDebugRenderResult RenderCpuDebug(const CpuPreparedScene& prepared_scene) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    CpuDebugRenderResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(prepared_scene.bvh.nodes.size());
    result.stats.bvh_max_depth = prepared_scene.bvh.max_depth;
    const auto start = std::chrono::steady_clock::now();
```

- [ ] **Step 3: Update primary ray BVH trace**

In `RenderCpuDebug()`, replace:

```cpp
const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
```

with:

```cpp
const BvhHit hit = IntersectBvh(scene, prepared_scene.bvh, ray, trace_stats);
```

- [ ] **Step 4: Pass prepared scene into hit shading**

Change `ShadeHit()` signature to:

```cpp
Color3f ShadeHit(
    const CpuPreparedScene& prepared_scene,
    const Ray3f& ray,
    const BvhHit& hit,
    CpuDebugRenderStats& stats
)
```

At the start of `ShadeHit()`, add:

```cpp
const RenderSceneIR& scene = prepared_scene.Scene();
```

Replace the shadow trace:

```cpp
const BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_trace);
```

with:

```cpp
const BvhHit shadow_hit = IntersectBvh(scene, prepared_scene.bvh, shadow_ray, shadow_trace);
```

In `RenderCpuDebug()`, replace:

```cpp
result.film.AddSample(x, y, ShadeHit(scene, ray, hit, result.stats));
```

with:

```cpp
result.film.AddSample(x, y, ShadeHit(prepared_scene, ray, hit, result.stats));
```

- [ ] **Step 5: Update CPU debug tests with a prepare helper**

In `tests/cpu_debug_renderer_tests.cpp`, replace the old `RebuildBvh()` helper with:

```cpp
yr::CpuPreparedScene PrepareDebugScene(const yr::RenderSceneIR& scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}
```

Add:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Change `MakeDebugTriangleScene()` return type from `yr::RenderScene` to `yr::RenderSceneIR` and remove the `RebuildBvh(scene);` call.

Replace each direct render call:

```cpp
const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
```

with:

```cpp
const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(PrepareDebugScene(scene));
```

For tests that mutate `scene.triangles` or `scene.materials`, mutate the IR first and then call `PrepareDebugScene(scene)`.

- [ ] **Step 6: Verify debug renderer tests compile**

Run:

```bash
cmake --build build --config Debug
```

Expected: compile errors now point mostly to CPU path tracer and backend callers.

---

### Task 5: Move CPU Path Tracer To Prepared Scene

**Files:**
- Modify: `include/yaoray/backends/cpu/cpu_path_tracer.hpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Update path tracer header**

In `include/yaoray/backends/cpu/cpu_path_tracer.hpp`, replace:

```cpp
#include <yaoray/render/render_scene.hpp>
```

with:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Replace:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene);
```

with:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene);
```

- [ ] **Step 2: Update shadow visibility to use prepared scene**

In `src/backends/cpu/cpu_path_tracer.cpp`, change:

```cpp
ShadowVisibility TraceShadowVisibility(
    const RenderScene& scene,
    Ray3f ray,
    float max_distance,
    CpuPathTraceStats& stats
)
```

to:

```cpp
ShadowVisibility TraceShadowVisibility(
    const CpuPreparedScene& prepared_scene,
    Ray3f ray,
    float max_distance,
    CpuPathTraceStats& stats
)
```

Add at the start:

```cpp
const RenderSceneIR& scene = prepared_scene.Scene();
```

Replace:

```cpp
const BvhHit hit = IntersectBvh(scene, ray, shadow_trace);
```

with:

```cpp
const BvhHit hit = IntersectBvh(scene, prepared_scene.bvh, ray, shadow_trace);
```

- [ ] **Step 3: Thread prepared scene through direct lighting helpers**

Change `EstimateDirectEnvironmentLight()` first parameter from `const RenderScene& scene` to `const CpuPreparedScene& prepared_scene`, then add:

```cpp
const RenderSceneIR& scene = prepared_scene.Scene();
```

Replace:

```cpp
TraceShadowVisibility(scene, Ray3f{shadow_origin, wi}, std::numeric_limits<float>::infinity(), stats);
```

with:

```cpp
TraceShadowVisibility(prepared_scene, Ray3f{shadow_origin, wi}, std::numeric_limits<float>::infinity(), stats);
```

Change `EstimateDirectLight()` first parameter from `const RenderScene& scene` to `const CpuPreparedScene& prepared_scene`, then add:

```cpp
const RenderSceneIR& scene = prepared_scene.Scene();
```

Replace:

```cpp
TraceShadowVisibility(scene, shadow_ray, shadow_distance - shadow_bias, stats);
```

with:

```cpp
TraceShadowVisibility(prepared_scene, shadow_ray, shadow_distance - shadow_bias, stats);
```

Replace the environment contribution call:

```cpp
radiance = radiance + EstimateDirectEnvironmentLight(scene, material, hit_point, normal, wo, sampler, stats);
```

with:

```cpp
radiance = radiance + EstimateDirectEnvironmentLight(prepared_scene, material, hit_point, normal, wo, sampler, stats);
```

- [ ] **Step 4: Update `TracePath()` to use prepared scene**

Change:

```cpp
Color3f TracePath(const RenderScene& scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats)
```

to:

```cpp
Color3f TracePath(const CpuPreparedScene& prepared_scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats)
```

Add at the start:

```cpp
const RenderSceneIR& scene = prepared_scene.Scene();
```

Replace:

```cpp
const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
```

with:

```cpp
const BvhHit hit = IntersectBvh(scene, prepared_scene.bvh, ray, trace_stats);
```

Replace:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, material, hit_point, normal, wo, sampler, stats));
```

with:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(prepared_scene, material, hit_point, normal, wo, sampler, stats));
```

- [ ] **Step 5: Update `RenderCpuPathTrace()` entry point**

Replace:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene) {
    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    const CpuTileSchedule schedule = BuildCpuTileSchedule(scene.width, scene.height, scene.threads);
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;
```

with:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene) {
    const RenderSceneIR& scene = prepared_scene.Scene();
    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    const CpuTileSchedule schedule = BuildCpuTileSchedule(scene.width, scene.height, scene.threads);
    result.stats.bvh_nodes = static_cast<int>(prepared_scene.bvh.nodes.size());
    result.stats.bvh_max_depth = prepared_scene.bvh.max_depth;
```

Replace:

```cpp
Color3f sample_radiance = TracePath(scene, ray, sampler, stats);
```

with:

```cpp
Color3f sample_radiance = TracePath(prepared_scene, ray, sampler, stats);
```

- [ ] **Step 6: Update CPU path tracer tests with a prepare helper**

In `tests/cpu_path_tracer_tests.cpp`, replace old `RebuildBvh()` with:

```cpp
yr::CpuPreparedScene PreparePathScene(const yr::RenderSceneIR& scene) {
    yr::CpuPrepareResult prepared = yr::PrepareCpuScene(scene);
    if (!prepared.ok || !prepared.scene.has_value()) {
        throw std::runtime_error(prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error);
    }
    return std::move(prepared.scene.value());
}
```

Add:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Change fixture return types from `yr::RenderScene` to `yr::RenderSceneIR`. Remove every `RebuildBvh(scene);` call.

Replace direct render calls:

```cpp
const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
```

with:

```cpp
const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(PreparePathScene(scene));
```

For calls that pass a temporary, replace:

```cpp
yr::RenderCpuPathTrace(MakeDiffuseFloorScene(7))
```

with:

```cpp
yr::RenderCpuPathTrace(PreparePathScene(MakeDiffuseFloorScene(7)))
```

- [ ] **Step 7: Verify path tracer compile path**

Run:

```bash
cmake --build build --config Debug
```

Expected: remaining compile errors are backend interface, environment/light test type names, or stale `scene.bvh` references.

---

### Task 6: Update Backend Interface, Remaining Tests, And Docs

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Modify: `src/backends/backend.cpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/environment_tests.cpp`
- Modify: `tests/light_sampling_tests.cpp`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-architecture-refactor-design.md`

- [ ] **Step 1: Update backend interface**

In `include/yaoray/backends/backend.hpp`, replace:

```cpp
virtual RenderResult Render(const RenderScene& scene, const RenderRequest& request) = 0;
```

with:

```cpp
virtual RenderResult Render(const RenderSceneIR& scene, const RenderRequest& request) = 0;
```

In `include/yaoray/backends/cpu/cpu_debug_backend.hpp`, replace:

```cpp
RenderResult Render(const RenderScene& scene, const RenderRequest& request) override;
```

with:

```cpp
RenderResult Render(const RenderSceneIR& scene, const RenderRequest& request) override;
```

- [ ] **Step 2: Update CUDA stub signature**

In `src/backends/backend.cpp`, replace:

```cpp
RenderResult Render(const RenderScene& scene, const RenderRequest& request) override {
```

with:

```cpp
RenderResult Render(const RenderSceneIR& scene, const RenderRequest& request) override {
```

- [ ] **Step 3: Update CPU backend preparation flow**

In `src/backends/cpu/cpu_debug_backend.cpp`, include:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>
```

Replace `CpuDebugBackend::Render()` with:

```cpp
RenderResult CpuDebugBackend::Render(const RenderSceneIR& scene, const RenderRequest& request) {
    (void)request;

    RenderResult result;

    CpuPrepareResult prepared = PrepareCpuScene(scene);
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    result.ok = true;
    if (scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(prepared.scene.value());
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(prepared.scene.value());
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}
```

- [ ] **Step 4: Update remaining render scene type names in tests**

In `tests/environment_tests.cpp` and `tests/light_sampling_tests.cpp`, replace fixture type names:

```cpp
yr::RenderScene
```

with:

```cpp
yr::RenderSceneIR
```

Do not add BVH preparation to these tests unless they render or trace; environment and light sampling helpers should remain IR-only.

- [ ] **Step 5: Remove stale `scene.bvh` references**

Run:

```bash
rg -n "\\.bvh|RenderScene\\b|scene\\.backend" include src tests
```

Expected after fixes:

- `.bvh` appears only in `CpuPreparedScene`, CPU renderer implementations, CPU tests that inspect prepared scenes, and direct BVH tests.
- `RenderScene` does not appear as a type name.
- `scene.backend` does not appear; shared IR uses `scene.requested_backend`.

- [ ] **Step 6: Update README architecture wording**

In `README.md`, update the current status bullet that mentions BVH so it reads:

```markdown
- BVH acceleration prepared by the CPU backend over compiled render triangles
```

Update the `render` command paragraph to say:

```markdown
The `render` command currently parses, compiles backend-neutral render input, lets the selected backend prepare its runtime data, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`.
```

- [ ] **Step 7: Update architecture overview wording**

In `docs/architecture/overview.md`, update the render layer paragraph to say:

```markdown
The render layer compiles that semantic scene into backend-neutral `RenderSceneIR` data. CPU, CUDA, and future OptiX backends consume this IR and prepare their own runtime acceleration structures or buffers.
```

Add this sentence near the CPU backend description:

```markdown
The CPU backend prepares a `CpuPreparedScene` from `RenderSceneIR` by building a CPU `RenderBvh`; the BVH is no longer part of the shared render compiler output.
```

- [ ] **Step 8: Append Phase 1 implementation status to the design spec**

In `docs/superpowers/specs/2026-05-22-yaoray-asset-resource-architecture-refactor-design.md`, replace:

```markdown
Design approved for phased planning.
```

with:

```markdown
Design approved for phased planning.

Phase 1 implementation status: `RenderSceneIR` and `CpuPreparedScene` have been implemented. The render compiler now outputs backend-neutral render input, and the CPU backend builds its own BVH during scene preparation.
```

- [ ] **Step 9: Verify build**

Run:

```bash
cmake --build build --config Debug
```

Expected: build succeeds.

---

### Task 7: Verification And Commit

**Files:**
- Verify all files changed in Tasks 1-6.

- [ ] **Step 1: Run focused test binary**

Run:

```bash
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 2: Run direct CLI smoke render**

Run:

```bash
./build/yaoray render tests/fixtures/scene/builtin_triangle.toml --backend cpu
```

Expected output includes:

```text
Scene parsed successfully:
Scene compiled successfully.
Requested backend: cpu
Integrator: debug_direct
Compiled triangles: 1
Rendered image:
BVH nodes: 1
BVH max depth: 1
```

`BVH nodes` and `BVH max depth` should come from final render stats, not pre-render compiler output.

- [ ] **Step 3: Run full CTest and record macOS PowerShell limitation**

Run:

```bash
ctest --test-dir build --output-on-failure -C Debug
```

Expected on this macOS workspace: core tests pass; PowerShell-backed CLI render tests may fail to run if `powershell` is unavailable.

- [ ] **Step 4: Inspect final diff**

Run:

```bash
git diff --stat
git diff --check
```

Expected: no whitespace errors. Diff should not include `AssetResource` or TOML syntax changes.

- [ ] **Step 5: Commit Phase 1**

Run:

```bash
git add CMakeLists.txt README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-22-yaoray-asset-resource-architecture-refactor-design.md include/yaoray/render/render_scene.hpp include/yaoray/render/scene_compiler.hpp include/yaoray/render/bvh.hpp include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_debug_backend.hpp include/yaoray/backends/cpu/cpu_debug_renderer.hpp include/yaoray/backends/cpu/cpu_path_tracer.hpp include/yaoray/backends/cpu/cpu_prepared_scene.hpp src/app/main.cpp src/render/scene_compiler.cpp src/render/bvh.cpp src/backends/backend.cpp src/backends/cpu/cpu_debug_backend.cpp src/backends/cpu/cpu_debug_renderer.cpp src/backends/cpu/cpu_path_tracer.cpp src/backends/cpu/cpu_prepared_scene.cpp tests/render_scene_tests.cpp tests/backend_tests.cpp tests/bvh_tests.cpp tests/cpu_debug_renderer_tests.cpp tests/cpu_path_tracer_tests.cpp tests/environment_tests.cpp tests/light_sampling_tests.cpp
git commit -m "refactor: split render ir from cpu prepared scene"
```

Expected: commit succeeds with only Phase 1 files.

## Self-Review Checklist

- Spec coverage: implements Phase 1 only; Phase 2 `AssetResource` and Phase 3 compiler cleanup remain future plans.
- Backend neutrality: shared `RenderSceneIR` has no BVH and no backend runtime handles.
- CPU ownership: CPU prepare stage owns `RenderBvh`.
- Behavior preservation: current render settings, geometry, material, environment, texture, debug, and path behavior are preserved.
- Test coverage: render compiler, BVH, backend dispatch, CPU debug renderer, and CPU path tracer are all touched by focused tests.
