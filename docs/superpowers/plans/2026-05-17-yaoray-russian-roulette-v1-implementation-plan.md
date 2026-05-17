# YaoRay Russian Roulette v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add fixed-policy Russian roulette path termination to the CPU path tracer while keeping `render.max_depth` as the hard depth limit.

**Architecture:** Keep the implementation inside `src/backends/cpu/cpu_path_tracer.cpp` because v1 is a CPU path-loop policy, not a render-level API. Use the existing `CpuSampler::Next1D()` for the roulette decision and keep all probabilities internal constants. Add tests in `tests/cpu_path_tracer_tests.cpp` using a deterministic mirror-hall scene that would otherwise trace to `max_depth` for every sample.

**Tech Stack:** C++20, existing `yr_test` harness, CMake/CTest, CPU path tracer, existing `RenderScene`, `CpuSampler`, and backend stats.

---

## Scope

This plan implements:

- `docs/superpowers/specs/2026-05-17-yaoray-russian-roulette-v1-design.md`

It does not implement scene schema changes, CLI flags, new render stats, sampler refactoring, environment MIS, material changes, CUDA work, or a full Integrator API.

## File Structure

- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Add fixed Russian roulette constants.
  - Add a small max-component helper for throughput.
  - Add a helper that either terminates or compensates the current throughput.
  - Call that helper after BSDF throughput update and before spawning the next ray.
- Modify `tests/cpu_path_tracer_tests.cpp`
  - Add a deterministic mirror-hall helper scene.
  - Add tests proving low depth is unchanged, high depth terminates early, and fixed seeds remain deterministic.
- Modify `README.md`
  - Document fixed-policy Russian roulette in the CPU path tracer feature list.
  - Keep user-configurable roulette and environment MIS as future work.
- Modify `docs/architecture/overview.md`
  - Document the fixed CPU path tracer policy and preserve future Integrator API boundaries.

No CMake changes are required.

## Task 1: Add CPU Path Tracer Russian Roulette Tests

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add mirror-hall helper scene**

In `tests/cpu_path_tracer_tests.cpp`, add this helper after `MakeMirrorToExplicitEmitterScene()` and before `MakeIndirectBounceScene()`:

```cpp
yr::RenderScene MakeMirrorHallScene(int max_depth, std::uint64_t seed = 101) {
    yr::RenderScene scene;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 128;
    scene.max_depth = max_depth;
    scene.seed = seed;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, 1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 0.01f;
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{};
    scene.environment.strength = 1.0f;
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Mirror,
        yr::Color3f{0.2f, 0.2f, 0.2f},
        yr::Color3f{}
    });

    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, 10.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, 1.0f},
        yr::Point3f{10.0f, 10.0f, 1.0f},
        yr::Point3f{-10.0f, 10.0f, 1.0f},
        yr::Vec3f{0.0f, 0.0f, -1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, 10.0f, -1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-10.0f, -10.0f, -1.0f},
        yr::Point3f{10.0f, 10.0f, -1.0f},
        yr::Point3f{-10.0f, 10.0f, -1.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });

    RebuildBvh(scene);
    return scene;
}
```

- [ ] **Step 2: Add failing and regression tests**

Add these tests after `cpu_path_tracer_delta_bsdf_sampled_emissive_hits_keep_full_weight`:

```cpp
YR_TEST(cpu_path_tracer_russian_roulette_does_not_start_before_fixed_depth) {
    const yr::RenderScene scene = MakeMirrorHallScene(4);
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, static_cast<std::uint64_t>(scene.spp * scene.max_depth));
    YR_EXPECT_EQ(result.stats.hits, result.stats.rays_traced);
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_russian_roulette_terminates_low_throughput_high_depth_paths) {
    const yr::RenderScene scene = MakeMirrorHallScene(12);
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const std::uint64_t no_roulette_rays = static_cast<std::uint64_t>(scene.spp * scene.max_depth);
    const std::uint64_t minimum_pre_roulette_rays = static_cast<std::uint64_t>(scene.spp * 4);

    YR_EXPECT_TRUE(result.stats.rays_traced >= minimum_pre_roulette_rays);
    YR_EXPECT_TRUE(result.stats.rays_traced < no_roulette_rays);
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_russian_roulette_is_deterministic_for_same_seed) {
    const yr::RenderScene scene = MakeMirrorHallScene(12, 202);

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}
```

- [ ] **Step 3: Build tests and confirm the new high-depth test is red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- `cpu_path_tracer_russian_roulette_terminates_low_throughput_high_depth_paths` fails because current code traces exactly `scene.spp * scene.max_depth` rays in the mirror hall.
- The failure should be the assertion:

```text
expectation failed: result.stats.rays_traced < no_roulette_rays
```

Do not change the implementation until this red failure is observed.

## Task 2: Implement Fixed-Policy Russian Roulette

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Test: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add Russian roulette constants**

In `src/backends/cpu/cpu_path_tracer.cpp`, add these constants after the existing shadow-bias constants:

```cpp
constexpr int RussianRouletteStartDepth = 3;
constexpr float RussianRouletteMinSurvival = 0.05f;
constexpr float RussianRouletteMaxSurvival = 0.95f;
```

The block should look like:

```cpp
constexpr float MinShadowBias = 1.0e-4f;
constexpr float ShadowBiasScale = 1.0e-5f;
constexpr float MaxShadowBiasDistanceFraction = 1.0e-2f;
constexpr int RussianRouletteStartDepth = 3;
constexpr float RussianRouletteMinSurvival = 0.05f;
constexpr float RussianRouletteMaxSurvival = 0.95f;
```

- [ ] **Step 2: Add throughput max-component helper**

In the anonymous namespace, add this helper after `IsNearBlack()`:

```cpp
float MaxComponent(Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
}
```

The nearby helpers should read:

```cpp
bool IsNearBlack(Color3f color) {
    return color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f;
}

float MaxComponent(Color3f color) {
    return std::max(color.x, std::max(color.y, color.z));
}

float MaxAbsComponent(Point3f point) {
    return std::max(std::fabs(point.x), std::max(std::fabs(point.y), std::fabs(point.z)));
}
```

- [ ] **Step 3: Add Russian roulette decision helper**

In the anonymous namespace, add this helper after `DirectLightSampleCount()`:

```cpp
bool SurviveRussianRoulette(int depth, Color3f& throughput, CpuSampler& sampler) {
    if (depth < RussianRouletteStartDepth) {
        return true;
    }

    const float survival = std::clamp(
        MaxComponent(throughput),
        RussianRouletteMinSurvival,
        RussianRouletteMaxSurvival
    );
    if (sampler.Next1D() >= survival) {
        return false;
    }

    throughput = throughput / survival;
    return true;
}
```

This function mutates `throughput` only for surviving paths. It consumes no random number before `RussianRouletteStartDepth`.

- [ ] **Step 4: Apply roulette after BSDF throughput update**

In `TracePath()`, replace this block:

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

with:

```cpp
throughput = Multiply(throughput, bsdf_sample.weight);
if (!SurviveRussianRoulette(depth, throughput, sampler)) {
    break;
}

previous_bounce = PreviousBounce{
    true,
    bsdf_sample.specular,
    hit_point,
    bsdf_sample.pdf,
    DirectLightSampleCount(scene)
};
ray = Ray3f{hit_point + normal * SurfaceBias(hit_point), bsdf_sample.wi};
```

This preserves the `max_depth` check above BSDF sampling. With `max_depth == 4`, the loop breaks at `depth == 3` before any roulette decision can run.

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `cpu_path_tracer_russian_roulette_does_not_start_before_fixed_depth` passes.
- `cpu_path_tracer_russian_roulette_terminates_low_throughput_high_depth_paths` passes.
- `cpu_path_tracer_russian_roulette_is_deterministic_for_same_seed` passes.
- Existing CPU path tracer tests still pass.

- [ ] **Step 6: Commit implementation**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: add russian roulette path termination"
```

## Task 3: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README path tracer paragraph**

In `README.md`, find the long paragraph beginning:

```markdown
The `render` command currently parses, compiles, builds a BVH,
```

Replace this phrase:

```markdown
MIS-weighted BSDF-sampled emissive hits, and tile-threaded CPU execution
```

with:

```markdown
MIS-weighted BSDF-sampled emissive hits, fixed-policy Russian roulette path termination with `render.max_depth` as a hard limit, and tile-threaded CPU execution
```

In the same paragraph, replace the limitations sentence:

```markdown
The path integrator still does not implement Russian roulette, environment MIS, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, glass refraction, textures, imported materials, CUDA materials, or other advanced material models.
```

with:

```markdown
The path integrator still does not implement user-configurable roulette parameters, environment MIS, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, glass refraction, textures, imported materials, CUDA materials, or other advanced material models.
```

- [ ] **Step 2: Update architecture overview path tracer paragraph**

In `docs/architecture/overview.md`, find the CPU backend paragraph beginning:

```markdown
The CPU backend supports two integrators.
```

Replace this phrase:

```markdown
direct-light MIS over explicit area-light samples and BSDF-sampled emissive hits, and row-major tile threading
```

with:

```markdown
direct-light MIS over explicit area-light samples and BSDF-sampled emissive hits, fixed-policy Russian roulette path termination with `render.max_depth` as a hard limit, and row-major tile threading
```

In the same paragraph, replace:

```markdown
It is still a v0 integrator without Russian roulette, denoising, adaptive sampling, Sobol/CMJ/blue-noise sampling, spectral rendering, environment MIS, arbitrary oriented area lights, or final-quality material models.
```

with:

```markdown
It is still a v0 integrator without user-configurable roulette parameters, denoising, adaptive sampling, Sobol/CMJ/blue-noise sampling, spectral rendering, environment MIS, arbitrary oriented area lights, or final-quality material models.
```

- [ ] **Step 3: Update future-work paragraph**

In `docs/architecture/overview.md`, replace the final paragraph:

```markdown
Spectral rendering, texture import, imported material files, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, more complete CPU path tracing, real CUDA rendering, and final-quality image output will be added in focused implementation plans. The full Integrator API refactor should wait until MIS, Russian roulette, environment sampling, or CUDA path tracing make the current path loop too crowded.
```

with:

```markdown
Spectral rendering, texture import, imported material files, soft shadows, glTF/GLB import, advanced BVH split methods, HDR output, more complete CPU path tracing, real CUDA rendering, and final-quality image output will be added in focused implementation plans. The full Integrator API refactor should wait until configurable integrator settings, environment sampling, or CUDA path tracing make the current path loop too crowded.
```

- [ ] **Step 4: Run docs scope check**

Run:

```powershell
rg -n "Russian roulette|user-configurable roulette|max_depth|environment MIS|CUDA path|Integrator API" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-russian-roulette-v1-design.md
```

Expected:

- README mentions fixed-policy Russian roulette.
- Architecture overview mentions fixed-policy Russian roulette.
- Future work mentions user-configurable roulette or configurable integrator settings.
- Environment MIS, CUDA path tracing, and Integrator API remain future-work concepts only.

- [ ] **Step 5: Commit docs**

Run:

```powershell
git add README.md docs\architecture\overview.md
git commit -m "docs: document russian roulette path termination"
```

## Task 4: Full Verification And Manual Render Checks

**Files:**
- No source changes expected.

- [ ] **Step 1: Run full Debug configure, build, and tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

- Configure succeeds.
- Debug build succeeds.
- All CTest tests pass.

- [ ] **Step 2: Run Release build**

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

- [ ] **Step 3: Render manual scenes**

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

- [ ] **Step 4: Run final scope checks**

Run:

```powershell
rg -n "RussianRoulette|SurviveRussianRoulette|Next1D|rr_|user-configurable roulette|environment MIS|CUDA path|Integrator API" src tests README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-17-yaoray-russian-roulette-v1-design.md
git status --short --branch
git log --oneline --decorate -10
```

Expected:

- `RussianRoulette*` and `SurviveRussianRoulette` appear only in `src/backends/cpu/cpu_path_tracer.cpp` and CPU path tracer tests.
- No `rr_*` scene parser fields exist.
- User-configurable roulette, environment MIS, CUDA path tracing, and Integrator API appear only in docs/spec future-work text.
- Working tree is clean except ignored generated outputs.
- Recent commits include:
  - `docs: design russian roulette v1`
  - `docs: plan russian roulette v1`
  - `feat: add russian roulette path termination`
  - `docs: document russian roulette path termination`

## Self-Review Checklist

- Spec coverage:
  - CPU path tracer roulette termination: Task 2.
  - `render.max_depth` remains hard bound: Task 2 placement and Task 1 low-depth test.
  - Fixed start depth: Task 2 constants and Task 1 low-depth test.
  - Throughput-based survival: Task 2 helper.
  - Throughput compensation: Task 2 helper.
  - Determinism: Task 1 deterministic test and existing thread tests.
  - No schema changes: no parser or scene model files in task file list.
  - Documentation: Task 3.
  - Manual renders: Task 4.
- Type consistency:
  - `Color3f` is used for throughput and supports scalar division.
  - `CpuSampler::Next1D()` already exists and returns `float`.
  - `scene.spp` and `scene.max_depth` are `int`; tests cast products to `std::uint64_t` for stat comparisons.
- Scope boundaries:
  - No new CLI output.
  - No new render stats.
  - No new `RenderScene` fields.
  - No CUDA implementation.
