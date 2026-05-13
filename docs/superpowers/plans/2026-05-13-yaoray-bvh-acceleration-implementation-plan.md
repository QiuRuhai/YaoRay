# YaoRay BVH Acceleration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a default BVH acceleration path over compiled `RenderScene::triangles`, use it in the CPU debug renderer, and expose BVH statistics in CLI output.

**Architecture:** Add `RenderBvh` and `BuildBvh()` to `yaoray_render`, then store the built BVH inside `RenderScene` during `CompileScene()`. Add BVH traversal in the render module and switch the CPU debug renderer from scene-wide linear triangle scanning to BVH traversal while preserving `RenderScene::triangles` for shading, debugging, tests, and future backend upload.

**Tech Stack:** C++20, CMake 3.24+, CTest, existing YaoRay `core`, `scene`, `render`, `backends`, and custom `yr_test` harness.

---

## Scope Check

This plan implements only the approved BVH acceleration design:

- `RenderBvh` data structures
- longest-axis median BVH builder
- BVH storage in `RenderScene`
- BVH build during `CompileScene()`
- BVH traversal helper
- CPU debug renderer default BVH traversal
- BVH render stats and CLI output
- tests and docs

It does not implement SAH, user-facing split options, linear runtime fallback, CUDA traversal, GPU upload data, path tracing changes, multithreaded build, persistent BVH cache, or a large showcase scene.

## File Structure

Create or modify these files:

```text
CMakeLists.txt
README.md
docs/architecture/overview.md
include/yaoray/backends/backend.hpp
include/yaoray/backends/cpu/cpu_debug_renderer.hpp
include/yaoray/render/bvh.hpp
include/yaoray/render/render_scene.hpp
src/app/main.cpp
src/backends/cpu/cpu_debug_backend.cpp
src/backends/cpu/cpu_debug_renderer.cpp
src/render/bvh.cpp
src/render/scene_compiler.cpp
tests/backend_tests.cpp
tests/bvh_tests.cpp
tests/cpu_debug_renderer_tests.cpp
tests/render_scene_tests.cpp
```

Responsibilities:

- `include/yaoray/render/bvh.hpp`: public BVH data types, build options, build result, traversal stats, traversal hit, and function declarations.
- `src/render/bvh.cpp`: longest-axis median BVH builder and BVH traversal.
- `include/yaoray/render/render_scene.hpp`: stores `RenderBvh` inside `RenderScene`.
- `src/render/scene_compiler.cpp`: builds BVH after triangle compilation and reports BVH build errors as scene diagnostics.
- `src/backends/cpu/cpu_debug_renderer.cpp`: uses `IntersectBvh()` instead of linear scene traversal.
- `include/yaoray/backends/*` and `src/backends/cpu/cpu_debug_backend.cpp`: carry BVH stats through backend result types.
- `src/app/main.cpp`: prints BVH scene and traversal statistics.
- `tests/bvh_tests.cpp`: builder and traversal unit tests.
- Existing test files: update helpers and assertions for BVH-backed scenes.

## Task 1: Add BVH Builder Module With Tests

**Files:**
- Create: `include/yaoray/render/bvh.hpp`
- Create: `src/render/bvh.cpp`
- Create: `tests/bvh_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add failing BVH builder tests**

Create `tests/bvh_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::RenderTriangle MakeTriangle(float x_offset) {
    return yr::RenderTriangle{
        yr::Point3f{x_offset - 0.25f, -0.25f, 0.0f},
        yr::Point3f{x_offset + 0.25f, -0.25f, 0.0f},
        yr::Point3f{x_offset, 0.25f, 0.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    };
}

bool HasErrorContaining(const yr::BvhBuildResult& result, const char* text) {
    for (const std::string& error : result.errors) {
        if (error.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(bvh_builder_returns_empty_bvh_for_empty_triangle_list) {
    const std::vector<yr::RenderTriangle> triangles;

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.bvh.max_depth, 0);
}

YR_TEST(bvh_builder_builds_single_leaf_for_one_triangle) {
    const std::vector<yr::RenderTriangle> triangles{MakeTriangle(0.0f)};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices[0], 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].left_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].right_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].first_triangle, 0);
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 1);
    YR_EXPECT_EQ(result.bvh.max_depth, 1);
}

YR_TEST(bvh_builder_keeps_four_triangles_in_default_leaf) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{4});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 4);
}

YR_TEST(bvh_builder_splits_five_triangles) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f),
        MakeTriangle(4.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_TRUE(result.bvh.nodes.size() >= std::size_t{3});
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{5});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 0);
    YR_EXPECT_TRUE(result.bvh.nodes[0].left_child > 0);
    YR_EXPECT_TRUE(result.bvh.nodes[0].right_child > 0);
    YR_EXPECT_TRUE(result.bvh.max_depth >= 2);
}

YR_TEST(bvh_builder_honors_max_leaf_triangles_one) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f)
    };
    const yr::BvhBuildOptions options{yr::BvhSplitMethod::LongestAxisMedian, 1};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.triangle_indices.size(), std::size_t{3});
    for (const yr::RenderBvhNode& node : result.bvh.nodes) {
        if (node.triangle_count > 0) {
            YR_EXPECT_EQ(node.triangle_count, 1);
        }
    }
}

YR_TEST(bvh_builder_rejects_invalid_leaf_size) {
    const std::vector<yr::RenderTriangle> triangles{MakeTriangle(0.0f)};
    const yr::BvhBuildOptions options{yr::BvhSplitMethod::LongestAxisMedian, 0};

    const yr::BvhBuildResult result = yr::BuildBvh(triangles, options);

    YR_EXPECT_TRUE(!result.errors.empty());
    YR_EXPECT_TRUE(HasErrorContaining(result, "max_leaf_triangles"));
    YR_EXPECT_TRUE(result.bvh.nodes.empty());
}

YR_TEST(bvh_builder_leaf_indices_are_valid_triangle_indices) {
    const std::vector<yr::RenderTriangle> triangles{
        MakeTriangle(0.0f),
        MakeTriangle(1.0f),
        MakeTriangle(2.0f),
        MakeTriangle(3.0f),
        MakeTriangle(4.0f)
    };

    const yr::BvhBuildResult result = yr::BuildBvh(triangles);

    YR_EXPECT_TRUE(result.errors.empty());
    for (const int triangle_index : result.bvh.triangle_indices) {
        YR_EXPECT_TRUE(triangle_index >= 0);
        YR_EXPECT_TRUE(static_cast<std::size_t>(triangle_index) < triangles.size());
    }
}
```

- [ ] **Step 2: Wire the new source and test into CMake**

In `CMakeLists.txt`, change `yaoray_render`:

```cmake
add_library(yaoray_render STATIC
    src/render/bvh.cpp
    src/render/scene_compiler.cpp
)
```

In the `yaoray_tests` executable source list, add `tests/bvh_tests.cpp` after `tests/assets_tests.cpp`:

```cmake
    tests/assets_tests.cpp
    tests/bvh_tests.cpp
```

- [ ] **Step 3: Run build to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
```

Expected: build fails because `include/yaoray/render/bvh.hpp` and `src/render/bvh.cpp` do not exist yet.

Do not commit this failing state.

- [ ] **Step 4: Add the BVH public API**

Create `include/yaoray/render/bvh.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>

namespace yr {

struct RenderScene;
struct RenderTriangle;

enum class BvhSplitMethod {
    LongestAxisMedian,
};

struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::LongestAxisMedian;
    int max_leaf_triangles = 4;
};

struct RenderBvhNode {
    Bounds3f bounds;
    int left_child = -1;
    int right_child = -1;
    int first_triangle = 0;
    int triangle_count = 0;
};

struct RenderBvh {
    std::vector<RenderBvhNode> nodes;
    std::vector<int> triangle_indices;
    int max_depth = 0;
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(
    const std::vector<RenderTriangle>& triangles,
    const BvhBuildOptions& options = {}
);

} // namespace yr
```

- [ ] **Step 5: Implement `BuildBvh()`**

Create `src/render/bvh.cpp`:

```cpp
#include <yaoray/render/bvh.hpp>

#include <yaoray/render/render_scene.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace yr {
namespace {

struct BvhPrimitive {
    int triangle_index = -1;
    Bounds3f bounds;
    Point3f centroid;
};

bool IsFinite(float value) {
    return std::isfinite(value);
}

bool IsFinite(Point3f point) {
    return IsFinite(point.x) && IsFinite(point.y) && IsFinite(point.z);
}

bool IsFinite(const Bounds3f& bounds) {
    return IsFinite(bounds.min) && IsFinite(bounds.max);
}

Bounds3f UnionBounds(const Bounds3f& a, const Bounds3f& b) {
    return Bounds3f{
        Point3f{
            std::min(a.min.x, b.min.x),
            std::min(a.min.y, b.min.y),
            std::min(a.min.z, b.min.z),
        },
        Point3f{
            std::max(a.max.x, b.max.x),
            std::max(a.max.y, b.max.y),
            std::max(a.max.z, b.max.z),
        },
    };
}

Bounds3f TriangleBounds(const RenderTriangle& triangle) {
    Bounds3f bounds;
    bounds = Union(bounds, triangle.p0);
    bounds = Union(bounds, triangle.p1);
    bounds = Union(bounds, triangle.p2);
    return bounds;
}

Point3f TriangleCentroid(const RenderTriangle& triangle) {
    return Point3f{
        (triangle.p0.x + triangle.p1.x + triangle.p2.x) / 3.0f,
        (triangle.p0.y + triangle.p1.y + triangle.p2.y) / 3.0f,
        (triangle.p0.z + triangle.p1.z + triangle.p2.z) / 3.0f
    };
}

float AxisValue(Point3f point, int axis) {
    if (axis == 0) {
        return point.x;
    }
    if (axis == 1) {
        return point.y;
    }
    return point.z;
}

Vec3f Extent(const Bounds3f& bounds) {
    return Vec3f{
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z
    };
}

int LongestAxis(const Bounds3f& bounds) {
    const Vec3f extent = Extent(bounds);
    if (extent.x >= extent.y && extent.x >= extent.z) {
        return 0;
    }
    if (extent.y >= extent.z) {
        return 1;
    }
    return 2;
}

int BuildRecursive(
    std::vector<BvhPrimitive>& primitives,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    RenderBvh& bvh,
    std::vector<std::string>& errors
) {
    if (begin >= end) {
        errors.push_back("BVH build produced an empty primitive range");
        return -1;
    }

    const int node_index = static_cast<int>(bvh.nodes.size());
    bvh.nodes.push_back(RenderBvhNode{});

    Bounds3f node_bounds;
    Bounds3f centroid_bounds;
    for (int i = begin; i < end; ++i) {
        node_bounds = UnionBounds(node_bounds, primitives[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, primitives[static_cast<std::size_t>(i)].centroid);
    }

    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return -1;
    }

    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) {
        const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
        for (int i = begin; i < end; ++i) {
            bvh.triangle_indices.push_back(primitives[static_cast<std::size_t>(i)].triangle_index);
        }
        bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
            node_bounds,
            -1,
            -1,
            first_triangle,
            primitive_count
        };
        bvh.max_depth = std::max(bvh.max_depth, depth);
        return node_index;
    }

    const int axis = LongestAxis(centroid_bounds);
    const int mid = begin + primitive_count / 2;
    std::nth_element(
        primitives.begin() + begin,
        primitives.begin() + mid,
        primitives.begin() + end,
        [axis](const BvhPrimitive& a, const BvhPrimitive& b) {
            return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
        }
    );

    if (mid == begin || mid == end) {
        errors.push_back("BVH median split produced an empty child range");
        return -1;
    }

    const int left_child = BuildRecursive(primitives, begin, mid, depth + 1, max_leaf_triangles, bvh, errors);
    const int right_child = BuildRecursive(primitives, mid, end, depth + 1, max_leaf_triangles, bvh, errors);
    if (left_child < 0 || right_child < 0) {
        return -1;
    }

    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds,
        left_child,
        right_child,
        0,
        0
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
    return node_index;
}

} // namespace

BvhBuildResult BuildBvh(const std::vector<RenderTriangle>& triangles, const BvhBuildOptions& options) {
    BvhBuildResult result;

    if (options.max_leaf_triangles < 1) {
        result.errors.push_back("BVH max_leaf_triangles must be at least 1");
        return result;
    }

    if (triangles.empty()) {
        return result;
    }

    std::vector<BvhPrimitive> primitives;
    primitives.reserve(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const RenderTriangle& triangle = triangles[i];
        const Bounds3f bounds = TriangleBounds(triangle);
        const Point3f centroid = TriangleCentroid(triangle);
        if (!IsFinite(bounds) || !IsFinite(centroid)) {
            result.errors.push_back("BVH build encountered non-finite triangle data");
            return result;
        }
        primitives.push_back(BvhPrimitive{static_cast<int>(i), bounds, centroid});
    }

    const int root = BuildRecursive(
        primitives,
        0,
        static_cast<int>(primitives.size()),
        1,
        options.max_leaf_triangles,
        result.bvh,
        result.errors
    );
    if (root != 0 || !result.errors.empty()) {
        result.bvh = RenderBvh{};
    }

    return result;
}

} // namespace yr
```

- [ ] **Step 6: Run BVH builder tests and full tests**

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

Run:

```powershell
git add CMakeLists.txt include/yaoray/render/bvh.hpp src/render/bvh.cpp tests/bvh_tests.cpp
git commit -m "feat: add render bvh builder"
```

## Task 2: Store BVH In RenderScene And Build It During Compilation

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp`
- Modify: `src/render/scene_compiler.cpp`
- Modify: `tests/render_scene_tests.cpp`

- [ ] **Step 1: Add failing scene compiler BVH tests**

In `tests/render_scene_tests.cpp`, add these assertions to `render_scene_defaults_are_backend_friendly`:

```cpp
    YR_EXPECT_TRUE(scene.bvh.nodes.empty());
    YR_EXPECT_TRUE(scene.bvh.triangle_indices.empty());
    YR_EXPECT_EQ(scene.bvh.max_depth, 0);
```

Append these tests to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_builds_empty_bvh_for_empty_scene) {
    const yr::SceneCompileResult result = yr::CompileScene(MakeBaseScene());

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene.value().triangles.empty());
    YR_EXPECT_TRUE(result.scene.value().bvh.nodes.empty());
    YR_EXPECT_TRUE(result.scene.value().bvh.triangle_indices.empty());
    YR_EXPECT_EQ(result.scene.value().bvh.max_depth, 0);
}

YR_TEST(scene_compiler_builds_bvh_for_builtin_triangle) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"triangle", "builtin:triangle"});
    scene.instances.push_back(yr::InstanceDescription{"triangle", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices[0], 0);
}

YR_TEST(scene_compiler_builds_bvh_for_obj_quad) {
    yr::SceneDescription scene = MakeBaseScene();
    scene.assets.push_back(yr::AssetDescription{"quad", FixturePath("assets/quad.obj")});
    scene.instances.push_back(yr::InstanceDescription{"quad", {}});

    const yr::SceneCompileResult result = yr::CompileScene(scene);

    YR_EXPECT_TRUE(!yr::HasSceneErrors(result.diagnostics));
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene.value().triangles.size(), std::size_t{2});
    YR_EXPECT_EQ(result.scene.value().bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.scene.value().bvh.triangle_indices.size(), std::size_t{2});
}
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build fails because `RenderScene` does not yet contain `bvh`.

Do not commit this failing state.

- [ ] **Step 3: Add BVH to `RenderScene`**

In `include/yaoray/render/render_scene.hpp`, add this include after the core include block:

```cpp
#include <yaoray/render/bvh.hpp>
```

Add this field at the end of `struct RenderScene`, after `area_lights`:

```cpp
    RenderBvh bvh;
```

- [ ] **Step 4: Build BVH in `CompileScene()`**

In `src/render/scene_compiler.cpp`, add:

```cpp
#include <yaoray/render/bvh.hpp>
```

Then replace the final success block:

```cpp
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }
    result.scene = compiled;
    return result;
```

with:

```cpp
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    BvhBuildResult bvh_result = BuildBvh(compiled.triangles);
    for (const std::string& error : bvh_result.errors) {
        result.diagnostics.push_back(Error(scene, "render.bvh", error));
    }
    if (HasSceneErrors(result.diagnostics)) {
        return result;
    }

    compiled.bvh = std::move(bvh_result.bvh);
    result.scene = std::move(compiled);
    return result;
```

- [ ] **Step 5: Run scene compiler tests and full tests**

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

Run:

```powershell
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: build bvh during scene compilation"
```

## Task 3: Add BVH Traversal And Switch CPU Debug Renderer

**Files:**
- Modify: `include/yaoray/render/bvh.hpp`
- Modify: `src/render/bvh.cpp`
- Modify: `include/yaoray/backends/backend.hpp`
- Modify: `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`
- Modify: `src/backends/cpu/cpu_debug_backend.cpp`
- Modify: `src/backends/cpu/cpu_debug_renderer.cpp`
- Modify: `tests/bvh_tests.cpp`
- Modify: `tests/backend_tests.cpp`
- Modify: `tests/cpu_debug_renderer_tests.cpp`

- [ ] **Step 1: Add failing BVH traversal tests**

Append to `tests/bvh_tests.cpp`:

```cpp
yr::RenderScene MakeBvhScene(std::vector<yr::RenderTriangle> triangles) {
    yr::RenderScene scene;
    scene.triangles = std::move(triangles);
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
    return scene;
}

YR_TEST(bvh_traversal_returns_miss_for_empty_bvh) {
    const yr::RenderScene scene = MakeBvhScene({});
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, ray, stats);

    YR_EXPECT_TRUE(!hit.hit);
    YR_EXPECT_TRUE(hit.triangle == nullptr);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_EQ(stats.node_tests, std::uint64_t{0});
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{0});
}

YR_TEST(bvh_traversal_hits_single_triangle) {
    const yr::RenderScene scene = MakeBvhScene({MakeTriangle(0.0f)});
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_TRUE(hit.triangle != nullptr);
    YR_EXPECT_EQ(hit.triangle_index, 0);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
    YR_EXPECT_TRUE(stats.node_tests > 0);
    YR_EXPECT_EQ(stats.triangle_tests, std::uint64_t{1});
}

YR_TEST(bvh_traversal_returns_nearest_hit) {
    std::vector<yr::RenderTriangle> triangles;
    triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-0.25f, -0.25f, -2.0f},
        yr::Point3f{0.25f, -0.25f, -2.0f},
        yr::Point3f{0.0f, 0.25f, -2.0f},
        yr::Vec3f{0.0f, 0.0f, 1.0f},
        0
    });
    triangles.push_back(MakeTriangle(0.0f));
    const yr::RenderScene scene = MakeBvhScene(std::move(triangles));
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.triangle_index, 1);
    YR_EXPECT_NEAR(hit.t, 1.0, 1e-6);
}

YR_TEST(bvh_traversal_culls_sparse_triangles) {
    const yr::RenderScene scene = MakeBvhScene({
        MakeTriangle(0.0f),
        MakeTriangle(10.0f),
        MakeTriangle(20.0f),
        MakeTriangle(30.0f),
        MakeTriangle(40.0f)
    });
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats stats;

    const yr::BvhHit hit = yr::IntersectBvh(scene, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_TRUE(stats.triangle_tests < std::uint64_t{5});
}
```

Also add these includes near the top of `tests/bvh_tests.cpp`:

```cpp
#include <stdexcept>
#include <utility>

#include <yaoray/core/ray.hpp>
```

- [ ] **Step 2: Run tests to verify the expected RED state**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: build fails because `BvhTraceStats`, `BvhHit`, and `IntersectBvh()` do not exist yet.

Do not commit this failing state.

- [ ] **Step 3: Extend the BVH public API with traversal**

In `include/yaoray/render/bvh.hpp`, add before `BvhBuildResult`:

```cpp
struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
};

struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    const RenderTriangle* triangle = nullptr;
    int triangle_index = -1;
};
```

Add this declaration after `BuildBvh()`:

```cpp
BvhHit IntersectBvh(
    const RenderScene& scene,
    const Ray3f& ray,
    BvhTraceStats& stats
);
```

- [ ] **Step 4: Implement `IntersectBvh()`**

In `src/render/bvh.cpp`, add these constants in the anonymous namespace:

```cpp
constexpr float MinHitT = 1.0e-5f;
constexpr float ParallelEpsilon = 1.0e-8f;
```

Add this helper in the anonymous namespace:

```cpp
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
```

Add this public function after `BuildBvh()`:

```cpp
BvhHit IntersectBvh(const RenderScene& scene, const Ray3f& ray, BvhTraceStats& stats) {
    BvhHit nearest;
    if (scene.bvh.nodes.empty()) {
        return nearest;
    }

    std::vector<int> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        const int node_index = stack.back();
        stack.pop_back();
        if (node_index < 0 || static_cast<std::size_t>(node_index) >= scene.bvh.nodes.size()) {
            continue;
        }

        const RenderBvhNode& node = scene.bvh.nodes[static_cast<std::size_t>(node_index)];
        ++stats.node_tests;
        if (!node.bounds.Intersects(ray, MinHitT, nearest.t)) {
            continue;
        }

        if (node.triangle_count > 0) {
            for (int i = 0; i < node.triangle_count; ++i) {
                const int index_position = node.first_triangle + i;
                if (index_position < 0 ||
                    static_cast<std::size_t>(index_position) >= scene.bvh.triangle_indices.size()) {
                    continue;
                }

                const int triangle_index = scene.bvh.triangle_indices[static_cast<std::size_t>(index_position)];
                if (triangle_index < 0 ||
                    static_cast<std::size_t>(triangle_index) >= scene.triangles.size()) {
                    continue;
                }

                ++stats.triangle_tests;
                float t = 0.0f;
                const RenderTriangle& triangle = scene.triangles[static_cast<std::size_t>(triangle_index)];
                if (IntersectTriangle(ray, triangle, t) && t < nearest.t) {
                    nearest.hit = true;
                    nearest.t = t;
                    nearest.triangle = &triangle;
                    nearest.triangle_index = triangle_index;
                }
            }
        } else {
            if (node.right_child >= 0) {
                stack.push_back(node.right_child);
            }
            if (node.left_child >= 0) {
                stack.push_back(node.left_child);
            }
        }
    }

    return nearest;
}
```

- [ ] **Step 5: Add BVH stats to backend stats types**

In `include/yaoray/backends/backend.hpp`, add fields to `RenderStats` after `triangle_tests`:

```cpp
    std::uint64_t bvh_node_tests = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
```

In `include/yaoray/backends/cpu/cpu_debug_renderer.hpp`, add the same fields to `CpuDebugRenderStats` after `triangle_tests`:

```cpp
    std::uint64_t bvh_node_tests = 0;
    int bvh_nodes = 0;
    int bvh_max_depth = 0;
```

In `src/backends/cpu/cpu_debug_backend.cpp`, update `ToRenderStats()`:

```cpp
    result.bvh_node_tests = stats.bvh_node_tests;
    result.bvh_nodes = stats.bvh_nodes;
    result.bvh_max_depth = stats.bvh_max_depth;
```

- [ ] **Step 6: Switch CPU debug renderer to `IntersectBvh()`**

In `src/backends/cpu/cpu_debug_renderer.cpp`, add:

```cpp
#include <yaoray/render/bvh.hpp>
```

Remove the local `ParallelEpsilon`, `TriangleHit`, `IntersectTriangle()`, and `FindNearestHit()` definitions from the anonymous namespace.

In `RenderCpuDebug()`, after creating `result`, initialize static BVH stats:

```cpp
    result.stats.bvh_nodes = static_cast<int>(scene.bvh.nodes.size());
    result.stats.bvh_max_depth = scene.bvh.max_depth;
```

Replace:

```cpp
            const TriangleHit hit = FindNearestHit(scene, ray, result.stats);
```

with:

```cpp
            BvhTraceStats trace_stats;
            const BvhHit hit = IntersectBvh(scene, ray, trace_stats);
            result.stats.bvh_node_tests += trace_stats.node_tests;
            result.stats.triangle_tests += trace_stats.triangle_tests;
```

The hit handling block remains the same because `BvhHit` exposes `hit.triangle`.

- [ ] **Step 7: Update test scenes to build BVH explicitly**

In `tests/backend_tests.cpp`, add:

```cpp
#include <yaoray/render/bvh.hpp>
```

At the end of `MakeBackendTriangleScene()`, before `return scene;`, add:

```cpp
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
```

Also add:

```cpp
#include <stdexcept>
```

In `tests/cpu_debug_renderer_tests.cpp`, add:

```cpp
#include <stdexcept>
#include <yaoray/render/bvh.hpp>
```

At the end of `MakeDebugTriangleScene()`, before `return scene;`, add:

```cpp
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;
```

In `cpu_debug_renderer_shades_environment_misses`, after `scene.triangles.clear();`, add:

```cpp
    scene.bvh = yr::RenderBvh{};
```

- [ ] **Step 8: Update CPU and backend stat assertions**

In `tests/backend_tests.cpp`, in `cpu_backend_renders_film_and_stats`, add:

```cpp
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
```

In `tests/cpu_debug_renderer_tests.cpp`, in `cpu_debug_renderer_traces_one_ray_per_pixel`, add:

```cpp
    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_EQ(result.stats.bvh_nodes, 1);
    YR_EXPECT_EQ(result.stats.bvh_max_depth, 1);
```

Append this test to `tests/cpu_debug_renderer_tests.cpp`:

```cpp
YR_TEST(cpu_debug_renderer_uses_bvh_to_reduce_triangle_tests) {
    yr::RenderScene scene = MakeDebugTriangleScene(5, 5);
    scene.triangles.clear();
    for (int i = 0; i < 5; ++i) {
        const float x = static_cast<float>(i) * 10.0f;
        scene.triangles.push_back(yr::RenderTriangle{
            yr::Point3f{x - 0.25f, -0.25f, 0.0f},
            yr::Point3f{x + 0.25f, -0.25f, 0.0f},
            yr::Point3f{x, 0.25f, 0.0f},
            yr::Vec3f{0.0f, 0.0f, 1.0f},
            0
        });
    }
    const yr::BvhBuildResult build = yr::BuildBvh(scene.triangles);
    if (!build.errors.empty()) {
        throw std::runtime_error(build.errors[0]);
    }
    scene.bvh = build.bvh;

    const yr::CpuDebugRenderResult result = yr::RenderCpuDebug(scene);

    YR_EXPECT_TRUE(result.stats.bvh_node_tests > 0);
    YR_EXPECT_TRUE(result.stats.triangle_tests < result.stats.rays_traced * std::uint64_t{scene.triangles.size()});
}
```

- [ ] **Step 9: Run traversal, renderer, and full tests**

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

- [ ] **Step 10: Commit**

Run:

```powershell
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp include/yaoray/backends/backend.hpp include/yaoray/backends/cpu/cpu_debug_renderer.hpp src/backends/cpu/cpu_debug_backend.cpp src/backends/cpu/cpu_debug_renderer.cpp tests/bvh_tests.cpp tests/backend_tests.cpp tests/cpu_debug_renderer_tests.cpp
git commit -m "feat: trace cpu debug rays through bvh"
```

## Task 4: Add BVH CLI Output And Documentation

**Files:**
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`

- [ ] **Step 1: Add failing CLI checks for BVH output**

In `CMakeLists.txt`, update `yaoray_cli_render_cpu` so the PowerShell string also checks:

```powershell
if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 };
```

The full `yaoray_cli_render_cpu` command should become:

```cmake
    add_test(NAME yaoray_cli_render_cpu
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/builtin.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/builtin_triangle.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if ($out -notmatch 'Rays traced:') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
```

Update `yaoray_cli_render_obj` so it checks the same BVH output strings:

```cmake
    add_test(NAME yaoray_cli_render_obj
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
            "$outPath = '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/obj_quad.ppm'; Remove-Item -Force -ErrorAction SilentlyContinue $outPath; $out = & '$<TARGET_FILE:yaoray>' render '${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/obj_quad.toml' --backend cpu 2>&1 | Out-String; Write-Output $out; if ($LASTEXITCODE -ne 0) { exit 1 }; if ($out -notmatch 'Compiled triangles: 2') { exit 1 }; if ($out -notmatch 'BVH nodes:') { exit 1 }; if ($out -notmatch 'BVH max depth:') { exit 1 }; if ($out -notmatch 'BVH node tests:') { exit 1 }; if ($out -notmatch 'Rendered image:') { exit 1 }; if (-not (Test-Path $outPath)) { exit 1 }; if ((Get-Content -Path $outPath -TotalCount 1) -ne 'P3') { exit 1 }"
    )
```

- [ ] **Step 2: Run CTest to verify the expected RED state**

Run:

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DYAORAY_ENABLE_CUDA=OFF
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Expected: CLI render tests fail because `yaoray render` does not print BVH stats yet.

Do not commit this failing state.

- [ ] **Step 3: Print BVH stats in the CLI**

In `src/app/main.cpp`, after:

```cpp
    std::cout << "Compiled triangles: " << render_scene.triangles.size() << '\n';
```

add:

```cpp
    std::cout << "BVH nodes: " << render_scene.bvh.nodes.size() << '\n';
    std::cout << "BVH max depth: " << render_scene.bvh.max_depth << '\n';
```

After:

```cpp
    std::cout << "Rays traced: " << render_result.stats.rays_traced << '\n';
```

add:

```cpp
    std::cout << "BVH node tests: " << render_result.stats.bvh_node_tests << '\n';
```

- [ ] **Step 4: Update README**

In `README.md`, add this status bullet after OBJ import:

```markdown
- BVH acceleration over compiled render triangles for the CPU debug renderer
```

Update the future-work sentence to remove generic BVH construction and mention advanced methods:

```markdown
Final path tracing quality, material and texture import, advanced BVH split methods, PNG output, glTF/GLB import, and real CUDA backend support are planned as separate implementation slices.
```

Update the run description paragraph:

```markdown
The `render` command currently parses, compiles, builds a BVH, and renders CPU debug images to ASCII PPM. It supports the built-in triangle and small geometry-only OBJ meshes. This is a correctness and smoke-test renderer, not the final path tracer or final image-quality target.
```

- [ ] **Step 5: Update architecture overview**

In `docs/architecture/overview.md`, update the render layer paragraph so it says `flat world-space triangles plus a BVH`:

```markdown
The render layer compiles that semantic scene into backend-friendly data. The current `yaoray_render` slice provides a minimal `RenderScene` with render settings, camera data, environment data, area lights, materials, flat world-space triangles, and a BVH over those triangles. Rendering is dispatched through a common backend interface so CPU, CUDA, and future OptiX backends can consume this compiled representation without app-layer special cases.
```

Add this implemented slice bullet:

```markdown
- BVH acceleration over compiled render triangles
```

Replace the CPU debug renderer paragraph with:

```markdown
The CPU debug renderer is a simple reference path through camera rays, BVH traversal, triangle intersection, Film accumulation, tone mapping, and PPM output. It is useful for smoke tests and future importer/BVH/CUDA comparisons, while final image quality remains the responsibility of later path tracing work.
```

Update the future-work sentence:

```markdown
PNG output, material and texture import, glTF/GLB import, advanced BVH split methods, a real CPU path tracer, real CUDA rendering, and final-quality image output will be added in focused implementation plans.
```

- [ ] **Step 6: Run docs smoke check, CLI render checks, and full tests**

Run:

```powershell
rg -n "BVH|bvh|advanced BVH|Compiled triangles|BVH node tests" README.md docs/architecture/overview.md src/app/main.cpp CMakeLists.txt
ctest --test-dir build --output-on-failure -C Debug
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
```

Expected CTest result:

```text
100% tests passed
```

Expected manual output includes:

```text
Compiled triangles:
BVH nodes:
BVH max depth:
BVH node tests:
Triangle tests:
```

- [ ] **Step 7: Commit**

Run:

```powershell
git add CMakeLists.txt README.md docs/architecture/overview.md src/app/main.cpp
git commit -m "docs: expose bvh render stats"
```

## Task 5: Final Verification

**Files:**
- Verify all files changed by this plan.

- [ ] **Step 1: Confirm dependency direction**

Run:

```powershell
rg -n "render/bvh|BuildBvh|IntersectBvh|RenderBvh" include\yaoray\scene src\scene include\yaoray\assets src\assets include\yaoray\film src\film src\app
rg -n "render/bvh|BuildBvh|IntersectBvh|RenderBvh" include\yaoray\render src\render include\yaoray\backends src\backends
```

Expected:

- First command prints no output; scene, assets, film, and app do not include BVH builder or traversal APIs directly.
- Second command shows render ownership and backend consumption.

- [ ] **Step 2: Confirm API and tests are discoverable**

Run:

```powershell
rg -n "BvhSplitMethod|BvhBuildOptions|RenderBvhNode|RenderBvh|BvhBuildResult|BvhTraceStats|BvhHit|BuildBvh|IntersectBvh" include src tests
```

Expected: matches appear in `include/yaoray/render/bvh.hpp`, `src/render/bvh.cpp`, `include/yaoray/render/render_scene.hpp`, renderer/backend files, and tests.

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

- [ ] **Step 4: Verify built-in triangle still renders with BVH stats**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\minimal.ppm) { Remove-Item -LiteralPath scenes\examples\out\minimal.ppm -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cpu
Get-Content -Path scenes\examples\out\minimal.ppm -TotalCount 4
```

Expected output includes:

```text
Compiled triangles: 1
BVH nodes: 1
BVH max depth: 1
Rendered image: scenes/examples/out/minimal.ppm
BVH node tests:
Triangle tests:
```

Expected PPM header:

```text
P3
640 360
255
```

- [ ] **Step 5: Verify OBJ example renders with BVH stats**

Run:

```powershell
if (Test-Path -LiteralPath scenes\examples\out\obj_pyramid.ppm) { Remove-Item -LiteralPath scenes\examples\out\obj_pyramid.ppm -Force }
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\obj_pyramid.toml --backend cpu
Get-Content -Path scenes\examples\out\obj_pyramid.ppm -TotalCount 4
```

Expected output includes:

```text
Compiled triangles: 6
BVH nodes:
BVH max depth:
Rendered image: scenes/examples/out/obj_pyramid.ppm
BVH node tests:
Triangle tests:
```

Expected PPM header begins:

```text
P3
640 360
255
```

- [ ] **Step 6: Verify CUDA and unsupported asset failures still behave**

Run:

```powershell
$yaoray = if (Test-Path -LiteralPath .\build\Debug\yaoray.exe) { ".\build\Debug\yaoray.exe" } else { ".\build\yaoray.exe" }
& $yaoray render scenes\examples\minimal.toml --backend cuda
& $yaoray render tests\fixtures\scene\minimal.toml --backend cpu
```

Expected first command exits non-zero and includes:

```text
CUDA backend not implemented yet.
```

Expected second command exits non-zero and includes:

```text
asset import not implemented yet
```

- [ ] **Step 7: Confirm clean worktree and recent commits**

Run:

```powershell
git status --short
git log --oneline --decorate --max-count=10
```

Expected: `git status --short` prints nothing after all task commits.

## Self-Review Notes

Spec coverage:

- `RenderBvh` data structures: Task 1.
- `BuildBvh()` longest-axis median builder: Task 1.
- `RenderScene` owns BVH: Task 2.
- `CompileScene()` builds BVH and converts errors to diagnostics: Task 2.
- Default CPU BVH traversal: Task 3.
- No runtime linear fallback: Task 3 removes CPU linear scene traversal.
- BVH stats in render/backend/CLI: Tasks 3 and 4.
- Empty scenes are legal: Tasks 1, 2, and 3 tests.
- Builder/traversal/renderer/CLI tests: Tasks 1, 2, 3, and 4.
- Docs: Task 4.

Type consistency:

- BVH types live in `include/yaoray/render/bvh.hpp`.
- `RenderScene::bvh` is the only persistent BVH storage.
- `BuildBvh()` returns `BvhBuildResult`, not scene diagnostics.
- `CompileScene()` converts BVH build strings to `SceneDiagnostic` field `render.bvh`.
- `IntersectBvh()` returns `BvhHit` and accumulates `BvhTraceStats`.
- Backend-facing stats use `bvh_node_tests`, `bvh_nodes`, and `bvh_max_depth`.

Implementation guardrails:

- Do not add SAH or other split methods in this plan.
- Do not expose BVH options in TOML in this plan.
- Do not add linear runtime fallback.
- Do not move OBJ loading or asset ownership.
- Do not make `scene`, `assets`, or `film` depend on `yaoray_render`.
- Keep `RenderScene::triangles` intact.
