# YaoRay Offline Render Workflow v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add CPU offline render progress, periodic preview PNG checkpointing, and resumable `.yrcheckpoint` state files for long path-traced renders.

**Architecture:** Keep workflow policy in the CLI, resumable film persistence in a focused film checkpoint module, render-affecting compatibility hashes in the render layer, and sample-pass progress hooks in the CPU path tracer. The CPU path tracer reports stable film snapshots only at completed sample-pass boundaries, so checkpoint PNG and state writes never read a film while worker threads are mutating it.

**Tech Stack:** C++20, CMake/CTest, TOML scene parsing, YaoRay `Film`, CPU path tracer tile scheduler, PNG writer, custom host-endian binary checkpoint files.

---

## File Structure

- Modify `include/yaoray/scene/scene.hpp`: add `OfflineSettings` and `SceneDescription::offline`.
- Modify `src/scene/scene_parser.cpp`: parse `[offline]`, validate it, and map deprecated `film.checkpoint_*` aliases to preview PNG checkpoint settings.
- Modify `tests/scene_tests.cpp`: cover offline defaults, parsing, validation, and deprecated alias behavior.
- Modify `include/yaoray/film/film.hpp`: expose `Pixels()` and checkpoint reconstruction setter.
- Modify `src/film/film.cpp`: implement checkpoint reconstruction setter with bounds validation.
- Create `include/yaoray/film/film_checkpoint.hpp`: checkpoint metadata, read/write result types, and read/write API.
- Create `src/film/film_checkpoint.cpp`: `.yrcheckpoint` binary serialization and validation.
- Modify `tests/film_tests.cpp`: add film checkpoint round-trip and rejection tests.
- Modify `CMakeLists.txt`: add `src/film/film_checkpoint.cpp` and later `src/render/render_scene_hash.cpp`.
- Modify `include/yaoray/backends/backend.hpp`: add render progress/cancel callback and resume film fields to `RenderRequest`.
- Modify `include/yaoray/backends/cpu/cpu_path_tracer.hpp`: add `ok/error` to `CpuPathTraceResult` and accept `RenderRequest`.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: refactor sample scheduling into sample passes, support resume, progress callbacks, and cancellation.
- Modify `src/backends/cpu/cpu_debug_backend.cpp`: pass `RenderRequest` to CPU path tracer and propagate cancellation errors.
- Modify `tests/cpu_path_tracer_tests.cpp`: add progress, resume equivalence, and cancellation tests.
- Create `include/yaoray/render/render_scene_hash.hpp`: stable render settings hash API.
- Create `src/render/render_scene_hash.cpp`: FNV-1a style hash over render-affecting scene data.
- Modify `tests/render_scene_tests.cpp`: cover hash stability and hash changes for key render settings.
- Modify `src/app/main.cpp`: orchestrate offline validation, checkpoint loading, progress output, checkpoint PNG/state writes, resume, and final checkpoint state write.
- Modify `tests/run_cli_render_test.cmake`: support extra generated files.
- Modify `CMakeLists.txt`: add offline CLI render tests and dependencies.
- Create `tests/fixtures/scene/offline_checkpoint.toml`: tiny CPU path scene that writes progress, PNG preview, and state checkpoint.
- Create `tests/fixtures/scene/offline_resume.toml`: same scene configured to resume from the checkpoint state.
- Modify `scenes/examples/local_sponza.toml`: enable conservative offline progress and checkpoint paths for manual large-scene runs.
- Modify `docs/assets/sponza-local-benchmark.md`: document offline progress, preview PNG, and resume state workflow.

## Task 1: Scene Offline Settings

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `tests/scene_tests.cpp`

- [ ] **Step 1: Write failing scene defaults and parsing tests**

Add these assertions to `scene_defaults_match_schema` in `tests/scene_tests.cpp`:

```cpp
    YR_EXPECT_TRUE(!scene.offline.progress);
    YR_EXPECT_EQ(scene.offline.progress_interval_seconds, 5);
    YR_EXPECT_EQ(scene.offline.checkpoint_png.generic_string(), std::string{});
    YR_EXPECT_EQ(scene.offline.checkpoint_png_interval_seconds, 60);
    YR_EXPECT_EQ(scene.offline.checkpoint_state.generic_string(), std::string{});
    YR_EXPECT_EQ(scene.offline.checkpoint_state_interval_seconds, 60);
    YR_EXPECT_TRUE(!scene.offline.resume);
```

Add these tests near the other scene parser setting tests:

```cpp
YR_TEST(scene_parser_loads_offline_settings) {
    const std::filesystem::path path = WriteTempScene(
        "offline_settings.toml",
        ValidSceneWith(R"toml(
[offline]
progress = true
progress_interval_seconds = 2
checkpoint_png = "out/progress.png"
checkpoint_png_interval_seconds = 3
checkpoint_state = "out/progress.yrcheckpoint"
checkpoint_state_interval_seconds = 4
resume = true
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::SceneDescription& scene = result.scene.value();
    const std::filesystem::path dir = path.parent_path();
    YR_EXPECT_TRUE(scene.offline.progress);
    YR_EXPECT_EQ(scene.offline.progress_interval_seconds, 2);
    YR_EXPECT_EQ(scene.offline.checkpoint_png.generic_string(), (dir / "out/progress.png").lexically_normal().generic_string());
    YR_EXPECT_EQ(scene.offline.checkpoint_png_interval_seconds, 3);
    YR_EXPECT_EQ(scene.offline.checkpoint_state.generic_string(), (dir / "out/progress.yrcheckpoint").lexically_normal().generic_string());
    YR_EXPECT_EQ(scene.offline.checkpoint_state_interval_seconds, 4);
    YR_EXPECT_TRUE(scene.offline.resume);
}

YR_TEST(scene_parser_rejects_offline_resume_without_state_path) {
    const std::filesystem::path path = WriteTempScene(
        "offline_resume_without_state.toml",
        ValidSceneWith(R"toml(
[offline]
resume = true
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.resume", "requires offline.checkpoint_state"));
}

YR_TEST(scene_parser_rejects_offline_bad_extensions_and_intervals) {
    const std::filesystem::path path = WriteTempScene(
        "offline_bad_fields.toml",
        ValidSceneWith(R"toml(
[offline]
progress = true
progress_interval_seconds = 0
checkpoint_png = "out/progress.ppm"
checkpoint_png_interval_seconds = 0
checkpoint_state = "out/progress.bin"
checkpoint_state_interval_seconds = 0
)toml")
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.progress_interval_seconds", "must be positive"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.checkpoint_png", "must use a .png extension"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.checkpoint_png_interval_seconds", "must be positive"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.checkpoint_state", "must use a .yrcheckpoint extension"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "offline.checkpoint_state_interval_seconds", "must be positive"));
}

YR_TEST(scene_parser_maps_deprecated_film_checkpoint_aliases_to_offline_preview) {
    const std::filesystem::path path = WriteTempScene(
        "film_checkpoint_alias.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
)toml",
            R"toml(
[film]
output = "out/test.png"
checkpoint_path = "out/alias.png"
checkpoint_interval_s = 9
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
    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.offline.checkpoint_png.generic_string(), (path.parent_path() / "out/alias.png").lexically_normal().generic_string());
    YR_EXPECT_EQ(scene.offline.checkpoint_png_interval_seconds, 9);
}

YR_TEST(scene_parser_warns_when_offline_overrides_film_checkpoint_aliases) {
    const std::filesystem::path path = WriteTempScene(
        "offline_wins_over_film_alias.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
)toml",
            R"toml(
[film]
output = "out/test.png"
checkpoint_path = "out/alias.png"
checkpoint_interval_s = 9
)toml",
            R"toml(
[camera]
type = "perspective"
position = [0, 1, 4]
target = [0, 1, 0]
fov_y = 45
)toml",
            R"toml(
[offline]
checkpoint_png = "out/offline.png"
checkpoint_png_interval_seconds = 3
)toml"
        )
    );

    const yr::SceneLoadResult result = yr::LoadSceneFile(path);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "film.checkpoint_path", "deprecated"));
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "film.checkpoint_interval_s", "deprecated"));
    const yr::SceneDescription& scene = result.scene.value();
    YR_EXPECT_EQ(scene.offline.checkpoint_png.generic_string(), (path.parent_path() / "out/offline.png").lexically_normal().generic_string());
    YR_EXPECT_EQ(scene.offline.checkpoint_png_interval_seconds, 3);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `SceneDescription::offline` and `OfflineSettings` do not exist.

- [ ] **Step 3: Add `OfflineSettings` to the scene model**

In `include/yaoray/scene/scene.hpp`, add this struct after `FilmSettings`:

```cpp
struct OfflineSettings {
    bool progress = false;
    int progress_interval_seconds = 5;
    std::filesystem::path checkpoint_png;
    int checkpoint_png_interval_seconds = 60;
    std::filesystem::path checkpoint_state;
    int checkpoint_state_interval_seconds = 60;
    bool resume = false;
};
```

Add the field to `SceneDescription` immediately after `film`:

```cpp
    OfflineSettings offline;
```

- [ ] **Step 4: Add parser helpers for warnings and extension checks**

In `src/scene/scene_parser.cpp`, add a warning helper after `Error()`:

```cpp
SceneDiagnostic Warning(const std::filesystem::path& file, std::string field, std::string message) {
    return SceneDiagnostic{DiagnosticSeverity::Warning, file, std::move(field), std::move(message)};
}
```

Add these helpers near `NormalizeScenePath()`:

```cpp
std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool HasExtension(const std::filesystem::path& path, std::string_view extension) {
    return LowerExtension(path) == extension;
}
```

Add standard includes:

```cpp
#include <algorithm>
#include <cctype>
```

- [ ] **Step 5: Implement `[offline]` parsing and deprecated alias mapping**

Add this parser after `ParseFilm()`:

```cpp
void ParseOffline(
    const toml::table& table,
    SceneDescription& scene,
    const std::filesystem::path& scene_dir,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    CheckUnknownFields(
        table,
        "offline",
        {
            "progress",
            "progress_interval_seconds",
            "checkpoint_png",
            "checkpoint_png_interval_seconds",
            "checkpoint_state",
            "checkpoint_state_interval_seconds",
            "resume",
        },
        file,
        diagnostics
    );

    if (const auto progress = ReadBool(table, "progress", file, "offline.progress", diagnostics)) {
        scene.offline.progress = *progress;
    }
    if (const auto interval = ReadInt(table, "progress_interval_seconds", file, "offline.progress_interval_seconds", diagnostics)) {
        scene.offline.progress_interval_seconds = *interval;
    }
    if (const auto path = ReadString(table, "checkpoint_png", file, "offline.checkpoint_png", diagnostics)) {
        scene.offline.checkpoint_png = path->empty() ? std::filesystem::path{} : NormalizeScenePath(scene_dir, *path);
    }
    if (const auto interval = ReadInt(table, "checkpoint_png_interval_seconds", file, "offline.checkpoint_png_interval_seconds", diagnostics)) {
        scene.offline.checkpoint_png_interval_seconds = *interval;
    }
    if (const auto path = ReadString(table, "checkpoint_state", file, "offline.checkpoint_state", diagnostics)) {
        scene.offline.checkpoint_state = path->empty() ? std::filesystem::path{} : NormalizeScenePath(scene_dir, *path);
    }
    if (const auto interval = ReadInt(table, "checkpoint_state_interval_seconds", file, "offline.checkpoint_state_interval_seconds", diagnostics)) {
        scene.offline.checkpoint_state_interval_seconds = *interval;
    }
    if (const auto resume = ReadBool(table, "resume", file, "offline.resume", diagnostics)) {
        scene.offline.resume = *resume;
    }

    if (scene.offline.progress && scene.offline.progress_interval_seconds <= 0) {
        diagnostics.push_back(Error(file, "offline.progress_interval_seconds", "must be positive when offline.progress is true"));
    }
    if (!scene.offline.checkpoint_png.empty()) {
        if (!HasExtension(scene.offline.checkpoint_png, ".png")) {
            diagnostics.push_back(Error(file, "offline.checkpoint_png", "must use a .png extension"));
        }
        if (scene.offline.checkpoint_png_interval_seconds <= 0) {
            diagnostics.push_back(Error(file, "offline.checkpoint_png_interval_seconds", "must be positive when offline.checkpoint_png is set"));
        }
    }
    if (!scene.offline.checkpoint_state.empty()) {
        if (!HasExtension(scene.offline.checkpoint_state, ".yrcheckpoint")) {
            diagnostics.push_back(Error(file, "offline.checkpoint_state", "must use a .yrcheckpoint extension"));
        }
        if (scene.offline.checkpoint_state_interval_seconds <= 0) {
            diagnostics.push_back(Error(file, "offline.checkpoint_state_interval_seconds", "must be positive when offline.checkpoint_state is set"));
        }
    }
    if (scene.offline.resume && scene.offline.checkpoint_state.empty()) {
        diagnostics.push_back(Error(file, "offline.resume", "requires offline.checkpoint_state"));
    }
}

void ApplyFilmCheckpointAliases(
    bool has_offline_table,
    SceneDescription& scene,
    const std::filesystem::path& file,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const bool has_film_checkpoint_path = !scene.film.checkpoint_path.empty();
    const bool has_film_checkpoint_interval = scene.film.checkpoint_interval_s > 0;
    if (!has_film_checkpoint_path && !has_film_checkpoint_interval) {
        return;
    }

    if (has_offline_table) {
        if (has_film_checkpoint_path) {
            diagnostics.push_back(Warning(file, "film.checkpoint_path", "deprecated; use offline.checkpoint_png"));
        }
        if (has_film_checkpoint_interval) {
            diagnostics.push_back(Warning(file, "film.checkpoint_interval_s", "deprecated; use offline.checkpoint_png_interval_seconds"));
        }
        return;
    }

    if (has_film_checkpoint_path) {
        scene.offline.checkpoint_png = scene.film.checkpoint_path;
    }
    if (has_film_checkpoint_interval) {
        scene.offline.checkpoint_png_interval_seconds = scene.film.checkpoint_interval_s;
    }
}
```

In `LoadSceneFile()`, update the root allowed fields:

```cpp
{"render", "film", "offline", "camera", "assets", "materials", "instances", "lights", "environment"}
```

Add:

```cpp
    const toml::table* offline = root["offline"].as_table();
```

After `ParseFilm(...)`, add:

```cpp
    if (offline != nullptr) {
        ParseOffline(*offline, scene, scene_dir, file, result.diagnostics);
    }
    ApplyFilmCheckpointAliases(offline != nullptr, scene, file, result.diagnostics);
```

- [ ] **Step 6: Run tests and commit**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass.

Commit:

```bash
git add include/yaoray/scene/scene.hpp src/scene/scene_parser.cpp tests/scene_tests.cpp
git commit -m "feat: parse offline render settings"
```

## Task 2: Film Checkpoint State Module

**Files:**
- Modify: `include/yaoray/film/film.hpp`
- Modify: `src/film/film.cpp`
- Create: `include/yaoray/film/film_checkpoint.hpp`
- Create: `src/film/film_checkpoint.cpp`
- Modify: `tests/film_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing film checkpoint tests**

Add `#include <yaoray/film/film_checkpoint.hpp>` to `tests/film_tests.cpp`.

Add these tests after `film_rejects_bad_samples`:

```cpp
YR_TEST(film_exposes_pixels_for_checkpoint_reconstruction) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{2.0f, 4.0f, 6.0f}, 2});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 3.0f, 5.0f}, 1});

    YR_EXPECT_EQ(film.Pixels().size(), std::size_t{2});
    YR_EXPECT_EQ(film.SampleCount(0, 0), 2);
    YR_EXPECT_EQ(film.SampleCount(1, 0), 1);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).x, 1.0, 1e-6);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).y, 2.0, 1e-6);
    YR_EXPECT_NEAR(film.LinearPixel(0, 0).z, 3.0, 1e-6);
}

YR_TEST(film_checkpoint_round_trips_tiny_film) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{2.0f, 4.0f, 6.0f}, 2});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 3.0f, 5.0f}, 2});
    const std::filesystem::path path = ImageWriterTestPath("roundtrip.yrcheckpoint");
    std::filesystem::remove(path);

    const yr::FilmCheckpointMetadata metadata{2, 1, 4, 2, std::uint64_t{12345}};
    const yr::FilmCheckpointWriteResult write = yr::WriteFilmCheckpoint(path, film, metadata);
    const yr::FilmCheckpointLoadResult load = yr::LoadFilmCheckpoint(path, 2, 1, 4, std::uint64_t{12345});

    YR_EXPECT_TRUE(write.ok);
    YR_EXPECT_TRUE(write.error.empty());
    YR_EXPECT_TRUE(load.ok);
    YR_EXPECT_TRUE(load.error.empty());
    YR_EXPECT_TRUE(load.film.has_value());
    YR_EXPECT_EQ(load.metadata.completed_spp, 2);
    YR_EXPECT_EQ(load.film->SampleCount(0, 0), 2);
    YR_EXPECT_NEAR(load.film->LinearPixel(0, 0).x, 1.0, 1e-6);
    YR_EXPECT_NEAR(load.film->LinearPixel(1, 0).z, 2.5, 1e-6);
}

YR_TEST(film_checkpoint_rejects_settings_hash_mismatch) {
    yr::Film film{1, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    const std::filesystem::path path = ImageWriterTestPath("bad_hash.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{1, 1, 2, 1, std::uint64_t{111}};

    const yr::FilmCheckpointWriteResult write = yr::WriteFilmCheckpoint(path, film, metadata);
    const yr::FilmCheckpointLoadResult load = yr::LoadFilmCheckpoint(path, 1, 1, 2, std::uint64_t{222});

    YR_EXPECT_TRUE(write.ok);
    YR_EXPECT_TRUE(!load.ok);
    YR_EXPECT_TRUE(load.error.find("settings hash") != std::string::npos);
}

YR_TEST(film_checkpoint_rejects_dimension_mismatch) {
    yr::Film film{1, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    const std::filesystem::path path = ImageWriterTestPath("bad_dimensions.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{1, 1, 2, 1, std::uint64_t{111}};

    const yr::FilmCheckpointWriteResult write = yr::WriteFilmCheckpoint(path, film, metadata);
    const yr::FilmCheckpointLoadResult load = yr::LoadFilmCheckpoint(path, 2, 1, 2, std::uint64_t{111});

    YR_EXPECT_TRUE(write.ok);
    YR_EXPECT_TRUE(!load.ok);
    YR_EXPECT_TRUE(load.error.find("dimensions") != std::string::npos);
}

YR_TEST(film_checkpoint_rejects_non_uniform_resume_samples) {
    yr::Film film{2, 1};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 1});
    film.SetPixelForCheckpoint(1, 0, yr::FilmPixel{yr::Color3f{1.0f, 2.0f, 3.0f}, 2});
    const std::filesystem::path path = ImageWriterTestPath("non_uniform.yrcheckpoint");
    const yr::FilmCheckpointMetadata metadata{2, 1, 4, 2, std::uint64_t{111}};

    const yr::FilmCheckpointWriteResult write = yr::WriteFilmCheckpoint(path, film, metadata);
    const yr::FilmCheckpointLoadResult load = yr::LoadFilmCheckpoint(path, 2, 1, 4, std::uint64_t{111});

    YR_EXPECT_TRUE(write.ok);
    YR_EXPECT_TRUE(!load.ok);
    YR_EXPECT_TRUE(load.error.find("sample count") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `film_checkpoint.hpp`, `Film::Pixels()`, and `Film::SetPixelForCheckpoint()` do not exist.

- [ ] **Step 3: Add film pixel checkpoint accessors**

In `include/yaoray/film/film.hpp`, add:

```cpp
    const std::vector<FilmPixel>& Pixels() const { return pixels_; }
    void SetPixelForCheckpoint(int x, int y, FilmPixel pixel);
```

In `src/film/film.cpp`, implement:

```cpp
void Film::SetPixelForCheckpoint(int x, int y, FilmPixel pixel) {
    if (!InBounds(x, y)) {
        return;
    }
    pixels_[Index(x, y)] = pixel;
}
```

- [ ] **Step 4: Add the checkpoint header**

Create `include/yaoray/film/film_checkpoint.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <yaoray/film/film.hpp>

namespace yr {

struct FilmCheckpointMetadata {
    int width = 0;
    int height = 0;
    int target_spp = 0;
    int completed_spp = 0;
    std::uint64_t settings_hash = 0;
};

struct FilmCheckpointWriteResult {
    bool ok = false;
    std::string error;
};

struct FilmCheckpointLoadResult {
    bool ok = false;
    std::string error;
    std::optional<Film> film;
    FilmCheckpointMetadata metadata;
};

FilmCheckpointWriteResult WriteFilmCheckpoint(
    const std::filesystem::path& path,
    const Film& film,
    FilmCheckpointMetadata metadata);

FilmCheckpointLoadResult LoadFilmCheckpoint(
    const std::filesystem::path& path,
    int expected_width,
    int expected_height,
    int expected_target_spp,
    std::uint64_t expected_settings_hash);

} // namespace yr
```

- [ ] **Step 5: Implement binary checkpoint read/write**

Create `src/film/film_checkpoint.cpp` with this structure:

```cpp
#include <yaoray/film/film_checkpoint.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace yr {
namespace {

constexpr std::array<char, 8> Magic{'Y', 'R', 'C', 'H', 'E', 'C', 'K', '1'};
constexpr std::uint32_t Version = 1;

struct FileHeader {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t target_spp = 0;
    std::uint32_t completed_spp = 0;
    std::uint64_t settings_hash = 0;
    std::uint64_t pixel_count = 0;
};

struct StoredPixel {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t samples = 0;
};

template <typename T>
bool WritePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(out);
}

template <typename T>
bool ReadPod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(in);
}

FilmCheckpointWriteResult EnsureParentDirectory(const std::filesystem::path& path) {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return FilmCheckpointWriteResult{false, "failed to create checkpoint directory: " + ec.message()};
        }
    }
    return FilmCheckpointWriteResult{true, {}};
}

} // namespace

FilmCheckpointWriteResult WriteFilmCheckpoint(
    const std::filesystem::path& path,
    const Film& film,
    FilmCheckpointMetadata metadata
) {
    if (metadata.width != film.Width() || metadata.height != film.Height()) {
        return FilmCheckpointWriteResult{false, "checkpoint metadata dimensions do not match film"};
    }
    if (metadata.target_spp <= 0 || metadata.completed_spp < 0 || metadata.completed_spp > metadata.target_spp) {
        return FilmCheckpointWriteResult{false, "checkpoint metadata has invalid sample counts"};
    }

    const FilmCheckpointWriteResult directory = EnsureParentDirectory(path);
    if (!directory.ok) {
        return directory;
    }

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        return FilmCheckpointWriteResult{false, "failed to open checkpoint for writing: " + path.generic_string()};
    }

    FileHeader header;
    header.magic = Magic;
    header.version = Version;
    header.width = static_cast<std::uint32_t>(metadata.width);
    header.height = static_cast<std::uint32_t>(metadata.height);
    header.target_spp = static_cast<std::uint32_t>(metadata.target_spp);
    header.completed_spp = static_cast<std::uint32_t>(metadata.completed_spp);
    header.settings_hash = metadata.settings_hash;
    header.pixel_count = static_cast<std::uint64_t>(film.Pixels().size());
    if (!WritePod(out, header)) {
        return FilmCheckpointWriteResult{false, "failed to write checkpoint header: " + path.generic_string()};
    }

    for (const FilmPixel& pixel : film.Pixels()) {
        const StoredPixel stored{pixel.sum.x, pixel.sum.y, pixel.sum.z, pixel.samples};
        if (!WritePod(out, stored)) {
            return FilmCheckpointWriteResult{false, "failed to write checkpoint pixels: " + path.generic_string()};
        }
    }

    return FilmCheckpointWriteResult{true, {}};
}

FilmCheckpointLoadResult LoadFilmCheckpoint(
    const std::filesystem::path& path,
    int expected_width,
    int expected_height,
    int expected_target_spp,
    std::uint64_t expected_settings_hash
) {
    FilmCheckpointLoadResult result;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        result.error = "failed to open checkpoint for reading: " + path.generic_string();
        return result;
    }

    FileHeader header;
    if (!ReadPod(in, header)) {
        result.error = "failed to read checkpoint header: " + path.generic_string();
        return result;
    }
    if (header.magic != Magic) {
        result.error = "checkpoint has invalid magic: " + path.generic_string();
        return result;
    }
    if (header.version != Version) {
        result.error = "checkpoint has unsupported version: " + path.generic_string();
        return result;
    }
    if (static_cast<int>(header.width) != expected_width || static_cast<int>(header.height) != expected_height) {
        result.error = "checkpoint dimensions do not match current render";
        return result;
    }
    if (static_cast<int>(header.target_spp) != expected_target_spp) {
        result.error = "checkpoint target spp does not match current render";
        return result;
    }
    if (header.settings_hash != expected_settings_hash) {
        result.error = "checkpoint settings hash does not match current render";
        return result;
    }
    const std::uint64_t expected_pixels =
        static_cast<std::uint64_t>(expected_width) * static_cast<std::uint64_t>(expected_height);
    if (header.pixel_count != expected_pixels) {
        result.error = "checkpoint pixel count does not match dimensions";
        return result;
    }

    Film film{expected_width, expected_height};
    for (std::uint64_t index = 0; index < header.pixel_count; ++index) {
        StoredPixel stored;
        if (!ReadPod(in, stored)) {
            result.error = "failed to read checkpoint pixels: " + path.generic_string();
            return result;
        }
        if (stored.samples != header.completed_spp) {
            result.error = "checkpoint pixel sample count is not uniform";
            return result;
        }
        const int x = static_cast<int>(index % static_cast<std::uint64_t>(expected_width));
        const int y = static_cast<int>(index / static_cast<std::uint64_t>(expected_width));
        film.SetPixelForCheckpoint(x, y, FilmPixel{Color3f{stored.x, stored.y, stored.z}, stored.samples});
    }

    result.ok = true;
    result.metadata = FilmCheckpointMetadata{
        static_cast<int>(header.width),
        static_cast<int>(header.height),
        static_cast<int>(header.target_spp),
        static_cast<int>(header.completed_spp),
        header.settings_hash
    };
    result.film.emplace(std::move(film));
    return result;
}

} // namespace yr
```

- [ ] **Step 6: Add checkpoint source to CMake and run tests**

In `CMakeLists.txt`, add `src/film/film_checkpoint.cpp` to `yaoray_film`:

```cmake
add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/film_checkpoint.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
```

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass.

Commit:

```bash
git add CMakeLists.txt include/yaoray/film/film.hpp src/film/film.cpp include/yaoray/film/film_checkpoint.hpp src/film/film_checkpoint.cpp tests/film_tests.cpp
git commit -m "feat: add film checkpoint state files"
```

## Task 3: CPU Path Tracer Progress and Resume

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_path_tracer.hpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Write failing CPU progress/resume tests**

In `tests/cpu_path_tracer_tests.cpp`, add `RunPathTrace` overload:

```cpp
yr::CpuPathTraceResult RunPathTrace(const yr::RenderSceneIR& scene, const yr::RenderRequest& request) {
    return yr::RenderCpuPathTrace(PreparePathScene(scene), request);
}
```

Add these tests after `cpu_path_tracer_accumulates_spp_samples`:

```cpp
YR_TEST(cpu_path_tracer_reports_progress_after_each_sample_pass) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;
    std::vector<int> completed;

    yr::RenderRequest request;
    request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
        completed.push_back(progress.completed_spp);
        YR_EXPECT_EQ(progress.target_spp, 3);
        YR_EXPECT_EQ(progress.target_samples, std::uint64_t{12});
        YR_EXPECT_EQ(progress.completed_samples, static_cast<std::uint64_t>(progress.completed_spp * 4));
        YR_EXPECT_EQ(film.SampleCount(0, 0), static_cast<std::uint32_t>(progress.completed_spp));
        return yr::RenderProgressDecision{};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_EQ(completed.size(), std::size_t{3});
    YR_EXPECT_EQ(completed[0], 1);
    YR_EXPECT_EQ(completed[1], 2);
    YR_EXPECT_EQ(completed[2], 3);
}

YR_TEST(cpu_path_tracer_resume_matches_uninterrupted_render) {
    yr::RenderSceneIR partial_scene = MakeThreadedDeterminismScene();
    partial_scene.spp = 2;
    partial_scene.threads = 2;
    yr::RenderSceneIR full_scene = partial_scene;
    full_scene.spp = 4;

    const yr::CpuPathTraceResult partial = RunPathTrace(partial_scene);
    yr::RenderRequest resume_request;
    resume_request.resume_film = &partial.film;
    resume_request.resume_completed_spp = 2;
    const yr::CpuPathTraceResult resumed = RunPathTrace(full_scene, resume_request);
    const yr::CpuPathTraceResult uninterrupted = RunPathTrace(full_scene);

    YR_EXPECT_TRUE(partial.ok);
    YR_EXPECT_TRUE(resumed.ok);
    YR_EXPECT_TRUE(uninterrupted.ok);
    YR_EXPECT_TRUE(FilmsEqual(resumed.film, uninterrupted.film));
    YR_EXPECT_TRUE(resumed.stats.rays_traced > 0);
}

YR_TEST(cpu_path_tracer_rejects_invalid_resume_sample_count) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 4;
    yr::Film film{2, 2};
    film.SetPixelForCheckpoint(0, 0, yr::FilmPixel{yr::Color3f{1.0f, 0.0f, 0.0f}, 1});

    yr::RenderRequest request;
    request.resume_film = &film;
    request.resume_completed_spp = 2;
    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("resume") != std::string::npos);
}

YR_TEST(cpu_path_tracer_progress_callback_can_cancel_render) {
    yr::RenderSceneIR scene = MakeBaseScene(2, 2);
    scene.spp = 3;

    yr::RenderRequest request;
    request.progress_callback = [](const yr::RenderProgress&, const yr::Film&) {
        return yr::RenderProgressDecision{true, "stop after first pass"};
    };

    const yr::CpuPathTraceResult result = RunPathTrace(scene, request);

    YR_EXPECT_TRUE(!result.ok);
    YR_EXPECT_TRUE(result.error.find("stop after first pass") != std::string::npos);
    YR_EXPECT_EQ(result.film.SampleCount(0, 0), 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `RenderRequest` has no progress/resume fields and `RenderCpuPathTrace()` does not accept a request.

- [ ] **Step 3: Add backend progress request types**

In `include/yaoray/backends/backend.hpp`, add includes:

```cpp
#include <functional>
```

Replace the empty `RenderRequest` with:

```cpp
struct RenderProgress {
    int completed_spp = 0;
    int target_spp = 0;
    std::uint64_t completed_samples = 0;
    std::uint64_t target_samples = 0;
    std::uint64_t rays_traced = 0;
    double elapsed_seconds = 0.0;
};

struct RenderProgressDecision {
    bool cancel = false;
    std::string error;
};

using RenderProgressCallback = std::function<RenderProgressDecision(const RenderProgress&, const Film&)>;

struct RenderRequest {
    const Film* resume_film = nullptr;
    int resume_completed_spp = 0;
    RenderProgressCallback progress_callback;
};
```

- [ ] **Step 4: Add CPU path trace result status and request parameter**

In `include/yaoray/backends/cpu/cpu_path_tracer.hpp`, add to `CpuPathTraceResult`:

```cpp
    bool ok = true;
    std::string error;
```

Add `#include <string>`.

Change the function declaration:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene, const RenderRequest& request = {});
```

- [ ] **Step 5: Refactor CPU path tracer to sample-pass scheduling**

In `src/backends/cpu/cpu_path_tracer.cpp`, change the function signature:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const CpuPreparedScene& prepared_scene, const RenderRequest& request) {
```

Inside the function, initialize the result from resume state:

```cpp
    CpuPathTraceResult result{request.resume_film == nullptr ? Film{scene.width, scene.height} : *request.resume_film, {}, true, {}};
```

Add resume validation before tracing:

```cpp
    const int samples_per_pixel = std::max(1, scene.spp);
    if (request.resume_completed_spp < 0 || request.resume_completed_spp > samples_per_pixel) {
        result.ok = false;
        result.error = "invalid resume completed spp";
        return result;
    }
    if (request.resume_film != nullptr) {
        if (request.resume_film->Width() != scene.width || request.resume_film->Height() != scene.height) {
            result.ok = false;
            result.error = "resume film dimensions do not match render scene";
            return result;
        }
        for (int y = 0; y < request.resume_film->Height(); ++y) {
            for (int x = 0; x < request.resume_film->Width(); ++x) {
                if (request.resume_film->SampleCount(x, y) != static_cast<std::uint32_t>(request.resume_completed_spp)) {
                    result.ok = false;
                    result.error = "resume film sample counts do not match resume completed spp";
                    return result;
                }
            }
        }
    }
```

Replace the current `ForEachCpuTile` loop with sample-pass scheduling:

```cpp
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t cumulative_rays = 0;
    for (int sample = request.resume_completed_spp; sample < samples_per_pixel; ++sample) {
        std::vector<CpuPathTraceStats> worker_stats(static_cast<std::size_t>(schedule.worker_count));
        ForEachCpuTile(schedule, [&](const CpuTile& tile, int worker_index) {
            CpuPathTraceStats& stats = worker_stats[static_cast<std::size_t>(worker_index)];
            for (int y = tile.y0; y < tile.y1; ++y) {
                for (int x = tile.x0; x < tile.x1; ++x) {
                    CpuSampler sampler{
                        scene.sampler,
                        SeedForPixelSample(scene.seed, x, y, sample),
                        sample,
                        samples_per_pixel,
                        DirectLightSampleCount(scene)
                    };
                    const Vec2f pixel_sample = sampler.NextPixel2D();
                    const Ray3f ray = MakeCameraRay(scene, x, y, pixel_sample.x, pixel_sample.y);
                    Color3f sample_radiance = TracePath(prepared_scene, ray, sampler, stats);
                    sample_radiance = ClampMaxComponent(sample_radiance, scene.radiance_clamp);
                    result.film.AddSample(x, y, sample_radiance);
                }
            }
        });

        for (const CpuPathTraceStats& stats : worker_stats) {
            MergeTraceStats(result.stats, stats);
        }
        cumulative_rays = result.stats.rays_traced;

        if (request.progress_callback) {
            const auto now = std::chrono::steady_clock::now();
            const int completed_spp = sample + 1;
            const RenderProgress progress{
                completed_spp,
                samples_per_pixel,
                static_cast<std::uint64_t>(completed_spp) * static_cast<std::uint64_t>(scene.width) * static_cast<std::uint64_t>(scene.height),
                static_cast<std::uint64_t>(samples_per_pixel) * static_cast<std::uint64_t>(scene.width) * static_cast<std::uint64_t>(scene.height),
                cumulative_rays,
                std::chrono::duration<double>(now - start).count()
            };
            const RenderProgressDecision decision = request.progress_callback(progress, result.film);
            if (decision.cancel) {
                result.ok = false;
                result.error = decision.error.empty() ? "render cancelled by progress callback" : decision.error;
                result.stats.elapsed_seconds = progress.elapsed_seconds;
                return result;
            }
        }
    }
```

Keep the existing final elapsed time assignment after the loop.

- [ ] **Step 6: Propagate CPU path cancellation through backend API**

In `src/backends/cpu/cpu_debug_backend.cpp`, change the path branch to:

```cpp
    if (render_scene.integrator == RenderIntegratorKind::Path) {
        CpuPathTraceResult path_result = RenderCpuPathTrace(*cpu_scene, request);
        if (!path_result.ok) {
            result.ok = false;
            result.error = path_result.error.empty() ? "CPU path tracing failed" : path_result.error;
            result.stats = ToRenderStats(path_result.stats);
            if (path_result.film.Width() > 0 && path_result.film.Height() > 0) {
                result.film.emplace(std::move(path_result.film));
            }
            return result;
        }
        result.film.emplace(std::move(path_result.film));
        result.stats = ToRenderStats(path_result.stats);
        return result;
    }
```

- [ ] **Step 7: Run tests and commit**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass.

Commit:

```bash
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_path_tracer.hpp src/backends/cpu/cpu_path_tracer.cpp src/backends/cpu/cpu_debug_backend.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add cpu path progress and resume hooks"
```

## Task 4: Render Settings Hash

**Files:**
- Create: `include/yaoray/render/render_scene_hash.hpp`
- Create: `src/render/render_scene_hash.cpp`
- Modify: `tests/render_scene_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing render settings hash tests**

Add `#include <yaoray/render/render_scene_hash.hpp>` to `tests/render_scene_tests.cpp`.

Add these tests near the render scene IR default tests:

```cpp
YR_TEST(render_settings_hash_is_stable_for_identical_scene) {
    const yr::SceneCompileResult first_result = yr::CompileScene(MakeBaseScene());
    const yr::SceneCompileResult second_result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(first_result.scene.has_value());
    YR_EXPECT_TRUE(second_result.scene.has_value());
    YR_EXPECT_EQ(
        yr::ComputeRenderSettingsHash(first_result.scene.value()),
        yr::ComputeRenderSettingsHash(second_result.scene.value())
    );
}

YR_TEST(render_settings_hash_changes_for_render_affecting_settings) {
    yr::SceneDescription base = MakeBaseScene();
    yr::SceneDescription changed_seed = base;
    changed_seed.render.seed = 999;
    yr::SceneDescription changed_spp = base;
    changed_spp.render.spp = 8;

    const yr::SceneCompileResult base_result = yr::CompileScene(base);
    const yr::SceneCompileResult seed_result = yr::CompileScene(changed_seed);
    const yr::SceneCompileResult spp_result = yr::CompileScene(changed_spp);

    YR_EXPECT_TRUE(base_result.scene.has_value());
    YR_EXPECT_TRUE(seed_result.scene.has_value());
    YR_EXPECT_TRUE(spp_result.scene.has_value());
    const std::uint64_t base_hash = yr::ComputeRenderSettingsHash(base_result.scene.value());
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(seed_result.scene.value()));
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(spp_result.scene.value()));
}

YR_TEST(render_settings_hash_changes_for_camera_and_resource_counts) {
    yr::SceneDescription base = MakeBaseScene();
    yr::SceneDescription changed_camera = base;
    changed_camera.camera->fov_y = 35.0f;
    yr::SceneDescription changed_resources = base;
    changed_resources.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    changed_resources.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult base_result = yr::CompileScene(base);
    const yr::SceneCompileResult camera_result = yr::CompileScene(changed_camera);
    const yr::SceneCompileResult resources_result = yr::CompileScene(changed_resources);

    YR_EXPECT_TRUE(base_result.scene.has_value());
    YR_EXPECT_TRUE(camera_result.scene.has_value());
    YR_EXPECT_TRUE(resources_result.scene.has_value());
    const std::uint64_t base_hash = yr::ComputeRenderSettingsHash(base_result.scene.value());
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(camera_result.scene.value()));
    YR_EXPECT_TRUE(base_hash != yr::ComputeRenderSettingsHash(resources_result.scene.value()));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
```

Expected: build fails because `yaoray/render/render_scene_hash.hpp` does not exist.

- [ ] **Step 3: Add render settings hash API**

Create `include/yaoray/render/render_scene_hash.hpp`:

```cpp
#pragma once

#include <cstdint>

#include <yaoray/render/render_scene.hpp>

namespace yr {

std::uint64_t ComputeRenderSettingsHash(const RenderSceneIR& scene);

} // namespace yr
```

Create `src/render/render_scene_hash.cpp`:

```cpp
#include <yaoray/render/render_scene_hash.hpp>

#include <cstring>
#include <string_view>

namespace yr {
namespace {

constexpr std::uint64_t FnvOffset = 14695981039346656037ull;
constexpr std::uint64_t FnvPrime = 1099511628211ull;

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= FnvPrime;
    }
}

template <typename T>
void HashValue(std::uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(T));
}

void HashString(std::uint64_t& hash, std::string_view value) {
    HashBytes(hash, value.data(), value.size());
}

void HashColor(std::uint64_t& hash, Color3f value) {
    HashValue(hash, value.x);
    HashValue(hash, value.y);
    HashValue(hash, value.z);
}

void HashVec3(std::uint64_t& hash, Vec3f value) {
    HashValue(hash, value.x);
    HashValue(hash, value.y);
    HashValue(hash, value.z);
}

} // namespace

std::uint64_t ComputeRenderSettingsHash(const RenderSceneIR& scene) {
    std::uint64_t hash = FnvOffset;
    HashString(hash, "YaoRayRenderSettingsHashV1");
    HashValue(hash, static_cast<int>(scene.requested_backend));
    HashValue(hash, static_cast<int>(scene.integrator));
    HashValue(hash, static_cast<int>(scene.sampler));
    HashValue(hash, scene.width);
    HashValue(hash, scene.height);
    HashValue(hash, scene.spp);
    HashValue(hash, scene.max_depth);
    HashValue(hash, scene.seed);
    HashValue(hash, scene.light_samples);
    HashValue(hash, scene.radiance_clamp);
    HashVec3(hash, scene.camera.origin);
    HashVec3(hash, scene.camera.forward);
    HashVec3(hash, scene.camera.right);
    HashVec3(hash, scene.camera.up);
    HashValue(hash, scene.camera.fov_y_radians);
    HashValue(hash, scene.camera.aperture);
    HashValue(hash, scene.camera.focus_distance);
    HashValue(hash, static_cast<int>(scene.environment.type));
    HashColor(hash, scene.environment.radiance);
    HashValue(hash, scene.environment.strength);
    HashValue(hash, scene.environment.rotation_radians);
    HashValue(hash, scene.triangles.size());
    HashValue(hash, scene.materials.size());
    HashValue(hash, scene.textures.size());
    HashValue(hash, scene.area_lights.size());
    return hash;
}

} // namespace yr
```

- [ ] **Step 4: Add source to CMake and run tests**

In `CMakeLists.txt`, add `src/render/render_scene_hash.cpp` to `yaoray_render`:

```cmake
    src/render/render_scene_hash.cpp
```

Run:

```bash
cmake --build build --config Debug --target yaoray_tests
./build/yaoray_tests
```

Expected: all unit tests pass.

Commit:

```bash
git add CMakeLists.txt include/yaoray/render/render_scene_hash.hpp src/render/render_scene_hash.cpp tests/render_scene_tests.cpp
git commit -m "feat: add render settings hash"
```

## Task 5: CLI Offline Workflow

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `tests/run_cli_render_test.cmake`
- Modify: `CMakeLists.txt`
- Create: `tests/fixtures/scene/offline_checkpoint.toml`
- Create: `tests/fixtures/scene/offline_resume.toml`

- [ ] **Step 1: Add tiny offline CLI fixtures**

Create `tests/fixtures/scene/offline_checkpoint.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 4
height = 3
spp = 2
max_depth = 1
seed = 17
sampler = "independent"
threads = 1
light_samples = 1

[film]
output = "out/offline_checkpoint.png"
tone_mapper = "aces"
exposure = 0.0

[offline]
progress = true
progress_interval_seconds = 1
checkpoint_png = "out/offline_checkpoint.preview.png"
checkpoint_png_interval_seconds = 1
checkpoint_state = "out/offline_checkpoint.yrcheckpoint"
checkpoint_state_interval_seconds = 1
resume = false

[camera]
type = "perspective"
position = [0.0, 0.0, 3.0]
target = [0.0, 0.0, 0.0]
fov_y = 45.0

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"

[environment]
type = "constant"
radiance = [0.02, 0.03, 0.04]
strength = 1.0
```

Create `tests/fixtures/scene/offline_resume.toml`:

```toml
[render]
backend = "cpu"
integrator = "path"
width = 4
height = 3
spp = 2
max_depth = 1
seed = 17
sampler = "independent"
threads = 1
light_samples = 1

[film]
output = "out/offline_resume.png"
tone_mapper = "aces"
exposure = 0.0

[offline]
progress = true
progress_interval_seconds = 1
checkpoint_png = "out/offline_resume.preview.png"
checkpoint_png_interval_seconds = 1
checkpoint_state = "out/offline_checkpoint.yrcheckpoint"
checkpoint_state_interval_seconds = 1
resume = true

[camera]
type = "perspective"
position = [0.0, 0.0, 3.0]
target = [0.0, 0.0, 0.0]
fov_y = 45.0

[[assets]]
name = "triangle"
path = "builtin:triangle"

[[instances]]
asset = "triangle"

[environment]
type = "constant"
radiance = [0.02, 0.03, 0.04]
strength = 1.0
```

- [ ] **Step 2: Extend CLI test helper for extra generated files**

In `CMakeLists.txt`, update the helper argument declaration:

```cmake
set(multiValueArgs EXPECT_REGEX EXPECT_FILE)
```

Add this after the `EXPECT_REGEX` argument block:

```cmake
list(LENGTH ARG_EXPECT_FILE expected_file_count)
list(APPEND test_args "-DEXPECT_FILE_COUNT=${expected_file_count}")
if(expected_file_count GREATER 0)
    math(EXPR last_expected_file_index "${expected_file_count} - 1")
    foreach(index RANGE 0 ${last_expected_file_index})
        list(GET ARG_EXPECT_FILE ${index} expected_file)
        list(APPEND test_args "-DEXPECT_FILE_${index}=${expected_file}")
    endforeach()
endif()
```

In `tests/run_cli_render_test.cmake`, remove expected files before running the command:

```cmake
if(DEFINED EXPECT_FILE_COUNT AND EXPECT_FILE_COUNT GREATER 0)
    math(EXPR last_expected_file_index "${EXPECT_FILE_COUNT} - 1")
    foreach(index RANGE 0 ${last_expected_file_index})
        set(expected_file_var "EXPECT_FILE_${index}")
        if(DEFINED ${expected_file_var})
            file(REMOVE "${${expected_file_var}}")
        endif()
    endforeach()
endif()
```

Place that block after the existing `OUTPUT_PATH` removal block.

At the end of the file, before image sanity, add:

```cmake
if(DEFINED EXPECT_FILE_COUNT AND EXPECT_FILE_COUNT GREATER 0)
    math(EXPR last_expected_file_index "${EXPECT_FILE_COUNT} - 1")
    foreach(index RANGE 0 ${last_expected_file_index})
        set(expected_file_var "EXPECT_FILE_${index}")
        if(NOT DEFINED ${expected_file_var})
            message(FATAL_ERROR "${expected_file_var} is required")
        endif()
        if(NOT EXISTS "${${expected_file_var}}")
            message(FATAL_ERROR "Expected generated file was not written: ${${expected_file_var}}")
        endif()
        file(SIZE "${${expected_file_var}}" expected_file_size)
        if(expected_file_size EQUAL 0)
            message(FATAL_ERROR "Expected generated file is empty: ${${expected_file_var}}")
        endif()
    endforeach()
endif()
```

- [ ] **Step 3: Add failing CLI tests**

In `CMakeLists.txt`, add these tests before failure tests:

```cmake
    add_yaoray_cli_render_test(yaoray_cli_render_offline_checkpoint
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_checkpoint.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.png"
        BACKEND cpu
        EXPECT_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.preview.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_checkpoint.yrcheckpoint"
        EXPECT_REGEX
            "Progress:"
            "Checkpoint image:"
            "Checkpoint state:"
            "Rendered image:"
    )
    add_yaoray_cli_render_test(yaoray_cli_render_offline_resume
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_resume.toml"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_resume.png"
        BACKEND cpu
        EXPECT_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/offline_resume.preview.png"
        EXPECT_REGEX
            "Resumed checkpoint:"
            "Rendered image:"
    )
    set_tests_properties(yaoray_cli_render_offline_resume PROPERTIES DEPENDS yaoray_cli_render_offline_checkpoint)
    add_yaoray_cli_render_test(yaoray_cli_render_offline_unsupported_cuda
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/offline_checkpoint.toml"
        BACKEND cuda
        EXPECT_FAILURE
        EXPECT_REGEX "offline workflow supports only cpu path renders"
    )
```

- [ ] **Step 4: Run tests to verify they fail**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build -R "yaoray_cli_render_offline" --output-on-failure -C Debug
```

Expected: offline CLI tests fail because `src/app/main.cpp` does not yet load checkpoints, print progress, or write checkpoint files.

- [ ] **Step 5: Add CLI offline helpers and validation**

In `src/app/main.cpp`, add includes:

```cpp
#include <yaoray/film/film_checkpoint.hpp>
#include <yaoray/render/render_scene_hash.hpp>

#include <chrono>
#include <sstream>
```

Add helper structs/functions near existing helpers:

```cpp
bool OfflineRequested(const yr::SceneDescription& scene) {
    return scene.offline.progress ||
           !scene.offline.checkpoint_png.empty() ||
           !scene.offline.checkpoint_state.empty() ||
           scene.offline.resume;
}

std::optional<std::string> ValidateOfflineWorkflow(
    const yr::SceneDescription& scene,
    const yr::RenderSceneIR& render_scene
) {
    if (!OfflineRequested(scene)) {
        return std::nullopt;
    }
    if (render_scene.requested_backend != yr::RenderBackendKind::Cpu ||
        render_scene.integrator != yr::RenderIntegratorKind::Path) {
        return "offline workflow supports only cpu path renders";
    }
    return std::nullopt;
}

double PercentComplete(const yr::RenderProgress& progress) {
    if (progress.target_samples == 0) {
        return 100.0;
    }
    return 100.0 * static_cast<double>(progress.completed_samples) / static_cast<double>(progress.target_samples);
}

double SafeEtaSeconds(const yr::RenderProgress& progress) {
    if (progress.elapsed_seconds <= 0.0 || progress.completed_samples == 0 || progress.completed_samples >= progress.target_samples) {
        return 0.0;
    }
    const double samples_per_second = static_cast<double>(progress.completed_samples) / progress.elapsed_seconds;
    return static_cast<double>(progress.target_samples - progress.completed_samples) / samples_per_second;
}
```

After compile succeeds and before backend creation, add:

```cpp
    if (const std::optional<std::string> offline_error = ValidateOfflineWorkflow(scene, render_scene)) {
        std::cerr << *offline_error << '\n';
        return 1;
    }
```

- [ ] **Step 6: Add resume load before render**

Before `backend->Render(...)`, compute hash and load resume state:

```cpp
    const std::uint64_t settings_hash = yr::ComputeRenderSettingsHash(render_scene);
    std::optional<yr::Film> resume_film;
    int resume_completed_spp = 0;
    if (scene.offline.resume) {
        yr::FilmCheckpointLoadResult checkpoint = yr::LoadFilmCheckpoint(
            scene.offline.checkpoint_state,
            render_scene.width,
            render_scene.height,
            render_scene.spp,
            settings_hash
        );
        if (!checkpoint.ok || !checkpoint.film.has_value()) {
            std::cerr << "Checkpoint load failed: " << checkpoint.error << '\n';
            return 1;
        }
        resume_completed_spp = checkpoint.metadata.completed_spp;
        resume_film.emplace(std::move(checkpoint.film.value()));
        std::cout << "Resumed checkpoint: " << scene.offline.checkpoint_state.generic_string()
                  << " at " << resume_completed_spp << "/" << render_scene.spp << " spp\n";
    }
```

- [ ] **Step 7: Add progress/checkpoint callback**

Replace the render call:

```cpp
    const yr::RenderResult render_result = backend->Render(*prepare_result.scene, yr::RenderRequest{});
```

with:

```cpp
    yr::RenderRequest render_request;
    if (resume_film.has_value()) {
        render_request.resume_film = &resume_film.value();
        render_request.resume_completed_spp = resume_completed_spp;
    }

    const yr::ToneMapSettings tone_map{
        ToFilmToneMapper(scene.film.tone_mapper),
        scene.film.exposure
    };

    auto last_progress = std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.progress_interval_seconds);
    auto last_checkpoint_png = std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.checkpoint_png_interval_seconds);
    auto last_checkpoint_state = std::chrono::steady_clock::now() - std::chrono::seconds(scene.offline.checkpoint_state_interval_seconds);
    bool wrote_final_checkpoint_state = false;

    if (OfflineRequested(scene)) {
        render_request.progress_callback = [&](const yr::RenderProgress& progress, const yr::Film& film) {
            const auto now = std::chrono::steady_clock::now();
            const bool final_pass = progress.completed_spp >= progress.target_spp;
            if (scene.offline.progress &&
                (final_pass || now - last_progress >= std::chrono::seconds(scene.offline.progress_interval_seconds))) {
                last_progress = now;
                std::cout << "Progress: " << progress.completed_spp << "/" << progress.target_spp
                          << " spp (" << PercentComplete(progress) << "%)"
                          << " elapsed=" << progress.elapsed_seconds << "s"
                          << " eta=" << SafeEtaSeconds(progress) << "s"
                          << " samples/sec=" << SafeRate(static_cast<double>(progress.completed_samples), progress.elapsed_seconds)
                          << " rays/sec=" << SafeRate(static_cast<double>(progress.rays_traced), progress.elapsed_seconds)
                          << '\n';
            }
            if (!scene.offline.checkpoint_png.empty() &&
                (final_pass || now - last_checkpoint_png >= std::chrono::seconds(scene.offline.checkpoint_png_interval_seconds))) {
                last_checkpoint_png = now;
                const yr::ImageWriteResult image = yr::WriteImage(film, tone_map, scene.offline.checkpoint_png);
                if (!image.ok) {
                    return yr::RenderProgressDecision{true, "checkpoint image write failed: " + image.error};
                }
                std::cout << "Checkpoint image: " << scene.offline.checkpoint_png.generic_string() << '\n';
            }
            if (!scene.offline.checkpoint_state.empty() &&
                (final_pass || now - last_checkpoint_state >= std::chrono::seconds(scene.offline.checkpoint_state_interval_seconds))) {
                last_checkpoint_state = now;
                const yr::FilmCheckpointMetadata metadata{
                    render_scene.width,
                    render_scene.height,
                    render_scene.spp,
                    progress.completed_spp,
                    settings_hash
                };
                const yr::FilmCheckpointWriteResult state = yr::WriteFilmCheckpoint(scene.offline.checkpoint_state, film, metadata);
                if (!state.ok) {
                    return yr::RenderProgressDecision{true, "checkpoint state write failed: " + state.error};
                }
                wrote_final_checkpoint_state = final_pass;
                std::cout << "Checkpoint state: " << scene.offline.checkpoint_state.generic_string() << '\n';
            }
            return yr::RenderProgressDecision{};
        };
    }

    const yr::RenderResult render_result = backend->Render(*prepare_result.scene, render_request);
```

Remove the later duplicate `tone_map` declaration before `WriteImage`.

After successful `render_result.film` validation and before final image write, add:

```cpp
    if (!scene.offline.checkpoint_state.empty() && !wrote_final_checkpoint_state) {
        const yr::FilmCheckpointMetadata metadata{
            render_scene.width,
            render_scene.height,
            render_scene.spp,
            render_scene.spp,
            settings_hash
        };
        const yr::FilmCheckpointWriteResult state =
            yr::WriteFilmCheckpoint(scene.offline.checkpoint_state, *render_result.film, metadata);
        if (!state.ok) {
            std::cerr << "Checkpoint state write failed: " << state.error << '\n';
            return 1;
        }
        std::cout << "Checkpoint state: " << scene.offline.checkpoint_state.generic_string() << '\n';
    }
```

- [ ] **Step 8: Run CLI tests and commit**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build -R "yaoray_cli_render_offline" --output-on-failure -C Debug
```

Expected: `yaoray_cli_render_offline_checkpoint`, `yaoray_cli_render_offline_resume`, and `yaoray_cli_render_offline_unsupported_cuda` pass.

Commit:

```bash
git add src/app/main.cpp tests/run_cli_render_test.cmake CMakeLists.txt tests/fixtures/scene/offline_checkpoint.toml tests/fixtures/scene/offline_resume.toml
git commit -m "feat: add offline render cli workflow"
```

## Task 6: Sponza Offline Defaults and Final Verification

**Files:**
- Modify: `scenes/examples/local_sponza.toml`
- Modify: `docs/assets/sponza-local-benchmark.md`

- [ ] **Step 1: Enable conservative offline defaults for local Sponza**

Add this block after `[film]` in `scenes/examples/local_sponza.toml`:

```toml
[offline]
progress = true
progress_interval_seconds = 5
checkpoint_png = "out/local_sponza.checkpoint.png"
checkpoint_png_interval_seconds = 60
checkpoint_state = "out/local_sponza.yrcheckpoint"
checkpoint_state_interval_seconds = 60
resume = false
```

- [ ] **Step 2: Document resume workflow**

In `docs/assets/sponza-local-benchmark.md`, replace the smoke render section command block with:

````markdown
```bash
cmake --build build --config Debug
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
test -s scenes/examples/out/local_sponza.checkpoint.png
test -s scenes/examples/out/local_sponza.yrcheckpoint
```
````

Add this paragraph after the smoke render explanation:

```markdown
To resume a later run, set `resume = true` in `[offline]`. YaoRay validates the
checkpoint against the current render settings before continuing. If the scene,
resolution, target spp, seed, camera, or compiled resource counts change, resume
fails instead of mixing incompatible samples.
```

- [ ] **Step 3: Run full automated verification**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all default tests pass.

- [ ] **Step 4: Run manual Sponza offline smoke render**

Run:

```bash
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
test -s scenes/examples/out/local_sponza.checkpoint.png
test -s scenes/examples/out/local_sponza.yrcheckpoint
git status --short external/assets
```

Expected: render exits with code 0, all three files exist and are non-empty, stdout includes `Progress:`, `Checkpoint image:`, and `Checkpoint state:`, and `git status --short external/assets` prints no output.

- [ ] **Step 5: Commit final docs and scene template**

Commit:

```bash
git add scenes/examples/local_sponza.toml docs/assets/sponza-local-benchmark.md
git commit -m "docs: enable sponza offline workflow"
```

## Task 7: Final Completion Checks

**Files:**
- No file changes expected.

- [ ] **Step 1: Run final verification after all commits**

Run:

```bash
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
test -s scenes/examples/out/local_sponza.checkpoint.png
test -s scenes/examples/out/local_sponza.yrcheckpoint
git status --short --branch
git status --short external/assets
```

Expected:

```text
100% tests passed, 0 tests failed out of 20
```

The exact CTest count can be higher than 20 if more CLI tests are added during implementation. `git status --short --branch` must show only the current branch header. `git status --short external/assets` must print no output.

- [ ] **Step 2: Prepare branch for integration**

Use `superpowers:finishing-a-development-branch` after Step 1 passes. Present merge/push options to the user before mutating `main`.

## Self-Review

- Spec coverage: this plan covers `[offline]`, progress/ETA, preview PNG checkpointing, `.yrcheckpoint` state, resume validation, CPU-only scope, unsupported backend failures, Sponza manual workflow, and fast default tests.
- Scope control: implementation is split into scene parsing, checkpoint serialization, CPU render hooks, settings hash, CLI orchestration, and docs/manual verification. Each task has its own tests and commit.
- Red-flag scan: no deferred implementation gaps are present; every task has concrete files, commands, code snippets, and expected results.
- Type consistency: `OfflineSettings`, `RenderProgress`, `RenderProgressDecision`, `RenderRequest`, `FilmCheckpointMetadata`, `FilmCheckpointLoadResult`, and `ComputeRenderSettingsHash()` are introduced before later tasks use them.
