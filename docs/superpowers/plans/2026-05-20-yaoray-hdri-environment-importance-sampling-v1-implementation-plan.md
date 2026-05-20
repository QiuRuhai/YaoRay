# HDRI Environment Importance Sampling v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Radiance `.hdr` environment maps that are visible on misses, illuminate surfaces through importance-sampled direct environment lighting, and participate in MIS when BSDF paths escape to the environment.

**Architecture:** Keep environment math in `yaoray_render`, not directly inside integrator-only helpers. `RenderScene` owns loaded HDR texels and an environment sampling distribution; the CPU path tracer calls render-level `EvaluateEnvironment()`, `SampleEnvironment()`, and `PdfEnvironment()` from miss handling and direct-light estimation.

**Tech Stack:** C++20, CMake, stb_image, toml++, custom `YR_TEST` test harness, CPU path tracer, existing `RenderScene` compile pipeline.

---

## Scope Check

This plan implements one subsystem: HDRI environment lighting with importance sampling and MIS for the current CPU path tracer. It does not implement EXR, filtered rough-specular environment lookup, portal lights, sun/sky, glTF light import, CUDA parity, or a full light-interface refactor.

## File Structure

- Modify `include/yaoray/scene/scene.hpp`: add `EnvironmentDescription::rotation_degrees`.
- Modify `src/scene/scene_parser.cpp`: parse `environment.rotation_degrees`, reject negative strength.
- Modify `include/yaoray/render/texture.hpp`: declare `LoadHdrTexture()`.
- Modify `src/render/texture.cpp`: allow stb PNG and HDR in one translation unit, implement HDR loader.
- Create `include/yaoray/render/environment.hpp`: public environment evaluation, mapping, sampling, distribution APIs.
- Create `src/render/environment.cpp`: equirectangular mapping, HDRI distribution build/sample/pdf logic.
- Modify `include/yaoray/render/render_scene.hpp`: add environment texture/distribution references and distribution storage.
- Modify `src/render/scene_compiler.cpp`: load `.hdr`, build environment distribution, compile rotation.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: use environment API for misses, direct environment lighting, and environment MIS.
- Modify `CMakeLists.txt`: compile `src/render/environment.cpp` and `tests/environment_tests.cpp`.
- Modify `tests/scene_tests.cpp`: parser/default tests.
- Modify `tests/texture_tests.cpp`: HDR loader tests.
- Create `tests/environment_tests.cpp`: mapping, distribution, sampling, and PDF tests.
- Modify `tests/render_scene_tests.cpp`: HDRI compiler tests.
- Modify `tests/cpu_path_tracer_tests.cpp`: CPU path tracer HDRI behavior tests.
- Create `tests/fixtures/assets/tiny_env.hdr`: tiny Radiance HDR fixture.
- Create `tests/fixtures/assets/bright_top_env.hdr`: deterministic HDRI importance fixture.
- Create `scenes/examples/assets/env/tiny_studio.hdr`: small example HDRI fixture.
- Create `scenes/examples/hdri_lighting_showcase.toml`: manual showcase scene.
- Modify `CMakeLists.txt`: add CLI smoke test for the showcase.
- Modify `README.md` and `docs/architecture/overview.md`: document new capability and remaining limits.

---

## Task 1: Scene Schema For HDRI Rotation And Strength Validation

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add failing parser/default tests**

Append these tests near the other environment/parser tests in `tests/scene_tests.cpp`:

```cpp
YR_TEST(scene_defaults_include_environment_rotation) {
    const yr::SceneDescription scene;

    YR_EXPECT_NEAR(scene.environment.strength, 1.0, 1e-6);
    YR_EXPECT_NEAR(scene.environment.rotation_degrees, 0.0, 1e-6);
}

YR_TEST(scene_parser_loads_hdri_environment_rotation) {
    const std::filesystem::path path = WriteTempScene(
        "hdri_environment_rotation.toml",
        R"toml(
[render]
width = 64
height = 32

[film]
output = "out/test.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "hdri"
path = "assets/env/studio.hdr"
strength = 1.5
rotation_degrees = 45.0
)toml"
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::EnvironmentDescription& environment = result.scene.value().environment;
    YR_EXPECT_EQ(environment.type, yr::EnvironmentKind::Hdri);
    YR_EXPECT_EQ(environment.path.filename().generic_string(), std::string{"studio.hdr"});
    YR_EXPECT_NEAR(environment.strength, 1.5, 1e-6);
    YR_EXPECT_NEAR(environment.rotation_degrees, 45.0, 1e-6);
}

YR_TEST(scene_parser_rejects_negative_environment_strength) {
    const std::filesystem::path path = WriteTempScene(
        "negative_environment_strength.toml",
        R"toml(
[render]
width = 64
height = 32

[film]
output = "out/test.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [1, 1, 1]
strength = -0.25
)toml"
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.strength", "must be non-negative"));
}

YR_TEST(scene_parser_rejects_non_numeric_environment_rotation) {
    const std::filesystem::path path = WriteTempScene(
        "bad_environment_rotation.toml",
        R"toml(
[render]
width = 64
height = 32

[film]
output = "out/test.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "hdri"
path = "assets/env/studio.hdr"
rotation_degrees = "east"
)toml"
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.rotation_degrees", "must be a finite float"));
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `EnvironmentDescription` has no `rotation_degrees`, or tests fail because `environment.rotation_degrees` is an unknown field.

- [ ] **Step 3: Add semantic field**

In `include/yaoray/scene/scene.hpp`, update `EnvironmentDescription`:

```cpp
struct EnvironmentDescription {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    std::filesystem::path path;
    float strength = 1.0f;
    float rotation_degrees = 0.0f;
};
```

- [ ] **Step 4: Parse and validate environment fields**

In `src/scene/scene_parser.cpp`, change `ParseEnvironment()` to allow `rotation_degrees`, reject negative strength, and store rotation:

```cpp
void ParseEnvironment(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& scene_dir,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    CheckUnknownFields(table, "environment", {"type", "radiance", "path", "strength", "rotation_degrees"}, file, diagnostics);

    if (const auto type = ReadValue<std::string>(table, "type")) {
        if (const auto parsed = ParseEnvironmentKindName(*type)) {
            scene.environment.type = *parsed;
        } else {
            diagnostics.push_back(Error(file, "environment.type", "unknown environment type"));
        }
    }
    if (const auto radiance = ReadVec3(table, "radiance", file, "environment.radiance", diagnostics)) {
        scene.environment.radiance = *radiance;
    }
    if (const auto path = ReadValue<std::string>(table, "path")) {
        scene.environment.path = path->empty() ? std::filesystem::path{} : NormalizeScenePath(scene_dir, *path);
    }
    if (const auto strength = ReadFloat(table, "strength", file, "environment.strength", diagnostics)) {
        if (*strength < 0.0f) {
            diagnostics.push_back(Error(file, "environment.strength", "must be non-negative"));
        } else {
            scene.environment.strength = *strength;
        }
    }
    if (const auto rotation = ReadFloat(table, "rotation_degrees", file, "environment.rotation_degrees", diagnostics)) {
        scene.environment.rotation_degrees = *rotation;
    }
}
```

- [ ] **Step 5: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 6: Commit**

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse hdri environment rotation"
```

---

## Task 2: HDR Texture Loading And Test Fixtures

**Files:**
- Modify: `include/yaoray/render/texture.hpp`
- Modify: `src/render/texture.cpp`
- Modify: `tests/texture_tests.cpp`
- Create: `tests/fixtures/assets/tiny_env.hdr`
- Create: `tests/fixtures/assets/bright_top_env.hdr`
- Create: `scenes/examples/assets/env/tiny_studio.hdr`

- [ ] **Step 1: Create deterministic HDR fixtures**

Run this PowerShell script from the repository root. It creates three tiny Radiance `.hdr` files with known RGBE pixels:

```powershell
$testAssets = Join-Path (Get-Location) "tests\fixtures\assets"
$sceneEnv = Join-Path (Get-Location) "scenes\examples\assets\env"
New-Item -ItemType Directory -Force -Path $testAssets | Out-Null
New-Item -ItemType Directory -Force -Path $sceneEnv | Out-Null

function Write-RgbeHdr($Path, [byte[]]$Pixels) {
    [byte[]]$header = [System.Text.Encoding]::ASCII.GetBytes("#?RADIANCE`nFORMAT=32-bit_rle_rgbe`n`n-Y 2 +X 2`n")
    [System.IO.File]::WriteAllBytes($Path, $header + $Pixels)
}

Write-RgbeHdr (Join-Path $testAssets "tiny_env.hdr") ([byte[]](
    128,0,0,129,
    0,128,0,129,
    0,0,128,129,
    128,128,128,129
))

Write-RgbeHdr (Join-Path $testAssets "bright_top_env.hdr") ([byte[]](
    128,128,128,132,
    16,16,16,129,
    16,16,16,129,
    16,16,16,129
))

Copy-Item -Force -Path (Join-Path $testAssets "bright_top_env.hdr") -Destination (Join-Path $sceneEnv "tiny_studio.hdr")
```

Expected files:

```text
tests/fixtures/assets/tiny_env.hdr
tests/fixtures/assets/bright_top_env.hdr
scenes/examples/assets/env/tiny_studio.hdr
```

- [ ] **Step 2: Add failing HDR loader tests**

Append these tests to `tests/texture_tests.cpp`:

```cpp
YR_TEST(texture_loader_reads_hdr_texels_as_linear_float) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/tiny_env.hdr"));

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.error.empty());
    YR_EXPECT_EQ(result.texture.width, 2);
    YR_EXPECT_EQ(result.texture.height, 2);
    YR_EXPECT_EQ(result.texture.filter, yr::TextureFilter::Bilinear);
    YR_EXPECT_EQ(result.texture.wrap_s, yr::TextureWrap::Repeat);
    YR_EXPECT_EQ(result.texture.wrap_t, yr::TextureWrap::ClampToEdge);
    YR_EXPECT_EQ(result.texture.texels.size(), std::size_t{4});
    YR_EXPECT_NEAR(result.texture.texels[0].x, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[0].y, 0.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[1].y, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[2].z, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].x, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].y, 1.0, 1e-5);
    YR_EXPECT_NEAR(result.texture.texels[3].z, 1.0, 1e-5);
}

YR_TEST(texture_loader_rejects_non_hdr_extension_for_hdr_load) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/checker_2x2.png"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".hdr") != std::string::npos);
}

YR_TEST(texture_loader_reports_missing_hdr_file) {
    const yr::TextureLoadResult result = yr::LoadHdrTexture(TextureFixturePath("assets/missing_environment.hdr"));

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("not found") != std::string::npos);
}
```

- [ ] **Step 3: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: build fails because `LoadHdrTexture()` is not declared.

- [ ] **Step 4: Declare HDR loader**

In `include/yaoray/render/texture.hpp`, add:

```cpp
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path);
```

Place it next to `LoadPngTexture()`.

- [ ] **Step 5: Implement HDR loader**

In `src/render/texture.cpp`, remove `#define STBI_ONLY_PNG` and include `<limits>`. Add this helper:

```cpp
bool IsFiniteColor(Color3f color) {
    return std::isfinite(color.x) && std::isfinite(color.y) && std::isfinite(color.z);
}
```

Then add this function after `LoadPngTexture()`:

```cpp
TextureLoadResult LoadHdrTexture(const std::filesystem::path& path) {
    if (LowerExtension(path) != ".hdr") {
        return TextureLoadResult{RenderTexture{}, false, "HDR environment path must use a .hdr extension: " + path.generic_string()};
    }
    if (!std::filesystem::exists(path)) {
        return TextureLoadResult{RenderTexture{}, false, "HDR environment file not found: " + path.generic_string()};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 3);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return TextureLoadResult{
            RenderTexture{},
            false,
            "failed to load HDR environment: " + path.generic_string() + (reason == nullptr ? "" : std::string{" ("} + reason + ")")
        };
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return TextureLoadResult{RenderTexture{}, false, "HDR environment has invalid dimensions: " + path.generic_string()};
    }

    RenderTexture texture;
    texture.width = width;
    texture.height = height;
    texture.filter = TextureFilter::Bilinear;
    texture.wrap_s = TextureWrap::Repeat;
    texture.wrap_t = TextureWrap::ClampToEdge;
    texture.texels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int i = 0; i < width * height; ++i) {
        const int base = i * 3;
        const Color3f color{pixels[base + 0], pixels[base + 1], pixels[base + 2]};
        if (!IsFiniteColor(color)) {
            stbi_image_free(pixels);
            return TextureLoadResult{RenderTexture{}, false, "HDR environment contains non-finite texels: " + path.generic_string()};
        }
        texture.texels.push_back(color);
    }
    stbi_image_free(pixels);

    if (texture.texels.empty()) {
        return TextureLoadResult{RenderTexture{}, false, "HDR environment contains no texels: " + path.generic_string()};
    }
    return TextureLoadResult{std::move(texture), true, {}};
}
```

- [ ] **Step 6: Run texture tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass. Existing PNG loader tests still pass, proving PNG sRGB conversion was not broken.

- [ ] **Step 7: Commit**

```powershell
git add include/yaoray/render/texture.hpp src/render/texture.cpp tests/texture_tests.cpp tests/fixtures/assets/tiny_env.hdr tests/fixtures/assets/bright_top_env.hdr scenes/examples/assets/env/tiny_studio.hdr
git commit -m "feat: load hdr environment textures"
```

---

## Task 3: Environment Mapping And Importance Distribution

**Files:**
- Create: `include/yaoray/render/environment.hpp`
- Create: `src/render/environment.cpp`
- Create: `tests/environment_tests.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing environment tests**

Create `tests/environment_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cmath>
#include <cstddef>

#include <yaoray/render/environment.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderTexture MakeEnvironmentTexture() {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.wrap_s = yr::TextureWrap::Repeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {
        yr::Color3f{8.0f, 8.0f, 8.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{1.0f, 1.0f, 1.0f}
    };
    return texture;
}

yr::RenderScene MakeHdriScene() {
    yr::RenderScene scene;
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.strength = 2.0f;
    scene.environment.texture_index = 0;
    scene.environment.distribution_index = 0;
    scene.textures.push_back(MakeEnvironmentTexture());
    scene.environment_distributions.push_back(yr::BuildEnvironmentDistribution(scene.textures[0]));
    return scene;
}

} // namespace

YR_TEST(environment_direction_uv_round_trips_cardinal_directions) {
    const yr::Vec3f directions[] = {
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        yr::Vec3f{1.0f, 0.0f, 0.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f}
    };

    for (const yr::Vec3f direction : directions) {
        const yr::Vec2f uv = yr::DirectionToEnvironmentUv(direction, 0.0f);
        const yr::Vec3f round_trip = yr::EnvironmentUvToDirection(uv, 0.0f);
        YR_EXPECT_NEAR(yr::Dot(yr::Normalize(direction), round_trip), 1.0, 1e-5);
    }
}

YR_TEST(environment_rotation_changes_horizontal_lookup) {
    const yr::Vec2f unrotated = yr::DirectionToEnvironmentUv(yr::Vec3f{0.0f, 0.0f, 1.0f}, 0.0f);
    const yr::Vec2f rotated = yr::DirectionToEnvironmentUv(yr::Vec3f{0.0f, 0.0f, 1.0f}, 3.14159265358979323846f * 0.5f);

    YR_EXPECT_NEAR(unrotated.x, 0.5, 1e-5);
    YR_EXPECT_NEAR(rotated.x, 0.75, 1e-5);
    YR_EXPECT_NEAR(unrotated.y, rotated.y, 1e-5);
}

YR_TEST(environment_evaluates_hdri_with_strength) {
    const yr::RenderScene scene = MakeHdriScene();

    const yr::Color3f color = yr::EvaluateEnvironment(scene, yr::EnvironmentUvToDirection(yr::Vec2f{0.25f, 0.25f}, 0.0f));

    YR_EXPECT_TRUE(color.x > 8.0f);
    YR_EXPECT_NEAR(color.x, color.y, 1e-5);
    YR_EXPECT_NEAR(color.y, color.z, 1e-5);
}

YR_TEST(environment_distribution_prefers_bright_texel) {
    const yr::RenderTexture texture = MakeEnvironmentTexture();
    const yr::RenderEnvironmentDistribution distribution = yr::BuildEnvironmentDistribution(texture);

    const std::size_t bright = 0;
    const std::size_t dim = 1;
    YR_EXPECT_TRUE(distribution.texel_weights[bright] > distribution.texel_weights[dim]);
    YR_EXPECT_TRUE(distribution.total_weight > 0.0f);
}

YR_TEST(environment_sampling_returns_positive_pdf) {
    const yr::RenderScene scene = MakeHdriScene();

    const yr::EnvironmentSample sample = yr::SampleEnvironment(scene, yr::Vec2f{0.1f, 0.1f});

    YR_EXPECT_TRUE(sample.valid);
    YR_EXPECT_TRUE(sample.pdf_solid_angle > 0.0f);
    YR_EXPECT_TRUE(yr::PdfEnvironment(scene, sample.direction) > 0.0f);
    YR_EXPECT_NEAR(yr::Length(sample.direction), 1.0, 1e-5);
}

YR_TEST(environment_black_distribution_stays_finite) {
    yr::RenderTexture texture;
    texture.width = 2;
    texture.height = 2;
    texture.texels = {
        yr::Color3f{},
        yr::Color3f{},
        yr::Color3f{},
        yr::Color3f{}
    };

    const yr::RenderEnvironmentDistribution distribution = yr::BuildEnvironmentDistribution(texture);

    YR_EXPECT_TRUE(distribution.uniform);
    YR_EXPECT_TRUE(distribution.total_weight > 0.0f);
    for (float weight : distribution.texel_weights) {
        YR_EXPECT_TRUE(std::isfinite(weight));
        YR_EXPECT_TRUE(weight > 0.0f);
    }
}
```

- [ ] **Step 2: Add test target file to CMake and verify failure**

In `CMakeLists.txt`, add `tests/environment_tests.cpp` to `yaoray_tests`.

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `yaoray/render/environment.hpp` does not exist.

- [ ] **Step 3: Extend render scene structures**

In `include/yaoray/render/render_scene.hpp`, update `RenderEnvironment` and add a distribution type:

```cpp
struct RenderEnvironment {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};

struct RenderEnvironmentDistribution {
    int width = 0;
    int height = 0;
    std::vector<float> texel_weights;
    std::vector<float> row_weights;
    std::vector<float> row_cdf;
    std::vector<float> conditional_cdfs;
    float total_weight = 0.0f;
    bool uniform = false;
};
```

Then add this member to `RenderScene`:

```cpp
std::vector<RenderEnvironmentDistribution> environment_distributions;
```

- [ ] **Step 4: Create environment API header**

Create `include/yaoray/render/environment.hpp`:

```cpp
#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct EnvironmentSample {
    Vec3f direction;
    Color3f radiance;
    float pdf_solid_angle = 0.0f;
    bool valid = false;
};

Vec2f DirectionToEnvironmentUv(Vec3f direction, float rotation_radians);
Vec3f EnvironmentUvToDirection(Vec2f uv, float rotation_radians);

RenderEnvironmentDistribution BuildEnvironmentDistribution(const RenderTexture& texture);

Color3f EvaluateEnvironment(const RenderScene& scene, Vec3f direction);
EnvironmentSample SampleEnvironment(const RenderScene& scene, Vec2f sample);
float PdfEnvironment(const RenderScene& scene, Vec3f direction);
bool HasSampleableEnvironment(const RenderScene& scene);

} // namespace yr
```

- [ ] **Step 5: Implement environment mapping and distribution**

Create `src/render/environment.cpp` with these core definitions:

```cpp
#include <yaoray/render/environment.hpp>

#include <yaoray/render/texture.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = 2.0f * Pi;
constexpr float MinSinTheta = 1.0e-6f;

float Fract(float value) {
    return value - std::floor(value);
}

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, std::nextafter(1.0f, 0.0f));
}

float Luminance(Color3f color) {
    return std::max(0.0f, 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z);
}

float RowTheta(int y, int height) {
    return (static_cast<float>(y) + 0.5f) * Pi / static_cast<float>(height);
}

std::size_t TexelIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

int FindCdfIndex(const std::vector<float>& cdf, float target) {
    const auto found = std::lower_bound(cdf.begin(), cdf.end(), target);
    if (found == cdf.end()) {
        return static_cast<int>(cdf.size()) - 1;
    }
    return static_cast<int>(found - cdf.begin());
}

bool ValidTextureIndex(const RenderScene& scene, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < scene.textures.size();
}

bool ValidDistributionIndex(const RenderScene& scene, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < scene.environment_distributions.size();
}

} // namespace

Vec2f DirectionToEnvironmentUv(Vec3f direction, float rotation_radians) {
    const Vec3f d = Normalize(direction);
    const float phi = std::atan2(d.x, d.z);
    const float u = Fract(0.5f + (phi + rotation_radians) / TwoPi);
    const float theta = std::acos(std::clamp(d.y, -1.0f, 1.0f));
    return Vec2f{u, std::clamp(theta / Pi, 0.0f, 1.0f)};
}

Vec3f EnvironmentUvToDirection(Vec2f uv, float rotation_radians) {
    const float phi = (uv.x - 0.5f) * TwoPi - rotation_radians;
    const float theta = std::clamp(uv.y, 0.0f, 1.0f) * Pi;
    const float sin_theta = std::sin(theta);
    return Normalize(Vec3f{
        sin_theta * std::sin(phi),
        std::cos(theta),
        sin_theta * std::cos(phi)
    });
}

RenderEnvironmentDistribution BuildEnvironmentDistribution(const RenderTexture& texture) {
    RenderEnvironmentDistribution distribution;
    distribution.width = texture.width;
    distribution.height = texture.height;
    if (texture.width <= 0 || texture.height <= 0 || texture.texels.empty()) {
        distribution.uniform = true;
        distribution.width = std::max(1, texture.width);
        distribution.height = std::max(1, texture.height);
    }

    const int width = std::max(1, distribution.width);
    const int height = std::max(1, distribution.height);
    distribution.texel_weights.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    distribution.row_weights.assign(static_cast<std::size_t>(height), 0.0f);
    distribution.row_cdf.assign(static_cast<std::size_t>(height), 0.0f);
    distribution.conditional_cdfs.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

    for (int y = 0; y < height; ++y) {
        const float sin_theta = std::max(MinSinTheta, std::sin(RowTheta(y, height)));
        float row_sum = 0.0f;
        for (int x = 0; x < width; ++x) {
            const std::size_t index = TexelIndex(x, y, width);
            const Color3f color = index < texture.texels.size() ? texture.texels[index] : Color3f{};
            const float weight = Luminance(color) * sin_theta;
            distribution.texel_weights[index] = weight;
            row_sum += weight;
            distribution.conditional_cdfs[index] = row_sum;
        }
        distribution.row_weights[static_cast<std::size_t>(y)] = row_sum;
        distribution.total_weight += row_sum;
        distribution.row_cdf[static_cast<std::size_t>(y)] = distribution.total_weight;
    }

    if (distribution.total_weight <= 0.0f || !std::isfinite(distribution.total_weight)) {
        distribution.uniform = true;
        distribution.total_weight = static_cast<float>(width * height);
        for (int y = 0; y < height; ++y) {
            float row_sum = 0.0f;
            for (int x = 0; x < width; ++x) {
                const std::size_t index = TexelIndex(x, y, width);
                distribution.texel_weights[index] = 1.0f;
                row_sum += 1.0f;
                distribution.conditional_cdfs[index] = row_sum;
            }
            distribution.row_weights[static_cast<std::size_t>(y)] = row_sum;
            distribution.row_cdf[static_cast<std::size_t>(y)] = static_cast<float>((y + 1) * width);
        }
    }

    return distribution;
}
```

Continue the same file with `EvaluateEnvironment()`, `SampleEnvironment()`, `PdfEnvironment()`, and `HasSampleableEnvironment()`:

```cpp
Color3f EvaluateEnvironment(const RenderScene& scene, Vec3f direction) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    if (scene.environment.type != EnvironmentKind::Hdri ||
        scene.environment.strength <= 0.0f ||
        !ValidTextureIndex(scene, scene.environment.texture_index)) {
        return Color3f{};
    }

    const RenderTexture& texture = scene.textures[static_cast<std::size_t>(scene.environment.texture_index)];
    const Vec2f uv = DirectionToEnvironmentUv(direction, scene.environment.rotation_radians);
    return SampleTexture(texture, uv) * scene.environment.strength;
}

bool HasSampleableEnvironment(const RenderScene& scene) {
    return scene.environment.type == EnvironmentKind::Hdri &&
           scene.environment.strength > 0.0f &&
           ValidTextureIndex(scene, scene.environment.texture_index) &&
           ValidDistributionIndex(scene, scene.environment.distribution_index);
}

EnvironmentSample SampleEnvironment(const RenderScene& scene, Vec2f sample) {
    if (!HasSampleableEnvironment(scene)) {
        return EnvironmentSample{};
    }

    const RenderEnvironmentDistribution& distribution =
        scene.environment_distributions[static_cast<std::size_t>(scene.environment.distribution_index)];
    const int width = distribution.width;
    const int height = distribution.height;
    if (width <= 0 || height <= 0 || distribution.total_weight <= 0.0f) {
        return EnvironmentSample{};
    }

    const float row_target = ClampUnit(sample.y) * distribution.total_weight;
    const int y = FindCdfIndex(distribution.row_cdf, row_target);
    const float row_previous = y == 0 ? 0.0f : distribution.row_cdf[static_cast<std::size_t>(y - 1)];
    const float row_weight = std::max(distribution.row_weights[static_cast<std::size_t>(y)], MinSinTheta);
    const float row_fraction = std::clamp((row_target - row_previous) / row_weight, 0.0f, 1.0f);

    const std::size_t row_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    const float column_target = ClampUnit(sample.x) * row_weight;
    const auto begin = distribution.conditional_cdfs.begin() + static_cast<std::ptrdiff_t>(row_offset);
    const auto end = begin + width;
    const auto found = std::lower_bound(begin, end, column_target);
    const int x = found == end ? width - 1 : static_cast<int>(found - begin);
    const float column_previous = x == 0 ? 0.0f : distribution.conditional_cdfs[row_offset + static_cast<std::size_t>(x - 1)];
    const float texel_weight = std::max(distribution.texel_weights[row_offset + static_cast<std::size_t>(x)], MinSinTheta);
    const float column_fraction = std::clamp((column_target - column_previous) / texel_weight, 0.0f, 1.0f);

    const Vec2f uv{
        (static_cast<float>(x) + column_fraction) / static_cast<float>(width),
        (static_cast<float>(y) + row_fraction) / static_cast<float>(height)
    };
    const Vec3f direction = EnvironmentUvToDirection(uv, scene.environment.rotation_radians);
    const float pdf = PdfEnvironment(scene, direction);
    if (pdf <= 0.0f) {
        return EnvironmentSample{};
    }
    return EnvironmentSample{direction, EvaluateEnvironment(scene, direction), pdf, true};
}

float PdfEnvironment(const RenderScene& scene, Vec3f direction) {
    if (!HasSampleableEnvironment(scene)) {
        return 0.0f;
    }
    const RenderEnvironmentDistribution& distribution =
        scene.environment_distributions[static_cast<std::size_t>(scene.environment.distribution_index)];
    const int width = distribution.width;
    const int height = distribution.height;
    if (width <= 0 || height <= 0 || distribution.total_weight <= 0.0f) {
        return 0.0f;
    }

    const Vec2f uv = DirectionToEnvironmentUv(direction, scene.environment.rotation_radians);
    const int x = std::clamp(static_cast<int>(std::floor(uv.x * static_cast<float>(width))), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(uv.y * static_cast<float>(height))), 0, height - 1);
    const std::size_t index = TexelIndex(x, y, width);
    const float probability = distribution.texel_weights[index] / distribution.total_weight;
    const float sin_theta = std::max(MinSinTheta, std::sin(RowTheta(y, height)));
    const float texel_solid_angle = (TwoPi / static_cast<float>(width)) * (Pi / static_cast<float>(height)) * sin_theta;
    return probability / texel_solid_angle;
}

} // namespace yr
```

- [ ] **Step 6: Add render module source to CMake**

In `CMakeLists.txt`, add `src/render/environment.cpp` to `yaoray_render`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/environment.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
    src/render/shading.cpp
    src/render/texture.cpp
)
```

- [ ] **Step 7: Run environment tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 8: Commit**

```powershell
git add CMakeLists.txt include/yaoray/render/render_scene.hpp include/yaoray/render/environment.hpp src/render/environment.cpp tests/environment_tests.cpp
git commit -m "feat: add hdri environment sampling"
```

---

## Task 4: Scene Compiler HDRI Support

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Replace obsolete rejection test with failing compiler tests**

In `tests/render_scene_tests.cpp`, replace `scene_compiler_rejects_hdri_environment` with:

```cpp
YR_TEST(scene_compiler_compiles_hdri_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.path = FixturePath("assets/tiny_env.hdr");
    scene.environment.strength = 1.5f;
    scene.environment.rotation_degrees = 90.0f;

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.environment.type, yr::EnvironmentKind::Hdri);
    YR_EXPECT_NEAR(compiled.environment.strength, 1.5, 1e-6);
    YR_EXPECT_NEAR(compiled.environment.rotation_radians, 1.57079637, 1e-5);
    YR_EXPECT_EQ(compiled.environment.texture_index, 0);
    YR_EXPECT_EQ(compiled.environment.distribution_index, 0);
    YR_EXPECT_EQ(compiled.textures.size(), std::size_t{1});
    YR_EXPECT_EQ(compiled.environment_distributions.size(), std::size_t{1});
}

YR_TEST(scene_compiler_rejects_hdri_environment_without_path) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.path", "must not be empty"));
}

YR_TEST(scene_compiler_rejects_hdri_environment_non_hdr_path) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.path = FixturePath("assets/checker_2x2.png");

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.path", ".hdr"));
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: `scene_compiler_compiles_hdri_environment` fails because the compiler still rejects HDRI.

- [ ] **Step 3: Include environment helpers in compiler**

In `src/render/scene_compiler.cpp`, add:

```cpp
#include <yaoray/render/environment.hpp>
```

- [ ] **Step 4: Add HDRI compile helper**

Add this helper near `CopyAreaLights()`:

```cpp
void CompileEnvironment(
    const SceneDescription& scene,
    RenderScene& compiled,
    std::vector<SceneDiagnostic>& diagnostics
) {
    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        compiled.environment.type = scene.environment.type;
        compiled.environment.radiance = scene.environment.radiance;
        compiled.environment.strength = scene.environment.strength;
        return;
    }

    if (scene.environment.type != EnvironmentKind::Hdri) {
        return;
    }
    if (scene.environment.path.empty()) {
        diagnostics.push_back(Error(scene, "environment.path", "must not be empty for hdri environment"));
        return;
    }

    TextureLoadResult load = LoadHdrTexture(scene.environment.path);
    if (!load.ok) {
        diagnostics.push_back(Error(scene, "environment.path", load.error));
        return;
    }

    const int texture_index = static_cast<int>(compiled.textures.size());
    compiled.textures.push_back(std::move(load.texture));
    const int distribution_index = static_cast<int>(compiled.environment_distributions.size());
    compiled.environment_distributions.push_back(BuildEnvironmentDistribution(compiled.textures[static_cast<std::size_t>(texture_index)]));

    compiled.environment.type = EnvironmentKind::Hdri;
    compiled.environment.strength = scene.environment.strength;
    compiled.environment.rotation_radians = DegreesToRadians(scene.environment.rotation_degrees);
    compiled.environment.texture_index = texture_index;
    compiled.environment.distribution_index = distribution_index;
}
```

- [ ] **Step 5: Replace old environment compile block**

In `CompileScene()`, replace the existing `if (scene.environment.type == ... Hdri)` block with:

```cpp
CompileEnvironment(scene, compiled, result.diagnostics);
```

Leave `CopyAreaLights(scene, compiled);` after environment compilation.

- [ ] **Step 6: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 7: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile hdri environments"
```

---

## Task 5: CPU Path Tracer Miss, Direct Environment Lighting, And MIS

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add failing CPU path tracer tests**

In `tests/cpu_path_tracer_tests.cpp`, add `#include <yaoray/render/environment.hpp>` with other render includes. Then add these helpers near existing scene factory helpers:

```cpp
yr::RenderScene MakeHdriMissScene(yr::Color3f env_color) {
    yr::RenderScene scene;
    scene.integrator = yr::RenderIntegratorKind::Path;
    scene.width = 1;
    scene.height = 1;
    scene.spp = 1;
    scene.max_depth = 2;
    scene.seed = 7;
    scene.threads = 1;
    scene.light_samples = 1;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.strength = 1.0f;
    scene.environment.texture_index = 0;
    scene.environment.distribution_index = 0;

    yr::RenderTexture texture;
    texture.width = 1;
    texture.height = 1;
    texture.wrap_s = yr::TextureWrap::Repeat;
    texture.wrap_t = yr::TextureWrap::ClampToEdge;
    texture.texels = {env_color};
    scene.textures.push_back(texture);
    scene.environment_distributions.push_back(yr::BuildEnvironmentDistribution(scene.textures[0]));
    scene.bvh = yr::BuildBvh(scene.triangles).bvh;
    return scene;
}

yr::RenderScene MakeDiffusePlaneUnderHdriScene(bool with_occluder) {
    yr::RenderScene scene = MakeHdriMissScene(yr::Color3f{2.0f, 2.0f, 2.0f});
    scene.spp = 8;
    scene.max_depth = 1;
    scene.light_samples = 4;
    scene.camera.origin = yr::Point3f{0.0f, 1.0f, 2.0f};
    scene.camera.forward = yr::Normalize(yr::Vec3f{0.0f, -0.45f, -1.0f});
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Normalize(yr::Cross(scene.camera.right, scene.camera.forward));
    scene.materials.push_back(yr::RenderMaterial{yr::MaterialKind::Diffuse, yr::Color3f{0.8f, 0.8f, 0.8f}});
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-2.0f, 0.0f, -2.0f},
        yr::Point3f{2.0f, 0.0f, -2.0f},
        yr::Point3f{0.0f, 0.0f, 2.0f},
        yr::Vec3f{0.0f, 1.0f, 0.0f},
        0
    });
    if (with_occluder) {
        scene.triangles.push_back(yr::RenderTriangle{
            yr::Point3f{-5.0f, 0.5f, -5.0f},
            yr::Point3f{5.0f, 0.5f, -5.0f},
            yr::Point3f{0.0f, 0.5f, 5.0f},
            yr::Vec3f{0.0f, -1.0f, 0.0f},
            0
        });
    }
    scene.bvh = yr::BuildBvh(scene.triangles).bvh;
    return scene;
}
```

Add tests:

```cpp
YR_TEST(cpu_path_tracer_camera_miss_sees_hdri_environment) {
    yr::RenderScene scene = MakeHdriMissScene(yr::Color3f{0.25f, 0.5f, 1.0f});

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_NEAR(pixel.x, 0.25, 1e-5);
    YR_EXPECT_NEAR(pixel.y, 0.5, 1e-5);
    YR_EXPECT_NEAR(pixel.z, 1.0, 1e-5);
}

YR_TEST(cpu_path_tracer_diffuse_surface_receives_direct_hdri_light) {
    yr::RenderScene scene = MakeDiffusePlaneUnderHdriScene(false);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_TRUE(pixel.x > 0.05f);
    YR_EXPECT_TRUE(result.stats.shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_occluder_blocks_direct_hdri_light) {
    const yr::CpuPathTraceResult open_result = yr::RenderCpuPathTrace(MakeDiffusePlaneUnderHdriScene(false));
    const yr::CpuPathTraceResult occluded_result = yr::RenderCpuPathTrace(MakeDiffusePlaneUnderHdriScene(true));

    const float open_luminance = open_result.film.LinearPixel(0, 0).x;
    const float occluded_luminance = occluded_result.film.LinearPixel(0, 0).x;

    YR_EXPECT_TRUE(open_luminance > occluded_luminance);
    YR_EXPECT_TRUE(occluded_result.stats.occluded_shadow_rays > 0);
}
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: HDRI miss test fails because miss rays still use constant-only environment color; direct-light test fails because environment direct sampling is absent.

- [ ] **Step 3: Include environment API in path tracer**

In `src/backends/cpu/cpu_path_tracer.cpp`, add:

```cpp
#include <yaoray/render/environment.hpp>
```

- [ ] **Step 4: Replace constant-only environment helper**

Replace `EnvironmentColor()` with:

```cpp
Color3f EnvironmentColor(const RenderScene& scene, Vec3f direction) {
    return EvaluateEnvironment(scene, direction);
}
```

Add:

```cpp
float EnvironmentHitMisWeight(const RenderScene& scene, const PreviousBounce& previous, Vec3f direction) {
    if (!previous.valid || previous.delta) {
        return 1.0f;
    }

    const float pdf_light = PdfEnvironment(scene, direction);
    return PowerHeuristic(1, previous.bsdf_pdf, previous.light_sample_count, pdf_light);
}
```

Then change the miss block in `TracePath()`:

```cpp
if (!hit.hit || hit.triangle == nullptr) {
    ++stats.misses;
    const float environment_weight = EnvironmentHitMisWeight(scene, previous_bounce, ray.direction);
    radiance = radiance + Multiply(throughput, EnvironmentColor(scene, ray.direction)) * environment_weight;
    break;
}
```

- [ ] **Step 5: Add direct environment lighting helper**

Add this function immediately before `EstimateDirectLight()` so it is declared before use:

```cpp
Color3f EstimateDirectEnvironmentLight(
    const RenderScene& scene,
    const RenderMaterial& material,
    Point3f hit_point,
    Vec3f normal,
    Vec3f wo,
    CpuSampler& sampler,
    CpuPathTraceStats& stats
) {
    if (!HasSampleableEnvironment(scene)) {
        return Color3f{};
    }

    Color3f radiance;
    const int light_sample_count = DirectLightSampleCount(scene);
    const float inverse_light_sample_count = 1.0f / static_cast<float>(light_sample_count);
    for (int sample_index = 0; sample_index < light_sample_count; ++sample_index) {
        const EnvironmentSample sample = SampleEnvironment(scene, sampler.NextLight2D(sample_index));
        if (!sample.valid || sample.pdf_solid_angle <= 0.0f || IsNearBlack(sample.radiance)) {
            continue;
        }

        const Vec3f wi = sample.direction;
        const float cos_surface = std::max(0.0f, Dot(normal, wi));
        if (cos_surface <= 0.0f) {
            continue;
        }

        const Color3f bsdf = EvaluateBsdf(material, wo, wi, normal);
        if (IsNearBlack(bsdf)) {
            continue;
        }

        const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
        ++stats.shadow_rays;
        BvhTraceStats shadow_trace;
        const BvhHit shadow_hit = IntersectBvh(scene, Ray3f{shadow_origin, wi}, shadow_trace);
        AccumulateTraceStats(stats, shadow_trace);
        if (shadow_hit.hit) {
            ++stats.occluded_shadow_rays;
            continue;
        }

        const float pdf_bsdf = PdfBsdf(material, wo, wi, normal);
        const float mis_weight = PowerHeuristic(light_sample_count, sample.pdf_solid_angle, 1, pdf_bsdf);
        radiance = radiance + Multiply(bsdf, sample.radiance) * (cos_surface * mis_weight / sample.pdf_solid_angle);
    }

    return radiance * inverse_light_sample_count;
}
```

- [ ] **Step 6: Include direct environment lighting in direct estimate**

At the end of `EstimateDirectLight()`, before returning, add:

```cpp
radiance = radiance + EstimateDirectEnvironmentLight(scene, material, hit_point, normal, wo, sampler, stats);
```

- [ ] **Step 7: Run tests and verify pass**

Run:

```powershell
cmake --build build --config Debug --target yaoray_tests
.\build\Debug\yaoray_tests.exe
```

Expected: all unit tests pass.

- [ ] **Step 8: Commit**

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: sample hdri environment in cpu path tracer"
```

---

## Task 6: Showcase Scene And CLI Smoke Test

**Files:**
- Create: `scenes/examples/hdri_lighting_showcase.toml`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add example scene**

Create `scenes/examples/hdri_lighting_showcase.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
sampler = "stratified"
width = 256
height = 256
spp = 16
max_depth = 4
seed = 19
threads = 0
light_samples = 4

[film]
output = "out/hdri_lighting_showcase.png"
tone_mapper = "aces"
exposure = -1.0

[camera]
type = "perspective"
position = [0.0, 1.2, 4.0]
target = [0.0, 0.7, 0.0]
fov_y = 42.0

[environment]
type = "hdri"
path = "assets/env/tiny_studio.hdr"
strength = 1.2
rotation_degrees = 0.0

[[materials]]
name = "matte_white"
type = "diffuse"
albedo = [0.75, 0.75, 0.72]

[[materials]]
name = "mirror"
type = "mirror"
albedo = [0.95, 0.95, 0.95]

[[assets]]
name = "floor"
quads = [
  [[-2.0, 0.0, -2.0], [2.0, 0.0, -2.0], [2.0, 0.0, 2.0], [-2.0, 0.0, 2.0]]
]

[[assets]]
name = "mirror_panel"
quads = [
  [[-0.65, 0.0, 0.0], [0.65, 0.0, 0.0], [0.65, 1.3, 0.0], [-0.65, 1.3, 0.0]]
]

[[instances]]
asset = "floor"
material = "matte_white"

[[instances]]
asset = "mirror_panel"
material = "mirror"
translate = [0.0, 0.0, -0.6]
rotate_degrees = [0.0, 20.0, 0.0]
```

- [ ] **Step 2: Add failing CLI smoke test**

In `CMakeLists.txt`, add a CTest entry after the other render example tests:

```cmake
    add_test(NAME yaoray_cli_render_hdri_showcase
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/out/hdri_lighting_showcase.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/scenes/examples/hdri_lighting_showcase.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Integrator: path') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Shadow rays:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 3: Run CLI smoke test and verify pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_cli_render_hdri_showcase
```

Expected: CTest reports `100% tests passed` for `yaoray_cli_render_hdri_showcase`.

- [ ] **Step 4: Commit**

```powershell
git add CMakeLists.txt scenes/examples/hdri_lighting_showcase.toml
git commit -m "test: add hdri showcase render smoke test"
```

---

## Task 7: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-20-yaoray-hdri-environment-importance-sampling-v1-design.md`

- [ ] **Step 1: Update README capability text**

Find the renderer capability paragraph in `README.md` and update the path tracer description to include:

```markdown
The CPU path integrator supports area-light MIS, Russian roulette, textured
diffuse albedo, glTF base-color sampler state, and HDRI environment lighting
with luminance-weighted equirectangular importance sampling.
```

Add this example command near other scene examples:

```powershell
.\build-release\Release\yaoray.exe render .\scenes\examples\hdri_lighting_showcase.toml --backend cpu
```

Update the limitations list to include:

```markdown
- HDRI environment maps are supported for CPU path tracing, but there are no
  environment mipmaps, portal lights, sun/sky model, EXR support, or CUDA
  environment sampling yet.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, update the CPU backend paragraph to say:

```markdown
`path` is the CPU path tracer: it adds deterministic multi-sample camera jitter,
diffuse and glossy bounce sampling, perfect mirror scattering, emissive hits,
random or stratified XZ-rectangle area-light sampling, direct area-light MIS,
HDRI environment lookup, HDRI direct environment sampling through a
luminance-weighted equirectangular distribution, BSDF-to-environment MIS,
Russian roulette, diffuse texture sampling, and row-major tile threading.
```

Update the asset/material limits paragraph to include:

```markdown
HDRI environment lighting does not yet include mipmapped rough-specular lookup,
portal lights, sun/sky, LDR environment color-space controls, EXR I/O, or CUDA
parity.
```

- [ ] **Step 3: Mark spec as implemented**

Append this section to `docs/superpowers/specs/2026-05-20-yaoray-hdri-environment-importance-sampling-v1-design.md`:

```markdown
## Implementation Status

Implemented in HDRI Environment Importance Sampling v1:

- Radiance `.hdr` loading through stb floating-point decode.
- Equirectangular HDRI evaluation with horizontal rotation.
- Luminance- and solid-angle-weighted environment importance distribution.
- CPU path tracer miss, direct environment lighting, shadow visibility, and
  BSDF-to-environment MIS integration.
- Parser, compiler, texture loader, environment module, path tracer, and CLI
  smoke tests.
```

- [ ] **Step 4: Run docs grep**

Run:

```powershell
rg -n "HDRI|environment importance|environment mipmap|portal|sun/sky|CUDA environment" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-20-yaoray-hdri-environment-importance-sampling-v1-design.md
```

Expected: matches show the new capability and remaining limitations.

- [ ] **Step 5: Commit**

```powershell
git add README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-20-yaoray-hdri-environment-importance-sampling-v1-design.md
git commit -m "docs: document hdri environment lighting"
```

---

## Task 8: Full Verification And Final Cleanup

**Files:**
- Inspect: all changed files

- [ ] **Step 1: Run full Debug build**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build succeeds. Third-party tinygltf or nlohmann warnings are acceptable when they match existing warnings; YaoRay source warnings introduced by this plan are not acceptable.

- [ ] **Step 2: Run full Debug test suite**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 3: Run manual showcase render**

Run:

```powershell
.\build\Debug\yaoray.exe render .\scenes\examples\hdri_lighting_showcase.toml --backend cpu
```

Expected output includes:

```text
Integrator: path
Rendered image: scenes/examples/out/hdri_lighting_showcase.png
Shadow rays:
Rays traced:
```

Expected file: `scenes/examples/out/hdri_lighting_showcase.png`.

- [ ] **Step 4: Inspect changed files**

Run:

```powershell
git status --short
git diff --stat
git diff --check
```

Expected:

- only intended source, test, fixture, scene, docs, and CMake files are changed.
- `git diff --check` reports no whitespace errors.
- local Duck files remain untracked unless the user separately asked to handle their license and commit them.

- [ ] **Step 5: Commit final cleanup when needed**

When Step 4 shows documentation-only, formatting-only, or small test cleanup changes left unstaged, commit them:

```powershell
git add CMakeLists.txt README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-20-yaoray-hdri-environment-importance-sampling-v1-design.md include src tests scenes/examples
git commit -m "chore: finish hdri environment sampling v1"
```

Expected: create this commit only when there are leftover intentional changes after Tasks 1-7.

---

## Execution Notes

- Use TDD at each task boundary: add the test, run it to see the expected failure, implement the smallest passing change, rerun tests, commit.
- Do not commit `scenes/examples/assets/gltf/Duck/` or `scenes/examples/duck_gltf.toml` in this plan.
- Keep `debug_direct` behavior unchanged except where shared environment evaluation helpers are harmlessly available. HDRI direct lighting is for the CPU `path` integrator.
- Keep `render.light_samples` as the only user-facing sample count for both area lights and environment direct samples in this slice.
- Preserve area-light MIS behavior and tests while adding environment MIS.
