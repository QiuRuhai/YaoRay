# YaoRay PNG Output Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PNG image output to YaoRay while keeping the existing PPM debug writer.

**Architecture:** Keep image-file ownership inside `yaoray_film`. Add `WritePng()` and `WriteImage()` beside `WritePpm()`, use `stb_image_write.h` for PNG encoding, and update the CLI to let `film.output` select `.png` or `.ppm` by extension.

**Tech Stack:** C++20, CMake 3.24+, CTest, existing YaoRay Film/tone mapping modules, vendored `stb_image_write.h`.

---

## Scope Check

This plan implements only PNG output:

- vendor `stb_image_write.h`
- add `WritePng()`
- add `WriteImage()` extension dispatch
- preserve `WritePpm()`
- update CLI render to call `WriteImage()`
- switch example and CLI render fixture outputs to PNG
- add film and CLI tests
- update docs

It does not implement HDR, 16-bit PNG, alpha, gamma/ICC metadata, image decoding, visual golden tests, material changes, path tracing changes, or a showcase scene.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
external/stb/README.md
external/stb/stb_image_write.h
include/yaoray/film/image_writer.hpp
src/app/main.cpp
src/film/image_writer.cpp
tests/film_tests.cpp
tests/fixtures/scene/builtin_triangle.toml
tests/fixtures/scene/obj_quad.toml
scenes/examples/minimal.toml
scenes/examples/obj_pyramid.toml
```

Responsibilities:

- `external/stb/stb_image_write.h`: vendored third-party image writer.
- `external/stb/README.md`: source URL, retrieval date, license notes, and YaoRay usage.
- `include/yaoray/film/image_writer.hpp`: public PPM, PNG, and extension-dispatch write APIs.
- `src/film/image_writer.cpp`: shared parent-directory creation, tone-mapped RGB8 conversion, PPM text writing, PNG binary writing through stb.
- `tests/film_tests.cpp`: focused unit coverage for PPM compatibility, PNG signature output, and `WriteImage()` dispatch errors.
- `src/app/main.cpp`: render command uses `WriteImage()` and help text no longer says PPM-only.
- `CMakeLists.txt`: add `stb` interface target, link it to `yaoray_film`, and update CLI render tests to assert PNG signatures.
- Scene files: default renderable examples and CLI fixtures use `.png`.
- Docs: document PNG support and remove PNG from future-work lists.

## Task 1: Add PNG Writer API, Dependency, And Film Tests

**Files:**
- Create: `external/stb/README.md`
- Create: `external/stb/stb_image_write.h`
- Modify: `CMakeLists.txt`
- Modify: `include/yaoray/film/image_writer.hpp`
- Modify: `src/film/image_writer.cpp`
- Modify: `tests/film_tests.cpp`

- [ ] **Step 1: Add failing film writer tests**

In `tests/film_tests.cpp`, add these includes:

```cpp
#include <array>
```

Rename the helper `PpmTestPath()` to `ImageWriterTestPath()`:

```cpp
std::filesystem::path ImageWriterTestPath(std::string_view name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "yaoray_image_writer_tests";
    std::filesystem::create_directories(dir);
    return dir / std::string{name};
}
```

Replace existing calls to `PpmTestPath(...)` with `ImageWriterTestPath(...)`.

Add this helper after `ReadTextFile()`:

```cpp
std::array<unsigned char, 8> ReadSignature8(const std::filesystem::path& path) {
    std::array<unsigned char, 8> bytes{};
    std::ifstream in{path, std::ios::binary};
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}
```

Append these tests:

```cpp
YR_TEST(png_writer_rejects_non_png_extension) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 0.0f, 0.0f});

    const yr::ImageWriteResult result = yr::WritePng(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        ImageWriterTestPath("bad.txt")
    );

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}

YR_TEST(png_writer_writes_png_signature) {
    yr::Film film{2, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 0.0f, 0.0f});
    film.AddSample(1, 0, yr::Color3f{0.0f, 1.0f, 0.0f});

    const std::filesystem::path path = ImageWriterTestPath("two_pixels.png");
    std::filesystem::remove(path);
    const yr::ImageWriteResult result = yr::WritePng(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        path
    );

    YR_EXPECT_TRUE(result.ok);
    const std::array<unsigned char, 8> signature = ReadSignature8(path);
    const std::array<unsigned char, 8> expected{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    YR_EXPECT_TRUE(signature == expected);
}

YR_TEST(image_writer_dispatches_ppm_and_png) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{0.25f, 0.5f, 1.0f});

    const std::filesystem::path ppm_path = ImageWriterTestPath("dispatch.ppm");
    const std::filesystem::path png_path = ImageWriterTestPath("dispatch.png");
    std::filesystem::remove(ppm_path);
    std::filesystem::remove(png_path);

    const yr::ToneMapSettings tone_map{yr::ToneMapper::None, 0.0f};
    const yr::ImageWriteResult ppm_result = yr::WriteImage(film, tone_map, ppm_path);
    const yr::ImageWriteResult png_result = yr::WriteImage(film, tone_map, png_path);

    YR_EXPECT_TRUE(ppm_result.ok);
    YR_EXPECT_TRUE(png_result.ok);
    YR_EXPECT_TRUE(ReadTextFile(ppm_path).find("P3\n1 1\n255\n") == 0);
    const std::array<unsigned char, 8> signature = ReadSignature8(png_path);
    const std::array<unsigned char, 8> expected{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    YR_EXPECT_TRUE(signature == expected);
}

YR_TEST(image_writer_rejects_unsupported_extension) {
    yr::Film film{1, 1};
    film.AddSample(0, 0, yr::Color3f{1.0f, 1.0f, 1.0f});

    const yr::ImageWriteResult result = yr::WriteImage(
        film,
        yr::ToneMapSettings{yr::ToneMapper::None, 0.0f},
        ImageWriterTestPath("bad.bmp")
    );

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find(".ppm") != std::string::npos);
    YR_EXPECT_TRUE(result.error.find(".png") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build fails because `WritePng()` and `WriteImage()` do not exist yet.

Do not commit this failing state.

- [ ] **Step 3: Vendor `stb_image_write.h`**

Create the dependency directory and download the header:

```powershell
New-Item -ItemType Directory -Force external\stb
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h" -OutFile "external\stb\stb_image_write.h"
```

Create `external/stb/README.md`:

```markdown
# stb

Vendored single-header image writing dependency.

- Source repository: https://github.com/nothings/stb
- Header URL: https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
- Retrieved: 2026-05-13
- Header: `stb_image_write.h`
- License: dual public domain / MIT-style terms, as stated in the header.

YaoRay uses `stb_image_write.h` only in `src/film/image_writer.cpp` to write 8-bit RGB PNG output from the Film image writer. PPM output remains implemented locally.
```

- [ ] **Step 4: Add the `stb` CMake interface target**

In `CMakeLists.txt`, add this block after `target_link_libraries(yaoray_film PUBLIC yaoray_core)`:

```cmake
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)
target_link_libraries(yaoray_film PRIVATE stb)
```

- [ ] **Step 5: Add the public image writer APIs**

In `include/yaoray/film/image_writer.hpp`, add after `WritePpm(...)`:

```cpp
ImageWriteResult WritePng(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

ImageWriteResult WriteImage(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);
```

- [ ] **Step 6: Implement PNG output and extension dispatch**

Replace `src/film/image_writer.cpp` with:

```cpp
#include <yaoray/film/image_writer.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace yr {
namespace {

int ToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<int>(std::lround(clamped * 255.0f));
}

std::string NormalizedExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool HasPpmExtension(const std::filesystem::path& path) {
    return NormalizedExtension(path) == ".ppm";
}

bool HasPngExtension(const std::filesystem::path& path) {
    return NormalizedExtension(path) == ".png";
}

ImageWriteResult EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return ImageWriteResult{false, "failed to create output directory: " + ec.message()};
        }
    }
    return ImageWriteResult{true, {}};
}

std::vector<unsigned char> BuildRgb8Pixels(const Film& film, const ToneMapSettings& tone_map) {
    std::vector<unsigned char> pixels;
    pixels.reserve(static_cast<std::size_t>(film.Width()) * static_cast<std::size_t>(film.Height()) * 3);

    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            const Color3f display = ToDisplayColor(film.LinearPixel(x, y), tone_map);
            pixels.push_back(static_cast<unsigned char>(ToByte(display.x)));
            pixels.push_back(static_cast<unsigned char>(ToByte(display.y)));
            pixels.push_back(static_cast<unsigned char>(ToByte(display.z)));
        }
    }

    return pixels;
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

    const ImageWriteResult directory_result = EnsureParentDirectory(path);
    if (!directory_result.ok) {
        return directory_result;
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

ImageWriteResult WritePng(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    if (!HasPngExtension(path)) {
        return ImageWriteResult{false, "PNG output path must use a .png extension"};
    }

    const ImageWriteResult directory_result = EnsureParentDirectory(path);
    if (!directory_result.ok) {
        return directory_result;
    }

    const std::vector<unsigned char> pixels = BuildRgb8Pixels(film, tone_map);
    const int stride_bytes = film.Width() * 3;
    const int ok = stbi_write_png(
        path.string().c_str(),
        film.Width(),
        film.Height(),
        3,
        pixels.data(),
        stride_bytes
    );
    if (ok == 0) {
        return ImageWriteResult{false, "failed to write PNG image: " + path.generic_string()};
    }

    return ImageWriteResult{true, {}};
}

ImageWriteResult WriteImage(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
) {
    const std::string extension = NormalizedExtension(path);
    if (extension == ".ppm") {
        return WritePpm(film, tone_map, path);
    }
    if (extension == ".png") {
        return WritePng(film, tone_map, path);
    }
    return ImageWriteResult{
        false,
        "unsupported image output extension: " + extension + " (expected .ppm or .png)"
    };
}

} // namespace yr
```

- [ ] **Step 7: Run film tests and full tests**

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

- [ ] **Step 8: Commit**

Run:

```powershell
git add CMakeLists.txt external/stb include/yaoray/film/image_writer.hpp src/film/image_writer.cpp tests/film_tests.cpp
git commit -m "feat: add png image writer"
```

## Task 2: Use PNG Through The CLI And Examples

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/fixtures/scene/builtin_triangle.toml`
- Modify: `tests/fixtures/scene/obj_quad.toml`
- Modify: `scenes/examples/minimal.toml`
- Modify: `scenes/examples/obj_pyramid.toml`

- [ ] **Step 1: Add failing CLI PNG checks**

In `tests/fixtures/scene/builtin_triangle.toml`, change:

```toml
output = "out/builtin.ppm"
```

to:

```toml
output = "out/builtin.png"
```

In `tests/fixtures/scene/obj_quad.toml`, change:

```toml
output = "out/obj_quad.ppm"
```

to:

```toml
output = "out/obj_quad.png"
```

In `CMakeLists.txt`, update `yaoray_cli_render_cpu` to check the PNG signature:

```cmake
    add_test(NAME yaoray_cli_render_cpu
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

Update `yaoray_cli_render_obj` similarly:

```cmake
    add_test(NAME yaoray_cli_render_obj
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.png'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; [byte[]]$bytes = [System.IO.File]::ReadAllBytes($outPath); [byte[]]$expected = 0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A; if ($bytes.Length -lt 8) { exit 1 }; for ($i = 0; $i -lt 8; $i++) { if ($bytes[$i] -ne $expected[$i]) { exit 1 } }"
    )
```

- [ ] **Step 2: Run CTest to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CLI render tests fail because the app still calls `WritePpm()` and rejects `.png`.

Do not commit this failing state.

- [ ] **Step 3: Switch the CLI to `WriteImage()`**

In `src/app/main.cpp`, change the help text in `PrintRenderHelp()` from:

```cpp
        << "The render command currently parses, compiles, and renders CPU debug PPM images.\n";
```

to:

```cpp
        << "The render command currently parses, compiles, and renders CPU debug PNG/PPM images.\n";
```

Change the image write call from:

```cpp
    const yr::ImageWriteResult write_result = yr::WritePpm(*render_result.film, tone_map, scene.film.output);
```

to:

```cpp
    const yr::ImageWriteResult write_result = yr::WriteImage(*render_result.film, tone_map, scene.film.output);
```

- [ ] **Step 4: Change human-facing examples to PNG**

In `scenes/examples/minimal.toml`, change:

```toml
output = "out/minimal.ppm"
```

to:

```toml
output = "out/minimal.png"
```

In `scenes/examples/obj_pyramid.toml`, change:

```toml
output = "out/obj_pyramid.ppm"
```

to:

```toml
output = "out/obj_pyramid.png"
```

- [ ] **Step 5: Run CLI and full tests**

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

- [ ] **Step 6: Manually verify example PNG output**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
```

Expected output includes:

```text
Rendered image: out/minimal.png
Rendered image: out/obj_pyramid.png
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add CMakeLists.txt src/app/main.cpp tests/fixtures/scene/builtin_triangle.toml tests/fixtures/scene/obj_quad.toml scenes/examples/minimal.toml scenes/examples/obj_pyramid.toml
git commit -m "feat: render cli scenes to png"
```

## Task 3: Update Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Update README status and run docs**

In `README.md`, add this status bullet after the CPU debug rendering bullet:

```markdown
- PNG output for renderable scenes, with PPM still available for debug/test output
```

Change the future-work sentence from:

```markdown
Final path tracing quality, material and texture import, advanced BVH split methods, PNG output, glTF/GLB import, and real CUDA backend support are planned as separate implementation slices.
```

to:

```markdown
Final path tracing quality, material and texture import, advanced BVH split methods, glTF/GLB import, HDR output, and real CUDA backend support are planned as separate implementation slices.
```

Change the run description from:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders CPU debug images to ASCII PPM. It supports the built-in triangle and small geometry-only OBJ meshes. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

to:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders CPU debug images to PNG or ASCII PPM based on `film.output`. The example scenes write PNG by default. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 2: Update architecture overview**

In `docs/architecture/overview.md`, add this implemented slice bullet after CPU debug rendering:

```markdown
- PNG output for renderable scenes, with PPM still available for debug/test output
```

Change:

```markdown
The CPU debug renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, Film accumulation, tone mapping, and PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

to:

```markdown
The CPU debug renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, Film accumulation, tone mapping, and PNG/PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

Change:

```markdown
PNG output, material and texture import, glTF/GLB import, advanced BVH split methods, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

to:

```markdown
Material and texture import, glTF/GLB import, advanced BVH split methods, HDR output, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 3: Run docs smoke check**

Run:

```powershell
rg -n "PNG|PPM|stb|WriteImage|WritePng|film.output|future" README.md docs/architecture/overview.md external/stb/README.md include/yaoray/film/image_writer.hpp src/film/image_writer.cpp
```

Expected: matches show PNG as implemented, PPM as still supported, and `WriteImage()`/`WritePng()` in the film module.

- [ ] **Step 4: Run full tests**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected:

```text
100% tests passed
```

- [ ] **Step 5: Commit**

Run:

```powershell
git add README.md docs/architecture/overview.md
git commit -m "docs: document png output"
```

## Task 4: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm image writer API ownership**

Run:

```powershell
rg -n "WritePng|WriteImage|stbi_write_png|stb_image_write|STB_IMAGE_WRITE_IMPLEMENTATION" include src tests external CMakeLists.txt
```

Expected:

- API declarations appear in `include/yaoray/film/image_writer.hpp`.
- Implementation appears in `src/film/image_writer.cpp`.
- Tests appear in `tests/film_tests.cpp`.
- Dependency metadata appears in `external/stb/README.md`.
- `STB_IMAGE_WRITE_IMPLEMENTATION` appears exactly once.

- [ ] **Step 2: Confirm no PPM-only CLI assumptions remain**

Run:

```powershell
rg -n "PPM images|ASCII PPM|WritePpm\\(|\\.ppm" src CMakeLists.txt README.md docs/architecture/overview.md scenes/examples tests/fixtures/scene
```

Expected:

- `WritePpm(` remains in film writer implementation and film tests.
- Example scenes use `.png`.
- CLI render tests use `.png`.
- Docs mention PPM only as still-supported debug/test output.

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

- [ ] **Step 4: Verify built-in PNG output**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\minimal.png) { Remove-Item -LiteralPath scenes\examples\out\minimal.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\minimal.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Rendered image: out/minimal.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
```

- [ ] **Step 5: Verify OBJ PNG output**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\obj_pyramid.png) { Remove-Item -LiteralPath scenes\examples\out\obj_pyramid.png -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
[byte[]]$bytes = [System.IO.File]::ReadAllBytes("scenes\examples\out\obj_pyramid.png")
$bytes[0..7] | ForEach-Object { $_.ToString("X2") }
```

Expected CLI output includes:

```text
Rendered image: out/obj_pyramid.png
```

Expected signature output:

```text
89
50
4E
47
0D
0A
1A
0A
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

- PNG writer: Task 1.
- `WriteImage()` dispatch: Task 1.
- PPM preservation: Task 1 tests and Task 4 verification.
- stb vendoring: Task 1.
- CLI render integration: Task 2.
- PNG examples and fixtures: Task 2.
- Docs: Task 3.
- Final verification: Task 4.

Type consistency:

- `ImageWriteResult` remains the shared success/error type.
- `WritePng()`, `WritePpm()`, and `WriteImage()` all accept `const Film&`, `const ToneMapSettings&`, and `const std::filesystem::path&`.
- `WriteImage()` is the only app-facing writer used by `src/app/main.cpp`.
- `stb_image_write.h` is private to `yaoray_film`.

Implementation guardrails:

- Do not remove `WritePpm()`.
- Do not make scene parsing validate image extensions in this slice.
- Do not add image-reading dependencies.
- Do not add HDR, EXR, 16-bit PNG, alpha, gamma, or ICC support.
- Do not change tone mapping output values.
