# YaoRay Configurable Area Light Samples Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `render.light_samples` so the CPU path tracer can trade more area-light shadow rays for lower direct-light and soft-shadow noise.

**Architecture:** Add the setting to the semantic render settings and compiled render scene, parse it as a positive integer with default `1`, and copy it through `CompileScene`. Use it only inside the CPU `path` integrator by averaging multiple random area-light samples per light per path hit; keep `debug_direct` unchanged.

**Tech Stack:** C++20, TOML scene parser with toml++, current in-repo `yr_test` harness, CMake/CTest, CPU path tracer, existing PNG examples.

---

## Scope

This plan implements the approved spec:

- `docs/superpowers/specs/2026-05-17-yaoray-configurable-area-light-samples-design.md`

It does not implement MIS, denoising, Russian roulette, adaptive sampling, stratified sampling, new materials, arbitrary area-light orientation, or CUDA rendering.

## File Structure

- Modify `include/yaoray/scene/scene.hpp`
  - Add `RenderSettings::light_samples = 1`.
- Modify `include/yaoray/render/render_scene.hpp`
  - Add `RenderScene::light_samples = 1`.
- Modify `src/scene/scene_parser.cpp`
  - Allow `[render] light_samples`.
  - Parse it as a positive integer.
- Modify `src/render/scene_compiler.cpp`
  - Copy `scene.render.light_samples` into `compiled.light_samples`.
- Modify `tests/scene_tests.cpp`
  - Add parser default/load/rejection tests.
- Modify `tests/render_scene_tests.cpp`
  - Add render-scene default and compiler propagation expectations.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Average multiple random area-light samples per light per path hit.
- Modify `tests/cpu_path_tracer_tests.cpp`
  - Add multiple-light-sample path tests and exercise thread determinism with `light_samples > 1`.
- Modify `scenes/examples/cornell_box_path.toml`
  - Add `light_samples = 4`.
- Modify `scenes/examples/cornell_box_path_threaded.toml`
  - Add `light_samples = 4`.
- Modify `README.md`
  - Document `render.light_samples`.
- Modify `docs/architecture/overview.md`
  - Document `render.light_samples` and the current non-goals.

## Task 1: Add Data Model, Parser, And Compiler Support

**Files:**
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `src/render/scene_compiler.cpp`

- [ ] **Step 1: Add failing parser tests**

In `tests/scene_tests.cpp`, add these tests after `scene_parser_allows_auto_render_threads`:

```cpp
YR_TEST(scene_parser_loads_render_light_samples) {
    const std::filesystem::path path = WriteTempScene(
        "render_light_samples.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
light_samples = 4
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
    YR_EXPECT_EQ(result.scene.value().render.light_samples, 4);
}

YR_TEST(scene_parser_rejects_zero_render_light_samples) {
    const std::filesystem::path path = WriteTempScene(
        "zero_render_light_samples.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
light_samples = 0
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.light_samples", "must be positive"));
}
```

Add these tests after `scene_parser_rejects_negative_render_threads`:

```cpp
YR_TEST(scene_parser_rejects_negative_render_light_samples) {
    const std::filesystem::path path = WriteTempScene(
        "negative_render_light_samples.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
light_samples = -1
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.light_samples", "must be positive"));
}
```

Add these tests after `scene_parser_rejects_string_render_threads`:

```cpp
YR_TEST(scene_parser_rejects_float_render_light_samples) {
    const std::filesystem::path path = WriteTempScene(
        "float_render_light_samples.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
light_samples = 1.5
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.light_samples", "must be an integer"));
}

YR_TEST(scene_parser_rejects_string_render_light_samples) {
    const std::filesystem::path path = WriteTempScene(
        "string_render_light_samples.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
light_samples = "many"
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.light_samples", "must be an integer"));
}
```

Also update `scene_defaults_match_schema` in `tests/scene_tests.cpp` to include:

```cpp
YR_EXPECT_EQ(scene.render.light_samples, 1);
```

- [ ] **Step 2: Add failing render-scene compiler tests**

In `tests/render_scene_tests.cpp`, update `MakeBaseScene()` by adding:

```cpp
scene.render.light_samples = 4;
```

after:

```cpp
scene.render.threads = 4;
```

Update `render_scene_defaults_are_backend_friendly` to include:

```cpp
YR_EXPECT_EQ(scene.light_samples, 1);
```

after:

```cpp
YR_EXPECT_EQ(scene.threads, 0);
```

Update `scene_compiler_copies_render_settings` to include:

```cpp
YR_EXPECT_EQ(compiled.light_samples, 4);
```

after:

```cpp
YR_EXPECT_EQ(compiled.threads, 4);
```

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build fails because `RenderSettings` and `RenderScene` do not yet have a `light_samples` member.
- If the build unexpectedly succeeds, `scene_parser_loads_render_light_samples` fails with an unknown-field diagnostic for `render.light_samples`.

- [ ] **Step 4: Add `light_samples` to semantic render settings**

In `include/yaoray/scene/scene.hpp`, replace `struct RenderSettings` with:

```cpp
struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
};
```

- [ ] **Step 5: Add `light_samples` to compiled render scene**

In `include/yaoray/render/render_scene.hpp`, replace `struct RenderScene` with:

```cpp
struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
    RenderBvh bvh;
};
```

- [ ] **Step 6: Parse `render.light_samples`**

In `src/scene/scene_parser.cpp`, update the allowed `[render]` fields in `ParseRender` from:

```cpp
{"backend", "integrator", "width", "height", "spp", "max_depth", "seed", "threads"},
```

to:

```cpp
{"backend", "integrator", "width", "height", "spp", "max_depth", "seed", "threads", "light_samples"},
```

After the existing `threads` parsing block, add:

```cpp
if (const auto light_samples = ReadInt(table, "light_samples", file, "render.light_samples", diagnostics)) {
    scene.render.light_samples = *light_samples;
}
```

After the existing `max_depth` positive check:

```cpp
if (scene.render.max_depth <= 0) {
    diagnostics.push_back(Error(file, "render.max_depth", "must be positive"));
}
```

add:

```cpp
if (scene.render.light_samples <= 0) {
    diagnostics.push_back(Error(file, "render.light_samples", "must be positive"));
}
```

- [ ] **Step 7: Copy `light_samples` through scene compilation**

In `src/render/scene_compiler.cpp`, after:

```cpp
compiled.threads = scene.render.threads;
```

add:

```cpp
compiled.light_samples = scene.render.light_samples;
```

- [ ] **Step 8: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R "yaoray_tests"
```

Expected:

- `yaoray_tests` passes.
- New parser tests pass for default, valid, zero, negative, float, and string `light_samples`.
- New compiler tests pass for default and propagation.

- [ ] **Step 9: Commit data model, parser, compiler, and tests**

Run:

```powershell
git add include\yaoray\scene\scene.hpp include\yaoray\render\render_scene.hpp src\scene\scene_parser.cpp src\render\scene_compiler.cpp tests\scene_tests.cpp tests\render_scene_tests.cpp
git commit -m "feat: parse configurable area light samples"
```

## Task 2: Use `light_samples` In The CPU Path Integrator

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add failing path tracer tests**

In `tests/cpu_path_tracer_tests.cpp`, add this test before `cpu_path_tracer_reports_single_thread_when_requested`:

```cpp
YR_TEST(cpu_path_tracer_light_samples_increase_shadow_rays) {
    yr::RenderScene one_sample = MakeDiffuseFloorScene(7);
    one_sample.light_samples = 1;

    yr::RenderScene four_samples = one_sample;
    four_samples.light_samples = 4;

    const yr::CpuPathTraceResult one_result = yr::RenderCpuPathTrace(one_sample);
    const yr::CpuPathTraceResult four_result = yr::RenderCpuPathTrace(four_samples);

    YR_EXPECT_TRUE(one_result.stats.shadow_rays > 0);
    YR_EXPECT_EQ(four_result.stats.shadow_rays, one_result.stats.shadow_rays * std::uint64_t{4});
    YR_EXPECT_EQ(four_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_is_deterministic_with_multiple_light_samples) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}
```

Update `MakeThreadedDeterminismScene()` in `tests/cpu_path_tracer_tests.cpp` by adding:

```cpp
scene.light_samples = 4;
```

after:

```cpp
scene.threads = 0;
```

If there is no explicit `scene.threads = 0;` assignment in the helper, add `scene.light_samples = 4;` after:

```cpp
scene.seed = 99;
```

- [ ] **Step 2: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build succeeds.
- `cpu_path_tracer_light_samples_increase_shadow_rays` fails because the current path integrator ignores `scene.light_samples` and uses one light sample per area light.
- The determinism test may pass before implementation; that is acceptable because the shadow-ray scaling test is the required red test.

- [ ] **Step 3: Add a helper for resolved light sample count**

In `src/backends/cpu/cpu_path_tracer.cpp`, after `EvaluateDiffuseBrdf`, add:

```cpp
int DirectLightSampleCount(const RenderScene& scene) {
    return std::max(1, scene.light_samples);
}
```

- [ ] **Step 4: Replace `EstimateDirectLight` with a multi-sample version**

Replace the whole existing `EstimateDirectLight` function in `src/backends/cpu/cpu_path_tracer.cpp` with:

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
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);

    for (const RenderAreaLight& light : scene.area_lights) {
        Color3f light_radiance;
        for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
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
            light_radiance = light_radiance + Multiply(brdf, sample->radiance) * weight;
        }

        radiance = radiance + light_radiance * inverse_light_sample_count;
    }

    return radiance;
}
```

- [ ] **Step 5: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- `cpu_path_tracer_light_samples_increase_shadow_rays` proves `light_samples = 4` emits four times the valid direct-light shadow rays of `light_samples = 1` in the controlled floor scene.
- `cpu_path_tracer_is_bitwise_identical_across_thread_counts` still passes with `light_samples = 4`.

- [ ] **Step 6: Commit path integrator implementation and tests**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: average multiple area light samples"
```

## Task 3: Update Cornell Examples And Documentation

**Files:**
- Modify: `scenes/examples/cornell_box_path.toml`
- Modify: `scenes/examples/cornell_box_path_threaded.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add `light_samples = 4` to Cornell path examples**

In both `scenes/examples/cornell_box_path.toml` and `scenes/examples/cornell_box_path_threaded.toml`, add:

```toml
light_samples = 4
```

inside the `[render]` table, immediately after:

```toml
threads = 0
```

If `scenes/examples/cornell_box_path.toml` does not already have `threads = 0`, add `light_samples = 4` immediately after:

```toml
seed = 12345
```

- [ ] **Step 2: Update README render paragraph**

In `README.md`, replace the final render paragraph with:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the CPU path tracer with diffuse bounce, deterministic sampling, random area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, explicit emissive hits, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). `render.light_samples` controls how many random direct area-light samples the path integrator averages per light per hit; the default is `1`, and higher values trade more shadow rays for lower soft-shadow and direct-light noise. The CLI reports actual threads plus samples/sec and rays/sec. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render. The path integrator still does not implement MIS, Russian roulette, denoising, adaptive sampling, stratified sampling, arbitrary oriented area lights, or advanced material models.
```

- [ ] **Step 3: Update architecture overview CPU backend paragraph**

In `docs/architecture/overview.md`, replace the CPU backend paragraph with:

```markdown
The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output; it remains single-threaded for reference debugging and ignores `render.light_samples`. `path` is the CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, random XZ-rectangle area-light surface sampling, configurable direct area-light sample averaging through `render.light_samples`, diffuse BRDF/PDF-weighted direct lighting, and row-major tile threading controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). It is still a v0 integrator without MIS, Russian roulette, denoising, adaptive sampling, stratified sampling, spectral rendering, random environment sampling, arbitrary oriented area lights, or final-quality material models.
```

- [ ] **Step 4: Run docs and scene checks**

Run:

```powershell
rg -n "light_samples|MIS|denoising|Russian roulette|stratified|debug_direct|render.threads" README.md docs\architecture\overview.md scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml
```

Expected:

- README mentions `render.light_samples`.
- Architecture overview says `debug_direct` ignores `render.light_samples`.
- Both Cornell path examples include `light_samples = 4`.
- Limitations mention no MIS, no denoising, no Russian roulette, and no stratified sampling.

- [ ] **Step 5: Commit examples and docs**

Run:

```powershell
git add scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml README.md docs\architecture\overview.md
git commit -m "docs: document configurable area light samples"
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
Shadow rays:
Rendered image: scenes/examples/out/cornell_box_path_threaded.png
```

The render should succeed. Because `light_samples = 4`, shadow-ray work should be higher than the prior one-light-sample Cornell path render at the same resolution, spp, and max depth.

- [ ] **Step 4: Check implementation scope**

Run:

```powershell
rg -n "light_samples|DirectLightSampleCount|EstimateDirectLight|MIS|denoising|Russian roulette|stratified|adaptive|debug_direct|RenderScene|RenderSettings" include src tests README.md docs\architecture\overview.md scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml docs\superpowers\specs\2026-05-17-yaoray-configurable-area-light-samples-design.md docs\superpowers\plans\2026-05-17-yaoray-configurable-area-light-samples-implementation-plan.md
```

Expected:

- `light_samples` appears in scene settings, render scene, parser, compiler, path tracer, tests, examples, and docs.
- `DirectLightSampleCount` appears only in `src/backends/cpu/cpu_path_tracer.cpp`.
- `debug_direct` appears in docs/tests but the debug renderer implementation does not consume `light_samples`.
- MIS, denoising, Russian roulette, stratified sampling, and adaptive sampling appear only as non-goals or limitations.

- [ ] **Step 5: Confirm clean git state**

Run:

```powershell
git status --short --branch
git log --oneline --decorate -6
```

Expected:

- Working tree is clean.
- Recent commits include:
  - `feat: parse configurable area light samples`
  - `feat: average multiple area light samples`
  - `docs: document configurable area light samples`

## Self-Review Checklist

- Spec coverage:
  - Scene-authored `render.light_samples`: Task 1.
  - Default `1`: Task 1 tests and data model.
  - Positive integer parser validation: Task 1 parser tests and implementation.
  - Compiler propagation: Task 1.
  - Path-only multi-sample direct lighting: Task 2.
  - `debug_direct` unchanged: Task 3 docs and Task 4 scope check.
  - Determinism and thread equality: Task 2 path tests and Task 4 full tests.
  - Cornell preview update: Task 3.
  - Documentation: Task 3.
  - No MIS/denoising/Russian roulette/adaptive/stratified work: Task 3 docs and Task 4 scope check.
- Placeholder scan:
  - Every task names concrete tests, validation rules, and code snippets.
- Type consistency:
  - `RenderSettings::light_samples`, `RenderScene::light_samples`, `DirectLightSampleCount`, and existing `EstimateDirectLight` names are used consistently.
  - `light_samples` remains an `int` in both semantic and compiled render data.
