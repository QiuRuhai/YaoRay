# YaoRay CPU Path Tracer Threading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic tile-based CPU multithreading for the `path` integrator, with scene-controlled thread count and lightweight CLI performance stats.

**Architecture:** Add `render.threads` to semantic and compiled scene settings. Introduce a thin CPU tile scheduler that knows only image tiles and worker dispatch. Keep `debug_direct` single-threaded and integrate the scheduler only inside `RenderCpuPathTrace`, using per-worker stats and per-pixel deterministic RNG so output does not depend on worker order.

**Tech Stack:** C++20, CMake/CTest, existing YaoRay `RenderScene`, CPU backend, `Film`, and path tracer modules. Use standard `<thread>`, `<atomic>`, `<functional>`, and `<vector>` only.

---

## File Structure

- `include/yaoray/scene/scene.hpp`: add semantic `RenderSettings::threads`.
- `src/scene/scene_parser.cpp`: parse and validate optional `[render].threads`.
- `include/yaoray/render/render_scene.hpp`: add compiled `RenderScene::threads`.
- `src/render/scene_compiler.cpp`: copy semantic thread setting to compiled scene.
- `include/yaoray/backends/cpu/cpu_tile_scheduler.hpp`: new small tile scheduler public header for tests and CPU path tracer.
- `src/backends/cpu/cpu_tile_scheduler.cpp`: build tile lists, resolve worker count, run tile callbacks.
- `include/yaoray/backends/backend.hpp`: add `RenderStats::threads`.
- `include/yaoray/backends/cpu/cpu_path_tracer.hpp`: add `CpuPathTraceStats::threads`.
- `src/backends/cpu/cpu_path_tracer.cpp`: render path tracer by tiles, merge worker-local stats.
- `src/backends/cpu/cpu_debug_backend.cpp`: map new `threads` stat for debug and path results.
- `src/app/main.cpp`: print `Threads`, `Samples/sec`, and `Rays/sec`.
- `CMakeLists.txt`: compile new tile scheduler and tests; extend CLI smoke output checks.
- `tests/scene_tests.cpp`: parser/default validation for `render.threads`.
- `tests/render_scene_tests.cpp`: compiled scene thread propagation.
- `tests/cpu_tile_scheduler_tests.cpp`: tile coverage and scheduler dispatch tests.
- `tests/cpu_path_tracer_tests.cpp`: path tracer determinism across thread counts.
- `tests/backend_tests.cpp`: backend stats mapping for thread count.
- `README.md` and `docs/architecture/overview.md`: document `render.threads` and lightweight performance counters.

## Task 1: Add `render.threads` Scene Schema And Compiler Propagation

**Files:**
- Modify: `include/yaoray/scene/scene.hpp`
- Modify: `src/scene/scene_parser.cpp`
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/scene_tests.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing scene parser/default tests**

In `tests/scene_tests.cpp`, update `scene_defaults_match_schema`:

```cpp
YR_EXPECT_EQ(scene.render.threads, 0);
```

Update `scene_parser_loads_minimal_scene_file` after the seed assertion:

```cpp
YR_EXPECT_EQ(scene.render.threads, 0);
```

Update `scene_parser_applies_defaults` after the max depth assertion:

```cpp
YR_EXPECT_EQ(scene.render.threads, 0);
```

Add these tests after `scene_parser_loads_render_integrator`:

```cpp
YR_TEST(scene_parser_loads_render_threads) {
    const std::filesystem::path path = WriteTempScene(
        "render_threads.toml",
        ValidScene(
            R"toml(
[render]
backend = "cpu"
integrator = "path"
width = 64
height = 32
threads = 4
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
    YR_EXPECT_EQ(result.scene.value().render.threads, 4);
}

YR_TEST(scene_parser_allows_auto_render_threads) {
    const std::filesystem::path path = WriteTempScene(
        "render_threads_auto.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
threads = 0
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
    YR_EXPECT_EQ(result.scene.value().render.threads, 0);
}

YR_TEST(scene_parser_rejects_negative_render_threads) {
    const std::filesystem::path path = WriteTempScene(
        "bad_threads_negative.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
threads = -1
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.threads", "must be non-negative"));
}

YR_TEST(scene_parser_rejects_float_render_threads) {
    const std::filesystem::path path = WriteTempScene(
        "bad_threads_float.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
threads = 1.5
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.threads", "must be an integer"));
}

YR_TEST(scene_parser_rejects_string_render_threads) {
    const std::filesystem::path path = WriteTempScene(
        "bad_threads_string.toml",
        ValidScene(
            R"toml(
[render]
width = 64
height = 32
threads = "fast"
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
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, "render.threads", "must be an integer"));
}
```

- [ ] **Step 2: Add failing render scene propagation tests**

In `tests/render_scene_tests.cpp`, update `MakeBaseScene()` after seed:

```cpp
scene.render.threads = 4;
```

Update `render_scene_defaults_are_backend_friendly` after seed:

```cpp
YR_EXPECT_EQ(scene.threads, 0);
```

Update `scene_compiler_copies_render_settings` after seed:

```cpp
YR_EXPECT_EQ(compiled.threads, 4);
```

- [ ] **Step 3: Run tests to verify red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build fails because `RenderSettings::threads` and `RenderScene::threads` do not exist.

- [ ] **Step 4: Add semantic and compiled fields**

In `include/yaoray/scene/scene.hpp`, add after `seed`:

```cpp
int threads = 0;
```

In `include/yaoray/render/render_scene.hpp`, add after `seed`:

```cpp
int threads = 0;
```

- [ ] **Step 5: Parse and validate `render.threads`**

In `src/scene/scene_parser.cpp`, update the `CheckUnknownFields` list in `ParseRender`:

```cpp
{"backend", "integrator", "width", "height", "spp", "max_depth", "seed", "threads"},
```

After seed parsing in `ParseRender`, add:

```cpp
if (const auto threads = ReadInt(table, "threads", file, "render.threads", diagnostics)) {
    if (*threads < 0) {
        diagnostics.push_back(Error(file, "render.threads", "must be non-negative"));
    } else {
        scene.render.threads = *threads;
    }
}
```

Keep `threads = 0` valid. Do not add required-field diagnostics for missing `threads`.

- [ ] **Step 6: Copy threads into `RenderScene`**

In `src/render/scene_compiler.cpp`, in `CompileScene`, after `compiled.seed = scene.render.seed;` add:

```cpp
compiled.threads = scene.render.threads;
```

- [ ] **Step 7: Run scene tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 8: Commit schema and compiler propagation**

Run:

```powershell
git add include/yaoray/scene/scene.hpp src/scene/scene_parser.cpp include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp tests/scene_tests.cpp tests/render_scene_tests.cpp
git commit -m "feat: add render thread setting"
```

## Task 2: Add Thin CPU Tile Scheduler

**Files:**
- Create: `include/yaoray/backends/cpu/cpu_tile_scheduler.hpp`
- Create: `src/backends/cpu/cpu_tile_scheduler.cpp`
- Create: `tests/cpu_tile_scheduler_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing tile scheduler tests**

Create `tests/cpu_tile_scheduler_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <atomic>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>

namespace {

std::set<std::pair<int, int>> CoveredPixels(const yr::CpuTileSchedule& schedule) {
    std::set<std::pair<int, int>> pixels;
    for (const yr::CpuTile& tile : schedule.tiles) {
        for (int y = tile.y0; y < tile.y1; ++y) {
            for (int x = tile.x0; x < tile.x1; ++x) {
                pixels.emplace(x, y);
            }
        }
    }
    return pixels;
}

} // namespace

YR_TEST(cpu_tile_scheduler_builds_single_edge_clamped_tile) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(7, 5, 1, 16);

    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{1});
    YR_EXPECT_EQ(schedule.tiles[0].x0, 0);
    YR_EXPECT_EQ(schedule.tiles[0].y0, 0);
    YR_EXPECT_EQ(schedule.tiles[0].x1, 7);
    YR_EXPECT_EQ(schedule.tiles[0].y1, 5);
    YR_EXPECT_EQ(schedule.worker_count, 1);
}

YR_TEST(cpu_tile_scheduler_covers_image_without_overlap) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(33, 17, 4, 16);
    const std::set<std::pair<int, int>> pixels = CoveredPixels(schedule);

    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{6});
    YR_EXPECT_EQ(pixels.size(), std::size_t{33 * 17});
    YR_EXPECT_TRUE(pixels.contains({0, 0}));
    YR_EXPECT_TRUE(pixels.contains({32, 16}));
}

YR_TEST(cpu_tile_scheduler_resolves_auto_workers_to_at_least_one) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(64, 64, 0, 16);

    YR_EXPECT_TRUE(schedule.worker_count >= 1);
    YR_EXPECT_TRUE(schedule.worker_count <= static_cast<int>(schedule.tiles.size()));
}

YR_TEST(cpu_tile_scheduler_caps_workers_to_tile_count) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(17, 17, 64, 16);

    YR_EXPECT_EQ(schedule.tiles.size(), std::size_t{4});
    YR_EXPECT_EQ(schedule.worker_count, 4);
}

YR_TEST(cpu_tile_scheduler_visits_all_tiles_with_one_worker) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(33, 17, 1, 16);
    std::vector<int> visits(schedule.tiles.size(), 0);

    yr::ForEachCpuTile(schedule, [&](const yr::CpuTile& tile, int worker_index) {
        YR_EXPECT_EQ(worker_index, 0);
        for (std::size_t i = 0; i < schedule.tiles.size(); ++i) {
            if (schedule.tiles[i].x0 == tile.x0 && schedule.tiles[i].y0 == tile.y0) {
                ++visits[i];
            }
        }
    });

    for (int count : visits) {
        YR_EXPECT_EQ(count, 1);
    }
}

YR_TEST(cpu_tile_scheduler_visits_all_tiles_with_multiple_workers) {
    const yr::CpuTileSchedule schedule = yr::BuildCpuTileSchedule(33, 17, 4, 16);
    std::vector<std::atomic<int>> visits(schedule.tiles.size());
    std::atomic<bool> worker_indexes_valid{true};

    yr::ForEachCpuTile(schedule, [&](const yr::CpuTile& tile, int worker_index) {
        if (worker_index < 0 || worker_index >= schedule.worker_count) {
            worker_indexes_valid = false;
        }
        for (std::size_t i = 0; i < schedule.tiles.size(); ++i) {
            if (schedule.tiles[i].x0 == tile.x0 && schedule.tiles[i].y0 == tile.y0) {
                ++visits[i];
            }
        }
    });

    YR_EXPECT_TRUE(worker_indexes_valid.load());
    for (const std::atomic<int>& count : visits) {
        YR_EXPECT_EQ(count.load(), 1);
    }
}
```

Add `tests/cpu_tile_scheduler_tests.cpp` to the `yaoray_tests` executable in `CMakeLists.txt`.

Add `src/backends/cpu/cpu_tile_scheduler.cpp` to the `yaoray_backends` library.

- [ ] **Step 2: Run build to verify tile scheduler red**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build fails because `cpu_tile_scheduler.hpp` and `cpu_tile_scheduler.cpp` do not exist.

- [ ] **Step 3: Add tile scheduler header**

Create `include/yaoray/backends/cpu/cpu_tile_scheduler.hpp`:

```cpp
#pragma once

#include <functional>
#include <vector>

namespace yr {

struct CpuTile {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
};

struct CpuTileSchedule {
    std::vector<CpuTile> tiles;
    int requested_threads = 0;
    int worker_count = 1;
    int tile_size = 16;
};

CpuTileSchedule BuildCpuTileSchedule(int width, int height, int requested_threads, int tile_size = 16);

using CpuTileCallback = std::function<void(const CpuTile& tile, int worker_index)>;

void ForEachCpuTile(const CpuTileSchedule& schedule, const CpuTileCallback& callback);

} // namespace yr
```

- [ ] **Step 4: Add tile scheduler implementation**

Create `src/backends/cpu/cpu_tile_scheduler.cpp`:

```cpp
#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace yr {
namespace {

int ResolveAutoWorkerCount() {
    const unsigned hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads <= 1) {
        return 1;
    }
    return static_cast<int>(hardware_threads - 1);
}

int ResolveWorkerCount(int requested_threads, std::size_t tile_count) {
    if (tile_count == 0) {
        return 1;
    }

    const int requested = requested_threads == 0 ? ResolveAutoWorkerCount() : requested_threads;
    return std::clamp(requested, 1, static_cast<int>(tile_count));
}

} // namespace

CpuTileSchedule BuildCpuTileSchedule(int width, int height, int requested_threads, int tile_size) {
    CpuTileSchedule schedule;
    schedule.requested_threads = requested_threads;
    schedule.tile_size = std::max(1, tile_size);

    if (width <= 0 || height <= 0) {
        schedule.worker_count = 1;
        return schedule;
    }

    for (int y = 0; y < height; y += schedule.tile_size) {
        for (int x = 0; x < width; x += schedule.tile_size) {
            schedule.tiles.push_back(CpuTile{
                x,
                y,
                std::min(x + schedule.tile_size, width),
                std::min(y + schedule.tile_size, height)
            });
        }
    }

    schedule.worker_count = ResolveWorkerCount(requested_threads, schedule.tiles.size());
    return schedule;
}

void ForEachCpuTile(const CpuTileSchedule& schedule, const CpuTileCallback& callback) {
    if (schedule.tiles.empty()) {
        return;
    }

    const int worker_count = std::max(1, schedule.worker_count);
    if (worker_count == 1) {
        for (const CpuTile& tile : schedule.tiles) {
            callback(tile, 0);
        }
        return;
    }

    std::atomic<std::size_t> next_tile{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));

    for (int worker_index = 0; worker_index < worker_count; ++worker_index) {
        workers.emplace_back([&schedule, &callback, &next_tile, worker_index]() {
            while (true) {
                const std::size_t tile_index = next_tile.fetch_add(1);
                if (tile_index >= schedule.tiles.size()) {
                    break;
                }
                callback(schedule.tiles[tile_index], worker_index);
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }
}

} // namespace yr
```

- [ ] **Step 5: Run tile scheduler tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 6: Commit tile scheduler**

Run:

```powershell
git add CMakeLists.txt include/yaoray/backends/cpu/cpu_tile_scheduler.hpp src/backends/cpu/cpu_tile_scheduler.cpp tests/cpu_tile_scheduler_tests.cpp
git commit -m "feat: add cpu tile scheduler"
```

## Task 3: Use Tile Scheduler In CPU Path Tracer Deterministically

**Files:**
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_path_tracer.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add failing stats and path determinism tests**

In `tests/backend_tests.cpp`, add to `cpu_backend_renders_film_and_stats`:

```cpp
YR_EXPECT_EQ(result.stats.threads, 1);
```

In `cpu_backend_dispatches_path_integrator`, set a path thread request and assert the mapped stat:

```cpp
scene.threads = 2;
```

After the sample-count assertion, add:

```cpp
YR_EXPECT_EQ(result.stats.threads, 2);
```

In `cpu_backend_keeps_debug_direct_as_default_integrator`, after `scene.spp = 4;` add:

```cpp
scene.threads = 4;
```

Then assert debug still reports one thread:

```cpp
YR_EXPECT_EQ(result.stats.threads, 1);
```

In `tests/cpu_path_tracer_tests.cpp`, add helpers after `AnyPixelDifferent`:

```cpp
bool FilmsEqual(const yr::Film& first, const yr::Film& second) {
    if (first.Width() != second.Width() || first.Height() != second.Height()) {
        return false;
    }
    for (int y = 0; y < first.Height(); ++y) {
        for (int x = 0; x < first.Width(); ++x) {
            if (first.SampleCount(x, y) != second.SampleCount(x, y)) {
                return false;
            }
            if (!ColorEqual(first.LinearPixel(x, y), second.LinearPixel(x, y))) {
                return false;
            }
        }
    }
    return true;
}

bool CoreStatsEqual(const yr::CpuPathTraceStats& first, const yr::CpuPathTraceStats& second) {
    return
        first.rays_traced == second.rays_traced &&
        first.shadow_rays == second.shadow_rays &&
        first.occluded_shadow_rays == second.occluded_shadow_rays &&
        first.triangle_tests == second.triangle_tests &&
        first.bvh_node_tests == second.bvh_node_tests &&
        first.bvh_nodes == second.bvh_nodes &&
        first.bvh_max_depth == second.bvh_max_depth &&
        first.hits == second.hits &&
        first.misses == second.misses;
}

yr::RenderScene MakeThreadedDeterminismScene(int threads) {
    yr::RenderScene scene = MakeIndirectBounceScene(2);
    scene.width = 17;
    scene.height = 17;
    scene.spp = 2;
    scene.seed = 19;
    scene.threads = threads;
    RebuildBvh(scene);
    return scene;
}
```

Add tests before `cpu_path_tracer_respects_max_depth_for_indirect_environment_bounce`:

```cpp
YR_TEST(cpu_path_tracer_reports_single_thread_when_requested) {
    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(MakeThreadedDeterminismScene(1));

    YR_EXPECT_EQ(result.stats.threads, 1);
}

YR_TEST(cpu_path_tracer_reports_capped_requested_threads) {
    yr::RenderScene scene = MakeThreadedDeterminismScene(4);

    const yr::CpuPathTraceResult result = yr::RenderCpuPathTrace(scene);

    YR_EXPECT_EQ(result.stats.threads, 4);
}

YR_TEST(cpu_path_tracer_is_bitwise_identical_across_thread_counts) {
    const yr::CpuPathTraceResult single = yr::RenderCpuPathTrace(MakeThreadedDeterminismScene(1));
    const yr::CpuPathTraceResult two = yr::RenderCpuPathTrace(MakeThreadedDeterminismScene(2));
    const yr::CpuPathTraceResult four = yr::RenderCpuPathTrace(MakeThreadedDeterminismScene(4));

    YR_EXPECT_TRUE(FilmsEqual(single.film, two.film));
    YR_EXPECT_TRUE(FilmsEqual(single.film, four.film));
    YR_EXPECT_TRUE(CoreStatsEqual(single.stats, two.stats));
    YR_EXPECT_TRUE(CoreStatsEqual(single.stats, four.stats));
    YR_EXPECT_EQ(single.stats.threads, 1);
    YR_EXPECT_EQ(two.stats.threads, 2);
    YR_EXPECT_EQ(four.stats.threads, 4);
}
```

- [ ] **Step 2: Run tests to verify red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build fails because `threads` is missing from `RenderStats` and `CpuPathTraceStats`.

- [ ] **Step 3: Add thread fields to stats surfaces**

In `include/yaoray/backends/backend.hpp`, add before `elapsed_seconds`:

```cpp
int threads = 1;
```

In `include/yaoray/backends/cpu/cpu_path_tracer.hpp`, add before `elapsed_seconds`:

```cpp
int threads = 1;
```

- [ ] **Step 4: Map thread stats in CPU backend**

In `src/backends/cpu/cpu_debug_backend.cpp`, in `ToRenderStats(const CpuDebugRenderStats& stats)`, before elapsed seconds:

```cpp
result.threads = 1;
```

In `ToRenderStats(const CpuPathTraceStats& stats)`, before elapsed seconds:

```cpp
result.threads = stats.threads;
```

- [ ] **Step 5: Refactor path tracer stats accumulation for worker-local merge**

In `src/backends/cpu/cpu_path_tracer.cpp`, add include:

```cpp
#include <yaoray/backends/cpu/cpu_tile_scheduler.hpp>
```

Add standard include:

```cpp
#include <vector>
```

After `AccumulateTraceStats`, add:

```cpp
void MergeTraceStats(CpuPathTraceStats& target, const CpuPathTraceStats& source) {
    target.rays_traced += source.rays_traced;
    target.shadow_rays += source.shadow_rays;
    target.occluded_shadow_rays += source.occluded_shadow_rays;
    target.triangle_tests += source.triangle_tests;
    target.bvh_node_tests += source.bvh_node_tests;
    target.hits += source.hits;
    target.misses += source.misses;
}
```

Replace `RenderCpuPathTrace` with:

```cpp
CpuPathTraceResult RenderCpuPathTrace(const RenderScene& scene) {
    CpuPathTraceResult result{Film{scene.width, scene.height}, {}};
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;

    const CpuTileSchedule schedule = BuildCpuTileSchedule(scene.width, scene.height, scene.threads, 16);
    result.stats.threads = schedule.worker_count;
    std::vector<CpuPathTraceStats> worker_stats(static_cast<std::size_t>(schedule.worker_count));

    const auto start = std::chrono::steady_clock::now();
    const int samples_per_pixel = std::max(1, scene.spp);
    ForEachCpuTile(schedule, [&](const CpuTile& tile, int worker_index) {
        CpuPathTraceStats& stats = worker_stats[static_cast<std::size_t>(worker_index)];
        for (int y = tile.y0; y < tile.y1; ++y) {
            for (int x = tile.x0; x < tile.x1; ++x) {
                for (int sample = 0; sample < samples_per_pixel; ++sample) {
                    Rng rng{SeedFor(scene, x, y, sample)};
                    const float pixel_u = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
                    const float pixel_v = samples_per_pixel == 1 ? 0.5f : rng.NextFloat();
                    const Ray3f ray = MakeCameraRay(scene, x, y, pixel_u, pixel_v);
                    result.film.AddSample(x, y, TracePath(scene, ray, rng, stats));
                }
            }
        }
    });

    for (const CpuPathTraceStats& stats : worker_stats) {
        MergeTraceStats(result.stats, stats);
    }

    const auto end = std::chrono::steady_clock::now();
    result.stats.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    return result;
}
```

Do not change `TracePath` sampling behavior. Do not change `debug_direct`.

- [ ] **Step 6: Run path tracer and backend tests**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes. The bitwise thread-count test must pass exactly, not with a tolerance.

- [ ] **Step 7: Commit path tracer tile integration**

Run:

```powershell
git add include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_path_tracer.hpp src/backends/cpu/cpu_debug_backend.cpp src/backends/cpu/cpu_path_tracer.cpp tests/backend_tests.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: thread cpu path tracer tiles"
```

## Task 4: Add CLI Performance Output And Documentation

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `tests/fixtures/scene/path_tracer_bounce.toml`

- [ ] **Step 1: Add failing CLI smoke checks**

In `tests/fixtures/scene/path_tracer_bounce.toml`, add `threads = 2` to `[render]`:

```toml
threads = 2
```

In `CMakeLists.txt`, update `yaoray_cli_render_path` so the command string also checks:

```powershell
if ($out -notmatch 'Threads: 2') { exit 1 };
if ($out -notmatch 'Samples/sec:') { exit 1 };
if ($out -notmatch 'Rays/sec:') { exit 1 };
```

Place these checks after the existing `Integrator: path` check and before `Rendered image`.

- [ ] **Step 2: Run CTest to verify CLI red**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_cli_render_path
```

Expected: `yaoray_cli_render_path` fails because the CLI does not print `Threads`, `Samples/sec`, or `Rays/sec`.

- [ ] **Step 3: Add CLI rate helper and output**

In `src/app/main.cpp`, add includes:

```cpp
#include <cstdint>
```

Add helpers before `RunRender`:

```cpp
double SafeRate(double numerator, double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) {
        return 0.0;
    }
    return numerator / elapsed_seconds;
}

std::uint64_t TotalSamples(const yr::Film& film) {
    std::uint64_t samples = 0;
    for (int y = 0; y < film.Height(); ++y) {
        for (int x = 0; x < film.Width(); ++x) {
            samples += film.SampleCount(x, y);
        }
    }
    return samples;
}
```

After image write succeeds and before printing trace counters, add:

```cpp
const std::uint64_t total_samples = TotalSamples(*render_result.film);
```

Then print after `Rendered image`:

```cpp
std::cout << "Threads: " << render_result.stats.threads << '\n';
std::cout << "Samples/sec: " << SafeRate(static_cast<double>(total_samples), render_result.stats.elapsed_seconds) << '\n';
std::cout << "Rays/sec: " << SafeRate(static_cast<double>(render_result.stats.rays_traced), render_result.stats.elapsed_seconds) << '\n';
```

Keep `Elapsed seconds` unchanged.

- [ ] **Step 4: Update README**

In `README.md`, under "Current Status", after:

```markdown
- selectable render integrators with a first deterministic CPU path tracer
```

add:

```markdown
- deterministic tile-based CPU path tracing with scene-controlled worker count
```

Update the render paragraph to mention `render.threads`:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders deterministic CPU images to PNG or ASCII PPM based on `film.output`. `render.integrator = "debug_direct"` is the default direct-lighting debug renderer for fast smoke tests. `render.integrator = "path"` selects the first CPU path tracer with diffuse bounce, deterministic sampling, explicit center-sampled direct light, and deterministic tile-based CPU worker scheduling. `render.threads = 0` chooses an automatic worker count, while positive values fix the worker count for benchmark comparisons. The Cornell Box path example uses Cornell's measured geometry with RGB material approximations; it is an indirect-lighting preview, not a physically matched spectral render.
```

- [ ] **Step 5: Update architecture docs**

In `docs/architecture/overview.md`, under "Current implemented slices", after:

```markdown
- render integrator selection with a deterministic CPU path tracer v0
```

add:

```markdown
- scene-controlled deterministic CPU tile scheduling for the path integrator
```

Replace the CPU backend paragraph with:

```markdown
The CPU backend supports two integrators. `debug_direct` is the simple reference path through camera rays, BVH traversal, triangle intersection, deterministic center-sampled area-light direct lighting, BVH shadow rays, Film accumulation, tone mapping, and PNG/PPM output. `path` is the first CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse bounce, emissive hits, explicit center-sampled direct light, and deterministic tile-based CPU worker scheduling controlled by `render.threads`. It is still a v0 integrator without MIS, Russian roulette, spectral rendering, random area-light sampling, or final-quality material models.
```

- [ ] **Step 6: Run full verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: full CTest passes.

- [ ] **Step 7: Manually compare thread counts on Cornell path**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
New-Item -ItemType Directory -Force -Path .cache | Out-Null
(Get-Content scenes\examples\cornell_box_path.toml) -replace 'spp = 16', 'spp = 4' -replace 'width = 256', 'width = 128' -replace 'height = 256', 'height = 128' -replace 'seed = 1', "seed = 1`nthreads = 1" | Set-Content .cache\cornell_thread_1.toml
(Get-Content scenes\examples\cornell_box_path.toml) -replace 'spp = 16', 'spp = 4' -replace 'width = 256', 'width = 128' -replace 'height = 256', 'height = 128' -replace 'seed = 1', "seed = 1`nthreads = 0" | Set-Content .cache\cornell_thread_auto.toml
& $yaoray render .cache\cornell_thread_1.toml --backend cpu
& $yaoray render .cache\cornell_thread_auto.toml --backend cpu
```

Expected:

- first output contains `Threads: 1`
- second output contains `Threads:` with a value at least `1`
- both runs render PNG output successfully

Do not commit files under `.cache`.

- [ ] **Step 8: Commit CLI output and docs**

Run:

```powershell
git add src/app/main.cpp CMakeLists.txt README.md docs/architecture/overview.md tests/fixtures/scene/path_tracer_bounce.toml
git commit -m "feat: report cpu path threading stats"
```

## Task 5: Final Scope Checks And Branch Verification

**Files:**
- No planned file edits.

- [ ] **Step 1: Run scope checks**

Run:

```powershell
rg -n "threads|tile|Samples/sec|Rays/sec|std::thread|hardware_concurrency|debug_direct|CUDA|MIS|Russian roulette|spectral" README.md docs/architecture/overview.md include src tests scenes docs/superpowers/specs/2026-05-15-yaoray-cpu-path-tracer-threading-design.md docs/superpowers/plans/2026-05-15-yaoray-cpu-path-tracer-threading-implementation-plan.md
```

Expected:

- `std::thread` and `hardware_concurrency` appear only in the tile scheduler implementation or related tests.
- `debug_direct` is documented as unchanged.
- CUDA/MIS/Russian roulette/spectral mentions are docs/non-goals or existing stubs, not new implementations.

- [ ] **Step 2: Run complete verification**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass.

- [ ] **Step 3: Check git state and log**

Run:

```powershell
git status --short --branch
git log --oneline --decorate --max-count 10
```

Expected:

- working tree is clean
- recent commits include:
  - `feat: report cpu path threading stats`
  - `feat: thread cpu path tracer tiles`
  - `feat: add cpu tile scheduler`
  - `feat: add render thread setting`

## Self-Review Checklist

- Spec coverage:
  - `render.threads` parser/compiler path: Task 1.
  - thin CPU tile helper: Task 2.
  - path-only tile integration: Task 3.
  - bitwise determinism across thread counts: Task 3.
  - CLI `Threads`, `Samples/sec`, and `Rays/sec`: Task 4.
  - docs and final verification: Tasks 4 and 5.

- Type consistency:
  - Semantic field: `RenderSettings::threads`.
  - Compiled field: `RenderScene::threads`.
  - Common stat: `RenderStats::threads`.
  - Path stat: `CpuPathTraceStats::threads`.
  - Tile helper API: `CpuTile`, `CpuTileSchedule`, `BuildCpuTileSchedule`, `ForEachCpuTile`.

- Scope boundaries:
  - Do not thread `debug_direct`.
  - Do not implement CUDA.
  - Do not add a benchmark subcommand.
  - Do not change path sampling, lighting, materials, or BVH traversal.
  - Do not add a large job system.
