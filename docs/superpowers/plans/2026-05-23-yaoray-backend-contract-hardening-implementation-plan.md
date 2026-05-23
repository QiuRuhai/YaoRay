# YaoRay Backend Contract Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Architecture Hardening v1 Slice 1 and Slice 2 by making prepared scenes own their render input and by adding an explicit backend capability/contract surface.

**Architecture:** `RenderBackend::Prepare()` will take `RenderSceneIR` by value, allowing callers to pass a temporary, copy, or moved compiled scene. Concrete prepared scenes will own the `RenderSceneIR` they render from plus their backend runtime data. CPU remains the runnable backend; CUDA remains a controlled non-rendering backend with explicit capabilities.

**Tech Stack:** C++20, CMake, custom `yr_test` test framework, existing `yaoray_core`, `yaoray_scene`, `yaoray_render`, and `yaoray_backends` libraries.

---

## Scope

This plan implements only:

- Slice 1: Backend lifetime hardening.
- Slice 2: Backend contract clarification.

It intentionally does not implement Render IR table migration, AssetResource decoupling, or CUDA packing. Those need separate plans after this boundary is stable.

## File Structure

- Modify `include/yaoray/backends/backend.hpp`
  - Owns the public backend contract.
  - Changes `Prepare()` to take `RenderSceneIR` by value.
  - Adds `RenderBackendCapabilities`.

- Modify `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`
  - Makes `CpuPreparedScene` own `RenderSceneIR`.
  - Removes the raw pointer source-scene field.
  - Changes `PrepareCpuScene()` to take `RenderSceneIR` by value.

- Modify `src/backends/cpu/cpu_prepared_scene.cpp`
  - Moves owned `RenderSceneIR` into `CpuPreparedScene`.
  - Builds CPU BVH from the owned scene input before moving it.

- Modify `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
  - Updates the `Prepare()` override signature.
  - Adds `Capabilities()` override.

- Modify `src/backends/cpu/cpu_debug_backend.cpp`
  - Updates prepare flow to move render scenes.
  - Adds CPU capability reporting.
  - Rejects scenes whose requested backend is not CPU.

- Modify `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`
  - Makes `CudaPreparedScene` own `RenderSceneIR`.

- Modify `src/backends/cuda/cuda_prepared_scene.cpp`
  - Moves owned `RenderSceneIR` into `CudaPreparedScene`.

- Modify `include/yaoray/backends/cuda/cuda_backend.hpp`
  - Updates the `Prepare()` override signature.
  - Adds `Capabilities()` override.

- Modify `src/backends/cuda/cuda_backend.cpp`
  - Adds CUDA capability reporting.
  - Keeps prepare/render as controlled not-implemented failures.

- Modify `tests/backend_tests.cpp`
  - Updates ownership expectations.
  - Adds tests for prepared-scene lifetime independence.
  - Adds tests for backend capabilities and CPU requested-backend rejection.

- Modify `docs/architecture/overview.md`
  - Records the new ownership and capability contract.

## Task 1: Write Failing Ownership Tests

**Files:**
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Add the missing include for moving prepared scenes**

In `tests/backend_tests.cpp`, add `<utility>` after the existing standard includes:

```cpp
#include <utility>
```

- [ ] **Step 2: Update CPU direct prepare ownership expectations**

In `YR_TEST(cpu_prepare_scene_builds_bvh_from_render_scene_ir)`, replace the source-scene pointer expectations:

```cpp
    YR_EXPECT_TRUE(prepared.scene->render_scene == &scene);
    YR_EXPECT_EQ(&prepared.scene->SourceScene(), &scene);
    YR_EXPECT_EQ(&prepared.scene->Scene(), &scene);
```

with:

```cpp
    YR_EXPECT_TRUE(&prepared.scene->SourceScene() != &scene);
    YR_EXPECT_TRUE(&prepared.scene->Scene() != &scene);
    YR_EXPECT_EQ(prepared.scene->SourceScene().width, scene.width);
    YR_EXPECT_EQ(prepared.scene->SourceScene().height, scene.height);
    YR_EXPECT_EQ(prepared.scene->SourceScene().triangles.size(), scene.triangles.size());
```

- [ ] **Step 3: Update CPU backend prepare ownership expectations**

In `YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene)`, replace:

```cpp
    YR_EXPECT_EQ(&cpu_scene->SourceScene(), &scene);
```

with:

```cpp
    YR_EXPECT_TRUE(&cpu_scene->SourceScene() != &scene);
    YR_EXPECT_EQ(cpu_scene->SourceScene().width, scene.width);
    YR_EXPECT_EQ(cpu_scene->SourceScene().height, scene.height);
    YR_EXPECT_EQ(cpu_scene->SourceScene().triangles.size(), scene.triangles.size());
```

- [ ] **Step 4: Add a CPU prepared lifetime test**

Add this test after `YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene)`:

```cpp
YR_TEST(cpu_backend_prepared_scene_outlives_source_render_scene_ir) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    std::unique_ptr<yr::PreparedScene> prepared_scene;

    {
        yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);
        yr::BackendPrepareResult prepared = backend->Prepare(scene);
        YR_EXPECT_TRUE(prepared.ok);
        YR_EXPECT_TRUE(prepared.scene != nullptr);
        prepared_scene = std::move(prepared.scene);
    }

    const yr::RenderResult result = backend->Render(*prepared_scene, yr::RenderRequest{});

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_TRUE(result.film.has_value());
    YR_EXPECT_EQ(result.film->Width(), 4);
    YR_EXPECT_EQ(result.film->Height(), 3);
}
```

- [ ] **Step 5: Update CUDA prepared-scene ownership expectations**

In `YR_TEST(cuda_prepared_scene_exposes_source_scene)`, replace:

```cpp
    YR_EXPECT_EQ(&prepared.SourceScene(), &scene);
```

with:

```cpp
    YR_EXPECT_TRUE(&prepared.SourceScene() != &scene);
    YR_EXPECT_EQ(prepared.SourceScene().width, scene.width);
    YR_EXPECT_EQ(prepared.SourceScene().height, scene.height);
    YR_EXPECT_EQ(prepared.SourceScene().triangles.size(), scene.triangles.size());
```

- [ ] **Step 6: Run backend tests and confirm the expected failure**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: build succeeds, and at least the ownership expectations fail because `CpuPreparedScene` and `CudaPreparedScene` still expose the caller-owned source scene address.

## Task 2: Make Prepared Scenes Own RenderSceneIR

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`
- Modify: `src/backends/cpu/cpu_prepared_scene.cpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`
- Modify: `src/backends/cuda/cuda_prepared_scene.cpp`
- Modify: `include/yaoray/backends/cuda/cuda_backend.hpp`
- Modify: `src/backends/cuda/cuda_backend.cpp`
- Test: `tests/backend_tests.cpp`

- [ ] **Step 1: Change the public backend prepare signature**

In `include/yaoray/backends/backend.hpp`, replace:

```cpp
    virtual BackendPrepareResult Prepare(const RenderSceneIR& scene) = 0;
```

with:

```cpp
    virtual BackendPrepareResult Prepare(RenderSceneIR scene) = 0;
```

- [ ] **Step 2: Change CPU prepared scene storage**

In `include/yaoray/backends/cpu/cpu_prepared_scene.hpp`, replace the `CpuPreparedScene` definition and `PrepareCpuScene()` declaration with:

```cpp
struct CpuPreparedScene final : public PreparedScene {
    CpuPreparedScene(RenderSceneIR scene, RenderBvh prepared_bvh);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;
    const RenderSceneIR& Scene() const;

    RenderSceneIR render_scene;
    RenderBvh bvh;
};

struct CpuPrepareResult {
    bool ok = false;
    std::string error;
    std::optional<CpuPreparedScene> scene;
    double elapsed_seconds = 0.0;
};

CpuPrepareResult PrepareCpuScene(RenderSceneIR scene);
```

- [ ] **Step 3: Implement CPU ownership**

In `src/backends/cpu/cpu_prepared_scene.cpp`, replace the constructor, `SourceScene()`, `Scene()`, and `PrepareCpuScene()` implementations with:

```cpp
CpuPreparedScene::CpuPreparedScene(RenderSceneIR scene, RenderBvh prepared_bvh)
    : render_scene(std::move(scene)),
      bvh(std::move(prepared_bvh)) {
}

RenderBackendKind CpuPreparedScene::Kind() const {
    return RenderBackendKind::Cpu;
}

const RenderSceneIR& CpuPreparedScene::SourceScene() const {
    return render_scene;
}

const RenderSceneIR& CpuPreparedScene::Scene() const {
    return SourceScene();
}

CpuPrepareResult PrepareCpuScene(RenderSceneIR scene) {
    CpuPrepareResult result;

    const auto start = std::chrono::steady_clock::now();
    BvhBuildResult build = BuildBvh(scene.triangles);
    const auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();

    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }

    result.ok = true;
    result.scene.emplace(std::move(scene), std::move(build.bvh));
    return result;
}
```

Keep the existing includes; `src/backends/cpu/cpu_prepared_scene.cpp` already includes `<utility>`.

- [ ] **Step 4: Update the CPU backend header**

In `include/yaoray/backends/cpu/cpu_debug_backend.hpp`, replace:

```cpp
    BackendPrepareResult Prepare(const RenderSceneIR& scene) override;
```

with:

```cpp
    BackendPrepareResult Prepare(RenderSceneIR scene) override;
```

- [ ] **Step 5: Update CPU backend prepare move flow**

In `src/backends/cpu/cpu_debug_backend.cpp`, update `ToBackendPrepareResult()` so it moves the owned render scene and BVH into the heap-owned prepared scene:

```cpp
BackendPrepareResult ToBackendPrepareResult(CpuPrepareResult prepared) {
    BackendPrepareResult result;
    result.elapsed_seconds = prepared.elapsed_seconds;
    if (!prepared.ok || !prepared.scene.has_value()) {
        result.ok = false;
        result.error = prepared.error.empty() ? "failed to prepare CPU scene" : prepared.error;
        return result;
    }

    CpuPreparedScene& cpu_scene = prepared.scene.value();
    result.ok = true;
    result.scene = std::make_unique<CpuPreparedScene>(
        std::move(cpu_scene.render_scene),
        std::move(cpu_scene.bvh)
    );
    return result;
}
```

Then replace:

```cpp
BackendPrepareResult CpuDebugBackend::Prepare(const RenderSceneIR& scene) {
    return ToBackendPrepareResult(PrepareCpuScene(scene));
}
```

with:

```cpp
BackendPrepareResult CpuDebugBackend::Prepare(RenderSceneIR scene) {
    return ToBackendPrepareResult(PrepareCpuScene(std::move(scene)));
}
```

- [ ] **Step 6: Change CUDA prepared scene storage**

In `include/yaoray/backends/cuda/cuda_prepared_scene.hpp`, replace the class with:

```cpp
class CudaPreparedScene final : public PreparedScene {
public:
    explicit CudaPreparedScene(RenderSceneIR scene);

    RenderBackendKind Kind() const override;
    const RenderSceneIR& SourceScene() const override;

private:
    RenderSceneIR render_scene_;
};
```

- [ ] **Step 7: Implement CUDA prepared scene ownership**

In `src/backends/cuda/cuda_prepared_scene.cpp`, replace the constructor and `SourceScene()` with:

```cpp
CudaPreparedScene::CudaPreparedScene(RenderSceneIR scene)
    : render_scene_(std::move(scene)) {
}

RenderBackendKind CudaPreparedScene::Kind() const {
    return RenderBackendKind::Cuda;
}

const RenderSceneIR& CudaPreparedScene::SourceScene() const {
    return render_scene_;
}
```

Add this include if it is not present:

```cpp
#include <utility>
```

- [ ] **Step 8: Update the CUDA backend header and implementation signature**

In `include/yaoray/backends/cuda/cuda_backend.hpp`, replace:

```cpp
    BackendPrepareResult Prepare(const RenderSceneIR& scene) override;
```

with:

```cpp
    BackendPrepareResult Prepare(RenderSceneIR scene) override;
```

In `src/backends/cuda/cuda_backend.cpp`, replace:

```cpp
BackendPrepareResult CudaBackend::Prepare(const RenderSceneIR& scene) {
```

with:

```cpp
BackendPrepareResult CudaBackend::Prepare(RenderSceneIR scene) {
```

Keep `(void)scene;` in the function body.

- [ ] **Step 9: Run ownership tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all `yaoray_tests` pass.

- [ ] **Step 10: Run CLI regression tests**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all CTest tests pass.

- [ ] **Step 11: Commit prepared-scene ownership hardening**

Run:

```powershell
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_prepared_scene.hpp src/backends/cpu/cpu_prepared_scene.cpp include/yaoray/backends/cpu/cpu_debug_backend.hpp src/backends/cpu/cpu_debug_backend.cpp include/yaoray/backends/cuda/cuda_prepared_scene.hpp src/backends/cuda/cuda_prepared_scene.cpp include/yaoray/backends/cuda/cuda_backend.hpp src/backends/cuda/cuda_backend.cpp tests/backend_tests.cpp
git commit -m "refactor: make prepared scenes own render input"
```

Expected: commit succeeds and includes only ownership-related code and tests.

## Task 3: Write Backend Capability And Validation Tests

**Files:**
- Modify: `tests/backend_tests.cpp`

- [ ] **Step 1: Add CPU capability test**

Add this test after `YR_TEST(create_render_backend_returns_cpu_backend)`:

```cpp
YR_TEST(cpu_backend_capabilities_describe_current_support) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);

    const yr::RenderBackendCapabilities capabilities = backend->Capabilities();

    YR_EXPECT_EQ(capabilities.kind, yr::RenderBackendKind::Cpu);
    YR_EXPECT_TRUE(capabilities.runnable);
    YR_EXPECT_TRUE(capabilities.supports_debug_direct);
    YR_EXPECT_TRUE(capabilities.supports_path);
    YR_EXPECT_TRUE(capabilities.supports_offline_progress);
    YR_EXPECT_TRUE(capabilities.supports_texture_materials);
}
```

- [ ] **Step 2: Add CPU requested-backend rejection test**

Add this test after `YR_TEST(cpu_backend_prepare_builds_cpu_prepared_scene)`:

```cpp
YR_TEST(cpu_backend_prepare_rejects_scene_requested_for_cuda) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cpu);
    yr::RenderSceneIR scene = MakeBackendTriangleScene(4, 3);
    scene.requested_backend = yr::RenderBackendKind::Cuda;

    const yr::BackendPrepareResult prepared = backend->Prepare(scene);

    YR_EXPECT_TRUE(!prepared.ok);
    YR_EXPECT_TRUE(prepared.scene == nullptr);
    YR_EXPECT_TRUE(prepared.error.find("CPU backend cannot prepare a scene requested for cuda") != std::string::npos);
}
```

- [ ] **Step 3: Add CUDA capability test**

Add this test after `YR_TEST(create_render_backend_returns_cuda_stub_backend)`:

```cpp
YR_TEST(cuda_backend_capabilities_describe_controlled_stub) {
    const std::unique_ptr<yr::RenderBackend> backend = yr::CreateRenderBackend(yr::RenderBackendKind::Cuda);

    const yr::RenderBackendCapabilities capabilities = backend->Capabilities();

    YR_EXPECT_EQ(capabilities.kind, yr::RenderBackendKind::Cuda);
    YR_EXPECT_TRUE(!capabilities.runnable);
    YR_EXPECT_TRUE(!capabilities.supports_debug_direct);
    YR_EXPECT_TRUE(!capabilities.supports_path);
    YR_EXPECT_TRUE(!capabilities.supports_offline_progress);
    YR_EXPECT_TRUE(!capabilities.supports_texture_materials);
}
```

- [ ] **Step 4: Run tests and confirm expected compile failure**

Run:

```powershell
cmake --build build
```

Expected: build fails because `RenderBackendCapabilities` and `RenderBackend::Capabilities()` do not exist yet.

## Task 4: Implement Backend Capabilities

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_backend.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `include/yaoray/backends/cuda/cuda_backend.hpp`
- Modify: `src/backends/cuda/cuda_backend.cpp`
- Test: `tests/backend_tests.cpp`

- [ ] **Step 1: Add `RenderBackendCapabilities` to the public backend contract**

In `include/yaoray/backends/backend.hpp`, add this struct after `BackendPrepareResult` and before `class RenderBackend`:

```cpp
struct RenderBackendCapabilities {
    RenderBackendKind kind = RenderBackendKind::Cpu;
    bool runnable = false;
    bool supports_debug_direct = false;
    bool supports_path = false;
    bool supports_offline_progress = false;
    bool supports_texture_materials = false;
};
```

Then add this pure virtual method to `class RenderBackend` before `Prepare()`:

```cpp
    virtual RenderBackendCapabilities Capabilities() const = 0;
```

The resulting method group should be:

```cpp
    virtual RenderBackendKind Kind() const = 0;
    virtual RenderBackendCapabilities Capabilities() const = 0;
    virtual BackendPrepareResult Prepare(RenderSceneIR scene) = 0;
    virtual RenderResult Render(const PreparedScene& scene, const RenderRequest& request) = 0;
```

- [ ] **Step 2: Add CPU capabilities declaration**

In `include/yaoray/backends/cpu/cpu_debug_backend.hpp`, add:

```cpp
    RenderBackendCapabilities Capabilities() const override;
```

between `Kind()` and `Prepare()`.

- [ ] **Step 3: Implement CPU capabilities**

In `src/backends/cpu/cpu_debug_backend.cpp`, add this method after `CpuDebugBackend::Kind()`:

```cpp
RenderBackendCapabilities CpuDebugBackend::Capabilities() const {
    return RenderBackendCapabilities{
        RenderBackendKind::Cpu,
        true,
        true,
        true,
        true,
        true
    };
}
```

- [ ] **Step 4: Add CUDA capabilities declaration**

In `include/yaoray/backends/cuda/cuda_backend.hpp`, add:

```cpp
    RenderBackendCapabilities Capabilities() const override;
```

between `Kind()` and `Prepare()`.

- [ ] **Step 5: Implement CUDA capabilities**

In `src/backends/cuda/cuda_backend.cpp`, add this method after `CudaBackend::Kind()`:

```cpp
RenderBackendCapabilities CudaBackend::Capabilities() const {
    return RenderBackendCapabilities{
        RenderBackendKind::Cuda,
        false,
        false,
        false,
        false,
        false
    };
}
```

- [ ] **Step 6: Run capability tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: capability tests compile and pass, except the CPU requested-backend rejection test still fails until Task 5.

## Task 5: Validate Requested Backend During CPU Prepare

**Files:**
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Test: `tests/backend_tests.cpp`

- [ ] **Step 1: Add CPU requested-backend validation**

In `src/backends/cpu/cpu_debug_backend.cpp`, replace:

```cpp
BackendPrepareResult CpuDebugBackend::Prepare(RenderSceneIR scene) {
    return ToBackendPrepareResult(PrepareCpuScene(std::move(scene)));
}
```

with:

```cpp
BackendPrepareResult CpuDebugBackend::Prepare(RenderSceneIR scene) {
    if (scene.requested_backend != RenderBackendKind::Cpu) {
        BackendPrepareResult result;
        result.ok = false;
        result.error = "CPU backend cannot prepare a scene requested for " +
            std::string{RenderBackendName(scene.requested_backend)};
        return result;
    }

    return ToBackendPrepareResult(PrepareCpuScene(std::move(scene)));
}
```

Add this include near the other standard includes if it is not already present:

```cpp
#include <string>
```

- [ ] **Step 2: Run backend tests**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
```

Expected: all `yaoray_tests` pass.

- [ ] **Step 3: Run CLI regression tests**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all CTest tests pass. In particular, `yaoray_cli_render_cuda` still fails through CUDA's controlled preparation error, not the CPU validation path.

- [ ] **Step 4: Commit backend capability and validation contract**

Run:

```powershell
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_debug_backend.hpp src/backends/cpu/cpu_debug_backend.cpp include/yaoray/backends/cuda/cuda_backend.hpp src/backends/cuda/cuda_backend.cpp tests/backend_tests.cpp
git commit -m "refactor: add backend capabilities"
```

Expected: commit succeeds and includes only capability and requested-backend validation changes.

## Task 6: Update Architecture Documentation

**Files:**
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update the backend architecture paragraph**

In `docs/architecture/overview.md`, replace the second paragraph:

```markdown
The render layer compiles that semantic scene into backend-neutral input. The current `yaoray_render` slice provides a minimal `RenderSceneIR` with render settings, camera data, environment data, area lights, materials, textures, and flat world-space triangles. Rendering is dispatched through a two-stage backend interface: each backend first prepares backend-owned runtime data from `RenderSceneIR`, then renders from that prepared scene without app-layer knowledge of CPU BVHs, CUDA buffers, or future OptiX handles.
```

with:

```markdown
The render layer compiles that semantic scene into backend-neutral input. The current `yaoray_render` slice provides a minimal `RenderSceneIR` with render settings, camera data, environment data, area lights, materials, textures, and flat world-space triangles. Rendering is dispatched through a two-stage backend interface: each backend first prepares backend-owned runtime data from a `RenderSceneIR` value, then renders from that prepared scene without app-layer knowledge of CPU BVHs, CUDA buffers, or future OptiX handles. Prepared scenes own the render input they need, so rendering does not depend on caller-owned `RenderSceneIR` lifetime after preparation.
```

- [ ] **Step 2: Add backend capability note**

After the paragraph:

```markdown
The CPU backend prepares `CpuPreparedScene` from `RenderSceneIR` by building the CPU `RenderBvh`. The BVH is no longer part of shared compiler output, which keeps future CUDA or OptiX acceleration structures out of the render IR.
```

add:

```markdown
Backends expose a small capability record so tests and future app code can distinguish runnable CPU support from controlled backend stubs. CPU currently reports debug, path, offline progress, and texture-backed material support; CUDA is present as a named backend but does not yet report runnable rendering support.
```

- [ ] **Step 3: Run documentation-adjacent checks**

Run:

```powershell
rg -n "PreparedScene|Capabilities|backend-owned|caller-owned" docs\architecture\overview.md include\yaoray\backends src\backends tests\backend_tests.cpp
```

Expected: output shows the architecture note, capability API, implementation methods, and backend tests.

- [ ] **Step 4: Run full verification**

Run:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Expected: build succeeds, all unit tests pass, and all CTest tests pass.

- [ ] **Step 5: Commit documentation update**

Run:

```powershell
git add docs/architecture/overview.md
git commit -m "docs: document backend ownership contract"
```

Expected: commit succeeds and includes only architecture documentation.

## Final Verification

- [ ] **Step 1: Confirm no unrelated files are staged**

Run:

```powershell
git status --short --branch
```

Expected: the branch may still show pre-existing unrelated local changes such as Duck assets or unrelated docs, but no uncommitted code changes from this plan remain.

- [ ] **Step 2: Confirm recent commits are scoped**

Run:

```powershell
git log --oneline --decorate -5
```

Expected: the latest commits include:

```text
refactor: make prepared scenes own render input
refactor: add backend capabilities
docs: document backend ownership contract
```

- [ ] **Step 3: Record verification commands in the final response**

Report these commands and their pass/fail status:

```powershell
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

## Self-Review Notes

Spec coverage:

- Slice 1 ownership is covered by Tasks 1 and 2.
- Slice 2 capabilities and prepare/render contract behavior are covered by Tasks 3, 4, and 5.
- Architecture documentation is covered by Task 6.
- Render IR table migration, AssetResource decoupling, and CUDA packing are intentionally excluded from this plan and remain separate follow-up plans.

Type consistency:

- The plan consistently uses `RenderBackendCapabilities`, `RenderBackend::Capabilities()`, `RenderBackend::Prepare(RenderSceneIR scene)`, `CpuPreparedScene::render_scene`, and `CudaPreparedScene::render_scene_`.
- Existing public names `PreparedScene`, `BackendPrepareResult`, `RenderResult`, `RenderRequest`, `PrepareCpuScene()`, and `CreateRenderBackend()` remain in place.
