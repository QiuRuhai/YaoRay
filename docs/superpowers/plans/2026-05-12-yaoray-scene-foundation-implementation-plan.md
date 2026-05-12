# YaoRay Scene Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build YaoRay's semantic scene input layer, TOML scene parser, strict validation diagnostics, and `yaoray render` CLI command shell.

**Architecture:** Add a focused `yaoray_scene` library that owns `SceneDescription`, diagnostics, TOML parsing, and validation. Keep `toml++` private to `src/scene/scene_parser.cpp`; public headers expose only YaoRay types. The CLI reads and validates scenes, reports the requested backend, and explicitly states that rendering is not implemented in this slice.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC, vendored `toml++` v3.4.0 single-header parser.

---

## Scope Check

This plan implements only the approved scene foundation design:

- `scene` module data model
- `toml++` vendored header integration
- scene TOML parsing
- strict validation and structured diagnostics
- `yaoray render <scene.toml> --backend cpu|cuda`
- scene fixtures, CLI smoke tests, README, and architecture docs

It intentionally does not implement path tracing, asset file loading, image writing, BVH construction, or CPU/CUDA render backends.

## File Structure

Create or modify these files:

```text
.gitignore
CMakeLists.txt
README.md
docs/architecture/overview.md
external/tomlplusplus/README.md
external/tomlplusplus/toml.hpp
include/yaoray/scene/diagnostic.hpp
include/yaoray/scene/scene.hpp
include/yaoray/scene/scene_parser.hpp
src/app/main.cpp
src/scene/diagnostic.cpp
src/scene/scene.cpp
src/scene/scene_parser.cpp
scenes/examples/minimal.toml
tests/fixtures/scene/bad_syntax.toml
tests/fixtures/scene/defaults.toml
tests/fixtures/scene/duplicate_asset.toml
tests/fixtures/scene/empty_scene.toml
tests/fixtures/scene/invalid_width.toml
tests/fixtures/scene/minimal.toml
tests/fixtures/scene/missing_asset_reference.toml
tests/fixtures/scene/missing_camera.toml
tests/fixtures/scene/missing_render.toml
tests/fixtures/scene/short_position.toml
tests/fixtures/scene/unknown_field.toml
tests/scene_tests.cpp
```

Responsibilities:

- `include/yaoray/scene/scene.hpp`: plain semantic scene data and enum/string conversion declarations.
- `include/yaoray/scene/diagnostic.hpp`: structured diagnostic types and formatting declarations.
- `include/yaoray/scene/scene_parser.hpp`: file loading API and backend override API.
- `src/scene/scene.cpp`: enum/string conversion and small data-model helpers.
- `src/scene/diagnostic.cpp`: diagnostic formatting helpers.
- `src/scene/scene_parser.cpp`: all TOML parsing and validation; the only YaoRay source file that includes `toml.hpp`.
- `src/app/main.cpp`: CLI argument routing and render command user-facing output.
- `tests/scene_tests.cpp`: unit coverage for parsing, defaults, and validation diagnostics.
- `tests/fixtures/scene/*.toml`: parser fixtures for success and failure cases.
- `scenes/examples/minimal.toml`: human-facing example scene.

## Task 1: Add `toml++` And Scene Build Target

**Files:**
- Modify: `.gitignore`
- Modify: `CMakeLists.txt`
- Create: `external/tomlplusplus/README.md`
- Create: `external/tomlplusplus/toml.hpp`

- [ ] **Step 1: Confirm `.cache/` is ignored**

`.gitignore` should include `.cache/` under build outputs:

```gitignore
# Build outputs
build/
build-*/
_build/
cmake-build-*/
out/
.cache/
```

Run:

```powershell
git check-ignore -v .cache
```

Expected: output includes `.gitignore` and `.cache/`.

- [ ] **Step 2: Vendor `toml++` v3.4.0 single header**

Run:

```powershell
New-Item -ItemType Directory -Force -Path external\tomlplusplus | Out-Null
Invoke-WebRequest `
  -Uri 'https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp' `
  -OutFile 'external\tomlplusplus\toml.hpp'
```

Create `external/tomlplusplus/README.md`:

```markdown
# toml++

This directory vendors the toml++ single-header parser for YaoRay scene files.

- Upstream: https://github.com/marzer/tomlplusplus
- Vendored version: v3.4.0
- Vendored file: `toml.hpp`
- License: MIT, included in the header comment

YaoRay includes this header only from scene parser implementation files. Public YaoRay headers must not expose toml++ types.
```

Run:

```powershell
Select-String -Path external\tomlplusplus\toml.hpp -Pattern 'toml\+\+ v3.4.0'
```

Expected: one match near the top of the header.

- [ ] **Step 3: Add an empty scene target to CMake**

Replace `CMakeLists.txt` with:

```cmake
cmake_minimum_required(VERSION 3.24)

project(YaoRay
    VERSION 0.1.0
    DESCRIPTION "Physically based offline path tracer"
    LANGUAGES CXX
)

option(YAORAY_ENABLE_CUDA "Build the CUDA backend when available" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE external/tomlplusplus)

add_library(yaoray_core STATIC
    src/core/version.cpp
)
target_include_directories(yaoray_core PUBLIC include)

add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)
target_link_libraries(yaoray_film PUBLIC yaoray_core)

add_library(yaoray_scene STATIC
    src/scene/diagnostic.cpp
    src/scene/scene.cpp
    src/scene/scene_parser.cpp
)
target_include_directories(yaoray_scene PUBLIC include)
target_link_libraries(yaoray_scene PUBLIC yaoray_core PRIVATE tomlplusplus)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/film_tests.cpp
    tests/scene_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene)

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
    add_test(NAME yaoray_cli_help COMMAND yaoray --help)
    set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
    add_test(NAME yaoray_cli_version COMMAND yaoray --version)
    set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
endif()
```

- [ ] **Step 4: Add temporary empty source files so CMake can configure**

Create `src/scene/diagnostic.cpp`:

```cpp
#include <yaoray/scene/diagnostic.hpp>
```

Create `src/scene/scene.cpp`:

```cpp
#include <yaoray/scene/scene.hpp>
```

Create `src/scene/scene_parser.cpp`:

```cpp
#include <yaoray/scene/scene_parser.hpp>
```

Create temporary headers that later tasks will replace:

`include/yaoray/scene/diagnostic.hpp`

```cpp
#pragma once

namespace yr {
} // namespace yr
```

`include/yaoray/scene/scene.hpp`

```cpp
#pragma once

namespace yr {
} // namespace yr
```

`include/yaoray/scene/scene_parser.hpp`

```cpp
#pragma once

namespace yr {
} // namespace yr
```

Create temporary `tests/scene_tests.cpp`:

```cpp
#include "yr_test.hpp"

YR_TEST(scene_test_target_is_wired) {
    YR_EXPECT_TRUE(true);
}
```

- [ ] **Step 5: Build and run tests**

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

```powershell
git add .gitignore CMakeLists.txt external/tomlplusplus include/yaoray/scene src/scene tests/scene_tests.cpp
git commit -m "build: add scene module and tomlplusplus"
```

## Task 2: Add Scene Data Model And Diagnostics

**Files:**
- Replace: `include/yaoray/scene/scene.hpp`
- Replace: `include/yaoray/scene/diagnostic.hpp`
- Replace: `src/scene/scene.cpp`
- Replace: `src/scene/diagnostic.cpp`
- Replace: `tests/scene_tests.cpp`

- [ ] **Step 1: Write failing data-model tests**

Replace `tests/scene_tests.cpp` with:

```cpp
#include "yr_test.hpp"

#include <string>
#include <string_view>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

YR_TEST(scene_defaults_match_schema) {
    const yr::SceneDescription scene;

    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.width, 0);
    YR_EXPECT_EQ(scene.render.height, 0);
    YR_EXPECT_EQ(scene.render.spp, 1);
    YR_EXPECT_EQ(scene.render.max_depth, 5);
    YR_EXPECT_EQ(scene.film.tone_mapper, yr::ToneMapperKind::Aces);
    YR_EXPECT_NEAR(scene.film.exposure, 0.0, 1e-6);
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::None);
}

YR_TEST(scene_enum_names_are_stable) {
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cpu), std::string_view{"cpu"});
    YR_EXPECT_EQ(yr::RenderBackendName(yr::RenderBackendKind::Cuda), std::string_view{"cuda"});
    YR_EXPECT_EQ(yr::ToneMapperName(yr::ToneMapperKind::Aces), std::string_view{"aces"});
    YR_EXPECT_EQ(yr::EnvironmentKindName(yr::EnvironmentKind::Constant), std::string_view{"constant"});
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cpu").value(), yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(yr::ParseRenderBackendName("cuda").value(), yr::RenderBackendKind::Cuda);
    YR_EXPECT_TRUE(!yr::ParseRenderBackendName("metal").has_value());
}

YR_TEST(scene_diagnostics_format_field_messages) {
    const yr::SceneDiagnostic diagnostic{
        yr::DiagnosticSeverity::Error,
        "scenes/examples/minimal.toml",
        "camera.position",
        "missing required field"
    };

    const std::string text = yr::FormatSceneDiagnostic(diagnostic);

    YR_EXPECT_TRUE(text.find("Scene error in scenes/examples/minimal.toml:") != std::string::npos);
    YR_EXPECT_TRUE(text.find("[camera.position] missing required field") != std::string::npos);
}
```

- [ ] **Step 2: Run build to verify it fails**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because scene types and functions are not defined.

- [ ] **Step 3: Implement the scene data model**

Replace `include/yaoray/scene/scene.hpp` with:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

enum class RenderBackendKind {
    Cpu,
    Cuda,
};

enum class ToneMapperKind {
    None,
    Reinhard,
    Aces,
};

enum class CameraKind {
    Perspective,
};

enum class LightKind {
    Area,
};

enum class EnvironmentKind {
    None,
    Constant,
    Hdri,
};

struct RenderSettings {
    RenderBackendKind backend = RenderBackendKind::Cpu;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
};

struct FilmSettings {
    std::filesystem::path output;
    ToneMapperKind tone_mapper = ToneMapperKind::Aces;
    float exposure = 0.0f;
    int checkpoint_interval_s = 0;
    std::filesystem::path checkpoint_path;
};

struct CameraDescription {
    CameraKind type = CameraKind::Perspective;
    Point3f position;
    Point3f target;
    float fov_y = 45.0f;
    float aperture = 0.0f;
    float focus_distance = 1.0f;
};

struct AssetDescription {
    std::string name;
    std::filesystem::path path;
};

struct TransformDescription {
    Vec3f translate;
    Vec3f rotate_degrees;
    Vec3f scale{1.0f, 1.0f, 1.0f};
};

struct InstanceDescription {
    std::string asset;
    TransformDescription transform;
};

struct AreaLightDescription {
    Point3f position;
    std::array<float, 2> size{1.0f, 1.0f};
    Color3f radiance{1.0f, 1.0f, 1.0f};
};

struct LightDescription {
    LightKind type = LightKind::Area;
    AreaLightDescription area;
};

struct EnvironmentDescription {
    EnvironmentKind type = EnvironmentKind::None;
    Color3f radiance;
    std::filesystem::path path;
    float strength = 1.0f;
};

struct SceneDescription {
    std::filesystem::path source_path;
    RenderSettings render;
    FilmSettings film;
    std::optional<CameraDescription> camera;
    std::vector<AssetDescription> assets;
    std::vector<InstanceDescription> instances;
    std::vector<LightDescription> lights;
    EnvironmentDescription environment;
};

std::string_view RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name);

std::string_view ToneMapperName(ToneMapperKind mapper);
std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name);

std::string_view CameraKindName(CameraKind kind);
std::optional<CameraKind> ParseCameraKindName(std::string_view name);

std::string_view LightKindName(LightKind kind);
std::optional<LightKind> ParseLightKindName(std::string_view name);

std::string_view EnvironmentKindName(EnvironmentKind kind);
std::optional<EnvironmentKind> ParseEnvironmentKindName(std::string_view name);

} // namespace yr
```

Replace `src/scene/scene.cpp` with:

```cpp
#include <yaoray/scene/scene.hpp>

namespace yr {

std::string_view RenderBackendName(RenderBackendKind backend) {
    switch (backend) {
        case RenderBackendKind::Cpu:
            return "cpu";
        case RenderBackendKind::Cuda:
            return "cuda";
    }
    return "cpu";
}

std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name) {
    if (name == "cpu") {
        return RenderBackendKind::Cpu;
    }
    if (name == "cuda") {
        return RenderBackendKind::Cuda;
    }
    return std::nullopt;
}

std::string_view ToneMapperName(ToneMapperKind mapper) {
    switch (mapper) {
        case ToneMapperKind::None:
            return "none";
        case ToneMapperKind::Reinhard:
            return "reinhard";
        case ToneMapperKind::Aces:
            return "aces";
    }
    return "aces";
}

std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name) {
    if (name == "none") {
        return ToneMapperKind::None;
    }
    if (name == "reinhard") {
        return ToneMapperKind::Reinhard;
    }
    if (name == "aces") {
        return ToneMapperKind::Aces;
    }
    return std::nullopt;
}

std::string_view CameraKindName(CameraKind kind) {
    switch (kind) {
        case CameraKind::Perspective:
            return "perspective";
    }
    return "perspective";
}

std::optional<CameraKind> ParseCameraKindName(std::string_view name) {
    if (name == "perspective") {
        return CameraKind::Perspective;
    }
    return std::nullopt;
}

std::string_view LightKindName(LightKind kind) {
    switch (kind) {
        case LightKind::Area:
            return "area";
    }
    return "area";
}

std::optional<LightKind> ParseLightKindName(std::string_view name) {
    if (name == "area") {
        return LightKind::Area;
    }
    return std::nullopt;
}

std::string_view EnvironmentKindName(EnvironmentKind kind) {
    switch (kind) {
        case EnvironmentKind::None:
            return "none";
        case EnvironmentKind::Constant:
            return "constant";
        case EnvironmentKind::Hdri:
            return "hdri";
    }
    return "none";
}

std::optional<EnvironmentKind> ParseEnvironmentKindName(std::string_view name) {
    if (name == "none") {
        return EnvironmentKind::None;
    }
    if (name == "constant") {
        return EnvironmentKind::Constant;
    }
    if (name == "hdri") {
        return EnvironmentKind::Hdri;
    }
    return std::nullopt;
}

} // namespace yr
```

- [ ] **Step 4: Implement diagnostics**

Replace `include/yaoray/scene/diagnostic.hpp` with:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yr {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct SceneDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::filesystem::path file;
    std::string field;
    std::string message;
};

bool HasSceneErrors(const std::vector<SceneDiagnostic>& diagnostics);
std::string FormatSceneDiagnostic(const SceneDiagnostic& diagnostic);
std::string FormatSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics);

} // namespace yr
```

Replace `src/scene/diagnostic.cpp` with:

```cpp
#include <yaoray/scene/diagnostic.hpp>

#include <sstream>

namespace yr {

bool HasSceneErrors(const std::vector<SceneDiagnostic>& diagnostics) {
    for (const SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

std::string FormatSceneDiagnostic(const SceneDiagnostic& diagnostic) {
    std::ostringstream out;
    out << (diagnostic.severity == DiagnosticSeverity::Error ? "Scene error" : "Scene warning");
    if (!diagnostic.file.empty()) {
        out << " in " << diagnostic.file.generic_string();
    }
    out << ":\n";
    if (!diagnostic.field.empty()) {
        out << "  [" << diagnostic.field << "] ";
    } else {
        out << "  ";
    }
    out << diagnostic.message;
    return out.str();
}

std::string FormatSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics) {
    std::ostringstream out;
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            out << '\n';
        }
        out << FormatSceneDiagnostic(diagnostics[i]);
    }
    return out.str();
}

} // namespace yr
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
git add include/yaoray/scene src/scene tests/scene_tests.cpp
git commit -m "feat: add scene data model"
```

## Task 3: Parse Valid Scene Files And Defaults

**Files:**
- Replace: `include/yaoray/scene/scene_parser.hpp`
- Replace: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`
- Create: `tests/fixtures/scene/minimal.toml`
- Create: `tests/fixtures/scene/defaults.toml`

- [ ] **Step 1: Add success fixtures**

Create `tests/fixtures/scene/minimal.toml`:

```toml
[render]
backend = "cpu"
width = 1280
height = 720
spp = 64
max_depth = 8
seed = 42

[film]
output = "out/example.png"
tone_mapper = "aces"
exposure = 0.0
checkpoint_interval_s = 0
checkpoint_path = ""

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
aperture = 0.0
focus_distance = 4.0

[[assets]]
name = "model"
path = "assets/models/model.glb"

[[instances]]
asset = "model"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]

[[lights]]
type = "area"
position = [0, 4, 2]
size = [2, 2]
radiance = [8, 7, 6]

[environment]
type = "constant"
radiance = [0.02, 0.025, 0.03]
strength = 1.0
```

Create `tests/fixtures/scene/defaults.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/defaults.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

- [ ] **Step 2: Add failing parser tests**

Append to `tests/scene_tests.cpp`:

```cpp
#include <yaoray/scene/scene_parser.hpp>

namespace {

std::filesystem::path SceneFixture(std::string_view name) {
    return std::filesystem::path{"tests"} / "fixtures" / "scene" / std::string{name};
}

} // namespace

YR_TEST(scene_parser_loads_complete_scene) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("minimal.toml"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.width, 1280);
    YR_EXPECT_EQ(scene.render.height, 720);
    YR_EXPECT_EQ(scene.render.spp, 64);
    YR_EXPECT_EQ(scene.render.max_depth, 8);
    YR_EXPECT_EQ(scene.render.seed, static_cast<std::uint64_t>(42));
    YR_EXPECT_EQ(scene.film.output.generic_string(), std::string{"out/example.png"});
    YR_EXPECT_TRUE(scene.camera.has_value());
    YR_EXPECT_NEAR(scene.camera->position.z, 4.0, 1e-6);
    YR_EXPECT_EQ(scene.assets.size(), static_cast<std::size_t>(1));
    YR_EXPECT_EQ(scene.instances.size(), static_cast<std::size_t>(1));
    YR_EXPECT_EQ(scene.lights.size(), static_cast<std::size_t>(1));
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::Constant);
}

YR_TEST(scene_parser_applies_defaults) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("defaults.toml"));

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());

    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.render.backend, yr::RenderBackendKind::Cpu);
    YR_EXPECT_EQ(scene.render.spp, 1);
    YR_EXPECT_EQ(scene.render.max_depth, 5);
    YR_EXPECT_EQ(scene.film.tone_mapper, yr::ToneMapperKind::Aces);
    YR_EXPECT_NEAR(scene.film.exposure, 0.0, 1e-6);
    YR_EXPECT_EQ(scene.film.checkpoint_interval_s, 0);
    YR_EXPECT_EQ(scene.environment.type, yr::EnvironmentKind::Constant);
    YR_EXPECT_NEAR(scene.environment.strength, 1.0, 1e-6);
}
```

- [ ] **Step 3: Run build to verify it fails**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `SceneLoadResult` and `LoadSceneFile()` are not defined.

- [ ] **Step 4: Add parser API**

Replace `include/yaoray/scene/scene_parser.hpp` with:

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene.hpp>

namespace yr {

struct SceneLoadResult {
    std::optional<SceneDescription> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneLoadResult LoadSceneFile(const std::filesystem::path& path);
void ApplyBackendOverride(SceneDescription& scene, RenderBackendKind backend);

} // namespace yr
```

- [ ] **Step 5: Implement valid scene parsing**

Replace `src/scene/scene_parser.cpp` with an implementation that:

```cpp
#include <yaoray/scene/scene_parser.hpp>

#include <toml.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace yr {
namespace {

SceneDiagnostic Error(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Error, file, std::move(field), std::move(message)};
}

bool HasKey(const toml::table& table, std::string_view key) {
    return table.contains(std::string{key});
}

std::optional<std::string> ReadString(const toml::table& table, std::string_view key) {
    if (const toml::node* node = table.get(std::string{key})) {
        if (auto value = node->value<std::string>()) {
            return *value;
        }
    }
    return std::nullopt;
}

std::optional<double> ReadNumberNode(const toml::node& node) {
    if (auto value = node.value<double>()) {
        return *value;
    }
    if (auto value = node.value<std::int64_t>()) {
        return static_cast<double>(*value);
    }
    return std::nullopt;
}

std::optional<double> ReadNumber(const toml::table& table, std::string_view key) {
    if (const toml::node* node = table.get(std::string{key})) {
        return ReadNumberNode(*node);
    }
    return std::nullopt;
}

std::optional<int> ReadInt(const toml::table& table, std::string_view key) {
    if (const toml::node* node = table.get(std::string{key})) {
        if (auto value = node->value<std::int64_t>()) {
            return static_cast<int>(*value);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> ReadUint64(const toml::table& table, std::string_view key) {
    if (const toml::node* node = table.get(std::string{key})) {
        if (auto value = node->value<std::int64_t>()) {
            if (*value >= 0) {
                return static_cast<std::uint64_t>(*value);
            }
        }
    }
    return std::nullopt;
}

std::optional<Vec3f> ReadVec3(const toml::table& table, std::string_view key) {
    const toml::node* node = table.get(std::string{key});
    if (node == nullptr) {
        return std::nullopt;
    }
    const toml::array* array = node->as_array();
    if (array == nullptr || array->size() != 3) {
        return std::nullopt;
    }

    const auto x = ReadNumberNode(*array->get(0));
    const auto y = ReadNumberNode(*array->get(1));
    const auto z = ReadNumberNode(*array->get(2));
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return Vec3f{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
}

std::optional<std::array<float, 2>> ReadVec2(const toml::table& table, std::string_view key) {
    const toml::node* node = table.get(std::string{key});
    if (node == nullptr) {
        return std::nullopt;
    }
    const toml::array* array = node->as_array();
    if (array == nullptr || array->size() != 2) {
        return std::nullopt;
    }

    const auto x = ReadNumberNode(*array->get(0));
    const auto y = ReadNumberNode(*array->get(1));
    if (!x || !y) {
        return std::nullopt;
    }
    return std::array<float, 2>{static_cast<float>(*x), static_cast<float>(*y)};
}

std::filesystem::path NormalizeScenePath(const std::filesystem::path& base, const std::string& text) {
    const std::filesystem::path path{text};
    if (path.is_absolute()) {
        return path.lexically_normal();
    }
    return (base / path).lexically_normal();
}

void ParseRender(const std::filesystem::path& file, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::table* render = root["render"].as_table();
    if (render == nullptr) {
        diagnostics.push_back(Error(file, "render", "missing required table"));
        return;
    }

    if (const auto backend = ReadString(*render, "backend")) {
        const auto parsed = ParseRenderBackendName(*backend);
        if (parsed) {
            scene.render.backend = *parsed;
        } else {
            diagnostics.push_back(Error(file, "render.backend", "expected one of: cpu, cuda"));
        }
    }
    if (const auto width = ReadInt(*render, "width")) {
        scene.render.width = *width;
    }
    if (const auto height = ReadInt(*render, "height")) {
        scene.render.height = *height;
    }
    if (const auto spp = ReadInt(*render, "spp")) {
        scene.render.spp = *spp;
    }
    if (const auto max_depth = ReadInt(*render, "max_depth")) {
        scene.render.max_depth = *max_depth;
    }
    if (const auto seed = ReadUint64(*render, "seed")) {
        scene.render.seed = *seed;
    }
}

void ParseFilm(const std::filesystem::path& file, const std::filesystem::path& base, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::table* film = root["film"].as_table();
    if (film == nullptr) {
        diagnostics.push_back(Error(file, "film", "missing required table"));
        return;
    }

    if (const auto output = ReadString(*film, "output")) {
        scene.film.output = NormalizeScenePath(base, *output);
    }
    if (const auto tone_mapper = ReadString(*film, "tone_mapper")) {
        const auto parsed = ParseToneMapperName(*tone_mapper);
        if (parsed) {
            scene.film.tone_mapper = *parsed;
        } else {
            diagnostics.push_back(Error(file, "film.tone_mapper", "expected one of: none, reinhard, aces"));
        }
    }
    if (const auto exposure = ReadNumber(*film, "exposure")) {
        scene.film.exposure = static_cast<float>(*exposure);
    }
    if (const auto interval = ReadInt(*film, "checkpoint_interval_s")) {
        scene.film.checkpoint_interval_s = *interval;
    }
    if (const auto checkpoint = ReadString(*film, "checkpoint_path")) {
        scene.film.checkpoint_path = checkpoint->empty() ? std::filesystem::path{} : NormalizeScenePath(base, *checkpoint);
    }
}

void ParseCamera(const std::filesystem::path& file, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::table* camera = root["camera"].as_table();
    if (camera == nullptr) {
        diagnostics.push_back(Error(file, "camera", "missing required table"));
        return;
    }

    CameraDescription description;
    if (const auto type = ReadString(*camera, "type")) {
        const auto parsed = ParseCameraKindName(*type);
        if (parsed) {
            description.type = *parsed;
        } else {
            diagnostics.push_back(Error(file, "camera.type", "expected perspective"));
        }
    }
    if (const auto position = ReadVec3(*camera, "position")) {
        description.position = *position;
    }
    if (const auto target = ReadVec3(*camera, "target")) {
        description.target = *target;
    }
    if (const auto fov = ReadNumber(*camera, "fov_y")) {
        description.fov_y = static_cast<float>(*fov);
    }
    if (const auto aperture = ReadNumber(*camera, "aperture")) {
        description.aperture = static_cast<float>(*aperture);
    }
    if (const auto focus = ReadNumber(*camera, "focus_distance")) {
        description.focus_distance = static_cast<float>(*focus);
    }
    scene.camera = description;
}

void ParseAssets(const std::filesystem::path& base, const toml::table& root, SceneDescription& scene) {
    const toml::array* assets = root["assets"].as_array();
    if (assets == nullptr) {
        return;
    }
    for (const toml::node& node : *assets) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            continue;
        }
        AssetDescription asset;
        if (const auto name = ReadString(*table, "name")) {
            asset.name = *name;
        }
        if (const auto path = ReadString(*table, "path")) {
            asset.path = NormalizeScenePath(base, *path);
        }
        scene.assets.push_back(std::move(asset));
    }
}

void ParseInstances(const toml::table& root, SceneDescription& scene) {
    const toml::array* instances = root["instances"].as_array();
    if (instances == nullptr) {
        return;
    }
    for (const toml::node& node : *instances) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            continue;
        }
        InstanceDescription instance;
        if (const auto asset = ReadString(*table, "asset")) {
            instance.asset = *asset;
        }
        if (const auto translate = ReadVec3(*table, "translate")) {
            instance.transform.translate = *translate;
        }
        if (const auto rotate = ReadVec3(*table, "rotate_degrees")) {
            instance.transform.rotate_degrees = *rotate;
        }
        if (const auto scale = ReadVec3(*table, "scale")) {
            instance.transform.scale = *scale;
        }
        scene.instances.push_back(instance);
    }
}

void ParseLights(const std::filesystem::path& file, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::array* lights = root["lights"].as_array();
    if (lights == nullptr) {
        return;
    }
    for (const toml::node& node : *lights) {
        const toml::table* table = node.as_table();
        if (table == nullptr) {
            continue;
        }
        LightDescription light;
        if (const auto type = ReadString(*table, "type")) {
            const auto parsed = ParseLightKindName(*type);
            if (parsed) {
                light.type = *parsed;
            } else {
                diagnostics.push_back(Error(file, "lights.type", "expected area"));
            }
        }
        if (const auto position = ReadVec3(*table, "position")) {
            light.area.position = *position;
        }
        if (const auto size = ReadVec2(*table, "size")) {
            light.area.size = *size;
        }
        if (const auto radiance = ReadVec3(*table, "radiance")) {
            light.area.radiance = *radiance;
        }
        scene.lights.push_back(light);
    }
}

void ParseEnvironment(const std::filesystem::path& file, const std::filesystem::path& base, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::table* environment = root["environment"].as_table();
    if (environment == nullptr) {
        return;
    }
    if (const auto type = ReadString(*environment, "type")) {
        const auto parsed = ParseEnvironmentKindName(*type);
        if (parsed) {
            scene.environment.type = *parsed;
        } else {
            diagnostics.push_back(Error(file, "environment.type", "expected one of: none, constant, hdri"));
        }
    }
    if (const auto radiance = ReadVec3(*environment, "radiance")) {
        scene.environment.radiance = *radiance;
    }
    if (const auto path = ReadString(*environment, "path")) {
        scene.environment.path = NormalizeScenePath(base, *path);
    }
    if (const auto strength = ReadNumber(*environment, "strength")) {
        scene.environment.strength = static_cast<float>(*strength);
    }
}

bool IsFinite(float value) {
    return std::isfinite(value);
}

bool IsFinite(Vec3f value) {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

void ValidateScene(const std::filesystem::path& file, const SceneDescription& scene, const toml::table& root, std::vector<SceneDiagnostic>& diagnostics) {
    const toml::table* render = root["render"].as_table();
    if (render != nullptr) {
        if (!HasKey(*render, "width")) {
            diagnostics.push_back(Error(file, "render.width", "missing required field"));
        }
        if (!HasKey(*render, "height")) {
            diagnostics.push_back(Error(file, "render.height", "missing required field"));
        }
    }
    if (scene.render.width <= 0) {
        diagnostics.push_back(Error(file, "render.width", "must be positive"));
    }
    if (scene.render.height <= 0) {
        diagnostics.push_back(Error(file, "render.height", "must be positive"));
    }
    if (scene.render.spp <= 0) {
        diagnostics.push_back(Error(file, "render.spp", "must be positive"));
    }
    if (scene.render.max_depth <= 0) {
        diagnostics.push_back(Error(file, "render.max_depth", "must be positive"));
    }

    const toml::table* film = root["film"].as_table();
    if (film != nullptr && !HasKey(*film, "output")) {
        diagnostics.push_back(Error(file, "film.output", "missing required field"));
    }
    if (scene.film.output.empty()) {
        diagnostics.push_back(Error(file, "film.output", "must not be empty"));
    }

    const toml::table* camera = root["camera"].as_table();
    if (camera != nullptr) {
        if (!HasKey(*camera, "type")) {
            diagnostics.push_back(Error(file, "camera.type", "missing required field"));
        }
        if (!HasKey(*camera, "position")) {
            diagnostics.push_back(Error(file, "camera.position", "missing required field"));
        }
        if (!HasKey(*camera, "target")) {
            diagnostics.push_back(Error(file, "camera.target", "missing required field"));
        }
        if (!HasKey(*camera, "fov_y")) {
            diagnostics.push_back(Error(file, "camera.fov_y", "missing required field"));
        }
    }
    if (scene.camera && (!IsFinite(scene.camera->position) || !IsFinite(scene.camera->target) || !IsFinite(scene.camera->fov_y))) {
        diagnostics.push_back(Error(file, "camera", "contains non-finite numeric value"));
    }

    std::set<std::string> asset_names;
    for (const AssetDescription& asset : scene.assets) {
        if (asset.name.empty()) {
            diagnostics.push_back(Error(file, "assets.name", "must not be empty"));
        }
        if (!asset.name.empty() && !asset_names.insert(asset.name).second) {
            diagnostics.push_back(Error(file, "assets.name", "duplicate asset name: " + asset.name));
        }
    }
    for (const InstanceDescription& instance : scene.instances) {
        if (instance.asset.empty()) {
            diagnostics.push_back(Error(file, "instances.asset", "missing required field"));
        } else if (!asset_names.contains(instance.asset)) {
            diagnostics.push_back(Error(file, "instances.asset", "references unknown asset: " + instance.asset));
        }
    }
    const bool has_contributor = !scene.instances.empty() || !scene.lights.empty() || scene.environment.type != EnvironmentKind::None;
    if (!has_contributor) {
        diagnostics.push_back(Error(file, "scene", "must contain at least one instance, light, or non-none environment"));
    }
}

} // namespace

SceneLoadResult LoadSceneFile(const std::filesystem::path& path) {
    SceneLoadResult result;
    const std::filesystem::path normalized_path = path.lexically_normal();
    if (!std::filesystem::exists(normalized_path)) {
        result.diagnostics.push_back(Error(normalized_path, "", "scene file not found"));
        return result;
    }

    toml::table root;
    try {
        root = toml::parse_file(normalized_path.string());
    } catch (const toml::parse_error& error) {
        std::ostringstream message;
        message << "invalid TOML: " << error.description();
        result.diagnostics.push_back(Error(normalized_path, "", message.str()));
        return result;
    }

    SceneDescription scene;
    scene.source_path = normalized_path;
    const std::filesystem::path base = normalized_path.parent_path();
    ParseRender(normalized_path, root, scene, result.diagnostics);
    ParseFilm(normalized_path, base, root, scene, result.diagnostics);
    ParseCamera(normalized_path, root, scene, result.diagnostics);
    ParseAssets(base, root, scene);
    ParseInstances(root, scene);
    ParseLights(normalized_path, root, scene, result.diagnostics);
    ParseEnvironment(normalized_path, base, root, scene, result.diagnostics);
    ValidateScene(normalized_path, scene, root, result.diagnostics);

    if (!HasSceneErrors(result.diagnostics)) {
        result.scene = std::move(scene);
    }
    return result;
}

void ApplyBackendOverride(SceneDescription& scene, RenderBackendKind backend) {
    scene.render.backend = backend;
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
git add include/yaoray/scene/scene_parser.hpp src/scene/scene_parser.cpp tests/scene_tests.cpp tests/fixtures/scene/minimal.toml tests/fixtures/scene/defaults.toml
git commit -m "feat: parse scene toml files"
```

## Task 4: Add Strict Validation And Unknown Field Diagnostics

**Files:**
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`
- Create: `tests/fixtures/scene/bad_syntax.toml`
- Create: `tests/fixtures/scene/duplicate_asset.toml`
- Create: `tests/fixtures/scene/empty_scene.toml`
- Create: `tests/fixtures/scene/invalid_width.toml`
- Create: `tests/fixtures/scene/missing_asset_reference.toml`
- Create: `tests/fixtures/scene/missing_camera.toml`
- Create: `tests/fixtures/scene/missing_render.toml`
- Create: `tests/fixtures/scene/short_position.toml`
- Create: `tests/fixtures/scene/unknown_field.toml`

- [ ] **Step 1: Add invalid scene fixtures**

Create `tests/fixtures/scene/missing_render.toml`:

```toml
[film]
output = "out/missing-render.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

Create `tests/fixtures/scene/missing_camera.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/missing-camera.png"

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

Create `tests/fixtures/scene/invalid_width.toml`:

```toml
[render]
width = 0
height = 32

[film]
output = "out/invalid-width.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

Create `tests/fixtures/scene/short_position.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/short-position.png"

[camera]
type = "perspective"
position = [0, 1]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

Create `tests/fixtures/scene/unknown_field.toml`:

```toml
[render]
width = 64
height = 32
widht = 1280

[film]
output = "out/unknown-field.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.1, 0.1, 0.1]
```

Create `tests/fixtures/scene/duplicate_asset.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/duplicate-asset.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[[assets]]
name = "model"
path = "assets/a.glb"

[[assets]]
name = "model"
path = "assets/b.glb"

[[instances]]
asset = "model"
```

Create `tests/fixtures/scene/missing_asset_reference.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/missing-asset-reference.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[[assets]]
name = "model"
path = "assets/model.glb"

[[instances]]
asset = "missing"
```

Create `tests/fixtures/scene/empty_scene.toml`:

```toml
[render]
width = 64
height = 32

[film]
output = "out/empty.png"

[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
```

Create `tests/fixtures/scene/bad_syntax.toml`:

```toml
[render
width = 64
```

- [ ] **Step 2: Add failing validation tests**

Append to `tests/scene_tests.cpp`:

```cpp
namespace {

bool DiagnosticsContain(const std::vector<yr::SceneDiagnostic>& diagnostics, std::string_view field, std::string_view message) {
    for (const yr::SceneDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.field == field && diagnostic.message.find(message) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(scene_parser_rejects_missing_render) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_render.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render", "missing required table"));
}

YR_TEST(scene_parser_rejects_missing_camera) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_camera.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(!result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "camera", "missing required table"));
}

YR_TEST(scene_parser_rejects_invalid_width) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("invalid_width.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.width", "must be positive"));
}

YR_TEST(scene_parser_rejects_short_vectors) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("short_position.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "camera.position", "expected three numeric values"));
}

YR_TEST(scene_parser_rejects_unknown_fields) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("unknown_field.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.widht", "unknown field"));
}

YR_TEST(scene_parser_rejects_duplicate_assets) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("duplicate_asset.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "assets.name", "duplicate asset name"));
}

YR_TEST(scene_parser_rejects_missing_asset_references) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("missing_asset_reference.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "instances.asset", "references unknown asset"));
}

YR_TEST(scene_parser_rejects_empty_scene) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("empty_scene.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "scene", "must contain at least one instance, light, or non-none environment"));
}

YR_TEST(scene_parser_reports_missing_files) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("does_not_exist.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "scene file not found"));
}

YR_TEST(scene_parser_reports_bad_toml) {
    const yr::SceneLoadResult result = yr::LoadSceneFile(SceneFixture("bad_syntax.toml"));

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "", "invalid TOML"));
}
```

- [ ] **Step 3: Run tests to verify failures**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: tests fail for short vector diagnostics and unknown field diagnostics.

- [ ] **Step 4: Add strict key and type validation helpers**

Modify `src/scene/scene_parser.cpp`:

Add this helper near `HasKey`:

```cpp
void CheckAllowedKeys(
    const std::filesystem::path& file,
    const toml::table& table,
    std::string_view prefix,
    const std::set<std::string_view>& allowed,
    std::vector<SceneDiagnostic>& diagnostics) {
    for (const auto& [key, value] : table) {
        const std::string key_name{key.str()};
        if (!allowed.contains(key_name)) {
            diagnostics.push_back(Error(file, std::string{prefix} + "." + key_name, "unknown field"));
        }
    }
}
```

Add this helper near `ReadVec3`:

```cpp
void ValidateVec3Field(
    const std::filesystem::path& file,
    const toml::table& table,
    std::string_view local_key,
    std::string_view diagnostic_field,
    std::vector<SceneDiagnostic>& diagnostics) {
    const toml::node* node = table.get(std::string{local_key});
    if (node == nullptr) {
        return;
    }
    const toml::array* array = node->as_array();
    if (array == nullptr || array->size() != 3) {
        diagnostics.push_back(Error(file, std::string{diagnostic_field}, "expected three numeric values"));
        return;
    }
    for (const toml::node& item : *array) {
        if (!ReadNumberNode(item)) {
            diagnostics.push_back(Error(file, std::string{diagnostic_field}, "expected three numeric values"));
            return;
        }
    }
}
```

Add this helper near `ValidateVec3Field`:

```cpp
void ValidateVec2Field(
    const std::filesystem::path& file,
    const toml::table& table,
    std::string_view local_key,
    std::string_view diagnostic_field,
    std::vector<SceneDiagnostic>& diagnostics) {
    const toml::node* node = table.get(std::string{local_key});
    if (node == nullptr) {
        return;
    }
    const toml::array* array = node->as_array();
    if (array == nullptr || array->size() != 2) {
        diagnostics.push_back(Error(file, std::string{diagnostic_field}, "expected two numeric values"));
        return;
    }
    for (const toml::node& item : *array) {
        if (!ReadNumberNode(item)) {
            diagnostics.push_back(Error(file, std::string{diagnostic_field}, "expected two numeric values"));
            return;
        }
    }
}
```

Call `CheckAllowedKeys()` at the start of each parse function:

```cpp
CheckAllowedKeys(file, *render, "render", {"backend", "width", "height", "spp", "max_depth", "seed"}, diagnostics);
CheckAllowedKeys(file, *film, "film", {"output", "tone_mapper", "exposure", "checkpoint_interval_s", "checkpoint_path"}, diagnostics);
CheckAllowedKeys(file, *camera, "camera", {"type", "position", "target", "fov_y", "aperture", "focus_distance"}, diagnostics);
```

For array-of-table sections, call it inside each item loop:

```cpp
// Change ParseAssets to accept file and diagnostics:
void ParseAssets(const std::filesystem::path& file, const std::filesystem::path& base, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics)

// Change ParseInstances to accept file and diagnostics:
void ParseInstances(const std::filesystem::path& file, const toml::table& root, SceneDescription& scene, std::vector<SceneDiagnostic>& diagnostics)

CheckAllowedKeys(file, *table, "assets", {"name", "path"}, diagnostics);
CheckAllowedKeys(file, *table, "instances", {"asset", "translate", "rotate_degrees", "scale"}, diagnostics);
CheckAllowedKeys(file, *table, "lights", {"type", "position", "size", "radiance"}, diagnostics);
```

For environment:

```cpp
CheckAllowedKeys(file, *environment, "environment", {"type", "radiance", "path", "strength"}, diagnostics);
```

Also call vector validators before reading vectors:

```cpp
ValidateVec3Field(file, *camera, "position", "camera.position", diagnostics);
ValidateVec3Field(file, *camera, "target", "camera.target", diagnostics);
ValidateVec3Field(file, *table, "translate", "instances.translate", diagnostics);
ValidateVec3Field(file, *table, "rotate_degrees", "instances.rotate_degrees", diagnostics);
ValidateVec3Field(file, *table, "scale", "instances.scale", diagnostics);
ValidateVec3Field(file, *table, "position", "lights.position", diagnostics);
ValidateVec2Field(file, *table, "size", "lights.size", diagnostics);
ValidateVec3Field(file, *table, "radiance", "lights.radiance", diagnostics);
ValidateVec3Field(file, *environment, "radiance", "environment.radiance", diagnostics);
```

Update the `LoadSceneFile()` parse calls to pass diagnostics into the changed functions:

```cpp
ParseAssets(normalized_path, base, root, scene, result.diagnostics);
ParseInstances(normalized_path, root, scene, result.diagnostics);
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
git add src/scene/scene_parser.cpp tests/scene_tests.cpp tests/fixtures/scene
git commit -m "test: cover scene validation diagnostics"
```

## Task 5: Add `yaoray render` CLI Command

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing CLI tests**

Modify the `if(BUILD_TESTING)` block in `CMakeLists.txt` to include:

```cmake
    add_test(NAME yaoray_cli_render_help COMMAND yaoray render --help)
    set_tests_properties(yaoray_cli_render_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:.*yaoray render")

    add_test(NAME yaoray_cli_render_cpu COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml --backend cpu)
    set_tests_properties(yaoray_cli_render_cpu PROPERTIES PASS_REGULAR_EXPRESSION "Requested backend: cpu")

    add_test(NAME yaoray_cli_render_cuda COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/minimal.toml --backend cuda)
    set_tests_properties(yaoray_cli_render_cuda PROPERTIES PASS_REGULAR_EXPRESSION "Requested backend: cuda")

    add_test(NAME yaoray_cli_render_missing_file COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/nope.toml)
    set_tests_properties(yaoray_cli_render_missing_file PROPERTIES
        WILL_FAIL TRUE
        PASS_REGULAR_EXPRESSION "scene file not found"
    )

    add_test(NAME yaoray_cli_render_invalid_scene COMMAND yaoray render ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/invalid_width.toml)
    set_tests_properties(yaoray_cli_render_invalid_scene PROPERTIES
        WILL_FAIL TRUE
        PASS_REGULAR_EXPRESSION "render.width"
    )
```

- [ ] **Step 2: Run CTest to verify failures**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: new CLI render tests fail because `src/app/main.cpp` does not support `render`.

- [ ] **Step 3: Implement render command routing**

Replace `src/app/main.cpp` with:

```cpp
#include <yaoray/core/version.hpp>
#include <yaoray/scene/diagnostic.hpp>
#include <yaoray/scene/scene_parser.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

void PrintHelp() {
    std::cout
        << "YaoRay " << yr::VersionString() << '\n'
        << '\n'
        << "Usage:\n"
        << "  yaoray --help\n"
        << "  yaoray --version\n"
        << "  yaoray render <scene.toml> [--backend cpu|cuda]\n";
}

void PrintRenderHelp() {
    std::cout
        << "Usage:\n"
        << "  yaoray render <scene.toml> [--backend cpu|cuda]\n"
        << '\n'
        << "The render command currently parses and validates scene files. Rendering is not implemented yet.\n";
}

int RunRender(int argc, char** argv) {
    if (argc == 3 && (std::string_view{argv[2]} == "--help" || std::string_view{argv[2]} == "-h")) {
        PrintRenderHelp();
        return 0;
    }
    if (argc < 3) {
        std::cerr << "Missing scene file path.\n";
        PrintRenderHelp();
        return 2;
    }

    const std::filesystem::path scene_path = argv[2];
    std::optional<yr::RenderBackendKind> backend_override;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --backend.\n";
                return 2;
            }
            const auto backend = yr::ParseRenderBackendName(argv[++i]);
            if (!backend) {
                std::cerr << "Unknown backend: " << argv[i] << '\n';
                return 2;
            }
            backend_override = *backend;
        } else {
            std::cerr << "Unknown render argument: " << arg << '\n';
            PrintRenderHelp();
            return 2;
        }
    }

    yr::SceneLoadResult result = yr::LoadSceneFile(scene_path);
    if (yr::HasSceneErrors(result.diagnostics) || !result.scene.has_value()) {
        std::cerr << yr::FormatSceneDiagnostics(result.diagnostics) << '\n';
        return 1;
    }

    yr::SceneDescription scene = std::move(result.scene.value());
    if (backend_override) {
        yr::ApplyBackendOverride(scene, *backend_override);
    }

    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Requested backend: " << yr::RenderBackendName(scene.render.backend) << '\n';
    std::cout << "Rendering is not implemented yet.\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        PrintHelp();
        return 0;
    }

    const std::string_view arg = argv[1];
    if (arg == "--help" || arg == "-h") {
        PrintHelp();
        return 0;
    }
    if (arg == "--version") {
        std::cout << yr::VersionString() << '\n';
        return 0;
    }
    if (arg == "render") {
        return RunRender(argc, argv);
    }

    std::cerr << "Unknown argument: " << arg << '\n';
    PrintHelp();
    return 2;
}
```

- [ ] **Step 4: Run tests**

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

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt src/app/main.cpp
git commit -m "feat: add scene render cli shell"
```

## Task 6: Add Example Scene And Documentation

**Files:**
- Create: `scenes/examples/minimal.toml`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Create human-facing example scene**

Create `scenes/examples/minimal.toml`:

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
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45

[environment]
type = "constant"
radiance = [0.02, 0.025, 0.03]
strength = 1.0
```

- [ ] **Step 2: Update README**

Replace the "Current Status" and "Run" sections in `README.md` with:

```markdown
## Current Status

The foundation slices provide:

- CMake project structure
- small CTest-based test harness
- core vector, ray, and bounds primitives
- Film accumulation and tone mapping basics
- CLI help/version shell
- TOML scene parsing and validation through `yaoray render`

Rendering, asset import, BVH construction, PNG output, and CUDA backend support are planned as separate implementation slices.

## Run

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
```

The `render` command currently validates scene files and reports the requested backend. It does not render images yet.
```

- [ ] **Step 3: Update architecture overview**

Replace `docs/architecture/overview.md` with:

```markdown
# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings. The current implementation parses this semantic layer from TOML scene files into `SceneDescription`.

The render layer will compile that semantic scene into backend-friendly data: flat arrays of triangles, BVH nodes, materials, textures, lights, camera data, and environment data. CPU, CUDA, and future OptiX backends will consume this compiled representation.

Current implemented slices:

- project structure, tests, core math primitives, Film accumulation, and CLI shell
- TOML scene parsing, validation diagnostics, and `yaoray render` command shell

Rendering-specific modules will be added in focused implementation plans.
```

- [ ] **Step 4: Run example command**

Run:

```powershell
cmake --build build --config Debug
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
```

Expected output includes:

```text
Scene parsed successfully
Requested backend: cpu
Rendering is not implemented yet.
```

- [ ] **Step 5: Run tests**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 6: Commit**

```powershell
git add README.md docs/architecture/overview.md scenes/examples/minimal.toml
git commit -m "docs: describe scene parsing stage"
```

## Task 7: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm `toml++` stays private**

Run:

```powershell
rg "toml" include src tests CMakeLists.txt
```

Expected:

```text
src/scene/scene_parser.cpp:#include <toml.hpp>
```

Other matches in tests, docs, comments, or CMake target names are acceptable. No public header under `include/yaoray/scene/` may include `toml.hpp` or expose `toml::` types.

- [ ] **Step 2: Configure a clean Release build**

Run:

```powershell
if (Test-Path build-release) { Remove-Item -LiteralPath build-release -Recurse -Force }
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
```

Expected: configure succeeds and prints that build files were written.

- [ ] **Step 3: Build Release**

Run:

```powershell
cmake --build build-release --config Release
```

Expected: `yaoray.exe` and `yaoray_tests.exe` build successfully.

- [ ] **Step 4: Run Release tests**

Run:

```powershell
ctest --test-dir build-release --output-on-failure -C Release
```

Expected:

```text
100% tests passed
```

- [ ] **Step 5: Verify CLI manually**

Run:

```powershell
.\build-release\Release\yaoray.exe render scenes\examples\minimal.toml --backend cpu
.\build-release\Release\yaoray.exe render scenes\examples\minimal.toml --backend cuda
```

Expected CPU output includes:

```text
Requested backend: cpu
Rendering is not implemented yet.
```

Expected CUDA output includes:

```text
Requested backend: cuda
Rendering is not implemented yet.
```

- [ ] **Step 6: Confirm clean worktree**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=8
```

Expected: `git status --short` prints nothing. Recent commits should show the task commits from this plan on branch `codex/scene-foundation`.

## Self-Review Notes

Spec coverage:

- `scene` module data model: Task 2.
- `toml++` dependency and private include rule: Tasks 1 and 7.
- TOML parsing and defaults: Task 3.
- Strict validation diagnostics: Task 4.
- CLI `yaoray render`: Task 5.
- Example scene and docs: Task 6.
- Clean build/test verification: Task 7.

Implementation guardrails:

- Keep `toml++` private to `src/scene/scene_parser.cpp`.
- Do not load model, texture, HDRI, or output files in this slice.
- Do not add CPU/CUDA backend behavior beyond recording and printing the requested backend.
- Keep ordinary scene input errors in `SceneDiagnostic`; do not expose raw parser exception names in CLI output.
