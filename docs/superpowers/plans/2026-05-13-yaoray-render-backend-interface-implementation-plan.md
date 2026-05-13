# YaoRay Render Backend Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a thin render backend interface so `yaoray render` dispatches through `CreateRenderBackend()` instead of calling the CPU debug renderer or CUDA stub directly.

**Architecture:** Keep scene loading, scene compilation, tone mapping, and image writing in the app/output layer. Add a `yaoray_backends` library that owns the common backend interface, backend factory, CPU debug backend adapter, CUDA not-implemented backend, and existing CPU debug renderer implementation. Defer Integrator, BVH, path tracing, CUDA rendering, and output-format changes.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC, existing `core`, `film`, `scene`, `render`, and `backends/cpu` modules.

---

## Scope Check

This plan implements only the approved Render Backend Interface design:

- common backend interface under `include/yaoray/backends/`
- backend factory from `RenderBackendKind`
- `CpuDebugBackend` wrapper around the existing `RenderCpuDebug()`
- CUDA not-implemented backend that returns a structured render failure
- CLI render dispatch through the backend interface
- backend tests for CPU success and CUDA failure
- docs that state rendering now goes through a backend interface

It does not implement Integrator, BVH, path tracing, asset import, real CUDA rendering, OptiX, image output ownership inside backends, PNG, EXR, progress callbacks, cancellation, multithreading, or tile scheduling.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/backends/backend.hpp
include/yaoray/backends/cpu/cpu_debug_backend.hpp
src/app/main.cpp
src/backends/backend.cpp
src/backends/cpu/cpu_debug_backend.cpp
tests/backend_tests.cpp
```

Responsibilities:

- `include/yaoray/backends/backend.hpp`: public backend API, `RenderRequest`, `RenderStats`, `RenderResult`, `RenderBackend`, and `CreateRenderBackend()`.
- `src/backends/backend.cpp`: backend factory and CUDA not-implemented backend.
- `include/yaoray/backends/cpu/cpu_debug_backend.hpp`: CPU debug backend adapter class.
- `src/backends/cpu/cpu_debug_backend.cpp`: adapter from `RenderCpuDebug()` to `RenderResult`.
- `tests/backend_tests.cpp`: backend factory, CPU backend, and CUDA stub tests.
- `src/app/main.cpp`: CLI orchestration through the common backend interface only.
- `CMakeLists.txt`: replace `yaoray_backend_cpu` with `yaoray_backends` and add backend tests.
- `README.md` and `docs/architecture/overview.md`: document backend-interface status.

Existing files that should remain conceptually unchanged:

```text
include/yaoray/backends/cpu/cpu_debug_renderer.hpp
src/backends/cpu/cpu_debug_renderer.cpp
tests/cpu_debug_renderer_tests.cpp
```

The CPU debug renderer remains a lower-level renderer function with its existing tests.

## Task 1: Add Backend Interface Tests And CMake Wiring

**Files:**
- Create: `tests/backend_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create failing backend tests**

Create `tests/backend_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstdint>
#include <memory>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderScene MakeBackendTriangleScene(int width = 4, int height = 3) {
    yr::RenderScene scene;
    scene.backend = yr::RenderBackendKind::Cpu;
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
    scene.materials.push_back(yr::RenderMaterial{yr::Color3f{1.0f, 0.2f, 0.1f}, yr::Color3f{}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.5f, -0.5f, 0.0f},
        yr::Point3f{0.5f, -0.5f, 0.0f},
        yr::Point3f{0.0f, 0.5f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    return scene;
}

} // namespace

YR_TEST(create_render_backend_returns_cpu_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cpu);
}

YR_TEST(cpu_backend_renders_film_and_stats) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderScene scene = MakeBackendTriangleScene(4, 3);

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->Width(), 4);
    YR_EXPECT_EQ(result.film->Height(), 3);
    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.triangle_tests, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(create_render_backend_returns_cuda_stub_backend) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);

    YR_EXPECT_TRUE(backend != nullptr);
    YR_EXPECT_EQ(backend->Kind(), yr::RenderBackendKind::Cuda);
}

YR_TEST(cuda_backend_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderScene scene = MakeBackendTriangleScene(1, 1);
    scene.backend = yr::RenderBackendKind::Cuda;

    const yr::RenderResult result = backend->Render(scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CUDA backend not implemented yet") != std::string::npos);
}
```

- [ ] **Step 2: Replace the backend CMake target and register backend tests**

In `CMakeLists.txt`, replace the existing `yaoray_backend_cpu` target:

```cmake
add_library(yaoray_backend_cpu STATIC
    src/backends/cpu/cpu_debug_renderer.cpp
)
target_include_directories(yaoray_backend_cpu PUBLIC include)
target_link_libraries(yaoray_backend_cpu PUBLIC yaoray_core yaoray_film yaoray_render)
```

with:

```cmake
add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
)
target_include_directories(yaoray_backends PUBLIC include)
target_link_libraries(yaoray_backends PUBLIC yaoray_core yaoray_film yaoray_render)
```

Add the backend tests to `yaoray_tests`:

```cmake
    tests/backend_tests.cpp
```

Update the test link line:

```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backends)
```

Update the app link line:

```cmake
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backends)
```

- [ ] **Step 3: Run build to verify the expected failure**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: configure or build fails because these files do not exist yet:

```text
include/yaoray/backends/backend.hpp
src/backends/backend.cpp
include/yaoray/backends/cpu/cpu_debug_backend.hpp
src/backends/cpu/cpu_debug_backend.cpp
```

Do not commit this failing state.

## Task 2: Implement The Backend Interface And Factory

**Files:**
- Create: `include/yaoray/backends/backend.hpp`
- Create: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Create: `src/backends/backend.cpp`
- Create: `src/backends/cpu/cpu_debug_backend.cpp`

- [ ] **Step 1: Add the common backend public API**

Create `include/yaoray/backends/backend.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct RenderRequest {
};

struct RenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct RenderResult {
    bool ok = false;
    std::string error;
    std::optional<Film> film;
    RenderStats stats;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual RenderResult Render(const RenderScene& scene, const RenderRequest& request) = 0;
};

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind);

} // namespace yr
```

- [ ] **Step 2: Add the CPU debug backend adapter header**

Create `include/yaoray/backends/cpu/cpu_debug_backend.hpp`:

```cpp
#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CpuDebugBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    RenderResult Render(const RenderScene& scene, const RenderRequest& request) override;
};

} // namespace yr
```

- [ ] **Step 3: Implement the CPU debug backend adapter**

Create `src/backends/cpu/cpu_debug_backend.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <utility>

namespace yr {
namespace {

RenderStats ToRenderStats(const CpuDebugRenderStats& stats) {
    RenderStats result;
    result.rays_traced = stats.rays_traced;
    result.triangle_tests = stats.triangle_tests;
    result.hits = stats.hits;
    result.misses = stats.misses;
    result.elapsed_seconds = stats.elapsed_seconds;
    return result;
}

} // namespace

RenderBackendKind CpuDebugBackend::Kind() const {
    return RenderBackendKind::Cpu;
}

RenderResult CpuDebugBackend::Render(const RenderScene& scene, const RenderRequest& request) {
    (void)request;

    CpuDebugRenderResult debug_result = RenderCpuDebug(scene);

    RenderResult result;
    result.ok = true;
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}

} // namespace yr
```

- [ ] **Step 4: Implement the backend factory and CUDA stub**

Create `src/backends/backend.cpp`:

```cpp
#include <yaoray/backends/backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_backend.hpp>

#include <memory>

namespace yr {
namespace {

class CudaNotImplementedBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override {
        return RenderBackendKind::Cuda;
    }

    RenderResult Render(const RenderScene& scene, const RenderRequest& request) override {
        (void)scene;
        (void)request;

        RenderResult result;
        result.ok = false;
        result.error = "CUDA backend not implemented yet.";
        return result;
    }
};

} // namespace

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::Cpu:
            return std::make_unique<CpuDebugBackend>();
        case RenderBackendKind::Cuda:
            return std::make_unique<CudaNotImplementedBackend>();
    }
    return nullptr;
}

} // namespace yr
```

- [ ] **Step 5: Run backend tests**

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
git add CMakeLists.txt include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_debug_backend.hpp src/backends/backend.cpp src/backends/cpu/cpu_debug_backend.cpp tests/backend_tests.cpp
git commit -m "feat: add render backend interface"
```

## Task 3: Route CLI Rendering Through The Backend Interface

**Files:**
- Modify: `src/app/main.cpp`

- [ ] **Step 1: Confirm the current app has CPU-specific dispatch**

Run:

```powershell
rg -n "cpu_debug_renderer|RenderCpuDebug|RenderBackendKind::Cuda" src\app\main.cpp
```

Expected: output shows that `src/app/main.cpp` includes the CPU debug renderer and branches directly on CUDA.

- [ ] **Step 2: Replace CPU-specific include with the common backend include**

In `src/app/main.cpp`, replace:

```cpp
#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
```

with:

```cpp
#include <yaoray/backends/backend.hpp>
```

Keep these includes:

```cpp
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
```

- [ ] **Step 3: Replace direct CPU/CUDA dispatch**

In `RunRender()`, keep this output block:

```cpp
    const yr::RenderScene& render_scene = compile_result.scene.value();
    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
```

Replace everything after that block through the final `return 0;` with:

```cpp
    const auto backend = yr::CreateRenderBackend(render_scene.backend);
    if (!backend) {
        std::cerr << "Render backend not available: " << yr::RenderBackendName(render_scene.backend) << '\n';
        return 1;
    }

    const yr::RenderResult render_result = backend->Render(render_scene, yr::RenderRequest{});
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
    if (!render_result.film.has_value()) {
        std::cerr << "Render backend completed without a film.\n";
        return 1;
    }

    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(scene.film.tone_mapper),
        scene.film.exposure
    };
    const yr::ImageWriteResult write_result = yr::WritePpm(*render_result.film, tone_map, scene.film.output);
    if (!write_result.ok) {
        std::cerr << "Image write error: " << write_result.error << '\n';
        return 1;
    }

    std::cout << "Rendered image: " << scene.film.output.generic_string() << '\n';
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
    std::cout << "Triangle tests: " << render_result.stats.triangle_tests << '\n';
    std::cout << "Hits: " << render_result.stats.hits << '\n';
    std::cout << "Misses: " << render_result.stats.misses << '\n';
    std::cout << "Elapsed seconds: " << render_result.stats.elapsed_seconds << '\n';
    return 0;
```

- [ ] **Step 4: Run tests and structural check**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
rg -n "cpu_debug_renderer|RenderCpuDebug|RenderBackendKind::Cuda" src\app\main.cpp
```

Expected CTest result:

```text
100% tests passed
```

Expected `rg` result: no output. The app must not include CPU-specific renderer headers, call `RenderCpuDebug()`, or branch directly on `RenderBackendKind::Cuda`.

- [ ] **Step 5: Run manual CLI checks**

Run:

```powershell
Remove-Item -Force -ErrorAction SilentlyContinue scenes\examples\out\minimal.ppm
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cpu
Get-Content -Path scenes\examples\out\minimal.ppm -TotalCount 4
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cuda
```

Expected CPU output includes:

```text
Rendered image: scenes/examples/out/minimal.ppm
Rays traced:
Triangle tests:
Hits:
Misses:
```

Expected PPM header:

```text
P3
640 360
255
```

Expected CUDA command exits non-zero and includes:

```text
CUDA backend not implemented yet.
```

If the configured build generator places the executable under `build\Debug\yaoray.exe`, use that path for the manual CLI checks.

- [ ] **Step 6: Commit**

Run:

```powershell
git add src/app/main.cpp
git commit -m "refactor: route cli rendering through backends"
```

## Task 4: Update Backend Interface Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README current status**

In `README.md`, update the foundation list so it includes:

```markdown
- render backend dispatch through a common backend interface
```

Update the sentence after the status list to:

```markdown
Final path tracing quality, asset import, BVH construction, PNG output, and real CUDA backend support are planned as separate implementation slices.
```

Keep the run-section wording that explains CPU debug PPM output.

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, update the render-layer description to:

```markdown
The render layer compiles that semantic scene into backend-friendly data. The current `yaoray_render` slice provides a minimal `RenderScene` with render settings, camera data, environment data, area lights, materials, and flat world-space triangles. Rendering is dispatched through a common backend interface so CPU, CUDA, and future OptiX backends can consume this compiled representation without app-layer special cases.
```

Update the implemented-slices list so it includes:

```markdown
- common render backend interface with CPU debug and CUDA not-implemented backends
```

Update the future-work sentence to:

```markdown
Asset import, BVH construction, PNG output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 3: Run docs smoke check and tests**

Run:

```powershell
rg -n "backend interface|backend dispatch|CUDA not-implemented|CPU debug" README.md docs/architecture/overview.md
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `rg` finds backend interface wording in both docs, and CTest passes.

- [ ] **Step 4: Commit**

Run:

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: describe render backend interface"
```

## Task 5: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm app/backend dependency direction**

Run:

```powershell
rg -n "cpu_debug_renderer|RenderCpuDebug|RenderBackendKind::Cuda" src\app\main.cpp
rg -n "backends/backend|backends/cpu|RenderBackend|CreateRenderBackend" include\yaoray\scene src\scene include\yaoray\render src\render
```

Expected: no output from both commands. The app should not know CPU-specific rendering details, and `scene`/`render` should not depend on backends.

- [ ] **Step 2: Confirm backend API discoverability**

Run:

```powershell
rg -n "RenderRequest|RenderStats|RenderResult|RenderBackend|CreateRenderBackend|CpuDebugBackend|CUDA backend not implemented" include src tests
```

Expected: matches in the common backend header, CPU debug backend adapter, factory implementation, tests, and CLI usage.

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

- [ ] **Step 4: Verify CPU output manually**

Run:

```powershell
Remove-Item -Force -ErrorAction SilentlyContinue scenes\examples\out\minimal.ppm
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cpu
Get-Content -Path scenes\examples\out\minimal.ppm -TotalCount 4
```

Expected CLI output includes:

```text
Rendered image: scenes/examples/out/minimal.ppm
Rays traced:
Triangle tests:
Hits:
Misses:
```

Expected PPM header:

```text
P3
640 360
255
```

If the configured build generator places the executable under `build\Debug\yaoray.exe`, use that path for the manual CLI check.

- [ ] **Step 5: Verify CUDA failure manually**

Run:

```powershell
.\build\yaoray.exe render scenes\examples\minimal.toml --backend cuda
```

Expected: command exits non-zero and output includes:

```text
CUDA backend not implemented yet.
```

If the configured build generator places the executable under `build\Debug\yaoray.exe`, use that path for the manual CLI check.

- [ ] **Step 6: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- Common backend interface: Task 2.
- Backend factory from `RenderBackendKind`: Task 2.
- CPU debug backend wrapper: Task 2.
- CUDA not-implemented backend: Task 2.
- Unified `RenderResult` with `Film`, stats, and error text: Task 2.
- CLI dispatch through backend interface: Task 3.
- Preserved CPU PPM success and CUDA failure behavior: Tasks 3 and 5.
- Backend-level tests: Task 1.
- Documentation updates: Task 4.

Type consistency:

- `RenderRequest`, `RenderStats`, `RenderResult`, `RenderBackend`, and `CreateRenderBackend()` live in `include/yaoray/backends/backend.hpp`.
- `CpuDebugBackend` lives in `include/yaoray/backends/cpu/cpu_debug_backend.hpp`.
- `RenderResult::film` is `std::optional<Film>` and must be checked before writing output.
- The backend library target is named `yaoray_backends`.

Implementation guardrails:

- Do not add an Integrator interface in this plan.
- Do not add BVH, path tracing, asset import, or real CUDA rendering in this plan.
- Do not move image writing or tone mapping into backends.
- Do not make `scene` or `render` depend on `backends`.
- Keep `RenderCpuDebug()` and its existing tests intact as lower-level CPU debug renderer coverage.
