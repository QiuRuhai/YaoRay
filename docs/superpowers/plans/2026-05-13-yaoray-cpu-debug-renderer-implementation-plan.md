# YaoRay CPU Debug Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a minimal CPU debug renderer that turns compiled `RenderScene` data into a real PPM image file from `yaoray render`.

**Architecture:** Keep `RenderScene` compilation in `yaoray_render`, add a focused `yaoray_backend_cpu` library that consumes `RenderScene` and returns a filled `Film`, and add a small PPM writer to `yaoray_film`. The CLI orchestrates parse, compile, CPU debug render, and PPM write; CUDA requests fail clearly until a CUDA backend exists.

**Tech Stack:** C++20, CMake 3.24+, CTest, MSVC, existing `core`, `film`, `scene`, and `render` modules, ASCII PPM output through the C++ standard library.

---

## Scope Check

This plan implements only the approved CPU Debug Renderer design:

- ASCII PPM writing in the `film` module
- one-sample-per-pixel CPU debug rendering
- direct triangle-loop intersection
- simple deterministic hit and miss shading
- render statistics
- CLI CPU render output
- clear CUDA-not-implemented CLI failure
- tests and docs for the first image-output loop

It does not implement PNG, EXR, path tracing, shadows, light sampling, BVH, multithreading, asset import, CUDA rendering, or final-quality rendering.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/backends/cpu/cpu_debug_renderer.hpp
include/yaoray/film/image_writer.hpp
src/app/main.cpp
src/backends/cpu/cpu_debug_renderer.cpp
src/film/image_writer.cpp
scenes/examples/minimal.toml
tests/cpu_debug_renderer_tests.cpp
tests/fixtures/scene/builtin_triangle.toml
tests/film_tests.cpp
```

Responsibilities:

- `include/yaoray/film/image_writer.hpp`: public image writer result type and `WritePpm()` declaration.
- `src/film/image_writer.cpp`: ASCII PPM writing, tone mapping, directory creation, and write diagnostics.
- `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`: CPU debug renderer stats/result/API.
- `src/backends/cpu/cpu_debug_renderer.cpp`: camera rays, Moller-Trumbore triangle tests, debug shading, Film accumulation.
- `tests/film_tests.cpp`: PPM writer unit coverage.
- `tests/cpu_debug_renderer_tests.cpp`: renderer unit coverage.
- `src/app/main.cpp`: CLI render command now renders CPU scenes and writes images.
- `CMakeLists.txt`: add `yaoray_backend_cpu`, image writer source, tests, and CLI test expectations.

## Task 1: Add PPM Writer To Film

**Files:**
- Create: `include/yaoray/film/image_writer.hpp`
- Create: `src/film/image_writer.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/film_tests.cpp`

- [ ] **Step 1: Write failing PPM writer tests**

Modify `tests/film_tests.cpp` to include the new header and filesystem helpers:

```cpp
#include "yr_test.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include <yaoray/film/film.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
```

Add this helper namespace near the top of `tests/film_tests.cpp`:

```cpp
namespace {

std::filesystem::path PpmTestPath(std::string_view name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "yaoray_ppm_writer_tests";
    std::filesystem::create_directories(dir);
    return dir / std::string{name};
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in{path};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace
```

Append these tests to `tests/film_tests.cpp`:

```cpp
YR_TEST(ppm_writer_rejects_non_ppm_extension) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 0.0f, 0.0f});

    const yr::ImageWriteResult result = yr::WritePpm(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        PpmTestPath("bad.txt")
    );

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".ppm") != std::string::npos);
}

YR_TEST(ppm_writer_writes_p3_header_and_rgb_values) {
    yr::Film film{2, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 0.0f, 0.0f});
    film.AddSample(1, 0, yr::Color3f{0.0f, 1.0f, 0.0f});

    const std::filesystem::path path = PpmTestPath("two_pixels.ppm");
    std::filesystem::remove(path);
    const yr::ImageWriteResult result = yr::WritePpm(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        path
    );

    YR_EXPECT_TRUE(result.ok);
    const std::string text = ReadTextFile(path);
    YR_EXPECT_TRUE(text.find("P3\n2 1\n255\n") == 0);
    YR_EXPECT_TRUE(text.find("255 0 0") != std::string::npos);
    YR_EXPECT_TRUE(text.find("0 255 0") != std::string::npos);
}
```

- [ ] **Step 2: Register the image writer source in CMake**

Modify the `yaoray_film` target in `CMakeLists.txt`:

```cmake
add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
```

- [ ] **Step 3: Run build to verify failure**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `include/yaoray/film/image_writer.hpp` and `src/film/image_writer.cpp` do not exist yet.

- [ ] **Step 4: Add the PPM writer public header**

Create `include/yaoray/film/image_writer.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>

#include <yaoray/film/film.hpp>
#include <yaoray/film/tone_mapping.hpp>

namespace yr {

struct ImageWriteResult {
    bool ok = false;
    std::string error;
};

ImageWriteResult WritePpm(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

} // namespace yr
```

- [ ] **Step 5: Implement ASCII PPM output**

Create `src/film/image_writer.cpp`:

```cpp
#include <yaoray/film/image_writer.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>

namespace yr {
namespace {

int ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::lround(clamped * 255.0f));
}

bool HasPpmExtension(const std::filesystem::path& path) {
    return path.extension() == ".ppm";
}

} // namespace

ImageWriteResult WritePpm(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    if (!HasPpmExtension(path)) {
        return ImageWriteResult{false, "PPM output path must use a .ppm extension"};
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return ImageWriteResult{false, "failed to create output directory: " + ec.message()};
        }
    }

    std::ofstream out{path, std::ios::out | std::ios::trunc};
    if (!out) {
        return ImageWriteResult{false, "failed to open output image: " + path.generic_string()};
    }

    out << "P3\n";
    out << film.Width() << ' ' << film.Height() << "\n";
    out << "255\n";

    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            const Color3f display = ToDisplayColor(film.LinearPixel(x, y), tone_map);
            out << ToByte(display.x) << ' '
                << ToByte(display.y) << ' '
                << ToByte(display.z) << '\n';
        }
    }

    if (!out) {
        return ImageWriteResult{false, "failed while writing output image: " + path.generic_string()};
    }

    return ImageWriteResult{true, {}};
}

} // namespace yr
```

- [ ] **Step 6: Run tests**

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

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt include/yaoray/film/image_writer.hpp src/film/image_writer.cpp tests/film_tests.cpp
git commit -m "feat: add ppm image writer"
```

## Task 2: Add CPU Debug Renderer Module

**Files:**
- Create: `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`
- Create: `src/backends/cpu/cpu_debug_renderer.cpp`
- Create: `tests/cpu_debug_renderer_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing CPU debug renderer tests**

Create `tests/cpu_debug_renderer_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstdint>

#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderScene MakeDebugTriangleScene(int width = 5, int height = 5) {
    yr::RenderScene scene;
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

YR_TEST(cpu_debug_renderer_traces_one_ray_per_pixel) {
    const yr::RenderScene scene = MakeDebugTriangleScene(4, 3);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_EQ(result.stats.rays_traced, std::uint64_t{12});
    YR_EXPECT_EQ(result.stats.triangle_tests, std::uint64_t{12});
    YR_EXPECT_EQ(result.film.Width(), 4);
    YR_EXPECT_EQ(result.film.Height(), 3);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
    YR_EXPECT_EQ(result.film.SampleCount(3, 2), 1);
}

YR_TEST(cpu_debug_renderer_records_hits_and_misses) {
    const yr::RenderScene scene = MakeDebugTriangleScene(5, 5);

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(result.stats.misses > 0);
    YR_EXPECT_EQ(result.stats.hits + result.stats.misses, result.stats.rays_traced);
}

YR_TEST(cpu_debug_renderer_shades_environment_misses) {
    yr::RenderScene scene = MakeDebugTriangleScene(2, 2);
    scene.triangles.clear();
    scene.materials.clear();
    scene.environment.radiance = yr::Color3f{0.2f, 0.3f, 0.4f};
    scene.environment.strength = 2.0f;

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f pixel = result.film.LinearPixel(0, 0);

    YR_EXPECT_EQ(result.stats.hits, std::uint64_t{0});
    YR_EXPECT_EQ(result.stats.misses, std::uint64_t{4});
    YR_EXPECT_NEAR(pixel.x, 0.4, 1e-6);
    YR_EXPECT_NEAR(pixel.y, 0.6, 1e-6);
    YR_EXPECT_NEAR(pixel.z, 0.8, 1e-6);
}

YR_TEST(cpu_debug_renderer_uses_fallback_color_for_invalid_material_indices) {
    yr::RenderScene scene = MakeDebugTriangleScene(3, 3);
    scene.triangles[0].material_index = 99;
    scene.materials.clear();

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);
    const yr::Color3f center = result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(result.stats.hits > 0);
    YR_EXPECT_TRUE(center.x > 0.9f);
    YR_EXPECT_TRUE(center.y < 0.1f);
    YR_EXPECT_TRUE(center.z > 0.9f);
}
```

- [ ] **Step 2: Add the CPU backend target to CMake**

Add this target after `yaoray_render` in `CMakeLists.txt`:

```cmake
add_library(yaoray_backend_cpu STATIC
    src/backends/cpu/cpu_debug_renderer.cpp
)
target_include_directories(yaoray_backend_cpu PUBLIC include)
target_link_libraries(yaoray_backend_cpu PUBLIC yaoray_core yaoray_film yaoray_render)
```

Add the new test source to `yaoray_tests`:

```cmake
    tests/cpu_debug_renderer_tests.cpp
```

Update the test link line:

```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backend_cpu)
```

- [ ] **Step 3: Run build to verify failure**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `include/yaoray/backends/cpu/cpu_debug_renderer.hpp` and `src/backends/cpu/cpu_debug_renderer.cpp` do not exist yet.

- [ ] **Step 4: Add the CPU debug renderer public API**

Create `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`:

```cpp
#pragma once

#include <cstdint>

#include <yaoray/film/film.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct CpuDebugRenderStats {
    std::uint64_t rays_traced = 0;
    std::uint64_t triangle_tests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    double elapsed_seconds = 0.0;
};

struct CpuDebugRenderResult {
    Film film;
    CpuDebugRenderStats stats;
};

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene);

} // namespace yr
```

- [ ] **Step 5: Implement CPU debug rendering**

Create `src/backends/cpu/cpu_debug_renderer.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>

#include <yaoray/core/ray.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace yr {
namespace {

constexpr float MinHitT = 1.0e-5f;
constexpr float ParallelEpsilon = 1.0e-8f;

struct TriangleHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    const RenderTriangle* triangle = nullptr;
};

Ray3f MakeCameraRay(const RenderScene& scene, int x, int y) {
    const float width = static_cast<float>(scene.width);
    const float height = static_cast<float>(scene.height);
    const float aspect = width / height;
    const float half_height = std::tan(scene.camera.fov_y_radians * 0.5f);
    const float screen_x = (2.0f * (static_cast<float>(x) + 0.5f) / width - 1.0f) * aspect * half_height;
    const float screen_y = (1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / height) * half_height;
    const Vec3f direction = Normalize(
        scene.camera.forward +
        scene.camera.right * screen_x +
        scene.camera.up * screen_y
    );
    return Ray3f{scene.camera.origin, direction};
}

bool IntersectTriangle(const Ray3f& ray, const RenderTriangle& triangle, float& t_out) {
    const Vec3f edge1 = triangle.p1 - triangle.p0;
    const Vec3f edge2 = triangle.p2 - triangle.p0;
    const Vec3f pvec = Cross(ray.direction, edge2);
    const float det = Dot(edge1, pvec);
    if (std::fabs(det) < ParallelEpsilon) {
        return false;
    }

    const float inv_det = 1.0f / det;
    const Vec3f tvec = ray.origin - triangle.p0;
    const float u = Dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const Vec3f qvec = Cross(tvec, edge1);
    const float v = Dot(ray.direction, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    const float t = Dot(edge2, qvec) * inv_det;
    if (t <= MinHitT) {
        return false;
    }

    t_out = t;
    return true;
}

TriangleHit FindNearestHit(const RenderScene& scene, const Ray3f& ray, CpuDebugRenderStats& stats) {
    TriangleHit nearest;
    for (const RenderTriangle& triangle : scene.triangles) {
        ++stats.triangle_tests;
        float t = 0.0f;
        if (IntersectTriangle(ray, triangle, t) && t < nearest.t) {
            nearest.hit = true;
            nearest.t = t;
            nearest.triangle = &triangle;
        }
    }
    return nearest;
}

Color3f EnvironmentColor(const RenderScene& scene) {
    if (scene.environment.type == EnvironmentKind::Constant) {
        return scene.environment.radiance * scene.environment.strength;
    }
    return Color3f{};
}

Color3f ShadeHit(const RenderScene& scene, const Ray3f& ray, const RenderTriangle& triangle) {
    if (triangle.material_index < 0 ||
        static_cast<std::size_t>(triangle.material_index) >= scene.materials.size()) {
        return Color3f{1.0f, 0.0f, 1.0f};
    }

    const RenderMaterial& material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
    const float normal_lighting = std::max(0.15f, std::fabs(Dot(triangle.normal, -ray.direction)));
    return material.albedo * normal_lighting + material.emission;
}

} // namespace

CpuDebugRenderResult RenderCpuDebug(const RenderScene& scene) {
    CpuDebugRenderResult result{Film{scene.width, scene.height}, {}};
    const auto start = std::chrono::steady_clock::now();

    for (int y = 0; y < scene.height; ++y) {
        for (int x = 0; x < scene.width; ++x) {
            const Ray3f ray = MakeCameraRay(scene, x, y);
            ++result.stats.rays_traced;

            const TriangleHit hit = FindNearestHit(scene, ray, result.stats);
            if (hit.hit && hit.triangle != nullptr) {
                ++result.stats.hits;
                result.film.AddSample(x, y, ShadeHit(scene, ray, *hit.triangle));
            } else {
                ++result.stats.misses;
                result.film.AddSample(x, y, EnvironmentColor(scene));
            }
        }
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Run tests**

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

- [ ] **Step 7: Commit**

```powershell
git add CMakeLists.txt include/yaoray/backends/cpu src/backends/cpu tests/cpu_debug_renderer_tests.cpp
git commit -m "feat: add cpu debug renderer"
```

## Task 3: Wire CLI To CPU Debug Rendering

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/main.cpp`
- Modify: `scenes/examples/minimal.toml`
- Modify: `tests/fixtures/scene/builtin_triangle.toml`

- [ ] **Step 1: Update scene outputs to `.ppm`**

Modify `scenes/examples/minimal.toml`:

```toml
[film]
output = "out/minimal.ppm"
tone_mapper = "aces"
exposure = 0.0
```

Modify `tests/fixtures/scene/builtin_triangle.toml`:

```toml
[film]
output = "out/builtin.ppm"
```

- [ ] **Step 2: Update CLI CTest expectations**

Modify the `yaoray` link line in `CMakeLists.txt`:

```cmake
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_scene yaoray_render yaoray_backend_cpu)
```

Replace the existing `yaoray_cli_render_cpu` and `yaoray_cli_render_cuda` tests with:

```cmake
    add_test(NAME yaoray_cli_render_cpu
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
    add_test(NAME yaoray_cli_render_cuda
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cuda 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -eq 0) { exit 1 }; if ($out -notmatch 'CUDA backend not implemented yet') { exit 1 }"
    )
```

- [ ] **Step 3: Run CTest to verify CLI failures**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CLI render tests fail because `src/app/main.cpp` still prints "Rendering is not implemented yet" and does not write a PPM.

- [ ] **Step 4: Add CPU render orchestration to CLI**

In `src/app/main.cpp`, add these includes:

```cpp
#include <yaoray/backends/cpu/cpu_debug_renderer.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
```

Add this helper in the anonymous namespace:

```cpp
yr::ToneMapper ToFilmToneMapper(yr::ToneMapperKind mapper) {
    switch (mapper) {
        case yr::ToneMapperKind::None:
            return yr::ToneMapper::None;
        case yr::ToneMapperKind::Reinhard:
            return yr::ToneMapper::Reinhard;
        case yr::ToneMapperKind::Aces:
            return yr::ToneMapper::Aces;
    }
    return yr::ToneMapper::Aces;
}
```

Replace the success output block at the end of `RunRender()` with:

```cpp
    const yr::RenderScene& render_scene = compile_result.scene.value();
    std::cout << "Scene parsed successfully: " << scene.source_path.generic_string() << '\n';
    std::cout << "Scene compiled successfully.\n";
    std::cout << "Requested backend: " << yr::RenderBackendName(render_scene.backend) << '\n';
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';

    if (render_scene.backend == yr::RenderBackendKind::Cuda) {
        std::cerr << "CUDA backend not implemented yet.\n";
        return 1;
    }

    const yr::CpuDebugRenderResult render_result = yr::RenderCpuDebug(render_scene);
    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(scene.film.tone_mapper),
        scene.film.exposure
    };
    const yr::ImageWriteResult write_result = yr::WritePpm(render_result.film, tone_map, scene.film.output);
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

Expected CPU output includes:

```text
Rendered image: scenes/examples/out/minimal.ppm
Rays traced:
Triangle tests:
Hits:
Misses:
```

Expected CUDA command exits non-zero and includes:

```text
CUDA backend not implemented yet.
```

- [ ] **Step 6: Commit**

```powershell
git add CMakeLists.txt src/app/main.cpp scenes/examples/minimal.toml tests/fixtures/scene/builtin_triangle.toml
git commit -m "feat: render cpu debug ppm from cli"
```

## Task 4: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README status and run description**

Edit `README.md` so the current status list includes:

```markdown
- CPU debug rendering of compiled triangle scenes to ASCII PPM
```

Edit the run section command description to:

```markdown
The `render` command currently parses, compiles, and renders CPU debug images to ASCII PPM. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 2: Update architecture overview**

Edit `docs/architecture/overview.md` so the current implemented slices include:

```markdown
- CPU debug rendering to PPM for the first image-output loop
```

Add this paragraph after the implemented-slices list:

```markdown
The CPU debug renderer is a simple reference path through camera rays, triangle intersection, Film accumulation, tone mapping, and PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

- [ ] **Step 3: Run docs smoke check**

Run:

```powershell
rg -n "CPU debug|PPM|path tracer|image-quality|RenderScene" README.md docs/architecture/overview.md
ctest --test-dir build --output-on-failure -C Debug
```

Expected: `rg` finds the new CPU debug renderer wording, and CTest passes.

- [ ] **Step 4: Commit**

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: describe cpu debug rendering"
```

## Task 5: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm dependency direction**

Run:

```powershell
rg -n "backends/cpu|cpu_debug_renderer|image_writer" include\yaoray\scene src\scene include\yaoray\render src\render
```

Expected: no output. Scene and render modules must not depend on the CPU backend or image writer.

- [ ] **Step 2: Confirm CPU backend and film APIs are discoverable**

Run:

```powershell
rg -n "RenderCpuDebug|CpuDebugRenderStats|WritePpm|ImageWriteResult" include src tests
```

Expected: matches in public headers, implementation files, tests, and CLI usage.

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

- [ ] **Step 4: Verify CPU output file manually**

Run:

```powershell
Remove-Item -Force -ErrorAction SilentlyContinue scenes\examples\out\minimal.ppm
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cpu
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

Expected PPM header begins:

```text
P3
640 360
255
```

- [ ] **Step 5: Verify CUDA failure manually**

Run:

```powershell
.\build\Debug\yaoray.exe render scenes\examples\minimal.toml --backend cuda
```

Expected: command exits non-zero and output includes:

```text
CUDA backend not implemented yet.
```

- [ ] **Step 6: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- PPM writer: Task 1.
- CPU debug renderer API, camera rays, direct triangle loop, hit/miss stats, and fallback material color: Task 2.
- CLI CPU render output and CUDA-not-implemented failure: Task 3.
- Example `.ppm` scene output: Task 3.
- Documentation wording that separates debug output from final path-tracing quality: Task 4.
- Final dependency and verification checks: Task 5.

Type consistency:

- `ImageWriteResult` and `WritePpm()` live in `include/yaoray/film/image_writer.hpp`.
- `CpuDebugRenderStats`, `CpuDebugRenderResult`, and `RenderCpuDebug()` live in `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`.
- The CPU backend target is named `yaoray_backend_cpu`.
- CLI tone mapping converts `ToneMapperKind` to `ToneMapper` locally in `src/app/main.cpp`.

Implementation guardrails:

- Do not add PNG or third-party image dependencies in this plan.
- Do not add BVH or path tracing in this plan.
- Do not make `scene` or `render` depend on `backends/cpu`.
- Keep CUDA as an explicit not-implemented render path.
