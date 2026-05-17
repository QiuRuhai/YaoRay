# YaoRay Sampler v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `render.sampler = "independent" | "stratified"` and route CPU path tracer pixel and area-light sampling through a deterministic sampler abstraction.

**Architecture:** Add a semantic and compiled sampler enum, parse it from TOML with default `independent`, and propagate it through `CompileScene`. Create a small CPU sampler module that owns the existing xorshift RNG and stratified 2D placement, then update the CPU path tracer to request pixel jitter and area-light UV samples from that sampler while keeping diffuse bounce sampling independent in this slice.

**Tech Stack:** C++20, CMake/CTest, current in-repo `yr_test` harness, TOML parser with toml++, CPU path tracer, existing Cornell Box examples and PNG output.

---

## Scope

This plan implements the approved spec:

- `docs/superpowers/specs/2026-05-17-yaoray-sampler-v1-design.md`

It does not implement Sobol, Halton, Hammersley, CMJ, blue noise, MIS, BSDF abstraction, CUDA sampling, adaptive sampling, denoising, Russian roulette, lens sampling, time sampling, or image regression.

## File Structure

- Modify `include/yaoray/core/vec.hpp`
  - Add a minimal `Vec2f` type for sampler outputs.
- Modify `include/yaoray/scene/scene.hpp`
  - Add `RenderSamplerKind`.
  - Add `RenderSettings::sampler = RenderSamplerKind::Independent`.
  - Declare sampler name/parse helpers.
- Modify `src/scene/scene.cpp`
  - Implement `RenderSamplerName` and `ParseRenderSamplerName`.
- Modify `src/scene/scene_parser.cpp`
  - Allow and parse `[render] sampler`.
- Modify `include/yaoray/render/render_scene.hpp`
  - Add `RenderScene::sampler = RenderSamplerKind::Independent`.
- Modify `src/render/scene_compiler.cpp`
  - Copy `scene.render.sampler` into `compiled.sampler`.
- Modify `tests/scene_tests.cpp`
  - Add enum, default, parser success, and parser rejection tests.
- Modify `tests/render_scene_tests.cpp`
  - Add render-scene default and compiler propagation expectations.
- Create `include/yaoray/backends/cpu/cpu_sampler.hpp`
  - Declare `CpuSampler` and pixel/sample seeding helpers.
- Create `src/backends/cpu/cpu_sampler.cpp`
  - Implement independent and stratified sampler behavior.
- Create `tests/cpu_sampler_tests.cpp`
  - Test independent and stratified sampler behavior without rendering.
- Modify `CMakeLists.txt`
  - Add the new sampler source file and test file.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`
  - Replace direct RNG sampling sites with `CpuSampler`.
- Modify `tests/cpu_path_tracer_tests.cpp`
  - Add stratified sampler path tests and exercise thread determinism with `stratified`.
- Modify `scenes/examples/cornell_box_path.toml`
  - Add `sampler = "stratified"`.
- Modify `scenes/examples/cornell_box_path_threaded.toml`
  - Add `sampler = "stratified"`.
- Modify `README.md`
  - Document `render.sampler`.
- Modify `docs/architecture/overview.md`
  - Document the sampler modes and current limitations.

## Task 1: Add Data Model, Parser, And Compiler Support

**Files:**
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene.cpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`

- [ ] **Step 1: Add failing scene enum and parser tests**

In `tests/scene_tests.cpp`, update `scene_defaults_match_schema` by adding this expectation after the integrator expectation:

```cpp
YR_EXPECT_EQ(scene.render.sampler, yr::RenderSamplerKind::Independent);
```

Update `scene_enum_names_are_stable` by adding:

```cpp
YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::Independent), std::string_view{"independent"});
YR_EXPECT_EQ(yr::RenderSamplerName(yr::RenderSamplerKind::Stratified), std::string_view{"stratified"});
```

Update `scene_enum_parsers_accept_stable_names` by adding:

```cpp
YR_EXPECT_EQ(yr::ParseRenderSamplerName("independent").value(), yr::RenderSamplerKind::Independent);
YR_EXPECT_EQ(yr::ParseRenderSamplerName("stratified").value(), yr::RenderSamplerKind::Stratified);
```

Update `scene_enum_parsers_reject_unknown_names` by adding:

```cpp
YR_EXPECT_TRUE(!yr::ParseRenderSamplerName("sobol").has_value());
```

Update `scene_enum_names_return_unknown_for_invalid_values` by adding:

```cpp
YR_EXPECT_EQ(yr::RenderSamplerName(static_cast<yr::RenderSamplerKind>(999)), std::string_view{"unknown"});
```

Add these tests after `scene_parser_loads_render_integrator`:

```cpp
YR_TEST(scene_parser_loads_independent_render_sampler) {
    const std::filesystem::path path = WriteTempScene(
        "independent_sampler.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
sampler = "independent"
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
    YR_EXPECT_EQ(result.scene.value().render.sampler, yr::RenderSamplerKind::Independent);
}

YR_TEST(scene_parser_loads_stratified_render_sampler) {
    const std::filesystem::path path = WriteTempScene(
        "stratified_sampler.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
sampler = "stratified"
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
    YR_EXPECT_EQ(result.scene.value().render.sampler, yr::RenderSamplerKind::Stratified);
}
```

Add these tests near the existing invalid render integrator tests:

```cpp
YR_TEST(scene_parser_rejects_unknown_render_sampler) {
    const std::filesystem::path path = WriteTempScene(
        "bad_sampler.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
sampler = "sobol"
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.sampler", "unknown sampler"));
}

YR_TEST(scene_parser_rejects_non_string_render_sampler) {
    const std::filesystem::path path = WriteTempScene(
        "bad_sampler_type.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
sampler = 123
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.sampler", "must be a string"));
}
```

- [ ] **Step 2: Add failing render scene compiler tests**

In `tests/render_scene_tests.cpp`, update `MakeBaseScene()` by adding:

```cpp
scene.render.sampler = yr::RenderSamplerKind::Stratified;
```

after:

```cpp
scene.render.integrator = yr::RenderIntegratorKind::Path;
```

Update `render_scene_defaults_are_backend_friendly` by adding:

```cpp
YR_EXPECT_EQ(scene.sampler, yr::RenderSamplerKind::Independent);
```

after the integrator expectation.

Update `scene_compiler_copies_render_settings` by adding:

```cpp
YR_EXPECT_EQ(compiled.sampler, yr::RenderSamplerKind::Stratified);
```

after the integrator expectation.

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build fails because `RenderSamplerKind`, `RenderSamplerName`, `ParseRenderSamplerName`, `RenderSettings::sampler`, and `RenderScene::sampler` do not exist yet.
- This is the expected red state for parser and compiler propagation.

- [ ] **Step 4: Add sampler enum and helpers to the semantic scene API**

In `include/yaoray/scene/scene.hpp`, add this enum after `RenderIntegratorKind`:

```cpp
enum class RenderSamplerKind {
    Independent,
    Stratified,
};
```

In `RenderSettings`, add `sampler` after `integrator`:

```cpp
struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
};
```

Add these declarations after the render integrator parse helper declarations:

```cpp
std::string_view RenderSamplerName(RenderSamplerKind sampler);
std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name);
```

In `src/scene/scene.cpp`, add these functions after `ParseRenderIntegratorName`:

```cpp
std::string_view RenderSamplerName(RenderSamplerKind sampler) {
    switch (sampler) {
        case RenderSamplerKind::Independent:
            return "independent";
        case RenderSamplerKind::Stratified:
            return "stratified";
    }
    return "unknown";
}

std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name) {
    if (name == "independent") {
        return RenderSamplerKind::Independent;
    }
    if (name == "stratified") {
        return RenderSamplerKind::Stratified;
    }
    return std::nullopt;
}
```

- [ ] **Step 5: Parse `render.sampler`**

In `src/scene/scene_parser.cpp`, update the allowed `[render]` fields in `ParseRender` from:

```cpp
{"backend", "integrator", "width", "height", "spp", "max_depth", "seed", "threads", "light_samples"},
```

to:

```cpp
{"backend", "integrator", "sampler", "width", "height", "spp", "max_depth", "seed", "threads", "light_samples"},
```

After the existing integrator parsing block:

```cpp
if (const auto integrator = ReadString(table, "integrator", file, "render.integrator", diagnostics)) {
    if (const auto parsed = ParseRenderIntegratorName(*integrator)) {
        scene.render.integrator = *parsed;
    } else {
        diagnostics.push_back(Error(file, "render.integrator", "unknown integrator"));
    }
}
```

add:

```cpp
if (const auto sampler = ReadString(table, "sampler", file, "render.sampler", diagnostics)) {
    if (const auto parsed = ParseRenderSamplerName(*sampler)) {
        scene.render.sampler = *parsed;
    } else {
        diagnostics.push_back(Error(file, "render.sampler", "unknown sampler"));
    }
}
```

- [ ] **Step 6: Add sampler to compiled render scene and compiler**

In `include/yaoray/render/render_scene.hpp`, add `sampler` after `integrator`:

```cpp
struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
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

In `src/render/scene_compiler.cpp`, add:

```cpp
compiled.sampler = scene.render.sampler;
```

after:

```cpp
compiled.integrator = scene.render.integrator;
```

- [ ] **Step 7: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Parser tests pass for default, `"independent"`, `"stratified"`, unknown sampler names, and non-string sampler values.
- Render-scene tests pass for default and compiler propagation.

- [ ] **Step 8: Commit parser and data model support**

Run:

```powershell
git add include\yaoray\scene\scene.hpp src\scene\scene.cpp src\scene\scene_parser.cpp include\yaoray\render\render_scene.hpp src\render\scene_compiler.cpp tests\scene_tests.cpp tests\render_scene_tests.cpp
git commit -m "feat: parse render sampler mode"
```

## Task 2: Add The CPU Sampler Module

**Files:**
- Modify: `include/yaoray/core/vec.hpp`
- Create: `include/yaoray/backends/cpu/cpu_sampler.hpp`
- Create: `src/backends/cpu/cpu_sampler.cpp`
- Create: `tests/cpu_sampler_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing sampler tests**

Create `tests/cpu_sampler_tests.cpp` with:

```cpp
#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/backends/cpu/cpu_sampler.hpp>
#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace {

bool InRange(float value, float low, float high) {
    return value >= low && value < high;
}

} // namespace

YR_TEST(cpu_sampler_independent_uses_center_pixel_for_one_spp) {
    yr::CpuSampler sampler{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{123},
        0,
        1,
        1
    };

    const yr::Vec2f pixel = sampler.NextPixel2D();

    YR_EXPECT_NEAR(pixel.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 0.5, 1e-6);
}

YR_TEST(cpu_sampler_independent_is_deterministic) {
    yr::CpuSampler first{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{456},
        3,
        8,
        4
    };
    yr::CpuSampler second{
        yr::RenderSamplerKind::Independent,
        std::uint64_t{456},
        3,
        8,
        4
    };

    const yr::Vec2f first_pixel = first.NextPixel2D();
    const yr::Vec2f second_pixel = second.NextPixel2D();
    const yr::Vec2f first_light = first.NextLight2D(2);
    const yr::Vec2f second_light = second.NextLight2D(2);

    YR_EXPECT_EQ(first_pixel.x, second_pixel.x);
    YR_EXPECT_EQ(first_pixel.y, second_pixel.y);
    YR_EXPECT_EQ(first_light.x, second_light.x);
    YR_EXPECT_EQ(first_light.y, second_light.y);
}

YR_TEST(cpu_sampler_stratifies_pixel_samples_in_square_grid) {
    for (int sample = 0; sample < 4; ++sample) {
        yr::CpuSampler sampler{
            yr::RenderSamplerKind::Stratified,
            std::uint64_t{100 + static_cast<std::uint64_t>(sample)},
            sample,
            4,
            1
        };

        const yr::Vec2f pixel = sampler.NextPixel2D();
        const int cell_x = sample % 2;
        const int cell_y = sample / 2;

        YR_EXPECT_TRUE(InRange(pixel.x, static_cast<float>(cell_x) * 0.5f, static_cast<float>(cell_x + 1) * 0.5f));
        YR_EXPECT_TRUE(InRange(pixel.y, static_cast<float>(cell_y) * 0.5f, static_cast<float>(cell_y + 1) * 0.5f));
    }
}

YR_TEST(cpu_sampler_stratifies_light_samples_in_square_grid) {
    yr::CpuSampler sampler{
        yr::RenderSamplerKind::Stratified,
        std::uint64_t{789},
        0,
        1,
        4
    };

    for (int sample = 0; sample < 4; ++sample) {
        const yr::Vec2f light = sampler.NextLight2D(sample);
        const int cell_x = sample % 2;
        const int cell_y = sample / 2;

        YR_EXPECT_TRUE(InRange(light.x, static_cast<float>(cell_x) * 0.5f, static_cast<float>(cell_x + 1) * 0.5f));
        YR_EXPECT_TRUE(InRange(light.y, static_cast<float>(cell_y) * 0.5f, static_cast<float>(cell_y + 1) * 0.5f));
    }
}

YR_TEST(cpu_sampler_stratifies_non_square_counts) {
    yr::CpuSampler sampler{
        yr::RenderSamplerKind::Stratified,
        std::uint64_t{999},
        5,
        6,
        1
    };

    const yr::Vec2f pixel = sampler.NextPixel2D();

    YR_EXPECT_TRUE(InRange(pixel.x, 2.0f / 3.0f, 1.0f));
    YR_EXPECT_TRUE(InRange(pixel.y, 0.5f, 1.0f));
}

YR_TEST(cpu_sampler_pixel_seed_is_deterministic_and_distinguishes_samples) {
    const std::uint64_t first = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t second = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 5);
    const std::uint64_t different_sample = yr::SeedForPixelSample(std::uint64_t{42}, 3, 4, 6);

    YR_EXPECT_EQ(first, second);
    YR_EXPECT_TRUE(first != different_sample);
}
```

- [ ] **Step 2: Add the sampler test file to CMake**

In `CMakeLists.txt`, add:

```cmake
    tests/cpu_sampler_tests.cpp
```

to the `add_executable(yaoray_tests ...)` source list immediately before:

```cmake
    tests/cpu_path_tracer_tests.cpp
```

- [ ] **Step 3: Run tests and confirm red**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- Build fails because `yaoray/backends/cpu/cpu_sampler.hpp`, `CpuSampler`, `Vec2f`, and `SeedForPixelSample` do not exist yet.

- [ ] **Step 4: Add `Vec2f`**

In `include/yaoray/core/vec.hpp`, add this type before `struct Vec3f`:

```cpp
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2f() = default;
    constexpr Vec2f(float x_value, float y_value)
        : x(x_value), y(y_value) {}
};
```

- [ ] **Step 5: Create the CPU sampler header**

Create `include/yaoray/backends/cpu/cpu_sampler.hpp`:

```cpp
#pragma once

#include <cstdint>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

class CpuSampler {
public:
    CpuSampler(
        RenderSamplerKind kind,
        std::uint64_t seed,
        int sample_index,
        int samples_per_pixel,
        int light_samples
    );

    Vec2f NextPixel2D();
    Vec2f NextLight2D(int light_sample_index);
    float Next1D();
    Vec2f Next2D();

private:
    std::uint32_t NextU32();
    float NextRandomFloat();
    Vec2f NextStratified2D(int sample_index, int sample_count);

    RenderSamplerKind kind_ = RenderSamplerKind::Independent;
    std::uint64_t state_ = 0;
    int sample_index_ = 0;
    int samples_per_pixel_ = 1;
    int light_samples_ = 1;
};

std::uint64_t SeedForPixelSample(std::uint64_t scene_seed, int x, int y, int sample);

} // namespace yr
```

- [ ] **Step 6: Implement the CPU sampler**

Create `src/backends/cpu/cpu_sampler.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_sampler.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace yr {
namespace {

constexpr std::uint64_t DefaultSeed = 0x9E3779B97F4A7C15ull;

struct StratifiedGrid {
    int columns = 1;
    int rows = 1;
};

int AtLeastOne(int value) {
    return std::max(1, value);
}

StratifiedGrid MakeStratifiedGrid(int sample_count) {
    const int count = AtLeastOne(sample_count);
    const int columns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
    const int rows = (count + columns - 1) / columns;
    return StratifiedGrid{columns, rows};
}

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, std::nextafter(1.0f, 0.0f));
}

std::uint64_t Mix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

} // namespace

CpuSampler::CpuSampler(
    RenderSamplerKind kind,
    std::uint64_t seed,
    int sample_index,
    int samples_per_pixel,
    int light_samples
)
    : kind_(kind),
      state_(seed == 0 ? DefaultSeed : seed),
      sample_index_(sample_index),
      samples_per_pixel_(AtLeastOne(samples_per_pixel)),
      light_samples_(AtLeastOne(light_samples)) {}

std::uint32_t CpuSampler::NextU32() {
    std::uint64_t x = state_;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    state_ = x;
    return static_cast<std::uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 32);
}

float CpuSampler::NextRandomFloat() {
    constexpr float scale = 1.0f / 16777216.0f;
    return static_cast<float>(NextU32() >> 8) * scale;
}

Vec2f CpuSampler::NextStratified2D(int sample_index, int sample_count) {
    const int count = AtLeastOne(sample_count);
    const int clamped_index = std::clamp(sample_index, 0, count - 1);
    const StratifiedGrid grid = MakeStratifiedGrid(count);
    const int cell_x = clamped_index % grid.columns;
    const int cell_y = clamped_index / grid.columns;

    const float x = (static_cast<float>(cell_x) + NextRandomFloat()) / static_cast<float>(grid.columns);
    const float y = (static_cast<float>(cell_y) + NextRandomFloat()) / static_cast<float>(grid.rows);
    return Vec2f{ClampUnit(x), ClampUnit(y)};
}

Vec2f CpuSampler::NextPixel2D() {
    if (samples_per_pixel_ == 1) {
        return Vec2f{0.5f, 0.5f};
    }
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(sample_index_, samples_per_pixel_);
    }
    return Next2D();
}

Vec2f CpuSampler::NextLight2D(int light_sample_index) {
    if (kind_ == RenderSamplerKind::Stratified) {
        return NextStratified2D(light_sample_index, light_samples_);
    }
    return Next2D();
}

float CpuSampler::Next1D() {
    return NextRandomFloat();
}

Vec2f CpuSampler::Next2D() {
    return Vec2f{NextRandomFloat(), NextRandomFloat()};
}

std::uint64_t SeedForPixelSample(std::uint64_t scene_seed, int x, int y, int sample) {
    std::uint64_t seed = Mix64(scene_seed);
    seed ^= Mix64(static_cast<std::uint64_t>(x) + 0xA24BAED4963EE407ull);
    seed ^= Mix64(static_cast<std::uint64_t>(y) + 0x9FB21C651E98DF25ull);
    seed ^= Mix64(static_cast<std::uint64_t>(sample) + 0xC2B2AE3D27D4EB4Full);
    return Mix64(seed);
}

} // namespace yr
```

- [ ] **Step 7: Add the sampler source file to CMake**

In `CMakeLists.txt`, add:

```cmake
    src/backends/cpu/cpu_sampler.cpp
```

to the `add_library(yaoray_backends STATIC ...)` source list immediately before:

```cmake
    src/backends/cpu/cpu_path_tracer.cpp
```

- [ ] **Step 8: Run focused sampler tests and confirm green**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- `cpu_sampler_*` tests prove deterministic independent behavior, square stratification, non-square stratification, and deterministic pixel/sample seeding.

- [ ] **Step 9: Commit CPU sampler module**

Run:

```powershell
git add include\yaoray\core\vec.hpp include\yaoray\backends\cpu\cpu_sampler.hpp src\backends\cpu\cpu_sampler.cpp tests\cpu_sampler_tests.cpp CMakeLists.txt
git commit -m "feat: add cpu sampler abstraction"
```

## Task 3: Use The Sampler In The CPU Path Tracer

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add failing path tracer sampler tests**

In `tests/cpu_path_tracer_tests.cpp`, add this test after `cpu_path_tracer_changes_stochastic_result_for_different_seed`:

```cpp
YR_TEST(cpu_path_tracer_stratified_sampler_changes_stochastic_result) {
    yr::RenderScene independent = MakeStochasticEdgeScene(123);
    independent.sampler = yr::RenderSamplerKind::Independent;

    yr::RenderScene stratified = independent;
    stratified.sampler = yr::RenderSamplerKind::Stratified;

    const yr::CpuPathTraceResult independent_result = yr::RenderCpuPathTrace(independent);
    const yr::CpuPathTraceResult stratified_result = yr::RenderCpuPathTrace(stratified);

    YR_EXPECT_TRUE(AnyPixelDifferent(independent_result.film, stratified_result.film));
}
```

Add this test after `cpu_path_tracer_is_deterministic_with_multiple_light_samples`:

```cpp
YR_TEST(cpu_path_tracer_stratified_sampler_is_deterministic) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.sampler = yr::RenderSamplerKind::Stratified;
    scene.spp = 4;
    scene.light_samples = 4;

    const yr::CpuPathTraceResult first = yr::RenderCpuPathTrace(scene);
    const yr::CpuPathTraceResult second = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_TRUE(FilmsEqual(first.film, second.film));
    YR_EXPECT_TRUE(CoreStatsEqual(first.stats, second.stats));
}
```

Update `MakeThreadedDeterminismScene()` by adding:

```cpp
scene.sampler = yr::RenderSamplerKind::Stratified;
```

after:

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
- `cpu_path_tracer_stratified_sampler_changes_stochastic_result` fails because the path tracer still ignores `scene.sampler` and uses the private RNG directly.
- The deterministic test may pass before implementation; the stochastic-result test is the required red test.

- [ ] **Step 3: Include the CPU sampler and remove private seed/RNG helpers from the path tracer**

In `src/backends/cpu/cpu_path_tracer.cpp`, add:

```cpp
#include <yaoray/backends/cpu/cpu_sampler.hpp>
```

after:

```cpp
#include <yaoray/backends/cpu/cpu_path_tracer.hpp>
```

Remove the private `struct Rng`, `Mix64`, and `SeedFor` definitions from the anonymous namespace. `CpuSampler` and `SeedForPixelSample` now own those responsibilities.

- [ ] **Step 4: Change area-light sampling to use sampler UVs**

Replace:

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
```

with:

```cpp
std::optional<AreaLightSample> SampleAreaLight(
    const RenderAreaLight& light,
    CpuSampler& sampler,
    int light_sample_index
) {
    const float area = light.width * light.height;
    if (area <= 0.0f) {
        return std::nullopt;
    }

    const Vec2f uv = sampler.NextLight2D(light_sample_index);
    const float offset_x = (uv.x - 0.5f) * light.width;
    const float offset_z = (uv.y - 0.5f) * light.height;
```

Keep the existing `return AreaLightSample{...};` block unchanged.

- [ ] **Step 5: Change cosine hemisphere sampling to use generic sampler draws**

Replace:

```cpp
Vec3f SampleCosineHemisphere(Vec3f normal, Rng& rng) {
    const float u1 = rng.NextFloat();
    const float u2 = rng.NextFloat();
```

with:

```cpp
Vec3f SampleCosineHemisphere(Vec3f normal, CpuSampler& sampler) {
    const Vec2f sample = sampler.Next2D();
    const float u1 = sample.x;
    const float u2 = sample.y;
```

Keep the rest of the function unchanged.

- [ ] **Step 6: Pass `CpuSampler` through direct lighting and path tracing**

Change the `EstimateDirectLight` parameter from:

```cpp
    Rng& rng,
```

to:

```cpp
    CpuSampler& sampler,
```

Inside `EstimateDirectLight`, replace:

```cpp
const std::optional<AreaLightSample> sample = SampleAreaLight(light, rng);
```

with:

```cpp
const std::optional<AreaLightSample> sample = SampleAreaLight(light, sampler, sample_index);
```

Change the `TracePath` signature from:

```cpp
Color3f TracePath(const RenderScene& scene, Ray3f ray, Rng& rng, CpuPathTraceStats& stats) {
```

to:

```cpp
Color3f TracePath(const RenderScene& scene, Ray3f ray, CpuSampler& sampler, CpuPathTraceStats& stats) {
```

Inside `TracePath`, replace:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, rng, stats));
```

with:

```cpp
radiance = radiance + Multiply(throughput, EstimateDirectLight(scene, hit_point, normal, material.albedo, sampler, stats));
```

Replace:

```cpp
const Vec3f bounce_direction = SampleCosineHemisphere(normal, rng);
```

with:

```cpp
const Vec3f bounce_direction = SampleCosineHemisphere(normal, sampler);
```

- [ ] **Step 7: Use `CpuSampler` in the render loop**

In `RenderCpuPathTrace`, replace:

```cpp
Rng rng{SeedFor(scene, x, y, sample)};
const float pixel_u = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
const float pixel_v = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
const Ray3f ray = MakeCameraRay(scene, x, y, pixel_u, pixel_v);
result.film.AddSample(x, y, TracePath(scene, ray, rng, stats));
```

with:

```cpp
CpuSampler sampler{
    scene.sampler,
    SeedForPixelSample(scene.seed, x, y, sample),
    sample,
    samples_per_pixel,
    DirectLightSampleCount(scene)
};
const Vec2f pixel_sample = sampler.NextPixel2D();
const Ray3f ray = MakeCameraRay(scene, x, y, pixel_sample.x, pixel_sample.y);
result.film.AddSample(x, y, TracePath(scene, ray, sampler, stats));
```

- [ ] **Step 8: Run focused tests and confirm green**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected:

- `yaoray_tests` passes.
- Existing independent-mode tests still pass.
- `cpu_path_tracer_stratified_sampler_changes_stochastic_result` passes because stratified pixel jitter changes the stochastic edge scene.
- `cpu_path_tracer_stratified_sampler_is_deterministic` passes.
- `cpu_path_tracer_is_bitwise_identical_across_thread_counts` passes with `scene.sampler = Stratified`.
- `cpu_path_tracer_light_samples_increase_shadow_rays` still passes.

- [ ] **Step 9: Commit path tracer sampler integration**

Run:

```powershell
git add src\backends\cpu\cpu_path_tracer.cpp tests\cpu_path_tracer_tests.cpp
git commit -m "feat: use sampler in cpu path tracer"
```

## Task 4: Update Cornell Examples And Documentation

**Files:**
- Modify: `scenes/examples/cornell_box_path.toml`
- Modify: `scenes/examples/cornell_box_path_threaded.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add `sampler = "stratified"` to Cornell path examples**

In both `scenes/examples/cornell_box_path.toml` and `scenes/examples/cornell_box_path_threaded.toml`, add:

```toml
sampler = "stratified"
```

inside the `[render]` table immediately after:

```toml
light_samples = 4
```

- [ ] **Step 2: Update README render paragraph**

In `README.md`, update the final render paragraph so it includes this content:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the CPU path tracer with diffuse bounce, deterministic sampling, random or stratified area-light surface sampling, diffuse BRDF/PDF-weighted direct lighting, explicit emissive hits, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). `render.sampler` controls path sample placement: `"independent"` is the default baseline, while `"stratified"` stratifies pixel jitter and direct area-light UV samples. `render.light_samples` controls how many direct area-light samples the path integrator averages per light per hit; the default is `1`, and higher values trade more shadow rays for lower soft-shadow and direct-light noise. The CLI reports actual threads plus samples/sec and rays/sec. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render. The path integrator still does not implement MIS, Russian roulette, denoising, adaptive sampling, advanced sampler sequences, arbitrary oriented area lights, or advanced material models.
```

- [ ] **Step 3: Update architecture overview CPU backend paragraph**

In `docs/architecture/overview.md`, replace the CPU backend paragraph with:

```markdown
The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output; it remains single-threaded for reference debugging and ignores `render.sampler` and `render.light_samples`. `path` is the CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, random or stratified XZ-rectangle area-light surface sampling selected by `render.sampler`, configurable direct area-light sample averaging through `render.light_samples`, diffuse BRDF/PDF-weighted direct lighting, and row-major tile threading controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count). It is still a v0 integrator without MIS, Russian roulette, denoising, adaptive sampling, Sobol/CMJ/blue-noise sampling, spectral rendering, random environment sampling, arbitrary oriented area lights, or final-quality material models.
```

- [ ] **Step 4: Run docs and scene checks**

Run:

```powershell
rg -n "sampler|light_samples|MIS|denoising|Russian roulette|Sobol|CMJ|blue-noise|debug_direct|render.threads" README.md docs\architecture\overview.md scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml
```

Expected:

- README mentions `render.sampler`, `"independent"`, and `"stratified"`.
- Architecture overview says `debug_direct` ignores `render.sampler` and `render.light_samples`.
- Both Cornell path examples include `sampler = "stratified"`.
- Limitations mention no MIS, no denoising, no Russian roulette, and no Sobol/CMJ/blue-noise sampling.

- [ ] **Step 5: Commit examples and docs**

Run:

```powershell
git add scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml README.md docs\architecture\overview.md
git commit -m "docs: document render sampler modes"
```

## Task 5: Full Verification And Manual Cornell Render

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

The render should succeed with `sampler = "stratified"` and `light_samples = 4`.

- [ ] **Step 4: Check implementation scope**

Run:

```powershell
rg -n "RenderSamplerKind|RenderSamplerName|ParseRenderSamplerName|sampler|CpuSampler|SeedForPixelSample|NextPixel2D|NextLight2D|Sobol|CMJ|blue-noise|MIS|denoising|Russian roulette|adaptive|debug_direct|RenderScene|RenderSettings" include src tests README.md docs\architecture\overview.md scenes\examples\cornell_box_path.toml scenes\examples\cornell_box_path_threaded.toml docs\superpowers\specs\2026-05-17-yaoray-sampler-v1-design.md docs\superpowers\plans\2026-05-17-yaoray-sampler-v1-implementation-plan.md
```

Expected:

- `RenderSamplerKind` appears in scene settings, render scene, parser/name helpers, compiler, tests, and CPU path tracer.
- `CpuSampler`, `SeedForPixelSample`, `NextPixel2D`, and `NextLight2D` appear only in the CPU sampler module, CPU path tracer, and sampler/path tests.
- `debug_direct` appears in docs/tests but the debug renderer implementation does not consume `sampler`.
- Sobol, CMJ, blue-noise, MIS, denoising, Russian roulette, and adaptive sampling appear only as non-goals or limitations.

- [ ] **Step 5: Confirm clean git state and recent commits**

Run:

```powershell
git status --short --branch
git log --oneline --decorate -8
```

Expected:

- Working tree is clean.
- Recent commits include:
  - `feat: parse render sampler mode`
  - `feat: add cpu sampler abstraction`
  - `feat: use sampler in cpu path tracer`
  - `docs: document render sampler modes`

## Self-Review Checklist

- Spec coverage:
  - Scene-authored `render.sampler`: Task 1.
  - Default `independent`: Task 1 tests and data model.
  - Accepted `"independent"` and `"stratified"` values: Task 1 parser tests and implementation.
  - Invalid sampler diagnostics: Task 1 parser tests.
  - Compiler propagation: Task 1.
  - Small sampler object instead of virtual hierarchy: Task 2.
  - Independent baseline behavior: Task 2 sampler tests and Task 3 path tests.
  - Stratified pixel jitter: Task 2 sampler tests and Task 3 path tests.
  - Stratified area-light UV sampling: Task 2 sampler tests and Task 3 path tracer integration.
  - Diffuse bounce remains independent: Task 3 uses `Next2D()` for bounce and does not stratify bounce dimensions.
  - Fixed-seed determinism: Task 2 and Task 3 tests.
  - Thread-count determinism: Task 3 updates the existing thread determinism scene to use `Stratified`.
  - Cornell examples and docs: Task 4.
  - No Sobol/CMJ/blue-noise/MIS/BSDF/CUDA/adaptive/denoiser/Russian roulette/lens/time work: Task 4 docs and Task 5 scope check.
- Placeholder scan:
  - Every task names concrete files, code snippets, commands, and expected outcomes.
- Type consistency:
  - `RenderSamplerKind`, `RenderSettings::sampler`, `RenderScene::sampler`, `CpuSampler`, `SeedForPixelSample`, `NextPixel2D`, and `NextLight2D` are used consistently.
  - `Vec2f` is defined before sampler code depends on it.
