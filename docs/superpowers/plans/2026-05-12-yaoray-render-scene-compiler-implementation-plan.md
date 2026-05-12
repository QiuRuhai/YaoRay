# YaoRay RenderScene Compiler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `yaoray_render` module that compiles validated `SceneDescription` values into a minimal renderer-ready `RenderScene`.

**Architecture:** Keep `scene` as the semantic input layer and add `render` as the renderer-facing layer. `SceneCompiler` consumes `SceneDescription`, emits `RenderScene`, and reports unsupported assets through existing `SceneDiagnostic` values. The CLI moves from parse-only to parse-plus-compile while still stopping before actual image rendering.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC, existing YaoRay `core` math and `scene` diagnostics.

---

## Scope Check

This plan implements only the approved RenderScene compiler design:

- `builtin:` asset path preservation in the parser
- `yaoray_render` library
- `RenderScene` data structures
- `SceneCompiler`
- built-in triangle expansion
- compile-time diagnostics for external assets and HDRI environments
- CLI parse-plus-compile wiring
- focused tests and documentation

It does not implement path tracing, BVH construction, glTF/OBJ import, HDRI loading, PNG output, CUDA kernels, or a backend interface.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/render/render_scene.hpp
include/yaoray/render/scene_compiler.hpp
src/app/main.cpp
src/render/scene_compiler.cpp
src/scene/scene_parser.cpp
scenes/examples/minimal.toml
tests/fixtures/scene/builtin_triangle.toml
tests/fixtures/scene/hdri_environment.toml
tests/render_scene_tests.cpp
tests/scene_tests.cpp
```

Responsibilities:

- `include/yaoray/render/render_scene.hpp`: plain renderer-facing scene data.
- `include/yaoray/render/scene_compiler.hpp`: compile result and public compiler API.
- `src/render/scene_compiler.cpp`: scene-to-render-scene conversion, built-in triangle expansion, diagnostics.
- `src/scene/scene_parser.cpp`: preserve `builtin:` asset identifiers.
- `tests/render_scene_tests.cpp`: compiler and render-scene unit coverage.
- `tests/scene_tests.cpp`: parser regression coverage for `builtin:` paths.
- `src/app/main.cpp`: CLI parse-plus-compile flow.

## Task 1: Preserve Built-In Asset Paths

**Files:**
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Add the failing parser regression test**

Append this test to `tests/scene_tests.cpp`:

```cpp
YR_TEST(scene_parser_preserves_builtin_asset_paths) {
    const std::filesystem::path path = WriteTempScene(
        "builtin_asset.toml",
        ValidSceneWith(R"toml(
[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().assets.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().assets[0].path.generic_string(), std::string{"builtin:triangle"});
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `scene_parser_preserves_builtin_asset_paths` fails because `builtin:triangle` is normalized relative to the scene file directory.

- [ ] **Step 3: Preserve `builtin:` values during asset path normalization**

In `src/scene/scene_parser.cpp`, add this helper near `NormalizeScenePath`:

```cpp
bool IsBuiltinAssetPath(std::string_view value) {
    return value.starts_with("builtin:");
}
```

Replace `NormalizeScenePath` with:

```cpp
std::filesystem::path NormalizeScenePath(const std::filesystem::path& scene_dir, const std::string& value) {
    std::filesystem::path path{value};
    if (path.is_relative()) {
        path = scene_dir / path;
    }
    return path.lexically_normal();
}

std::filesystem::path NormalizeAssetPath(const std::filesystem::path& scene_dir, const std::string& value) {
    if (IsBuiltinAssetPath(value)) {
        return std::filesystem::path{value};
    }
    return NormalizeScenePath(scene_dir, value);
}
```

In `ParseAssets`, replace:

```cpp
asset.path = NormalizeScenePath(scene_dir, *path);
```

with:

```cpp
asset.path = NormalizeAssetPath(scene_dir, *path);
```

- [ ] **Step 4: Run tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 5: Commit**

```powershell
git add src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "fix: preserve builtin scene asset paths"
```

## Task 2: Add Render Module And Data Model

**Files:**
- Modify: `CMakeLists.txt`
- Create: `include/yaoray/render/render_scene.hpp`
- Create: `include/yaoray/render/scene_compiler.hpp`
- Create: `src/render/scene_compiler.cpp`
- Create: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add the failing render-scene data tests**

Create `tests/render_scene_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace {

yr::SceneDescription MakeBaseScene() {
    yr::SceneDescription scene;
    scene.source_path = "tests/fixtures/scene/generated.toml";
    scene.render.backend = yr::RenderBackendKind::Cuda;
    scene.render.width = 320;
    scene.render.height = 180;
    scene.render.spp = 4;
    scene.render.max_depth = 6;
    scene.render.seed = std::uint64_t{123};
    scene.camera = yr::CameraDescription{};
    scene.camera->position = yr::Point3f{0.0f, 0.0f, 4.0f};
    scene.camera->target = yr::Point3f{0.0f, 0.0f, 0.0f};
    scene.camera->fov_y = 60.0f;
    return scene;
}

} // namespace

YR_TEST(render_scene_defaults_are_backend_friendly) {
    const yr::RenderScene scene;

    YR_EXPECT_EQ(scene.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.width, 0);
    YR_EXPECT_EQ(scene.height, 0);
    YR_EXPECT_EQ(scene.spp, 1);
    YR_EXPECT_EQ(scene.max_depth, 5);
    YR_EXPECT_EQ(scene.seed, std::uint64_t{0});
    YR_EXPECT_TRUE(scene.triangles.empty());
    YR_EXPECT_TRUE(scene.materials.empty());
    YR_EXPECT_TRUE(scene.area_lights.empty());
}

YR_TEST(scene_compiler_copies_render_settings) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderScene& compiled = result.scene.value();
    YR_EXPECT_EQ(compiled.backend, yr::RenderBackendKind::Cuda);
    YR_EXPECT_EQ(compiled.width, 320);
    YR_EXPECT_EQ(compiled.height, 180);
    YR_EXPECT_EQ(compiled.spp, 4);
    YR_EXPECT_EQ(compiled.max_depth, 6);
    YR_EXPECT_EQ(compiled.seed, std::uint64_t{123});
}
```

- [ ] **Step 2: Add the render target to CMake**

Modify `CMakeLists.txt` after the `yaoray_scene` target:

```cmake
add_library(yaoray_render STATIC
    src/render/scene_compiler.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_scene)
```

Modify the `yaoray_tests` source list to include:

```cmake
    tests/render_scene_tests.cpp
```

Modify the `yaoray_tests` link line to:

```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render)
```

- [ ] **Step 3: Run the build and verify it fails**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `RenderScene` and `CompileScene` are not defined.

- [ ] **Step 4: Add the render-scene public model**

Create `include/yaoray/render/render_scene.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct RenderCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
    float aperture = 0.0f;
    float focus_distance = 1.0f;
};

struct RenderEnvironment {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    float strength = 1.0f;
};

struct RenderMaterial {
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};

struct RenderTriangle {
    Point3f p0;
    Point3f p1;
    Point3f p2;
    Vec3f normal{0.0f, 0.0f, 1.0f};
    int material_index = 0;
};

struct RenderAreaLight {
    Point3f position;
    float width = 1.0f;
    float height = 1.0f;
    Color3f radiance{1.0f, 1.0f, 1.0f};
};

struct RenderScene {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    RenderCamera camera;
    RenderEnvironment environment;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
};

} // namespace yr
```

- [ ] **Step 5: Add the compiler API and minimal implementation**

Create `include/yaoray/render/scene_compiler.hpp`:

```cpp
#pragma once

#include <optional>
#include <vector>

#include <yaoray/render/render_scene.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderScene> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompileScene(const SceneDescription& scene);

} // namespace yr
```

Create `src/render/scene_compiler.cpp`:

```cpp
#include <yaoray/render/scene_compiler.hpp>

namespace yr {

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderScene compiled;
    compiled.backend = scene.render.backend;
    compiled.width = scene.render.width;
    compiled.height = scene.render.height;
    compiled.spp = scene.render.spp;
    compiled.max_depth = scene.render.max_depth;
    compiled.seed = scene.render.seed;
    result.scene = compiled;
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Run tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt include/yaoray/render src/render tests/render_scene_tests.cpp
git commit -m "feat: add render scene module"
```

## Task 3: Compile Camera, Environment, And Area Lights

**Files:**
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing camera, environment, and light tests**

Append to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_builds_camera_basis) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::RenderCamera& camera = result.scene.value().camera;
    YR_EXPECT_NEAR(camera.origin.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.origin.z, 4.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.forward.z, -1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.right.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(camera.up.z, 0.0, 1e-6);
    YR_EXPECT_NEAR(camera.fov_y_radians, 1.04719758, 1e-6);
}

YR_TEST(scene_compiler_copies_constant_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Constant;
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.x, 0.2, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.y, 0.3, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.radiance.z, 0.4, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().environment.strength, 2.0, 1e-6);
}

YR_TEST(scene_compiler_copies_area_lights) {
    yr::SceneDescription scene = MakeBaseScene();
    yr::LightDescription light;
    light.type = yr::LightKind::Area;
    light.area.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.area.size = {4.0f, 5.0f};
    light.area.radiance = yr::Color3f{6.0f, 7.0f, 8.0f};
    scene.lights.push_back(light);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().area_lights.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].position.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].width, 4.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].height, 5.0, 1e-6);
    YR_EXPECT_NEAR(result.scene.value().area_lights[0].radiance.z, 8.0, 1e-6);
}
```

- [ ] **Step 2: Run tests and verify they fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: camera, environment, and area light tests fail because the compiler only copies render settings.

- [ ] **Step 3: Implement camera, environment, and area light compilation**

Replace `src/render/scene_compiler.cpp` with:

```cpp
#include <yaoray/render/scene_compiler.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace yr {
namespace {

constexpr float Pi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * Pi / 180.0f;
}

SceneDiagnostic Error(const SceneDescription& scene, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, scene.source_path, std::move(field), std::move(message)};
}

RenderCamera CompileCamera(const CameraDescription& camera) {
    RenderCamera compiled;
    compiled.origin = camera.position;
    compiled.forward = Normalize(camera.target - camera.position);
    if (LengthSquared(compiled.forward) == 0.0f) {
        compiled.forward = Vec3f{0.0f, 0.0f, -1.0f};
    }
    const Vec3f world_up{0.0f, 1.0f, 0.0f};
    compiled.right = Normalize(Cross(compiled.forward, world_up));
    if (LengthSquared(compiled.right) == 0.0f) {
        compiled.right = Vec3f{1.0f, 0.0f, 0.0f};
    }
    compiled.up = Normalize(Cross(compiled.right, compiled.forward));
    compiled.fov_y_radians = DegreesToRadians(camera.fov_y);
    compiled.aperture = camera.aperture;
    compiled.focus_distance = camera.focus_distance;
    return compiled;
}

void CopyAreaLights(const SceneDescription& scene, RenderScene& compiled) {
    for (const LightDescription& light : scene.lights) {
        if (light.type != LightKind::Area) {
            continue;
        }
        compiled.area_lights.push_back(RenderAreaLight{
            light.area.position,
            light.area.size[0],
            light.area.size[1],
            light.area.radiance
        });
    }
}

} // namespace

SceneCompileResult CompileScene(const SceneDescription& scene) {
    SceneCompileResult result;
    RenderScene compiled;
    compiled.backend = scene.render.backend;
    compiled.width = scene.render.width;
    compiled.height = scene.render.height;
    compiled.spp = scene.render.spp;
    compiled.max_depth = scene.render.max_depth;
    compiled.seed = scene.render.seed;

    if (!scene.camera.has_value()) {
        result.diagnostics.push_back(Error(scene, "camera", "missing camera"));
    } else {
        compiled.camera = CompileCamera(scene.camera.value());
    }

    if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
        compiled.environment.type = scene.environment.type;
        compiled.environment.radiance = scene.environment.radiance;
        compiled.environment.strength = scene.environment.strength;
    }

    CopyAreaLights(scene, compiled);

    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }
    result.scene = compiled;
    return result;
}

} // namespace yr
```

- [ ] **Step 4: Run tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 5: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile scene camera and lights"
```

## Task 4: Compile Built-In Triangle Instances

**Files:**
- Create: `tests/fixtures/scene/builtin_triangle.toml`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add the built-in triangle fixture**

Create `tests/fixtures/scene/builtin_triangle.toml`:

```toml
[render]
backend = "cpu"
width = 64
height = 32

[film]
output = "out/builtin.png"

[camera]
type = "perspective"
position = [0, 0, 4]
target = [0, 0, 0]
fov_y = 60

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[environment]
type = "constant"
radiance = [0.02, 0.02, 0.02]
```

- [ ] **Step 2: Add failing built-in triangle tests**

Append to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_expands_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().materials.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});

    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, -0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p0.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(triangle.p1.x, 0.5, 1e-6);
    YR_EXPECT_NEAR(triangle.p2.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(triangle.normal.z, 1.0, 1e-6);
    YR_EXPECT_EQ(triangle.material_index, 0);
}

YR_TEST(scene_compiler_applies_builtin_triangle_transform) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    yr::InstanceDescription instance;
    instance.asset = "triangle";
    instance.transform.translate = yr::Vec3f{1.0f, 2.0f, 3.0f};
    instance.transform.rotate_degrees = yr::Vec3f{0.0f, 0.0f, 90.0f};
    instance.transform.scale = yr::Vec3f{2.0f, 1.0f, 1.0f};
    scene.instances.push_back(instance);

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderTriangle& triangle = result.scene.value().triangles[0];
    YR_EXPECT_NEAR(triangle.p0.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.y, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p0.z, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.x, 1.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p1.y, 3.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.x, 0.0, 1e-5);
    YR_EXPECT_NEAR(triangle.p2.y, 2.0, 1e-5);
}
```

- [ ] **Step 3: Run tests and verify they fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: built-in triangle tests fail because asset instances are not compiled yet.

- [ ] **Step 4: Implement built-in triangle expansion**

In `src/render/scene_compiler.cpp`, add includes:

```cpp
#include <filesystem>
#include <string>
#include <unordered_map>
```

Add these helpers inside the anonymous namespace:

```cpp
Vec3f RotateX(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x, value.y * c - value.z * s, value.y * s + value.z * c};
}

Vec3f RotateY(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

Vec3f RotateZ(Vec3f value, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3f{value.x * c - value.y * s, value.x * s + value.y * c, value.z};
}

Point3f ApplyTransform(Point3f point, const TransformDescription& transform) {
    Vec3f value{
        point.x * transform.scale.x,
        point.y * transform.scale.y,
        point.z * transform.scale.z
    };
    value = RotateX(value, DegreesToRadians(transform.rotate_degrees.x));
    value = RotateY(value, DegreesToRadians(transform.rotate_degrees.y));
    value = RotateZ(value, DegreesToRadians(transform.rotate_degrees.z));
    return Point3f{
        value.x + transform.translate.x,
        value.y + transform.translate.y,
        value.z + transform.translate.z
    };
}

std::unordered_map<std::string, std::filesystem::path> BuildAssetMap(const SceneDescription& scene) {
    std::unordered_map<std::string, std::filesystem::path> assets;
    for (const AssetDescription& asset : scene.assets) {
        assets.emplace(asset.name, asset.path);
    }
    return assets;
}

void AppendBuiltinTriangle(RenderScene& compiled, const TransformDescription& transform) {
    constexpr Point3f p0{-0.5f, 0.0f, 0.0f};
    constexpr Point3f p1{0.5f, 0.0f, 0.0f};
    constexpr Point3f p2{0.0f, 1.0f, 0.0f};

    const Point3f world_p0 = ApplyTransform(p0, transform);
    const Point3f world_p1 = ApplyTransform(p1, transform);
    const Point3f world_p2 = ApplyTransform(p2, transform);

    const int material_index = static_cast<int>(compiled.materials.size());
    compiled.materials.push_back(RenderMaterial{});
    compiled.triangles.push_back(RenderTriangle{
        world_p0,
        world_p1,
        world_p2,
        Normalize(Cross(world_p1 - world_p0, world_p2 - world_p0)),
        material_index
    });
}
```

Before the final error check in `CompileScene`, add:

```cpp
const std::unordered_map<std::string, std::filesystem::path> assets = BuildAssetMap(scene);
for (const InstanceDescription& instance : scene.instances) {
    const auto asset = assets.find(instance.asset);
    if (asset == assets.end()) {
        result.diagnostics.push_back(Error(scene, "instances.asset", "references unknown asset"));
        continue;
    }

    const std::string asset_path = asset->second.generic_string();
    if (asset_path == "builtin:triangle") {
        AppendBuiltinTriangle(compiled, instance.transform);
    }
}
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp tests/fixtures/scene/builtin_triangle.toml
git commit -m "feat: compile builtin triangle scenes"
```

## Task 5: Add Unsupported Asset And HDRI Diagnostics

**Files:**
- Create: `tests/fixtures/scene/hdri_environment.toml`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add the HDRI fixture**

Create `tests/fixtures/scene/hdri_environment.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/hdri.png"

[camera]
type = "perspective"
position = [0, 0, 4]
target = [0, 0, 0]
fov_y = 60

[environment]
type = "hdri"
path = "assets/hdri/studio.hdr"
strength = 1.0
```

- [ ] **Step 2: Add failing diagnostic tests**

Append to `tests/render_scene_tests.cpp`:

```cpp
namespace {

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    std::string_view field,
    std::string_view message
) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_compiler_rejects_external_assets) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"model", "assets/model.glb"});
    scene.instances.push_back(yr::InstanceDescription{"model", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.path", "asset import not implemented yet"));
}

YR_TEST(scene_compiler_rejects_hdri_environment) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.environment.type = yr::EnvironmentKind::Hdri;
    scene.environment.path = "assets/hdri/studio.hdr";

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "environment.path", "HDRI environment import not implemented yet"));
}
```

If a `DiagnosticsContain` helper already exists in this file after local edits, keep one copy and place it in the existing anonymous namespace.

- [ ] **Step 3: Run tests and verify they fail**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: external asset and HDRI tests fail because the compiler does not diagnose those unsupported inputs.

- [ ] **Step 4: Emit unsupported-input diagnostics**

In `CompileScene`, replace the environment block with:

```cpp
if (scene.environment.type == EnvironmentKind::None || scene.environment.type == EnvironmentKind::Constant) {
    compiled.environment.type = scene.environment.type;
    compiled.environment.radiance = scene.environment.radiance;
    compiled.environment.strength = scene.environment.strength;
} else if (scene.environment.type == EnvironmentKind::Hdri) {
    result.diagnostics.push_back(Error(scene, "environment.path", "HDRI environment import not implemented yet"));
}
```

In the instance compilation loop, after the `builtin:triangle` branch, add the external asset diagnostic:

```cpp
if (asset_path == "builtin:triangle") {
    AppendBuiltinTriangle(compiled, instance.transform);
} else {
    result.diagnostics.push_back(Error(scene, "assets.path", "asset import not implemented yet: " + asset_path));
}
```

Keep the final error check as:

```cpp
if (HasSceneErrors(result.diagnostics)) {
    return result;
}
result.scene = compiled;
return result;
```

- [ ] **Step 5: Run tests**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

```powershell
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp tests/fixtures/scene/hdri_environment.toml
git commit -m "feat: diagnose unsupported scene compile inputs"
```

## Task 6: Wire CLI Through Scene Compilation

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/main.cpp`
- Modify: `scenes/examples/minimal.toml`

- [ ] **Step 1: Update CLI CTest coverage**

Modify the `yaoray` target link line in `CMakeLists.txt`:

```cmake
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render)
```

Modify the successful render CLI tests to use `tests/fixtures/scene/builtin_triangle.toml`:

```cmake
add_test(NAME yaoray_cli_render_cpu COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml --backend cpu)
set_tests_properties(yaoray_cli_render_cpu PROPERTIES PASS_REGULAR_EXPRESSION "Compiled triangles: 1")
add_test(NAME yaoray_cli_render_cuda COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml --backend cuda)
set_tests_properties(yaoray_cli_render_cuda PROPERTIES PASS_REGULAR_EXPRESSION "Requested backend: cuda")
```

Add an external-asset failure test:

```cmake
add_test(NAME yaoray_cli_render_unsupported_asset
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
        "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml' 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'asset import not implemented yet') { exit 1 }"
)
```

- [ ] **Step 2: Run CTest and verify CLI failures**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CLI render success tests fail because `src/app/main.cpp` has not called `CompileScene()` yet.

- [ ] **Step 3: Update the render command implementation**

In `src/app/main.cpp`, add:

```cpp
#include <yaoray/render/scene_compiler.hpp>
```

After backend override handling in `RunRender`, replace the current success output block with:

```cpp
    const yr::SceneCompileResult compile_result = yr::CompileScene(scene);
    if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
        return 1;
    }

    const yr::RenderScene& render_scene = compile_result.scene.value();
    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
    std::cout << "Rendering is not implemented yet.\n";
    return 0;
```

- [ ] **Step 4: Update the human-facing example scene**

Replace `scenes/examples/minimal.toml` with:

```toml
[render]
backend = "cpu"
width = 640
height = 360
spp = 1
max_depth = 5
seed = 42

[film]
output = "out/minimal.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0, 0, 4]
target = [0, 0, 0]
fov_y = 60

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[environment]
type = "constant"
radiance = [0.02, 0.025, 0.03]
strength = 1.0
```

- [ ] **Step 5: Run tests and manual CLI checks**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cuda
```

Expected CTest result:

```text
100% tests passed
```

Expected manual CPU output includes:

```text
Scene compiled successfully.
Requested backend: cpu
Compiled triangles: 1
Rendering is not implemented yet.
```

Expected manual CUDA output includes:

```text
Requested backend: cuda
Compiled triangles: 1
```

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt src/app/main.cpp scenes/examples/minimal.toml
git commit -m "feat: compile scenes from render cli"
```

## Task 7: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README status and run instructions**

Edit `README.md` so the current status section lists:

```markdown
- TOML scene parsing and validation through `yaoray render`
- initial `RenderScene` compilation through the `yaoray_render` module
- temporary `builtin:triangle` scenes for compiler and CLI verification
```

Edit the render command description so it says:

```markdown
The `render` command currently parses and compiles scene files, then reports the requested backend and compiled triangle count. It does not render images yet.
```

- [ ] **Step 2: Update architecture overview**

Replace `docs/architecture/overview.md` with:

```markdown
# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings. The current implementation parses this semantic layer from TOML scene files into `SceneDescription`.

The render layer compiles that semantic scene into backend-friendly data. The current `yaoray_render` slice provides a minimal `RenderScene` with render settings, camera data, environment data, area lights, materials, and flat world-space triangles. CPU, CUDA, and future OptiX backends will consume this compiled representation.

Current implemented slices:

- project structure, tests, core math primitives, Film accumulation, and CLI shell
- TOML scene parsing, validation diagnostics, and `yaoray render` command shell
- initial `RenderScene` compilation with temporary `builtin:triangle` asset support

Rendering backends, asset import, BVH construction, and image output will be added in focused implementation plans.
```

- [ ] **Step 3: Run documentation-adjacent smoke checks**

Run:

```powershell
rg -n "parse|compile|builtin:triangle|RenderScene" README.md docs/architecture/overview.md scenes/examples/minimal.toml
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `rg` output mentions parse, compile, `builtin:triangle`, and `RenderScene`; CTest passes.

- [ ] **Step 4: Commit**

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: describe render scene compilation"
```

## Task 8: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm dependency direction**

Run:

```powershell
rg -n "yaoray/render|render_scene|scene_compiler" include/yaoray/scene src/scene
```

Expected: no output. The `scene` module must not include or reference the `render` module.

- [ ] **Step 2: Confirm render module owns compiler API**

Run:

```powershell
rg -n "CompileScene|RenderScene|builtin:triangle|asset import not implemented yet" include/yaoray/render src/render tests src/app/main.cpp
```

Expected: matches in render headers, render source, render tests, and CLI only.

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

- [ ] **Step 4: Verify CLI success and failure manually**

Run:

```powershell
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cuda
.\build\Debug\yaoray.exe render tests\fixtures\scene\minimal.toml --backend cpu
```

Expected first command includes:

```text
Requested backend: cpu
Compiled triangles: 1
Rendering is not implemented yet.
```

Expected second command includes:

```text
Requested backend: cuda
Compiled triangles: 1
```

Expected third command exits non-zero and includes:

```text
asset import not implemented yet
```

- [ ] **Step 5: Confirm clean worktree**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- `builtin:` parser preservation: Task 1.
- `yaoray_render` target and public model: Task 2.
- camera, environment, and area light compilation: Task 3.
- built-in triangle expansion and transforms: Task 4.
- unsupported external assets and HDRI diagnostics: Task 5.
- CLI parse-plus-compile behavior: Task 6.
- docs: Task 7.
- final verification: Task 8.

Type consistency:

- `SceneCompileResult` is declared in `include/yaoray/render/scene_compiler.hpp`.
- `RenderScene` is declared in `include/yaoray/render/render_scene.hpp`.
- `CompileScene(const SceneDescription&)` is the only public compiler entry point.
- Diagnostics use the existing `SceneDiagnostic` and `HasSceneErrors()` APIs.

Implementation guardrails:

- Do not add an asset importer in this plan.
- Do not add a backend interface in this plan.
- Do not make `scene` depend on `render`.
- Keep `builtin:triangle` exact and temporary.
