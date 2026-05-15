# YaoRay CPU Path Tracer v0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a deterministic CPU path-tracing integrator that preserves the current debug direct renderer and produces visible diffuse indirect lighting.

**Architecture:** Add `RenderIntegratorKind` to the semantic scene and compiled render scene so `backend` remains the execution target and `integrator` selects the render algorithm. Keep the existing CPU backend object, dispatch inside it between the current debug direct renderer and a new standalone CPU path tracer, and reuse existing `RenderScene`, BVH traversal, materials, Film, PNG output, and CLI flow.

**Tech Stack:** C++20, CMake 3.24+, CTest, existing YaoRay `scene`, `render`, `backends`, `film`, and custom `yr_test` harness.

---

## Scope Check

This plan implements only CPU Path Tracer v0 from the approved spec:

- `render.integrator = "debug_direct" | "path"`
- default `debug_direct` behavior for existing scenes
- CPU path tracer with Lambertian diffuse bounce
- explicit center-sampled direct light reuse
- deterministic per-pixel/per-sample RNG
- `spp`, `max_depth`, and `seed` behavior
- backend dispatch from the existing CPU backend
- CLI output showing the selected integrator
- small path-tracer CLI smoke fixture
- separate Cornell path example scene for manual review
- docs updates

It does not implement MIS, Russian roulette, random area-light surface sampling, glossy/metal/dielectric materials, textures, spectral rendering, CUDA, multithreading, denoising, or final physical Cornell matching.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/scene/scene.hpp
src/scene/scene.cpp
src/scene/scene_parser.cpp
include/yaoray/render/render_scene.hpp
src/render/scene_compiler.cpp
include/yaoray/backends/cpu/cpu_path_tracer.hpp
src/backends/cpu/cpu_path_tracer.cpp
src/backends/cpu/cpu_debug_backend.cpp
src/app/main.cpp
tests/scene_tests.cpp
tests/render_scene_tests.cpp
tests/backend_tests.cpp
tests/cpu_path_tracer_tests.cpp
tests/fixtures/scene/path_tracer_bounce.toml
scenes/examples/cornell_box_path.toml
```

Responsibilities:

- `scene.hpp` / `scene.cpp`: define and parse stable integrator enum names.
- `scene_parser.cpp`: parse optional `[render].integrator`, defaulting to `debug_direct`.
- `render_scene.hpp` / `scene_compiler.cpp`: carry integrator into backend-friendly compiled data.
- `cpu_path_tracer.hpp/.cpp`: implement deterministic CPU path tracer v0.
- `cpu_debug_backend.cpp`: keep the CPU backend object and dispatch by `scene.integrator`.
- `main.cpp`: print selected integrator in CLI output.
- `CMakeLists.txt`: compile the new path tracer and tests; add low-cost CLI smoke test.
- `tests/*`: cover parser, compiler, backend dispatch, path tracer behavior, and CLI path smoke.
- `cornell_box_path.toml`: manual-review path-traced Cornell scene separate from the debug Cornell scene.
- Docs: document integrator selection and path tracer v0 limits.

## Task 1: Add Render Integrator Scene Schema And Parser Support

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene.cpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add failing schema and parser tests**

In `tests/scene_tests.cpp`, in `scene_defaults_match_schema`, after:

```cpp
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
```

add:

```cpp
    YR_EXPECT_EQ(scene.render.integrator, yr::RenderIntegratorKind::DebugDirect);
```

In `scene_enum_names_are_stable`, after the render backend assertions:

```cpp
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cpu), std::string_view{"cpu"});
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cuda), std::string_view{"cuda"});
```

add:

```cpp
    YR_EXPECT_EQ(yr::RenderIntegratorName(yr::RenderIntegratorKind::DebugDirect), std::string_view{"debug_direct"});
    YR_EXPECT_EQ(yr::RenderIntegratorName(yr::RenderIntegratorKind::Path), std::string_view{"path"});
```

In `scene_enum_parsers_accept_stable_names`, after the render backend parser assertions:

```cpp
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cpu").value(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cuda").value(), yr::RenderBackendKind::Cuda);
```

add:

```cpp
    YR_EXPECT_EQ(yr::ParseRenderIntegratorName("debug_direct").value(), yr::RenderIntegratorKind::DebugDirect);
    YR_EXPECT_EQ(yr::ParseRenderIntegratorName("path").value(), yr::RenderIntegratorKind::Path);
```

In `scene_enum_parsers_reject_unknown_names`, after:

```cpp
    YR_EXPECT_TRUE(!yr::ParseRenderBackendName("metal").has_value());
```

add:

```cpp
    YR_EXPECT_TRUE(!yr::ParseRenderIntegratorName("bidirectional").has_value());
```

In `scene_enum_names_return_unknown_for_invalid_values`, after:

```cpp
    YR_EXPECT_EQ(yr::RenderBackendName(static_cast<yr::RenderBackendKind>(999)), std::string_view{"unknown"});
```

add:

```cpp
    YR_EXPECT_EQ(yr::RenderIntegratorName(static_cast<yr::RenderIntegratorKind>(999)), std::string_view{"unknown"});
```

In `scene_parser_loads_minimal_scene_file`, after:

```cpp
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
```

add:

```cpp
    YR_EXPECT_EQ(scene.render.integrator, yr::RenderIntegratorKind::DebugDirect);
```

In `scene_parser_applies_defaults`, after:

```cpp
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
```

add:

```cpp
    YR_EXPECT_EQ(scene.render.integrator, yr::RenderIntegratorKind::DebugDirect);
```

After `scene_parser_applies_defaults`, add:

```cpp
YR_TEST(scene_parser_loads_render_integrator) {
    const std::filesystem::path path = WriteTempScene(
        "path_integrator.toml",
        ValidScene(
            R"toml(
[render]
backend = "cpu"
integrator = "path"
width = 64
height = 32
)toml",
            R"toml(
[film]
output = "out/test.png"
)toml",
            R"toml(
[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
)toml"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().render.integrator, yr::RenderIntegratorKind::Path);
}

YR_TEST(scene_parser_rejects_unknown_render_integrator) {
    const std::filesystem::path path = WriteTempScene(
        "bad_integrator.toml",
        ValidScene(
            R"toml(
[render]
integrator = "light_transport_magic"
width = 64
height = 32
)toml",
            R"toml(
[film]
output = "out/test.png"
)toml",
            R"toml(
[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
)toml"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.integrator", "unknown integrator"));
}
```

- [ ] **Step 2: Run tests to verify schema red**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `RenderIntegratorKind`, `RenderIntegratorName`, and `ParseRenderIntegratorName` do not exist.

- [ ] **Step 3: Add integrator enum and declarations**

In `include/yaoray/scene/scene.hpp`, after:

```cpp
enum class RenderBackendKind {
    Cpu,
    Cuda,
};
```

add:

```cpp
enum class RenderIntegratorKind {
    DebugDirect,
    Path,
};
```

In `RenderSettings`, replace:

```cpp
    RenderBackendKind backend = RenderBackendKind::Cpu;
    int width = 0;
```

with:

```cpp
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
```

Near the existing render backend functions:

```cpp
std::string_view RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name);
```

add:

```cpp
std::string_view RenderIntegratorName(RenderIntegratorKind integrator);
std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name);
```

- [ ] **Step 4: Implement integrator enum names**

In `src/scene/scene.cpp`, after `ParseRenderBackendName(...)`, add:

```cpp
std::string_view RenderIntegratorName(RenderIntegratorKind integrator) {
    switch (integrator) {
        case RenderIntegratorKind::DebugDirect:
            return "debug_direct";
        case RenderIntegratorKind::Path:
            return "path";
    }
    return "unknown";
}

std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name) {
    if (name == "debug_direct") {
        return RenderIntegratorKind::DebugDirect;
    }
    if (name == "path") {
        return RenderIntegratorKind::Path;
    }
    return std::nullopt;
}
```

- [ ] **Step 5: Parse `[render].integrator`**

In `src/scene/scene_parser.cpp`, in `ParseRender(...)`, change the render unknown-field allow list from:

```cpp
    CheckUnknownFields(table, "render",
        {"backend", "width", "height", "spp", "max_depth", "seed"},
        file,
        diagnostics
    );
```

to:

```cpp
    CheckUnknownFields(table, "render",
        {"backend", "integrator", "width", "height", "spp", "max_depth", "seed"},
        file,
        diagnostics
    );
```

After the existing backend parse block:

```cpp
    if (const auto backend = ReadValue<std::string>(table, "backend")) {
        if (const auto parsed = ParseRenderBackendName(*backend)) {
            scene.render.backend = *parsed;
        } else {
            diagnostics.push_back(Error(file, "render.backend", "unknown backend"));
        }
    }
```

add:

```cpp
    if (const auto integrator = ReadValue<std::string>(table, "integrator")) {
        if (const auto parsed = ParseRenderIntegratorName(*integrator)) {
            scene.render.integrator = *parsed;
        } else {
            diagnostics.push_back(Error(file, "render.integrator", "unknown integrator"));
        }
    }
```

- [ ] **Step 6: Run parser tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 7: Commit scene integrator schema**

Run:

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene.cpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse render integrator"
```

## Task 2: Carry Integrator Through RenderScene And CLI Output

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `src/app/main.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing render scene compiler tests**

In `tests/render_scene_tests.cpp`, in `render_scene_defaults_are_backend_friendly`, after:

```cpp
    YR_EXPECT_EQ(scene.backend, yr::RenderBackendKind::Cpu);
```

add:

```cpp
    YR_EXPECT_EQ(scene.integrator, yr::RenderIntegratorKind::DebugDirect);
```

In `MakeBaseScene()`, after:

```cpp
    scene.render.backend = yr::RenderBackendKind::Cuda;
```

add:

```cpp
    scene.render.integrator = yr::RenderIntegratorKind::Path;
```

In `scene_compiler_copies_render_settings`, after:

```cpp
    YR_EXPECT_EQ(compiled.backend, yr::RenderBackendKind::Cuda);
```

add:

```cpp
    YR_EXPECT_EQ(compiled.integrator, yr::RenderIntegratorKind::Path);
```

- [ ] **Step 2: Run tests to verify render scene red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build fails because `RenderScene::integrator` does not exist.

- [ ] **Step 3: Add `RenderScene::integrator`**

In `include/yaoray/render/render_scene.hpp`, in `RenderScene`, replace:

```cpp
    RenderBackendKind backend = RenderBackendKind::Cpu;
    int width = 0;
```

with:

```cpp
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
```

- [ ] **Step 4: Copy integrator during scene compilation**

In `src/render/scene_compiler.cpp`, in `CompileScene(...)`, after:

```cpp
    compiled.backend = scene.render.backend;
```

add:

```cpp
    compiled.integrator = scene.render.integrator;
```

- [ ] **Step 5: Print selected integrator in CLI output**

In `src/app/main.cpp`, after:

```cpp
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
```

add:

```cpp
    std::cout << "Integrator: " << yr::RenderIntegratorName(render_scene.integrator) << '\n';
```

- [ ] **Step 6: Run render scene tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 7: Commit render scene integrator propagation**

Run:

```powershell
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp src/app/main.cpp tests/render_scene_tests.cpp
git commit -m "feat: carry render integrator"
```

## Task 3: Add Standalone CPU Path Tracer

**Files:**
- Create: `include/yaoray/backends/cpu/cpu_path_tracer.hpp`
- Create: `src/backends/cpu/cpu_path_tracer.cpp`
- Create: `tests/cpu_path_tracer_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing path tracer tests**

Create `tests/cpu_path_tracer_tests.cpp` with:

```cpp
#include "yr_test.hpp"

#include <cstdint>
#include <stdexcept>

#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

void RebuildBvh(yr::RenderScene& scene) {
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
}

yr::RenderScene MakePathTriangleScene(int width = 3, int height = 3) {
    yr::RenderScene scene;
    scene.backend = yr::RenderBackendKind::Cpu;
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.width = width;
    scene.height = height;
    scene.spp = 1;
    scene.max_depth = 1;
    scene.seed = 7;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.0f, 0.0f, 0.0f};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{0.8f, 0.8f, 0.8f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeDiffuseBounceScene(int max_depth) {
    yr::RenderScene scene = MakePathTriangleScene(1, 1);
    scene.max_depth = max_depth;
    scene.spp = 8;
    scene.seed = 11;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.25f, 0.5f, 1.0f};
    scene.environment.strength = 1.0f;
    scene.materials[0].albedo = yr::Color3f{0.5f, 0.5f, 0.5f};
    scene.triangles[0] = yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 0.0f},
        yr::Point3f{10.0f, -10.0f, 0.0f},
        yr::Point3f{0.0f, 10.0f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    };
    RebuildBvh(scene);
    return scene;
}

bool Different(yr::Color3f a, yr::Color3f b) {
    return a.x != b.x || a.y != b.y || a.z != b.z;
}

} // namespace

YR_TEST(cpu_path_tracer_traces_one_sample_per_pixel) {
    const yr::RenderScene scene = MakePathTriangleScene(4, 3);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
}

YR_TEST(cpu_path_tracer_accumulates_spp_samples) {
    yr::RenderScene scene = MakePathTriangleScene(2, 2);
    scene.spp = 4;

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 4);
    YR_EXPECT_EQ(result.film.SampleCount(1, 1), 4);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{16});
}

YR_TEST(cpu_path_tracer_sees_emissive_surfaces) {
    yr::RenderScene scene = MakePathTriangleScene(3, 3);
    scene.materials[0].albedo = yr::Color3f{};
    scene.materials[0].emission = yr::Color3f{0.25f, 0.5f, 0.75f};

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(center.x, 0.25, 1e-6);
    YR_EXPECT_NEAR(center.y, 0.5, 1e-6);
    YR_EXPECT_NEAR(center.z, 0.75, 1e-6);
}

YR_TEST(cpu_path_tracer_is_deterministic_for_same_seed) {
    yr::RenderScene scene = MakeDiffuseBounceScene(2);
    scene.spp = 16;
    scene.seed = 123;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(first.film.SampleCount(0, 0), second.film.SampleCount(0, 0));
    YR_EXPECT_NEAR(first.film.LinearPixel(0, 0).x, second.film.LinearPixel(0, 0).x, 1e-7);
    YR_EXPECT_NEAR(first.film.LinearPixel(0, 0).y, second.film.LinearPixel(0, 0).y, 1e-7);
    YR_EXPECT_NEAR(first.film.LinearPixel(0, 0).z, second.film.LinearPixel(0, 0).z, 1e-7);
}

YR_TEST(cpu_path_tracer_changes_stochastic_result_for_different_seed) {
    yr::RenderScene first_scene = MakePathTriangleScene(5, 5);
    first_scene.spp = 8;
    first_scene.seed = 1;
    first_scene.environment.radiance = yr::Color3f{0.1f, 0.2f, 0.3f};

    yr::RenderScene second_scene = first_scene;
    second_scene.seed = 2;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(first_scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(second_scene);

    bool any_difference = false;
    for (int y = 0; y < first.film.Height(); ++y) {
        for (int x = 0; x < first.film.Width(); ++x) {
            any_difference = any_difference || Different(first.film.LinearPixel(x, y), second.film.LinearPixel(x, y));
        }
    }
    YR_EXPECT_TRUE(any_difference);
}

YR_TEST(cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce) {
    const yr::CpuPathTraceResult depth_one = yr::RenderCpuPathTrace(MakeDiffuseBounceScene(1));
    const yr::CpuPathTraceResult depth_two = yr::RenderCpuPathTrace(MakeDiffuseBounceScene(2));

    YR_EXPECT_NEAR(depth_one.film.LinearPixel(0, 0).x, 0.0, 1e-6);
    YR_EXPECT_TRUE(depth_two.film.LinearPixel(0, 0).x > 0.0f);
    YR_EXPECT_TRUE(depth_two.stats.rays_traced > depth_one.stats.rays_traced);
}
```

In `CMakeLists.txt`, add `src/backends/cpu/cpu_path_tracer.cpp` to `yaoray_backends` and add `tests/cpu_path_tracer_tests.cpp` to `yaoray_tests`:

```cmake
add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
    src/backends/cpu/cpu_path_tracer.cpp
)
```

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
    tests/backend_tests.cpp
    tests/cpu_debug_renderer_tests.cpp
    tests/cpu_path_tracer_tests.cpp
)
```

- [ ] **Step 2: Run build to verify path tracer red**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `cpu_path_tracer.hpp` and `cpu_path_tracer.cpp` do not exist.

- [ ] **Step 3: Add path tracer header**

Create `include/yaoray/backends/cpu/cpu_path_tracer.hpp`:

```cpp
#pragma once

#include <cstdint>

#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuPathTraceStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t shadow_rays = 0;
    std::uint64_t occluded_shadow_rays = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t bvh_node_tests = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct CpuPathTraceResult {
    Film film;
    CpuPathTraceStats stats;
};

CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene);

} // namespace yr
```

- [ ] **Step 4: Add path tracer implementation**

Create `src/backends/cpu/cpu_path_tracer.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>

#include <yaoray/core/ray.hpp>
#include <yaoray/render/bvh.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;

struct Rng {
    std::uint64_t state = 0;

    explicit Rng(std::uint64_t seed) : state(seed) {}

    std::uint32_t NextU32() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z = z ^ (z >> 31);
        return static_cast<std::uint32_t>(z >> 32);
    }

    float NextFloat() {
        return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
    }
};

std::uint64_t Mix(std::uint64_t value) {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

std::uint64_t SeedFor(const RenderScene& scene, int x, int y, int sample) {
    std::uint64_t value = scene.seed;
    value ^= Mix(static_cast<std::uint64_t>(x) + 0x9E3779B97F4A7C15ull);
    value ^= Mix(static_cast<std::uint64_t>(y) + 0xBF58476D1CE4E5B9ull);
    value ^= Mix(static_cast<std::uint64_t>(sample) + 0x94D049BB133111EBull);
    return Mix(value);
}

Color3f Multiply(Color3f a, Color3f b) {
    return Color3f{a.x * b.x, a.y * b.y, a.z * b.z};
}

bool IsNearBlack(Color3f value) {
    return value.x <= 0.0f && value.y <= 0.0f && value.z <= 0.0f;
}

float MaxAbsComponent(Point3f point) {
    return std::max(std::fabs(point.x), std::max(std::fabs(point.y), std::fabs(point.z)));
}

float SurfaceBias(Point3f point) {
    return std::max(MinShadowBias, MaxAbsComponent(point) * ShadowBiasScale);
}

float ShadowBias(Point3f origin, Point3f target, float distance) {
    const float coordinate_scale = std::max(MaxAbsComponent(origin), MaxAbsComponent(target));
    const float scaled_bias = coordinate_scale * ShadowBiasScale;
    const float capped_bias = distance * MaxShadowBiasDistanceFraction;
    return std::min(std::max(MinShadowBias, scaled_bias), capped_bias);
}

Vec3f FaceForward(Vec3f normal, Vec3f reference) {
    return Dot(normal, reference) < 0.0f ? -normal : normal;
}

Color3f EnvironmentColor(const RenderScene& scene) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

bool IsValidMaterialIndex(const RenderScene& scene, int material_index) {
    return material_index >= 0 && static_cast<std::size_t>(material_index) < scene.materials.size();
}

void AccumulateTraceStats(CpuPathTraceStats& stats, const BvhTraceStats& trace_stats) {
    stats.bvh_node_tests += trace_stats.node_tests;
    stats.triangle_tests += trace_stats.triangle_tests;
}

Ray3f MakeCameraRay(const RenderScene& scene, int x, int y, float sample_x, float sample_y) {
    const float width = static_cast<float>(scene.width);
    const float height = static_cast<float>(scene.height);
    const float aspect = width / height;
    const float half_height = std::tan(scene.camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * (static_cast<float>(x) + sample_x) / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * (static_cast<float>(y) + sample_y) / height) * half_height;
    const Vec3f direction = Normalize(
        scene.camera.forward +
        scene.camera.right * screen_x +
        scene.camera.up * screen_y
    );
    return Ray3f{scene.camera.origin, direction};
}

Vec3f SampleCosineHemisphere(Vec3f normal, Rng& rng) {
    const float r1 = rng.NextFloat();
    const float r2 = rng.NextFloat();
    const float phi = 2.0f * Pi * r1;
    const float radius = std::sqrt(r2);
    const float local_x = std::cos(phi) * radius;
    const float local_y = std::sin(phi) * radius;
    const float local_z = std::sqrt(std::max(0.0f, 1.0f - r2));

    const Vec3f helper = std::fabs(normal.x) > 0.9f ? Vec3f{0.0f, 1.0f, 0.0f} : Vec3f{1.0f, 0.0f, 0.0f};
    const Vec3f tangent = Normalize(Cross(helper, normal));
    const Vec3f bitangent = Cross(normal, tangent);
    return Normalize(tangent * local_x + bitangent * local_y + normal * local_z);
}

Color3f EstimateDirectLight(
    const RenderScene& scene,
    Point3f hit_point,
    Vec3f normal,
    const RenderMaterial& material,
    CpuPathTraceStats& stats
) {
    Color3f radiance;
    for (const RenderAreaLight& light : scene.area_lights) {
        const float area = light.width * light.height;
        if (area <= 0.0f) {
            continue;
        }

        const Vec3f to_light = light.position - hit_point;
        const float distance_squared = LengthSquared(to_light);
        if (distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float distance = std::sqrt(distance_squared);
        const float shadow_bias = ShadowBias(hit_point, light.position, distance);
        if (distance <= shadow_bias) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * shadow_bias;
        const Vec3f shadow_to_light = light.position - shadow_origin;
        const float shadow_distance_squared = LengthSquared(shadow_to_light);
        if (shadow_distance_squared <= MinShadowBias * MinShadowBias) {
            continue;
        }

        const float shadow_distance = std::sqrt(shadow_distance_squared);
        const Vec3f wi = shadow_to_light / shadow_distance;
        const float n_dot_l = std::max(0.0f, Dot(normal, wi));
        if (n_dot_l <= 0.0f) {
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

        const float scale = area * n_dot_l / distance_squared;
        radiance = radiance + Multiply(material.albedo, light.radiance) * scale;
    }
    return radiance;
}

Color3f TracePath(const RenderScene& scene, Ray3f ray, Rng& rng, CpuPathTraceStats& stats) {
    Color3f radiance;
    Color3f throughput{1.0f, 1.0f, 1.0f};

    for (int depth = 0; depth < scene.max_depth; ++depth) {
        ++stats.rays_traced;
        BvhTraceStats trace_stats;
        const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
        AccumulateTraceStats(stats, trace_stats);

        if (!hit.hit || hit.triangle == nullptr) {
            ++stats.misses;
            radiance = radiance + Multiply(throughput, EnvironmentColor(scene));
            break;
        }

        ++stats.hits;
        const RenderTriangle& triangle = *hit.triangle;
        if (!IsValidMaterialIndex(scene, triangle.material_index)) {
            radiance = radiance + Multiply(throughput, Color3f{1.0f, 0.0f, 1.0f});
            break;
        }

        const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
        const Point3f hit_point = ray.origin + ray.direction * hit.t;
        const Vec3f normal = FaceForward(Normalize(triangle.normal), -ray.direction);

        radiance = radiance + Multiply(throughput, material.emission);
        radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material, stats));

        if (depth + 1 >= scene.max_depth) {
            break;
        }

        throughput = Multiply(throughput, material.albedo);
        if (IsNearBlack(throughput)) {
            break;
        }

        const Vec3f bounce_direction = SampleCosineHemisphere(normal, rng);
        ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bounce_direction};
    }

    return radiance;
}

} // namespace

CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene) {
    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;
    const auto start = std::chrono::steady_clock::now();

    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            for (int sample = 0; sample < scene.spp; ++sample) {
                Rng rng{SeedFor(scene, x, y, sample)};
                const float sample_x = scene.spp == 1 ? 0.5f : rng.NextFloat();
                const float sample_y = scene.spp == 1 ? 0.5f : rng.NextFloat();
                const Ray3f ray = MakeCameraRay(scene, x, y, sample_x, sample_y);
                result.film.AddSample(x, y, TracePath(scene, ray, rng, result.stats));
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
```

- [ ] **Step 5: Run path tracer tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 6: Commit standalone CPU path tracer**

Run:

```powershell
git add CMakeLists.txt include/yaoray/backends/cpu/cpu_path_tracer.hpp src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add cpu path tracer"
```

## Task 4: Dispatch CPU Backend By Integrator

**Files:**
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Add failing backend dispatch tests**

In `tests/backend_tests.cpp`, after `cpu_backend_renders_film_and_stats`, add:

```cpp
YR_TEST(cpu_backend_dispatches_path_integrator) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderScene scene = MakeBackendTriangleScene(3, 3);
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.spp = 4;

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->SampleCount(0, 0), 4);
    YR_EXPECT_TRUE(result.stats.rays_traced >= std::uint64_t{36});
}

YR_TEST(cpu_backend_keeps_debug_direct_as_default_integrator) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderScene scene = MakeBackendTriangleScene(4, 3);
    scene.integrator = yr::RenderIntegratorKind::DebugDirect;
    scene.spp = 4;

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
}
```

- [ ] **Step 2: Run tests to verify backend red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `cpu_backend_dispatches_path_integrator` fails because the CPU backend still always calls `RenderCpuDebug`.

- [ ] **Step 3: Dispatch inside CPU backend**

In `src/backends/cpu/cpu_debug_backend.cpp`, add include:

```cpp
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
```

After the existing `ToRenderStats(const CpuDebugRenderStats& stats)` helper, add:

```cpp
RenderStats ToRenderStats(const CpuPathTraceStats& stats) {
    RenderStats result;
    result.rays_traced = stats.rays_traced;
    result.shadow_rays = stats.shadow_rays;
    result.occluded_shadow_rays = stats.occluded_shadow_rays;
    result.triangle_tests = stats.triangle_tests;
    result.bvh_node_tests = stats.bvh_node_tests;
    result.bvh_nodes = stats.bvh_nodes;
    result.bvh_max_depth = stats.bvh_max_depth;
    result.hits = stats.hits;
    result.misses = stats.misses;
    result.elapsed_seconds = stats.elapsed_seconds;
    return result;
}
```

Replace `CpuDebugBackend::Render(...)` with:

```cpp
RenderResult CpuDebugBackend::Render(const RenderScene& scene, const RenderRequest& request) {
    (void)request;

    RenderResult result;
    result.ok = true;
    if (scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(scene);
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}
```

Do not rename `CpuDebugBackend` in this slice; the class already represents the CPU backend in the public factory. A later cleanup can rename it to `CpuBackend`.

- [ ] **Step 4: Run backend tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 5: Commit CPU backend integrator dispatch**

Run:

```powershell
git add src/backends/cpu/cpu_debug_backend.cpp tests/backend_tests.cpp
git commit -m "feat: dispatch cpu integrators"
```

## Task 5: Add CLI Path Smoke Fixture And Cornell Path Example

**Files:**
- Create: `tests/fixtures/scene/path_tracer_bounce.toml`
- Create: `scenes/examples/cornell_box_path.toml`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create low-cost path tracer fixture**

Create `tests/fixtures/scene/path_tracer_bounce.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 8
height = 8
spp = 4
max_depth = 2
seed = 17

[film]
output = "out/path_tracer_bounce.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0.0, 0.0, 4.0]
target = [0.0, 0.0, 0.0]
fov_y = 45.0

[[materials]]
name = "matte"
albedo = [0.7, 0.7, 0.7]
emission = [0, 0, 0]

[[assets]]
name = "panel"
quads = [
  [[-2.0, -2.0, 0.0], [2.0, -2.0, 0.0], [2.0, 2.0, 0.0], [-2.0, 2.0, 0.0]]
]

[[instances]]
asset = "panel"
material = "matte"

[environment]
type = "constant"
radiance = [0.15, 0.2, 0.25]
strength = 1.0
```

- [ ] **Step 2: Add CLI smoke test**

In `CMakeLists.txt`, after `yaoray_cli_render_cornell_box`, add:

```cmake
    add_test(NAME yaoray_cli_render_path
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/path_tracer_bounce.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/path_tracer_bounce.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Integrator: path') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

Also add `Integrator: debug_direct` checks to the existing CPU, OBJ, and Cornell debug CTest command strings. For example, in `yaoray_cli_render_cpu`, add this check after the `$LASTEXITCODE` check:

```powershell
if ($out -notmatch 'Integrator: debug_direct') { exit 1 };
```

Apply the same check to `yaoray_cli_render_obj` and `yaoray_cli_render_cornell_box`.

- [ ] **Step 3: Create Cornell path example**

Create `scenes/examples/cornell_box_path.toml` by copying `scenes/examples/cornell_box.toml`, then replace the `[render]` block with:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 256
height = 256
spp = 16
max_depth = 5
seed = 1
```

Replace the `[film]` output line:

```toml
output = "out/cornell_box.png"
```

with:

```toml
output = "out/cornell_box_path.png"
```

Keep all camera, material, asset, instance, light, and environment data identical to `cornell_box.toml`.

- [ ] **Step 4: Run full CTest**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CTest reports 12 tests passed after adding `yaoray_cli_render_path`.

- [ ] **Step 5: Manually render path Cornell preview**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\cornell_box_path.png) { Remove-Item -LiteralPath scenes\examples\out\cornell_box_path.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\cornell_box_path.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\cornell_box_path.png")
($bytes[0..7] | ForEach-Object { $_.ToString("X2") }) -join " "
```

Expected output includes:

```text
Integrator: path
Compiled triangles: 38
Rendered image: scenes/examples/out/cornell_box_path.png
89 50 4E 47 0D 0A 1A 0A
```

The image is expected to be noisy at `spp = 16`, but large debug-renderer black regions should receive some indirect light.

- [ ] **Step 6: Commit CLI fixture and Cornell path example**

Run:

```powershell
git add CMakeLists.txt tests/fixtures/scene/path_tracer_bounce.toml scenes/examples/cornell_box_path.toml
git commit -m "feat: add path tracer scene smoke tests"
```

## Task 6: Update Documentation And Final Verification

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README**

In `README.md`, under "Current Status", after:

```markdown
- scene-authored inline quad assets and a Cornell Box geometry smoke scene
```

add:

```markdown
- selectable render integrators with a first deterministic CPU path tracer
```

In the "Run" command block, after:

```powershell
build\Debug\yaoray.exe render scenes\examples\cornell_box.toml --backend cpu
```

add:

```powershell
build\Debug\yaoray.exe render scenes\examples\cornell_box_path.toml --backend cpu
```

Replace the render-command paragraph with:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the first CPU path tracer with diffuse bounce, deterministic sampling, and explicit center-sampled direct light. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render.
```

- [ ] **Step 2: Update architecture docs**

In `docs/architecture/overview.md`, under "Current implemented slices", after:

```markdown
- scene-authored inline quad assets and a Cornell Box example based on Cornell measured geometry
```

add:

```markdown
- render integrator selection with a deterministic CPU path tracer v0
```

Replace:

```markdown
The CPU renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

with:

```markdown
The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output. `path` is the first CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, and explicit center-sampled direct light. It is still a v0 integrator without MIS, Russian roulette, spectral rendering, random area-light sampling, or final-quality material models.
```

After the inline quad paragraph, add:

```markdown
The path-traced Cornell example is separate from the debug Cornell scene. The debug scene remains a fast geometry and pipeline smoke test; the path scene is for manual visual review of indirect diffuse lighting.
```

- [ ] **Step 3: Run scope checks**

Run:

```powershell
rg -n "integrator|debug_direct|path tracer|Path|spp|max_depth|seed|Russian roulette|MIS|spectral|CUDA|multithreading" README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-15-yaoray-cpu-path-tracer-v0-design.md docs/superpowers/plans/2026-05-15-yaoray-cpu-path-tracer-v0-implementation-plan.md
rg -n "MIS|Russian roulette|spectral-to|microfacet|dielectric|metal|texture|denois|thread|std::thread|cuda path|optix" include src tests scenes README.md docs/architecture/overview.md
```

Expected:

- README and architecture docs describe `path` as v0 and list future limitations.
- Production code does not contain new MIS, Russian roulette, spectral, texture, denoising, thread, CUDA path, or OptiX implementation.
- Existing CUDA not-implemented stub references remain acceptable.

- [ ] **Step 4: Run fresh full verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: full CTest passes, including `yaoray_cli_render_path`.

- [ ] **Step 5: Re-render debug and path Cornell examples**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\cornell_box.png) { Remove-Item -LiteralPath scenes\examples\out\cornell_box.png -Force }
if (Test-Path -LiteralPath scenes\examples\out\cornell_box_path.png) { Remove-Item -LiteralPath scenes\examples\out\cornell_box_path.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\cornell_box.toml --backend cpu
& $yaoray render scenes\examples\cornell_box_path.toml --backend cpu
[byte[]]$debugBytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\cornell_box.png")
[byte[]]$pathBytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\cornell_box_path.png")
($debugBytes[0..7] | ForEach-Object { $_.ToString("X2") }) -join " "
($pathBytes[0..7] | ForEach-Object { $_.ToString("X2") }) -join " "
```

Expected:

```text
Integrator: debug_direct
Integrator: path
89 50 4E 47 0D 0A 1A 0A
89 50 4E 47 0D 0A 1A 0A
```

Manual visual expectation: `cornell_box_path.png` is noisier but has more indirect illumination than `cornell_box.png`.

- [ ] **Step 6: Commit documentation updates**

Run:

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: document cpu path tracer v0"
```

- [ ] **Step 7: Final git state check**

Run:

```powershell
git status --short --branch
git log --oneline --decorate --max-count 10
```

Expected:

- Working tree is clean.
- Latest commits include:
  - `docs: document cpu path tracer v0`
  - `feat: add path tracer scene smoke tests`
  - `feat: dispatch cpu integrators`
  - `feat: add cpu path tracer`
  - `feat: carry render integrator`
  - `feat: parse render integrator`

## Self-Review Checklist

- Spec coverage:
  - `render.integrator` schema and parser: Task 1.
  - render-scene propagation and CLI output: Task 2.
  - CPU path tracer with diffuse bounce, emission, direct light, deterministic RNG, `spp`, `max_depth`, `seed`: Task 3.
  - CPU backend dispatch by integrator: Task 4.
  - CLI smoke fixture and separate Cornell path scene: Task 5.
  - docs and final scope checks: Task 6.

- Type consistency:
  - Semantic enum: `RenderIntegratorKind`.
  - Stable names: `"debug_direct"` and `"path"`.
  - Parser function: `ParseRenderIntegratorName`.
  - Name function: `RenderIntegratorName`.
  - Semantic storage: `RenderSettings::integrator`.
  - Compiled storage: `RenderScene::integrator`.
  - New renderer API: `RenderCpuPathTrace`.
  - New result type: `CpuPathTraceResult`.

- Scope boundaries:
  - Do not implement MIS.
  - Do not implement Russian roulette.
  - Do not randomly sample area-light surfaces.
  - Do not add spectral rendering.
  - Do not add material types beyond existing diffuse/emissive RGB.
  - Do not add multithreading.
  - Do not change CUDA from not implemented.
  - Do not replace the debug Cornell scene; add `cornell_box_path.toml` separately.
