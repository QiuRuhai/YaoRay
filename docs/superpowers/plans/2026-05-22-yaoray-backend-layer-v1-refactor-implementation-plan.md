# YaoRay Backend Layer v1 Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the backend layer to expose an explicit `Prepare(RenderSceneIR) -> PreparedScene` and `Render(PreparedScene)` lifecycle while preserving current CPU rendering behavior and CUDA controlled failure behavior.

**Architecture:** Keep `RenderSceneIR` backend-neutral and move backend-owned runtime data behind a polymorphic `PreparedScene` interface. CPU prepares a `CpuPreparedScene` with the existing CPU BVH, while CUDA gets named C++ placeholder classes with no CUDA runtime dependency. The CLI keeps the same parse, compile, backend-select, render, write flow, but calls backend preparation before rendering.

**Tech Stack:** C++17, CMake, custom `yr_test` test harness, CPU renderer modules under `yaoray_backends`, existing `ctest` CLI render tests.

---

## File Structure

- Modify `include/yaoray/backends/backend.hpp`: add the shared `PreparedScene` base class, add `BackendPrepareResult`, and change `RenderBackend` to the two-stage API.
- Modify `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`: make `CpuPreparedScene` implement `PreparedScene` and add a constructor so it can own `RenderBvh` after gaining virtual methods.
- Modify `src/backends/cpu/cpu_prepared_scene.cpp`: implement `CpuPreparedScene` constructor, `Kind()`, `SourceScene()`, `Scene()`, and preserve `PrepareCpuScene()`.
- Modify `include/yaoray/backends/cpu/cpu_debug_backend.hpp`: expose `Prepare()` plus `Render(const PreparedScene&, ...)`.
- Modify `src/backends/cpu/cpu_debug_backend.cpp`: move CPU scene preparation into `Prepare()` and make `Render()` validate/downcast `CpuPreparedScene`.
- Modify `src/backends/backend.cpp`: keep the factory public surface stable, first adapt the current CUDA not-implemented backend to the new API, then replace it with a named `CudaBackend`.
- Create `include/yaoray/backends/cuda/cuda_backend.hpp`: named CUDA backend class with ordinary C++ declarations.
- Create `src/backends/cuda/cuda_backend.cpp`: CUDA prepare/render controlled failure implementation.
- Create `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`: minimal CUDA prepared scene type for future CUDA scene data.
- Create `src/backends/cuda/cuda_prepared_scene.cpp`: source-scene plumbing for `CudaPreparedScene`.
- Modify `src/app/main.cpp`: route CLI rendering through `backend->Prepare()` before `backend->Render()`.
- Modify `tests/backend_tests.cpp`: lock CPU prepare/render behavior, wrong prepared-scene rejection, and CUDA controlled failures.
- Modify `CMakeLists.txt`: compile the new CUDA placeholder source files and update the CUDA CLI failure regex.
- Modify `docs/architecture/overview.md`: describe the two-stage backend lifecycle and named CUDA placeholder.
- Modify `docs/superpowers/specs/2026-05-22-yaoray-backend-layer-v1-refactor-design.md`: append implementation status after the refactor lands.

## Task 1: Public Backend Contract And CPU Flow

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`
- Modify: `src/backends/cpu/cpu_prepared_scene.cpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `src/backends/backend.cpp`
- Modify: `src/app/main.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update backend tests to require the two-stage API**

In `tests/backend_tests.cpp`, add this helper class inside the anonymous namespace after `MakeBackendTriangleScene()`:

```cpp
class ForeignPreparedScene final : public yr::PreparedScene {
public:
    explicit ForeignPreparedScene(const yr::RenderSceneIR& scene)
        : scene_(&scene) {
    }

    yr::RenderBackendKind Kind() const override {
        return yr::RenderBackendKind::Cuda;
    }

    const yr::RenderSceneIR& SourceScene() const override {
        return *scene_;
    }

private:
    const yr::RenderSceneIR* scene_ = nullptr;
};
```

In `cpu_prepare_scene_builds_bvh_from_render_scene_ir`, add source-scene assertions after the existing `render_scene` pointer assertion:

```cpp
    YR_EXPECT_EQ(&prepared.scene->SourceScene(), &scene);
    YR_EXPECT_EQ(&prepared.scene->Scene(), &scene);
    YR_EXPECT_EQ(prepared.scene->Kind(), yr::RenderBackendKind::Cpu);
```

Add this test after `cpu_prepare_scene_builds_bvh_from_render_scene_ir`:

```cpp
YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);

    yr::BackendPrepareResult prepared = backend->Prepare(scene);

    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.error.empty());
    YR_EXPECT_TRUE(prepared.scene != nullptr);
    YR_EXPECT_EQ(prepared.scene->Kind(), yr::RenderBackendKind::Cpu);

    const auto* cpu_scene = dynamic_cast<const yr::CpuPreparedScene*>(prepared.scene.get());
    YR_EXPECT_TRUE(cpu_scene != nullptr);
    YR_EXPECT_EQ(&cpu_scene->SourceScene(), &scene);
    YR_EXPECT_EQ(cpu_scene->bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(cpu_scene->bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(cpu_scene->bvh.max_depth, 1);
}
```

Replace each direct CPU render call in `cpu_backend_renders_film_and_stats`, `cpu_backend_dispatches_path_integrator`, and `cpu_backend_keeps_debug_direct_as_default_integrator` with this pattern:

```cpp
    yr::BackendPrepareResult prepared = backend->Prepare(scene);
    YR_EXPECT_TRUE(prepared.ok);
    YR_EXPECT_TRUE(prepared.scene != nullptr);

    const yr::RenderResult result = backend->Render(*prepared.scene, yr::RenderRequest{});
```

Add this test after the CPU render tests:

```cpp
YR_TEST(cpu_backend_rejects_non_cpu_prepared_scene) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    const ForeignPreparedScene foreign_scene(scene);

    const yr::RenderResult result = backend->Render(foreign_scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CPU backend received a non-CPU prepared scene") != std::string::npos);
}
```

Replace `cuda_backend_returns_not_implemented_failure` with these two tests:

```cpp
YR_TEST(cuda_backend_prepare_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    scene.requested_backend = yr::RenderBackendKind::Cuda;

    yr::BackendPrepareResult prepared = backend->Prepare(scene);

    YR_EXPECT_TRUE(!prepared.ok);
    YR_EXPECT_TRUE(prepared.scene == nullptr);
    YR_EXPECT_TRUE(prepared.error.find("CUDA backend preparation is not implemented yet") != std::string::npos);
}

YR_TEST(cuda_backend_render_returns_not_implemented_failure) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(1, 1);
    const ForeignPreparedScene prepared(scene);

    const yr::RenderResult result = backend->Render(prepared, yr::RenderRequest{});

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(!result.film.has_value());
    YR_EXPECT_TRUE(result.error.find("CUDA backend rendering is not implemented yet") != std::string::npos);
}
```

- [ ] **Step 2: Run the focused build and verify the tests fail for the right reason**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target yaoray_tests --config Debug
```

Expected: configuration succeeds, then compilation fails because `yr::PreparedScene`, `yr::BackendPrepareResult`, and `RenderBackend::Prepare()` do not exist yet.

- [ ] **Step 3: Add the shared prepared-scene backend API**

Replace the public backend class section in `include/yaoray/backends/backend.hpp` with:

```cpp
class PreparedScene {
public:
    virtual ~PreparedScene() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual const RenderSceneIR& SourceScene() const = 0;
};

struct BackendPrepareResult {
    bool ok = false;
    std::string error;
    std::unique_ptr<PreparedScene> scene;
};

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual RenderBackendKind Kind() const = 0;
    virtual BackendPrepareResult Prepare(const RenderSceneIR& scene) = 0;
    virtual RenderResult Render(const PreparedScene& scene, const RenderRequest& request) = 0;
};
```

Keep `RenderRequest`, `RenderStats`, `RenderResult`, and `CreateRenderBackend(RenderBackendKind kind)` unchanged.

- [ ] **Step 4: Make `CpuPreparedScene` implement `PreparedScene`**

Replace `include/yaoray/backends/cpu/cpu_prepared_scene.hpp` with:

```cpp
#pragma once

#include <optional>
#include <string>

#include <yaoray/backends/backend.hpp>
#include <yaoray/render/bvh.hpp>

namespace yr {

struct CpuPreparedScene final : public PreparedScene {
    CpuPreparedScene() = default;
    CpuPreparedScene(const RenderSceneIR& scene, RenderBvh prepared_bvh);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSceneIR& Scene() const;

    const RenderSceneIR* render_scene = nullptr;
    RenderBvh bvh;
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
};

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene);

} // namespace yr
```

Replace `src/backends/cpu/cpu_prepared_scene.cpp` with:

```cpp
#include <yaoray/backends/cpu/cpu_prepared_scene.hpp>

#include <utility>

namespace yr {

CpuPreparedScene::CpuPreparedScene(const RenderSceneIR& scene, RenderBvh prepared_bvh)
    : render_scene(&scene),
      bvh(std::move(prepared_bvh)) {
}

RenderBackendKind CpuPreparedScene::Kind() const {
    return RenderBackendKind::Cpu;
}

const RenderSceneIR& CpuPreparedScene::SourceScene() const {
    return *render_scene;
}

const RenderSceneIR& CpuPreparedScene::Scene() const {
    return SourceScene();
}

CpuPrepareResult PrepareCpuScene(const RenderSceneIR& scene) {
    CpuPrepareResult result;

    BvhBuildResult build = BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(scene, std::move(build.bvh));
    return result;
}

} // namespace yr
```

- [ ] **Step 5: Split `CpuDebugBackend` into prepare and render stages**

Replace `include/yaoray/backends/cpu/cpu_debug_backend.hpp` with:

```cpp
#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CpuDebugBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    BackendPrepareResult Prepare(const RenderSceneIR& scene) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
```

In `src/backends/cpu/cpu_debug_backend.cpp`, add this helper inside the anonymous namespace after the `ToRenderStats()` overloads:

```cpp
BackendPrepareResult ToBackendPrepareResult(CpuPrepareResult prepared) {
    BackendPrepareResult result;
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    result.ok = true;
    result.scene = std::make_unique<CpuPreparedScene>(std::move(prepared.scene.value()));
    return result;
}
```

Replace `CpuDebugBackend::Render(const RenderSceneIR& scene, ...)` with:

```cpp
BackendPrepareResult CpuDebugBackend::Prepare(const RenderSceneIR& scene) {
    return ToBackendPrepareResult(PrepareCpuScene(scene));
}

RenderResult CpuDebugBackend::Render(const PreparedScene& scene, const RenderRequest& request) {
    (void)request;

    RenderResult result;
    const auto* cpu_scene = dynamic_cast<const CpuPreparedScene*>(&scene);
    if (scene.Kind() != RenderBackendKind::Cpu || cpu_scene == nullptr) {
        result.ok = false;
        result.error = "CPU backend received a non-CPU prepared scene.";
        return result;
    }

    const RenderSceneIR& render_scene = cpu_scene->Scene();
    result.ok = true;
    if (render_scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(*cpu_scene);
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }

    CpuDebugRenderResult debug_result = RenderCpuDebug(*cpu_scene);
    result.film.emplace(std::move(debug_result.film));
    result.stats = ToRenderStats(debug_result.stats);
    return result;
}
```

Confirm the file includes `<memory>` and `<utility>` because `ToBackendPrepareResult()` creates a `std::unique_ptr` and moves the prepared scene into it.

- [ ] **Step 6: Adapt the temporary CUDA not-implemented backend to the new API**

In `src/backends/backend.cpp`, replace the body of `CudaNotImplementedBackend` with:

```cpp
class CudaNotImplementedBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override {
        return RenderBackendKind::Cuda;
    }

    BackendPrepareResult Prepare(const RenderSceneIR& scene) override {
        (void)scene;

        BackendPrepareResult result;
        result.ok = false;
        result.error = "CUDA backend preparation is not implemented yet.";
        return result;
    }

    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override {
        (void)scene;
        (void)request;

        RenderResult result;
        result.ok = false;
        result.error = "CUDA backend rendering is not implemented yet.";
        return result;
    }
};
```

- [ ] **Step 7: Route the CLI through backend preparation**

In `src/app/main.cpp`, replace:

```cpp
    const yr::RenderResult render_result = backend->Render(render_scene, yr::RenderRequest{});
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
```

with:

```cpp
    yr::BackendPrepareResult prepare_result = backend->Prepare(render_scene);
    if (!prepare_result.ok || prepare_result.scene == nullptr) {
        std::cerr << "Render backend preparation failed: "
                  << (prepare_result.error.empty() ? "unknown error" : prepare_result.error)
                  << '\n';
        return 1;
    }

    const yr::RenderResult render_result = backend->Render(*prepare_result.scene, yr::RenderRequest{});
    if (!render_result.ok) {
        std::cerr << render_result.error << '\n';
        return 1;
    }
```

- [ ] **Step 8: Update the CUDA CLI expected failure text**

In `CMakeLists.txt`, replace the CUDA CLI test regex:

```cmake
        EXPECT_REGEX "CUDA backend not implemented yet"
```

with:

```cmake
        EXPECT_REGEX "Render backend preparation failed: CUDA backend preparation is not implemented yet"
```

- [ ] **Step 9: Run focused verification for CPU backend behavior and CUDA controlled failure**

Run:

```bash
cmake --build build --target yaoray_tests --config Debug
ctest --test-dir build -R "yaoray_tests" --output-on-failure -C Debug
cmake --build build --target yaoray --config Debug
ctest --test-dir build -R "yaoray_cli_render_cuda" --output-on-failure -C Debug
```

Expected: all four commands pass. `yaoray_cli_render_cuda` must still be an expected-failure test, and its output must include the backend preparation failure string from Step 8.

- [ ] **Step 10: Commit the API and CPU flow**

Run:

```bash
git add include/yaoray/backends/backend.hpp \
    include/yaoray/backends/cpu/cpu_prepared_scene.hpp \
    src/backends/cpu/cpu_prepared_scene.cpp \
    include/yaoray/backends/cpu/cpu_debug_backend.hpp \
    src/backends/cpu/cpu_debug_backend.cpp \
    src/backends/backend.cpp \
    src/app/main.cpp \
    tests/backend_tests.cpp \
    CMakeLists.txt
git commit -m "refactor: split backend prepare and render"
```

## Task 2: Named CUDA Backend Slice

**Files:**
- Create: `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`
- Create: `src/backends/cuda/cuda_prepared_scene.cpp`
- Create: `include/yaoray/backends/cuda/cuda_backend.hpp`
- Create: `src/backends/cuda/cuda_backend.cpp`
- Modify: `src/backends/backend.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add CUDA prepared-scene tests before the implementation**

In `tests/backend_tests.cpp`, add:

```cpp
#include <yaoray/backends/cuda/cuda_prepared_scene.hpp>
```

Remove the `ForeignPreparedScene` helper class from the anonymous namespace.

Add this test after `cpu_backend_rejects_non_cpu_prepared_scene`:

```cpp
YR_TEST(cuda_prepared_scene_exposes_source_scene) {
    const yr::RenderSceneIR scene = MakeBackendTriangleScene(2, 2);

    const yr::CudaPreparedScene prepared(scene);

    YR_EXPECT_EQ(prepared.Kind(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(&prepared.SourceScene(), &scene);
}
```

In `cpu_backend_rejects_non_cpu_prepared_scene`, replace the foreign prepared scene construction:

```cpp
    const ForeignPreparedScene foreign_scene(scene);

    const yr::RenderResult result = backend->Render(foreign_scene, yr::RenderRequest{});
```

with:

```cpp
    const yr::CudaPreparedScene cuda_scene(scene);

    const yr::RenderResult result = backend->Render(cuda_scene, yr::RenderRequest{});
```

In `cuda_backend_render_returns_not_implemented_failure`, replace:

```cpp
    const ForeignPreparedScene prepared(scene);
```

with:

```cpp
    const yr::CudaPreparedScene prepared(scene);
```

- [ ] **Step 2: Run the focused build and verify it fails for missing CUDA headers**

Run:

```bash
cmake --build build --target yaoray_tests --config Debug
```

Expected: compilation fails with an include error for `yaoray/backends/cuda/cuda_prepared_scene.hpp`.

- [ ] **Step 3: Create `CudaPreparedScene`**

Create `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`:

```cpp
#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaPreparedScene final : public PreparedScene {
public:
    explicit CudaPreparedScene(const RenderSceneIR& scene);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;

private:
    const RenderSceneIR* render_scene_ = nullptr;
};

} // namespace yr
```

Create `src/backends/cuda/cuda_prepared_scene.cpp`:

```cpp
#include <yaoray/backends/cuda/cuda_prepared_scene.hpp>

namespace yr {

CudaPreparedScene::CudaPreparedScene(const RenderSceneIR& scene)
    : render_scene_(&scene) {
}

RenderBackendKind CudaPreparedScene::Kind() const {
    return RenderBackendKind::Cuda;
}

const RenderSceneIR& CudaPreparedScene::SourceScene() const {
    return *render_scene_;
}

} // namespace yr
```

- [ ] **Step 4: Create the named CUDA backend**

Create `include/yaoray/backends/cuda/cuda_backend.hpp`:

```cpp
#pragma once

#include <yaoray/backends/backend.hpp>

namespace yr {

class CudaBackend final : public RenderBackend {
public:
    RenderBackendKind Kind() const override;
    BackendPrepareResult Prepare(const RenderSceneIR& scene) override;
    RenderResult Render(const PreparedScene& scene, const RenderRequest& request) override;
};

} // namespace yr
```

Create `src/backends/cuda/cuda_backend.cpp`:

```cpp
#include <yaoray/backends/cuda/cuda_backend.hpp>

namespace yr {

RenderBackendKind CudaBackend::Kind() const {
    return RenderBackendKind::Cuda;
}

BackendPrepareResult CudaBackend::Prepare(const RenderSceneIR& scene) {
    (void)scene;

    BackendPrepareResult result;
    result.ok = false;
    result.error = "CUDA backend preparation is not implemented yet.";
    return result;
}

RenderResult CudaBackend::Render(const PreparedScene& scene, const RenderRequest& request) {
    (void)scene;
    (void)request;

    RenderResult result;
    result.ok = false;
    result.error = "CUDA backend rendering is not implemented yet.";
    return result;
}

} // namespace yr
```

- [ ] **Step 5: Wire the factory to the named CUDA backend**

Replace `src/backends/backend.cpp` with:

```cpp
#include <yaoray/backends/backend.hpp>

#include <yaoray/backends/cpu/cpu_debug_backend.hpp>
#include <yaoray/backends/cuda/cuda_backend.hpp>

#include <memory>

namespace yr {

std::unique_ptr<RenderBackend> CreateRenderBackend(RenderBackendKind kind) {
    switch (kind) {
        case RenderBackendKind::Cpu:
            return std::make_unique<CpuDebugBackend>();
        case RenderBackendKind::Cuda:
            return std::make_unique<CudaBackend>();
    }
    return nullptr;
}

} // namespace yr
```

- [ ] **Step 6: Add CUDA placeholder sources to the backend library**

In `CMakeLists.txt`, update `yaoray_backends` sources to include the CUDA C++ files:

```cmake
add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
    src/backends/cpu/cpu_prepared_scene.cpp
    src/backends/cpu/cpu_tile_scheduler.cpp
    src/backends/cpu/cpu_sampler.cpp
    src/backends/cpu/cpu_path_tracer.cpp
    src/backends/cuda/cuda_backend.cpp
    src/backends/cuda/cuda_prepared_scene.cpp
)
```

- [ ] **Step 7: Run named CUDA slice verification**

Run:

```bash
cmake --build build --target yaoray_tests --config Debug
ctest --test-dir build -R "yaoray_tests" --output-on-failure -C Debug
cmake --build build --target yaoray --config Debug
ctest --test-dir build -R "yaoray_cli_render_cuda" --output-on-failure -C Debug
```

Expected: all commands pass. `CreateRenderBackend(RenderBackendKind::Cuda)` returns a backend whose `Kind()` is `Cuda`, `CudaPreparedScene` exposes its source scene, and the CLI CUDA path still exits through a controlled not-implemented preparation error.

- [ ] **Step 8: Commit the named CUDA backend slice**

Run:

```bash
git add include/yaoray/backends/cuda/cuda_prepared_scene.hpp \
    src/backends/cuda/cuda_prepared_scene.cpp \
    include/yaoray/backends/cuda/cuda_backend.hpp \
    src/backends/cuda/cuda_backend.cpp \
    src/backends/backend.cpp \
    tests/backend_tests.cpp \
    CMakeLists.txt
git commit -m "refactor: add named cuda backend placeholder"
```

## Task 3: Documentation, Cleanup, And Full Verification

**Files:**
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-22-yaoray-backend-layer-v1-refactor-design.md`

- [ ] **Step 1: Update architecture overview with the implemented backend lifecycle**

In `docs/architecture/overview.md`, replace the sentence:

```markdown
Rendering is dispatched through a common backend interface so CPU, CUDA, and future OptiX backends can prepare and consume their own runtime representations without app-layer special cases.
```

with:

```markdown
Rendering is dispatched through a two-stage backend interface: each backend first prepares backend-owned runtime data from `RenderSceneIR`, then renders from that prepared scene without app-layer knowledge of CPU BVHs, CUDA buffers, or future OptiX handles.
```

Replace the current implemented-slice bullet:

```markdown
- common render backend interface with CPU debug and CUDA not-implemented backends
```

with:

```markdown
- two-stage render backend interface with CPU `CpuPreparedScene` rendering and a named CUDA not-implemented backend placeholder
```

- [ ] **Step 2: Append implementation status to the design spec**

Append this section to `docs/superpowers/specs/2026-05-22-yaoray-backend-layer-v1-refactor-design.md`:

```markdown
## Implementation Status

Backend Layer v1 is implemented as a two-stage backend lifecycle. `RenderBackend::Prepare()` produces backend-owned prepared-scene data, and `RenderBackend::Render()` consumes a `PreparedScene` instead of raw `RenderSceneIR`.

CPU rendering keeps the existing debug and path integrator behavior through `CpuPreparedScene`, including CPU BVH preparation outside `RenderSceneIR`. CUDA is represented by named `CudaBackend` and `CudaPreparedScene` classes that compile as ordinary C++ on macOS and return controlled not-implemented errors without requiring CUDA headers, kernels, or CMake CUDA language support.
```

- [ ] **Step 3: Run repository-wide cleanup checks**

Run:

```bash
rg -n "CudaNotImplementedBackend|Render\\(const RenderSceneIR|backend->Render\\(render_scene" include src tests
git diff --check
```

Expected: `rg` returns no matches for stale backend implementation patterns, and `git diff --check` reports no whitespace errors.

- [ ] **Step 4: Run full verification**

Run:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build succeeds and every CTest test passes on macOS. CPU render outputs and stats semantics remain unchanged, and `yaoray_cli_render_cuda` remains a controlled expected-failure test.

- [ ] **Step 5: Run direct CLI smoke checks**

Run:

```bash
./build/yaoray render tests/fixtures/scene/builtin_triangle.toml --backend cpu
```

Expected: exit code `0`, output includes `Requested backend: cpu`, `Integrator: debug_direct`, `Rendered image:`, `BVH nodes:`, and `Rays traced:`.

Run:

```bash
./build/yaoray render tests/fixtures/scene/builtin_triangle.toml --backend cuda
```

Expected: exit code `1`, output includes `Render backend preparation failed: CUDA backend preparation is not implemented yet.`.

- [ ] **Step 6: Commit docs and verification cleanup**

Run:

```bash
git add docs/architecture/overview.md \
    docs/superpowers/specs/2026-05-22-yaoray-backend-layer-v1-refactor-design.md
git commit -m "docs: document backend layer v1 implementation"
```

## Final Review Checklist

- [ ] `RenderBackend` exposes `Prepare(const RenderSceneIR&)` and `Render(const PreparedScene&, const RenderRequest&)`.
- [ ] `RenderSceneIR` contains no CPU BVH, CUDA pointer, CUDA buffer, or OptiX handle fields.
- [ ] `CpuPreparedScene` owns the CPU `RenderBvh` and implements `PreparedScene`.
- [ ] `CpuDebugBackend::Render()` validates the prepared scene before downcasting.
- [ ] CUDA has named `CudaBackend` and `CudaPreparedScene` classes under `include/yaoray/backends/cuda` and `src/backends/cuda`.
- [ ] CUDA files include no CUDA runtime, driver, kernel, or OptiX headers.
- [ ] CLI rendering calls `Prepare()` before `Render()`.
- [ ] CUDA CLI behavior is a controlled not-implemented failure, not a missing-backend failure.
- [ ] `ctest --test-dir build --output-on-failure -C Debug` passes.
