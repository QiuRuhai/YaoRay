# YaoRay Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the clean YaoRay project foundation: new history root, CMake/test harness, core math primitives, Film basics, and a CLI shell that can be extended by renderer-specific plans.

**Architecture:** This is the first implementation plan for the approved YaoRay rewrite spec. It creates a small, testable C++20 project with focused libraries (`yaoray_core`, `yaoray_film`) and a CLI executable (`yaoray`). Scene parsing, asset import, path tracing, and CUDA are intentionally split into subsequent implementation plans so each plan produces working software.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC/Clang/GCC, no third-party test framework for the foundation slice.

---

## Scope Check

The approved spec covers several independent subsystems: clean project history, scene files, asset import, CPU path tracing, Film, CUDA, OptiX-ready backend boundaries, and tests. This plan implements only the foundation slice:

- clean YaoRay root from the existing repository
- CMake project structure and test runner
- version API
- core `Vec3f`, `Ray3f`, and `Bounds3f`
- Film accumulation and display tone mapping
- CLI help/version shell
- README and architecture stub

The next plan should start with `SceneDescription`, TOML parsing, and `RenderScene` compilation.

## File Structure

Create this clean tree after the orphan-history task:

```text
.
  CMakeLists.txt
  README.md
  docs/
    architecture/
      overview.md
    superpowers/
      specs/
        2026-05-12-yaoray-renderer-rewrite-design.md
      plans/
        2026-05-12-yaoray-foundation-implementation-plan.md
  include/
    yaoray/
      core/
        bounds.hpp
        ray.hpp
        vec.hpp
        version.hpp
      film/
        film.hpp
        tone_mapping.hpp
  src/
    app/
      main.cpp
    core/
      version.cpp
    film/
      film.cpp
      tone_mapping.cpp
  tests/
    core_tests.cpp
    film_tests.cpp
    test_main.cpp
    version_tests.cpp
    yr_test.hpp
```

Responsibilities:

- `include/yaoray/core/*`: stable math and ray primitives used by all future subsystems.
- `include/yaoray/film/*`: image accumulation and display transforms shared by CPU and future CUDA outputs.
- `src/app/main.cpp`: CLI command shell only; no rendering logic yet.
- `tests/yr_test.hpp`: tiny local test harness so the foundation has zero test dependencies.

## Task 1: Preserve The Old Project And Create The Clean YaoRay Root

**Files:**
- Preserve: `docs/superpowers/specs/2026-05-12-yaoray-renderer-rewrite-design.md`
- Preserve: `docs/superpowers/plans/2026-05-12-yaoray-foundation-implementation-plan.md`
- Delete from new root: old ToyRender source, demo, generated build files, and old project metadata
- Create: `.gitignore`

- [ ] **Step 1: Verify current branch and dirty state**

Run from the repository root:

```powershell
git branch --show-current
git status --short
git log -1 --oneline
```

Expected:

```text
yaoray-rewrite
```

The status may show this plan file if the plan was copied into the worktree before execution. Do not proceed if unrelated source edits are present in the worktree.

- [ ] **Step 2: Create an archive branch for the old committed state**

```powershell
git branch archive/toyrender-before-yaoray bf20c64
git branch --list archive/toyrender-before-yaoray
```

Expected:

```text
  archive/toyrender-before-yaoray
```

If the branch already exists, verify it points at the old design checkpoint:

```powershell
git rev-parse archive/toyrender-before-yaoray
git rev-parse bf20c64
```

Expected: both commands print the same commit hash.

- [ ] **Step 3: Save the approved documents outside the working tree**

```powershell
$carry = Join-Path $env:TEMP 'yaoray-foundation-carry'
if (Test-Path $carry) { Remove-Item -LiteralPath $carry -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $carry 'docs\superpowers\specs') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $carry 'docs\superpowers\plans') | Out-Null
Copy-Item -LiteralPath 'docs\superpowers\specs\2026-05-12-yaoray-renderer-rewrite-design.md' -Destination (Join-Path $carry 'docs\superpowers\specs\2026-05-12-yaoray-renderer-rewrite-design.md')
Copy-Item -LiteralPath 'docs\superpowers\plans\2026-05-12-yaoray-foundation-implementation-plan.md' -Destination (Join-Path $carry 'docs\superpowers\plans\2026-05-12-yaoray-foundation-implementation-plan.md')
Get-ChildItem -Recurse $carry | Select-Object FullName
```

Expected: the carry directory contains the design and this plan.

- [ ] **Step 4: Create the orphan implementation branch**

```powershell
git switch --orphan yaoray-foundation
git rm -r --cached .
```

Expected: Git stages old tracked files for deletion. This is safe because the archive branch preserves the old committed project state.

- [ ] **Step 5: Remove old working-tree files safely**

```powershell
$root = (Resolve-Path '.').Path
$preserve = @('.git')
Get-ChildItem -Force $root | Where-Object { $preserve -notcontains $_.Name } | ForEach-Object {
    $path = $_.FullName
    if (-not $path.StartsWith($root)) { throw "Refusing outside root: $path" }
    Remove-Item -LiteralPath $path -Recurse -Force
}
```

Expected: the working tree contains only `.git`.

- [ ] **Step 6: Restore the approved documents and create `.gitignore`**

Create `.gitignore`:

```gitignore
# Build outputs
build/
_build/
cmake-build-*/
out/

# Local IDE state
.vs/
.vscode/
.idea/

# Superpowers visual companion scratch files
.superpowers/brainstorm/

# OS and compiler noise
*.user
*.suo
*.log
*.obj
*.pdb
*.ilk
*.exe
```

Restore docs:

```powershell
$carry = Join-Path $env:TEMP 'yaoray-foundation-carry'
Copy-Item -Recurse -Force -LiteralPath (Join-Path $carry 'docs') -Destination '.'
git status --short
```

Expected: `.gitignore`, the design spec, and this plan are present.

- [ ] **Step 7: Commit the clean root checkpoint**

```powershell
git add .gitignore docs/superpowers/specs/2026-05-12-yaoray-renderer-rewrite-design.md docs/superpowers/plans/2026-05-12-yaoray-foundation-implementation-plan.md
git commit -m "chore: start YaoRay rewrite"
```

Expected: commit succeeds with only `.gitignore` and docs.

## Task 2: Add CMake Project And Version Test

**Files:**
- Create: `CMakeLists.txt`
- Create: `include/yaoray/core/version.hpp`
- Create: `src/core/version.cpp`
- Create: `tests/yr_test.hpp`
- Create: `tests/test_main.cpp`
- Create: `tests/version_tests.cpp`

- [ ] **Step 1: Write the failing version test and test harness**

Create `tests/yr_test.hpp`:

```cpp
#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace yrtest {

using TestFn = void (*)();

struct TestCase {
    std::string_view name;
    TestFn fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string_view name, TestFn fn) {
        Registry().push_back(TestCase{name, fn});
    }
};

inline void Fail(const char* expr, const char* file, int line) {
    std::ostringstream out;
    out << file << ':' << line << ": expectation failed: " << expr;
    throw std::runtime_error(out.str());
}

inline void ExpectNear(double actual, double expected, double eps, const char* expr, const char* file, int line) {
    if (std::fabs(actual - expected) > eps) {
        std::ostringstream out;
        out << file << ':' << line << ": near expectation failed: " << expr
            << " actual=" << actual << " expected=" << expected << " eps=" << eps;
        throw std::runtime_error(out.str());
    }
}

inline int RunAll() {
    int failed = 0;
    for (const auto& test : Registry()) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}

} // namespace yrtest

#define YR_TEST(name) \
    static void name(); \
    static ::yrtest::Registrar name##_registrar{#name, &name}; \
    static void name()

#define YR_EXPECT_TRUE(expr) \
    do { if (!(expr)) ::yrtest::Fail(#expr, __FILE__, __LINE__); } while (false)

#define YR_EXPECT_EQ(actual, expected) \
    do { if (!((actual) == (expected))) ::yrtest::Fail(#actual " == " #expected, __FILE__, __LINE__); } while (false)

#define YR_EXPECT_NEAR(actual, expected, eps) \
    do { ::yrtest::ExpectNear((actual), (expected), (eps), #actual " ~= " #expected, __FILE__, __LINE__); } while (false)
```

Create `tests/test_main.cpp`:

```cpp
#include "yr_test.hpp"

int main() {
    return yrtest::RunAll();
}
```

Create `include/yaoray/core/version.hpp`:

```cpp
#pragma once

#include <string_view>

namespace yr {

std::string_view VersionString();

} // namespace yr
```

Create `tests/version_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/core/version.hpp>

YR_TEST(version_string_is_present) {
    YR_EXPECT_EQ(yr::VersionString(), std::string_view{"0.1.0"});
}
```

Create `CMakeLists.txt`:

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

add_library(yaoray_core INTERFACE)
target_include_directories(yaoray_core INTERFACE include)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
endif()
```

- [ ] **Step 2: Run the test build to verify it fails**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
```

Expected: build fails at link time because `yr::VersionString()` is declared but not defined.

- [ ] **Step 3: Write minimal version implementation**

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

add_library(yaoray_core STATIC
    src/core/version.cpp
)
target_include_directories(yaoray_core PUBLIC include)

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
endif()
```

Create `src/core/version.cpp`:

```cpp
#include <yaoray/core/version.hpp>

namespace yr {

std::string_view VersionString() {
    return "0.1.0";
}

} // namespace yr
```

- [ ] **Step 4: Run the tests to verify they pass**

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
git add CMakeLists.txt include/yaoray/core/version.hpp src/core/version.cpp tests/yr_test.hpp tests/test_main.cpp tests/version_tests.cpp
git commit -m "test: add YaoRay CMake and version baseline"
```

## Task 3: Add Core Vector Math

**Files:**
- Create: `include/yaoray/core/vec.hpp`
- Modify: `tests/core_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing vector tests**

Create `tests/core_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/core/vec.hpp>

YR_TEST(vec3_adds_and_scales_components) {
    const yr::Vec3f a{1.0f, 2.0f, 3.0f};
    const yr::Vec3f b{4.0f, -1.0f, 0.5f};

    const yr::Vec3f sum = a + b;
    const yr::Vec3f scaled = 2.0f * a;

    YR_EXPECT_NEAR(sum.x, 5.0, 1e-6);
    YR_EXPECT_NEAR(sum.y, 1.0, 1e-6);
    YR_EXPECT_NEAR(sum.z, 3.5, 1e-6);
    YR_EXPECT_NEAR(scaled.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(scaled.y, 4.0, 1e-6);
    YR_EXPECT_NEAR(scaled.z, 6.0, 1e-6);
}

YR_TEST(vec3_dot_cross_and_normalize_are_correct) {
    const yr::Vec3f x{1.0f, 0.0f, 0.0f};
    const yr::Vec3f y{0.0f, 1.0f, 0.0f};

    const yr::Vec3f z = yr::Cross(x, y);
    const yr::Vec3f n = yr::Normalize(yr::Vec3f{0.0f, 3.0f, 4.0f});

    YR_EXPECT_NEAR(yr::Dot(x, y), 0.0, 1e-6);
    YR_EXPECT_NEAR(z.x, 0.0, 1e-6);
    YR_EXPECT_NEAR(z.y, 0.0, 1e-6);
    YR_EXPECT_NEAR(z.z, 1.0, 1e-6);
    YR_EXPECT_NEAR(yr::Length(n), 1.0, 1e-6);
    YR_EXPECT_NEAR(n.y, 0.6, 1e-6);
    YR_EXPECT_NEAR(n.z, 0.8, 1e-6);
}
```

Modify the `add_executable(yaoray_tests ...)` block in `CMakeLists.txt`:

```cmake
add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
)
```

- [ ] **Step 2: Run the test build to verify it fails**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `yaoray/core/vec.hpp` does not exist.

- [ ] **Step 3: Implement `Vec3f`**

Create `include/yaoray/core/vec.hpp`:

```cpp
#pragma once

#include <cmath>

namespace yr {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3f() = default;
    constexpr Vec3f(float x_value, float y_value, float z_value)
        : x(x_value), y(y_value), z(z_value) {}

    constexpr Vec3f operator-() const {
        return Vec3f{-x, -y, -z};
    }
};

using Point3f = Vec3f;
using Color3f = Vec3f;

constexpr Vec3f operator+(Vec3f a, Vec3f b) {
    return Vec3f{a.x + b.x, a.y + b.y, a.z + b.z};
}

constexpr Vec3f operator-(Vec3f a, Vec3f b) {
    return Vec3f{a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr Vec3f operator*(Vec3f v, float scale) {
    return Vec3f{v.x * scale, v.y * scale, v.z * scale};
}

constexpr Vec3f operator*(float scale, Vec3f v) {
    return v * scale;
}

constexpr Vec3f operator/(Vec3f v, float scale) {
    return Vec3f{v.x / scale, v.y / scale, v.z / scale};
}

constexpr float Dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vec3f Cross(Vec3f a, Vec3f b) {
    return Vec3f{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float LengthSquared(Vec3f v) {
    return Dot(v, v);
}

inline float Length(Vec3f v) {
    return std::sqrt(LengthSquared(v));
}

inline Vec3f Normalize(Vec3f v) {
    const float len = Length(v);
    return len > 0.0f ? v / len : Vec3f{};
}

} // namespace yr
```

- [ ] **Step 4: Run tests**

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt include/yaoray/core/vec.hpp tests/core_tests.cpp
git commit -m "feat: add core vector math"
```

## Task 4: Add Ray And Bounds Intersections

**Files:**
- Create: `include/yaoray/core/ray.hpp`
- Create: `include/yaoray/core/bounds.hpp`
- Modify: `tests/core_tests.cpp`

- [ ] **Step 1: Add failing ray and bounds tests**

Append to `tests/core_tests.cpp`:

```cpp
#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>

YR_TEST(ray_evaluates_points_along_direction) {
    const yr::Ray3f ray{yr::Point3f{1.0f, 2.0f, 3.0f}, yr::Vec3f{0.0f, 2.0f, 0.0f}};
    const yr::Point3f p = ray.At(2.5f);

    YR_EXPECT_NEAR(p.x, 1.0, 1e-6);
    YR_EXPECT_NEAR(p.y, 7.0, 1e-6);
    YR_EXPECT_NEAR(p.z, 3.0, 1e-6);
}

YR_TEST(bounds_intersects_ray_interval) {
    const yr::Bounds3f box{yr::Point3f{-1.0f, -1.0f, -1.0f}, yr::Point3f{1.0f, 1.0f, 1.0f}};

    const yr::Ray3f hit_ray{yr::Point3f{0.0f, 0.0f, -3.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};
    const yr::Ray3f miss_ray{yr::Point3f{3.0f, 3.0f, -3.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};

    YR_EXPECT_TRUE(box.Intersects(hit_ray, 0.001f, 100.0f));
    YR_EXPECT_TRUE(!box.Intersects(miss_ray, 0.001f, 100.0f));
}
```

- [ ] **Step 2: Run the test build to verify it fails**

```powershell
cmake --build build --config Debug
```

Expected: build fails because `ray.hpp` and `bounds.hpp` do not exist.

- [ ] **Step 3: Implement ray and bounds**

Create `include/yaoray/core/ray.hpp`:

```cpp
#pragma once

#include <yaoray/core/vec.hpp>

namespace yr {

struct Ray3f {
    Point3f origin;
    Vec3f direction;
    float time = 0.0f;

    constexpr Ray3f() = default;
    constexpr Ray3f(Point3f ray_origin, Vec3f ray_direction, float ray_time = 0.0f)
        : origin(ray_origin), direction(ray_direction), time(ray_time) {}

    constexpr Point3f At(float t) const {
        return origin + direction * t;
    }
};

} // namespace yr
```

Create `include/yaoray/core/bounds.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <limits>

#include <yaoray/core/ray.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct Bounds3f {
    Point3f min;
    Point3f max;

    Bounds3f()
        : min{std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::infinity()},
          max{-std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity(),
              -std::numeric_limits<float>::infinity()} {}

    constexpr Bounds3f(Point3f p_min, Point3f p_max)
        : min(p_min), max(p_max) {}

    bool Intersects(const Ray3f& ray, float t_min, float t_max) const {
        const float origins[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
        const float dirs[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
        const float mins[3] = {min.x, min.y, min.z};
        const float maxs[3] = {max.x, max.y, max.z};

        for (int axis = 0; axis < 3; ++axis) {
            const float inv_d = 1.0f / dirs[axis];
            float t0 = (mins[axis] - origins[axis]) * inv_d;
            float t1 = (maxs[axis] - origins[axis]) * inv_d;
            if (inv_d < 0.0f) {
                std::swap(t0, t1);
            }
            t_min = std::max(t_min, t0);
            t_max = std::min(t_max, t1);
            if (t_max <= t_min) {
                return false;
            }
        }
        return true;
    }
};

inline Bounds3f Union(const Bounds3f& bounds, Point3f p) {
    return Bounds3f{
        Point3f{
            std::min(bounds.min.x, p.x),
            std::min(bounds.min.y, p.y),
            std::min(bounds.min.z, p.z),
        },
        Point3f{
            std::max(bounds.max.x, p.x),
            std::max(bounds.max.y, p.y),
            std::max(bounds.max.z, p.z),
        },
    };
}

} // namespace yr
```

- [ ] **Step 4: Run tests**

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```powershell
git add include/yaoray/core/ray.hpp include/yaoray/core/bounds.hpp tests/core_tests.cpp
git commit -m "feat: add ray and bounds primitives"
```

## Task 5: Add Film Accumulation And Tone Mapping

**Files:**
- Create: `include/yaoray/film/tone_mapping.hpp`
- Create: `src/film/tone_mapping.cpp`
- Create: `include/yaoray/film/film.hpp`
- Create: `src/film/film.cpp`
- Create: `tests/film_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing Film tests**

Create `tests/film_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <limits>

#include <yaoray/film/film.hpp>
#include <yaoray/film/tone_mapping.hpp>

YR_TEST(film_accumulates_average_radiance) {
    yr::Film film{2, 1};

    film.AddSample(0, 0, yr::Color3f{1.0f, 2.0f, 3.0f});
    film.AddSample(0, 0, yr::Color3f{3.0f, 4.0f, 5.0f});

    const yr::Color3f avg = film.LinearPixel(0, 0);
    YR_EXPECT_NEAR(avg.x, 2.0, 1e-6);
    YR_EXPECT_NEAR(avg.y, 3.0, 1e-6);
    YR_EXPECT_NEAR(avg.z, 4.0, 1e-6);
    YR_EXPECT_EQ(film.SampleCount(0, 0), 2);
}

YR_TEST(film_rejects_bad_samples) {
    yr::Film film{1, 1};

    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});
    film.AddSample(0, 0, yr::Color3f{std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f});

    const yr::Color3f avg = film.LinearPixel(0, 0);
    YR_EXPECT_NEAR(avg.x, 1.0, 1e-6);
    YR_EXPECT_EQ(film.BadSampleCount(), 1);
}

YR_TEST(tone_mapping_produces_display_range_colors) {
    const yr::ToneMapSettings settings{yr::ToneMapper::Reinhard, 0.0f};
    const yr::Color3f mapped = yr::ToDisplayColor(yr::Color3f{4.0f, 1.0f, 0.25f}, settings);

    YR_EXPECT_TRUE(mapped.x >= 0.0f && mapped.x <= 1.0f);
    YR_EXPECT_TRUE(mapped.y >= 0.0f && mapped.y <= 1.0f);
    YR_EXPECT_TRUE(mapped.z >= 0.0f && mapped.z <= 1.0f);
    YR_EXPECT_TRUE(mapped.x > mapped.y);
    YR_EXPECT_TRUE(mapped.y > mapped.z);
}
```

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

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/film_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
endif()
```

- [ ] **Step 2: Run the test build to verify it fails**

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
```

Expected: build fails because Film headers and sources do not exist.

- [ ] **Step 3: Implement tone mapping**

Create `include/yaoray/film/tone_mapping.hpp`:

```cpp
#pragma once

#include <yaoray/core/vec.hpp>

namespace yr {

enum class ToneMapper {
    None,
    Reinhard,
    Aces,
};

struct ToneMapSettings {
    ToneMapper mapper = ToneMapper::Aces;
    float exposure = 0.0f;
};

Color3f ToDisplayColor(Color3f linear_hdr, const ToneMapSettings& settings);

} // namespace yr
```

Create `src/film/tone_mapping.cpp`:

```cpp
#include <yaoray/film/tone_mapping.hpp>

#include <algorithm>
#include <cmath>

namespace yr {
namespace {

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

Color3f Clamp01(Color3f c) {
    return Color3f{Clamp01(c.x), Clamp01(c.y), Clamp01(c.z)};
}

Color3f ApplyExposure(Color3f c, float exposure) {
    const float scale = std::exp2(exposure);
    return c * scale;
}

Color3f Reinhard(Color3f c) {
    return Color3f{
        c.x / (1.0f + c.x),
        c.y / (1.0f + c.y),
        c.z / (1.0f + c.z),
    };
}

float AcesChannel(float x) {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return Clamp01((x * (a * x + b)) / (x * (c * x + d) + e));
}

Color3f Aces(Color3f c) {
    return Color3f{AcesChannel(c.x), AcesChannel(c.y), AcesChannel(c.z)};
}

float LinearToSrgb(float x) {
    x = Clamp01(x);
    if (x <= 0.0031308f) {
        return 12.92f * x;
    }
    return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

Color3f LinearToSrgb(Color3f c) {
    return Color3f{LinearToSrgb(c.x), LinearToSrgb(c.y), LinearToSrgb(c.z)};
}

} // namespace

Color3f ToDisplayColor(Color3f linear_hdr, const ToneMapSettings& settings) {
    Color3f c = ApplyExposure(linear_hdr, settings.exposure);
    switch (settings.mapper) {
        case ToneMapper::None:
            c = Clamp01(c);
            break;
        case ToneMapper::Reinhard:
            c = Reinhard(c);
            break;
        case ToneMapper::Aces:
            c = Aces(c);
            break;
    }
    return LinearToSrgb(c);
}

} // namespace yr
```

- [ ] **Step 4: Implement Film**

Create `include/yaoray/film/film.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yaoray/core/vec.hpp>

namespace yr {

struct FilmPixel {
    Color3f sum;
    std::uint32_t samples = 0;
};

class Film {
public:
    Film(int width, int height);

    int Width() const { return width_; }
    int Height() const { return height_; }

    void AddSample(int x, int y, Color3f radiance);
    Color3f LinearPixel(int x, int y) const;
    std::uint32_t SampleCount(int x, int y) const;
    std::uint64_t BadSampleCount() const { return bad_sample_count_; }

private:
    std::size_t Index(int x, int y) const;
    bool InBounds(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<FilmPixel> pixels_;
    std::uint64_t bad_sample_count_ = 0;
};

} // namespace yr
```

Create `src/film/film.cpp`:

```cpp
#include <yaoray/film/film.hpp>

#include <cmath>
#include <stdexcept>

namespace yr {
namespace {

bool IsFinite(Color3f c) {
    return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

} // namespace

Film::Film(int width, int height)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Film dimensions must be positive");
    }
}

void Film::AddSample(int x, int y, Color3f radiance) {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film sample coordinate out of range");
    }
    if (!IsFinite(radiance)) {
        ++bad_sample_count_;
        return;
    }
    FilmPixel& pixel = pixels_[Index(x, y)];
    pixel.sum = pixel.sum + radiance;
    ++pixel.samples;
}

Color3f Film::LinearPixel(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    const FilmPixel& pixel = pixels_[Index(x, y)];
    if (pixel.samples == 0) {
        return Color3f{};
    }
    return pixel.sum / static_cast<float>(pixel.samples);
}

std::uint32_t Film::SampleCount(int x, int y) const {
    if (!InBounds(x, y)) {
        throw std::out_of_range("Film pixel coordinate out of range");
    }
    return pixels_[Index(x, y)].samples;
}

std::size_t Film::Index(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
}

bool Film::InBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

} // namespace yr
```

- [ ] **Step 5: Run tests**

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt include/yaoray/film/tone_mapping.hpp src/film/tone_mapping.cpp include/yaoray/film/film.hpp src/film/film.cpp tests/film_tests.cpp
git commit -m "feat: add film accumulation and tone mapping"
```

## Task 6: Add CLI Help And Version Shell

**Files:**
- Create: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing CLI smoke tests to CTest**

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

add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/film_tests.cpp
)
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film)

add_executable(yaoray
    src/app/main.cpp
)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film)

if(BUILD_TESTING)
    add_test(NAME yaoray_tests COMMAND yaoray_tests)
    add_test(NAME yaoray_cli_help COMMAND yaoray --help)
    set_tests_properties(yaoray_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "YaoRay")
    add_test(NAME yaoray_cli_version COMMAND yaoray --version)
    set_tests_properties(yaoray_cli_version PROPERTIES PASS_REGULAR_EXPRESSION "0.1.0")
endif()
```

- [ ] **Step 2: Run CMake to verify it fails**

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
```

Expected: build fails because `src/app/main.cpp` does not exist.

- [ ] **Step 3: Implement CLI shell**

Create `src/app/main.cpp`:

```cpp
#include <yaoray/core/version.hpp>

#include <iostream>
#include <string_view>

namespace {

void PrintHelp() {
    std::cout
        << "YaoRay " << yr::VersionString() << '\n'
        << '\n'
        << "Usage:\n"
        << "  yaoray --help\n"
        << "  yaoray --version\n"
        << '\n'
        << "Renderer commands will be added after the scene and backend layers are implemented.\n";
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

    std::cerr << "Unknown argument: " << arg << '\n';
    PrintHelp();
    return 2;
}
```

- [ ] **Step 4: Run tests**

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
git add CMakeLists.txt src/app/main.cpp
git commit -m "feat: add YaoRay CLI shell"
```

## Task 7: Add README And Architecture Overview

**Files:**
- Create: `README.md`
- Create: `docs/architecture/overview.md`

- [ ] **Step 1: Write README**

Create `README.md`:

```markdown
# YaoRay

YaoRay is a learning-oriented, engineering-grade offline path tracer focused on physically based rendering, clean architecture, and future CUDA acceleration.

This repository is a rewrite of the previous ToyRender experiment. The old code is preserved on `archive/toyrender-before-yaoray`; the new project starts from a clean architecture.

## Current Status

The foundation slice provides:

- CMake project structure
- small CTest-based test harness
- core vector, ray, and bounds primitives
- Film accumulation and tone mapping basics
- CLI help/version shell

Scene files, asset import, path tracing, and CUDA backend support are planned as separate implementation slices.

## Build

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

## Run

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
```
```

- [ ] **Step 2: Write architecture overview**

Create `docs/architecture/overview.md`:

```markdown
# YaoRay Architecture Overview

YaoRay uses a two-layer renderer architecture.

The semantic layer describes the scene in terms people can author and debug: cameras, lights, assets, instances, material overrides, render settings, and Film settings.

The render layer compiles that semantic scene into backend-friendly data: flat arrays of triangles, BVH nodes, materials, textures, lights, camera data, and environment data. CPU, CUDA, and future OptiX backends consume this compiled representation.

The foundation slice in this branch establishes the project structure, tests, core math primitives, Film accumulation, and CLI shell. Rendering-specific modules will be added in focused implementation plans.
```

- [ ] **Step 3: Verify docs and tests**

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
git status --short
```

Expected:

```text
100% tests passed
```

`git status --short` shows only `README.md` and `docs/architecture/overview.md` as uncommitted changes.

- [ ] **Step 4: Commit**

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: describe YaoRay foundation"
```

## Task 8: Final Foundation Verification

**Files:**
- Verify: all files in this plan

- [ ] **Step 1: Configure a clean Release build**

```powershell
if (Test-Path build-release) { Remove-Item -LiteralPath build-release -Recurse -Force }
cmake -S . -B build-release -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
```

Expected: configure succeeds and prints `Build files have been written`.

- [ ] **Step 2: Build Release**

```powershell
cmake --build build-release --config Release
```

Expected: `yaoray` and `yaoray_tests` build successfully.

- [ ] **Step 3: Run Release tests**

```powershell
ctest --test-dir build-release --output-on-failure -C Release
```

Expected:

```text
100% tests passed
```

- [ ] **Step 4: Verify CLI manually**

```powershell
.\build-release\Release\yaoray.exe --help
.\build-release\Release\yaoray.exe --version
```

Expected:

```text
YaoRay 0.1.0
```

and:

```text
0.1.0
```

- [ ] **Step 5: Confirm branch history**

```powershell
git log --oneline --decorate --max-count=8
git status --short
```

Expected:

```text
git status --short
```

prints no tracked source changes. Build directories may appear only if `.gitignore` was not applied correctly; fix `.gitignore` and commit that fix before handoff.

- [ ] **Step 6: Commit verification metadata only if needed**

If `.gitignore` needed a correction, commit it:

```powershell
git add .gitignore
git commit -m "chore: ignore generated build outputs"
```

If no correction was needed, do not create an empty commit.

## Self-Review Notes

Spec coverage in this plan:

- Clean YaoRay identity and new root: Task 1
- CMake and test foundation: Task 2
- Core math primitives: Tasks 3 and 4
- Film accumulation and tone mapping foundation: Task 5
- CLI executable: Task 6
- Documentation foundation: Task 7
- Clean build/test verification: Task 8

Spec items intentionally assigned to subsequent plans:

- TOML scene parser and `SceneDescription`
- `ImportedAsset`, glTF/.glb, OBJ, and texture loading
- `SceneCompiler`, `RenderScene`, and BVH
- CPU path tracer
- CUDA backend
- PNG/EXR output writers and progressive checkpoint files
- golden image tests
