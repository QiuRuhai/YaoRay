# YaoRay M2 Slice 1 — SAH BVH Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the median-split BVH builder with a binned Surface Area Heuristic (SAH) builder. Make SAH the default; keep `LongestAxisMedian` available as a fallback enum value. Existing 173 unit tests + 8 CTest entries stay green; new tests cover SAH-specific behavior. The `dining-room` smoke scene must render at least 2× faster than today's M1 baseline.

**Architecture:** Inside `src/render/bvh.cpp`, introduce a parallel recursive builder `BuildRecursiveSah` alongside the existing `BuildRecursive` (median-split). Both share the same `BvhPrimRef` input format and `RenderBvh` output format — only the split-decision logic differs. `BuildBvh` dispatches on `options.split_method`. The SAH algorithm bins primitives into 12 buckets per axis (X, Y, Z), uses traversal-vs-intersection cost ratio `c_T = 0.5`, and applies the leaf criterion `SAH cost ≥ count` OR `count ≤ max_leaf_triangles` (default 4). When all centroids land in one bucket on every axis (degenerate input), the algorithm falls back to an even partition along the longest centroid axis.

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. No new third-party libraries.

---

## Spec Coverage

This plan implements **Slice 1** from `docs/superpowers/specs/2026-05-28-yaoray-m2-barcelona-pavilion-design.md`:

1. **Binned SAH algorithm** — 12 buckets per axis, c_T = 0.5, axis × split selection across X/Y/Z.
2. **Leaf criterion** — leaf when `SAH cost ≥ count` OR `count ≤ max_leaf_triangles`.
3. **Public interface** — `BvhSplitMethod::SahBucketBinning` exists and becomes the default; `LongestAxisMedian` stays as a fallback value.
4. **Existing tests stay green** — 173 unit tests + 8 CTest entries pass without modification.
5. **New SAH unit tests** — coverage for SAH validity, leaf criterion, hit-equivalence with median, degenerate fallback.
6. **Performance gate** — `dining-room` renders ≥ 2× faster than M1 baseline (~210 s → ≤ 105 s on the operator's reference machine, 1280×720 / 64 spp). Manual measurement documented in the PR description.

**Out of scope (deferred to subsequent slices):**

- Parallel BVH construction (Slice 2)
- Barcelona Pavilion scene integration (Slice 3)
- Ray packets, SIMD intrinsics
- CUDA backend (M4)

---

## File Structure

**Modified files:**

| Path | Change |
|------|--------|
| `include/yaoray/render/bvh.hpp` | Add `SahBucketBinning` to `BvhSplitMethod` enum; flip `BvhBuildOptions::split_method` default to it. |
| `src/render/bvh.cpp` | Add `SahBucket` struct + `SurfaceArea` + `CentroidToBucket` + `ChooseBestSahSplit` + `BuildRecursiveSah`; dispatch on `options.split_method` in `BuildBvh`. |
| `tests/bvh_tests.cpp` | Add five SAH-specific tests and a `MakeClusteredTriangleScene` helper. |

**New files:** None. The SAH work lives entirely inside the existing BVH module.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `7046d3c`, post-PR-#4 merge). Use the harness-native `EnterWorktree` tool with name `m2-slice1-sah-bvh` (or have a controller run `superpowers:using-git-worktrees`).

Verify the baseline before any change:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe        # expect 173/173 PASS
cd build && ctest --output-on-failure -C Release   # expect 8/8 PASS
cd ..
```

If counts don't match, stop and investigate — this plan assumes the post-PR-#4 baseline is intact. All commits in this plan land on the worktree branch.

---

## Task 1: Add `SahBucketBinning` enum value + dispatch stub

**Files:**

- Modify: `include/yaoray/render/bvh.hpp` — add the enum value (default stays `LongestAxisMedian` for this task).
- Modify: `src/render/bvh.cpp` — add a dispatch stub in `BuildBvh` that errors when SAH is selected.

This task introduces the public-API surface and the stub that makes the next task's tests fail naturally. The default split method is **not** changed yet — that flip lands in Task 3 once the algorithm is in place.

- [ ] **Step 1: Extend the enum**

In `include/yaoray/render/bvh.hpp`, line 15, change:

```cpp
enum class BvhSplitMethod { LongestAxisMedian };
```

to:

```cpp
enum class BvhSplitMethod { LongestAxisMedian, SahBucketBinning };
```

Leave `BvhBuildOptions::split_method` defaulted to `LongestAxisMedian` for now. Leave `max_leaf_triangles = 4` unchanged.

- [ ] **Step 2: Add the dispatch stub in `BuildBvh`**

In `src/render/bvh.cpp`, inside `BuildBvh`, immediately after the existing `max_leaf_triangles < 1` guard (around line 165), add:

```cpp
    if (options.split_method == BvhSplitMethod::SahBucketBinning) {
        result.errors.push_back("SahBucketBinning builder not yet implemented");
        return result;
    }
```

This stub is replaced by real dispatch in Task 2.

- [ ] **Step 3: Build and re-verify the baseline**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: 173/173 unit tests PASS; 8/8 CTest entries PASS. The dispatch only triggers when callers explicitly set `split_method = SahBucketBinning`, and no existing test does that — so nothing changes.

- [ ] **Step 4: Commit**

```bash
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp
git commit -m "$(cat <<'EOF'
feat(bvh): add SahBucketBinning enum value + dispatch stub

Adds the new enum value to BvhSplitMethod and wires a dispatch stub
in BuildBvh that errors when SAH is selected. Sets up the
failing-test ground for the SAH algorithm implementation in the
next commit.

The default split method stays LongestAxisMedian; existing tests
are untouched.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Implement the SAH algorithm

**Files:**

- Modify: `src/render/bvh.cpp` — add SAH helpers, `BuildRecursiveSah`, replace stub with real dispatch.
- Modify: `tests/bvh_tests.cpp` — add five SAH-specific tests and a helper.

This task is the bulk of Slice 1. It introduces:

1. `SahBucket` struct (count + bounds accumulator).
2. `SurfaceArea` helper (handles degenerate bounds).
3. `CentroidToBucket` helper (maps centroid axis-value → bucket index 0..11, clamped).
4. `ChooseBestSahSplit` (returns best `(axis, split_idx, cost)` across X/Y/Z; false when no axis splittable).
5. `BuildRecursiveSah` (recursive builder mirroring `BuildRecursive` but with SAH split logic + fallback).
6. `BuildBvh` dispatch on `options.split_method`.

Constants (file-scope, anonymous namespace):

- `kSahBucketCount = 12` — number of bins per axis.
- `kSahTraversalCost = 0.5f` — c_T per the M2 spec.

The leaf criterion is implemented in `BuildRecursiveSah`:

- If `primitive_count <= max_leaf_triangles` (4 by default): leaf, regardless of SAH.
- Else compute SAH best split. If `best_cost >= primitive_count` (leaf cost): leaf.
- Else split using the SAH partition.
- If `ChooseBestSahSplit` returns false (no axis has non-degenerate centroid extent): fallback to median split along longest centroid axis.
- If the SAH partition collapses to one side (floating-point bucket-boundary edge case): fallback to median split.

- [ ] **Step 1: Write the failing SAH tests + helper in `tests/bvh_tests.cpp`**

Open `tests/bvh_tests.cpp`. Inside the anonymous namespace at the top of the file (after `AddTriangle` and before `HasErrorContaining`, around line 38), add the cluster-scene helper:

```cpp
yr::RenderSceneIR MakeClusteredTriangleScene() {
    // Three clusters of 4 triangles each, along the X axis at x = 0, 5, 10.
    // SAH should isolate one cluster on the first split (4 vs 8 partition),
    // producing a smaller tree than the median-split builder's balanced
    // 6-vs-6 partition.
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 0.05f);
    AddTriangle(scene, 0.10f);
    AddTriangle(scene, 0.15f);
    AddTriangle(scene, 5.00f);
    AddTriangle(scene, 5.05f);
    AddTriangle(scene, 5.10f);
    AddTriangle(scene, 5.15f);
    AddTriangle(scene, 10.00f);
    AddTriangle(scene, 10.05f);
    AddTriangle(scene, 10.10f);
    AddTriangle(scene, 10.15f);
    return scene;  // 12 triangles total
}
```

Then append the five new tests at the bottom of the file (after the last existing `YR_TEST` block):

```cpp
YR_TEST(bvh_sah_builds_valid_bvh_for_five_triangles) {
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 1.0f);
    AddTriangle(scene, 2.0f);
    AddTriangle(scene, 3.0f);
    AddTriangle(scene, 4.0f);

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 5);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
    YR_EXPECT_TRUE(result.bvh.max_depth >= 1);
}

YR_TEST(bvh_sah_chooses_better_split_than_median_for_clusters) {
    // Asymmetric input: SAH isolates a 4-triangle cluster on the first
    // split, producing fewer total nodes than median's balanced 6-vs-6
    // split. Expected: SAH = 5 nodes, median = 7 nodes.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult sah = yr::BuildBvh(scene, sah_options);

    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;
    const yr::BvhBuildResult median = yr::BuildBvh(scene, median_options);

    YR_EXPECT_TRUE(sah.errors.empty());
    YR_EXPECT_TRUE(median.errors.empty());
    YR_EXPECT_EQ(sah.bvh.total_triangles, 12);
    YR_EXPECT_EQ(median.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(sah.bvh.nodes.size() < median.bvh.nodes.size());
}

YR_TEST(bvh_sah_and_median_return_same_hit_for_simple_ray) {
    // Both split methods must produce a BVH that, traced with the same
    // ray, returns the same hit. Geometry organization may differ but
    // the per-pixel intersection result must not.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions sah_options;
    sah_options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult sah = yr::BuildBvh(scene, sah_options);

    yr::BvhBuildOptions median_options;
    median_options.split_method = yr::BvhSplitMethod::LongestAxisMedian;
    const yr::BvhBuildResult median = yr::BuildBvh(scene, median_options);

    // Ray straight down through the middle cluster at x = 5.
    const yr::Ray3f ray{yr::Point3f{5.0f, 0.0f, 1.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    yr::BvhTraceStats sah_stats;
    yr::BvhTraceStats median_stats;
    const yr::BvhHit sah_hit = yr::IntersectBvh(scene, sah.bvh, ray, sah_stats);
    const yr::BvhHit median_hit = yr::IntersectBvh(scene, median.bvh, ray, median_stats);

    YR_EXPECT_TRUE(sah_hit.hit);
    YR_EXPECT_TRUE(median_hit.hit);
    YR_EXPECT_NEAR(sah_hit.t, median_hit.t, 1e-6f);
    // Both must select the same triangle (the closest-t resolution
    // should agree even though the BVH organization differs).
    YR_EXPECT_EQ(sah_hit.triangle_index, median_hit.triangle_index);
}

YR_TEST(bvh_sah_handles_degenerate_centroid_bounds) {
    // 8 triangles all at the same centroid → centroid bounds is
    // degenerate on every axis → SAH cannot find a valid split.
    // Builder must still produce a valid BVH via the median fallback.
    yr::RenderSceneIR scene;
    for (int i = 0; i < 8; ++i) {
        AddTriangle(scene, 0.0f);
    }

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 8);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}

YR_TEST(bvh_sah_respects_max_leaf_triangles) {
    // With max_leaf_triangles = 4 and 3 well-separated triangles, SAH
    // must produce a single leaf (count ≤ max_leaf_triangles triggers
    // the leaf criterion regardless of SAH cost).
    yr::RenderSceneIR scene = MakeSingleTriangleScene(0.0f);
    AddTriangle(scene, 1.0f);
    AddTriangle(scene, 2.0f);

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.nodes.size(), std::size_t{1});
    YR_EXPECT_EQ(result.bvh.nodes[0].triangle_count, 3);
    YR_EXPECT_EQ(result.bvh.nodes[0].left_child, -1);
    YR_EXPECT_EQ(result.bvh.nodes[0].right_child, -1);
}
```

- [ ] **Step 2: Run the new tests, verify they FAIL**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the five new `bvh_sah_*` tests FAIL with errors propagated from the dispatch stub ("SahBucketBinning builder not yet implemented"). The existing 173 tests continue to PASS.

If the new tests pass instead of failing, the dispatch stub from Task 1 isn't reaching `BuildBvh` — investigate before continuing.

- [ ] **Step 3: Implement the SAH algorithm in `src/render/bvh.cpp`**

Open `src/render/bvh.cpp`. At the top of the file, ensure `<limits>` is included:

```cpp
#include <limits>
```

Add it to the existing standard-library include block (after `<cstddef>`).

Inside the anonymous namespace (between the existing `BuildRecursive` function and the closing `}` of the namespace, around line 158), add the SAH helpers and the new recursive builder:

```cpp
constexpr int kSahBucketCount = 12;
constexpr float kSahTraversalCost = 0.5f;

struct SahBucket {
    int count = 0;
    Bounds3f bounds;
};

float SurfaceArea(const Bounds3f& bounds) {
    if (bounds.min.x > bounds.max.x ||
        bounds.min.y > bounds.max.y ||
        bounds.min.z > bounds.max.z) {
        return 0.0f;
    }
    const Vec3f e = Extent(bounds);
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

int CentroidToBucket(float centroid_axis, float bounds_min_axis, float extent_axis) {
    // Map centroid_axis ∈ [bounds_min_axis, bounds_min_axis + extent_axis]
    // → bucket ∈ [0, kSahBucketCount). Caller guarantees extent_axis > 0.
    const float t = (centroid_axis - bounds_min_axis) / extent_axis;
    int bucket = static_cast<int>(t * static_cast<float>(kSahBucketCount));
    if (bucket < 0) bucket = 0;
    if (bucket >= kSahBucketCount) bucket = kSahBucketCount - 1;
    return bucket;
}

// Returns true and populates *out_axis, *out_split_idx, *out_cost if a valid
// SAH split is found. Returns false when every axis has near-zero centroid
// extent (degenerate input) or every candidate split would leave one side
// empty.
bool ChooseBestSahSplit(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int* out_axis,
    int* out_split_idx,
    float* out_cost
) {
    const float parent_area = SurfaceArea(node_bounds);
    if (parent_area <= 0.0f) {
        return false;
    }
    const Vec3f centroid_extent = Extent(centroid_bounds);

    float best_cost = std::numeric_limits<float>::infinity();
    int best_axis = -1;
    int best_split = -1;

    for (int axis = 0; axis < 3; ++axis) {
        const float extent_axis =
            (axis == 0) ? centroid_extent.x :
            (axis == 1) ? centroid_extent.y : centroid_extent.z;
        if (extent_axis < ParallelEpsilon) {
            continue;
        }
        const float min_axis = AxisValue(centroid_bounds.min, axis);

        SahBucket buckets[kSahBucketCount];
        for (int i = begin; i < end; ++i) {
            const BvhPrimRef& p = prims[static_cast<std::size_t>(i)];
            const int b = CentroidToBucket(AxisValue(p.centroid, axis), min_axis, extent_axis);
            buckets[b].count += 1;
            buckets[b].bounds = UnionBounds(buckets[b].bounds, p.bounds);
        }

        // Forward sweep: prefix counts + prefix bounds (left side of each split).
        SahBucket left[kSahBucketCount];
        left[0] = buckets[0];
        for (int i = 1; i < kSahBucketCount; ++i) {
            left[i].count = left[i - 1].count + buckets[i].count;
            left[i].bounds = UnionBounds(left[i - 1].bounds, buckets[i].bounds);
        }

        // Backward sweep: suffix counts + suffix bounds (right side of each split).
        SahBucket right[kSahBucketCount];
        right[kSahBucketCount - 1] = buckets[kSahBucketCount - 1];
        for (int i = kSahBucketCount - 2; i >= 0; --i) {
            right[i].count = right[i + 1].count + buckets[i].count;
            right[i].bounds = UnionBounds(right[i + 1].bounds, buckets[i].bounds);
        }

        // Evaluate splits between bucket split_idx and split_idx + 1.
        for (int split_idx = 0; split_idx < kSahBucketCount - 1; ++split_idx) {
            const int n_left = left[split_idx].count;
            const int n_right = right[split_idx + 1].count;
            if (n_left == 0 || n_right == 0) {
                continue;
            }
            const float cost =
                kSahTraversalCost +
                (static_cast<float>(n_left) * SurfaceArea(left[split_idx].bounds) +
                 static_cast<float>(n_right) * SurfaceArea(right[split_idx + 1].bounds)) /
                parent_area;
            if (cost < best_cost) {
                best_cost = cost;
                best_axis = axis;
                best_split = split_idx;
            }
        }
    }

    if (best_axis < 0) {
        return false;
    }
    *out_axis = best_axis;
    *out_split_idx = best_split;
    *out_cost = best_cost;
    return true;
}

int BuildRecursiveSah(
    std::vector<BvhPrimRef>& prims,
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
        node_bounds = UnionBounds(node_bounds, prims[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, prims[static_cast<std::size_t>(i)].centroid);
    }

    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return -1;
    }

    const int primitive_count = end - begin;

    // Leaf criterion (a): small enough → leaf regardless of SAH cost.
    if (primitive_count <= max_leaf_triangles) {
        const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
        for (int i = begin; i < end; ++i) {
            bvh.triangle_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
        }
        bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
            node_bounds, -1, -1, first_triangle, primitive_count
        };
        bvh.max_depth = std::max(bvh.max_depth, depth);
        return node_index;
    }

    int best_axis = -1;
    int best_split = -1;
    float best_cost = std::numeric_limits<float>::infinity();
    const bool found_split = ChooseBestSahSplit(
        prims, begin, end, node_bounds, centroid_bounds,
        &best_axis, &best_split, &best_cost
    );

    // Leaf criterion (b): SAH says splitting is more expensive than leafing.
    const float leaf_cost = static_cast<float>(primitive_count);
    if (found_split && best_cost >= leaf_cost) {
        const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
        for (int i = begin; i < end; ++i) {
            bvh.triangle_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
        }
        bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
            node_bounds, -1, -1, first_triangle, primitive_count
        };
        bvh.max_depth = std::max(bvh.max_depth, depth);
        return node_index;
    }

    int mid = -1;
    if (found_split) {
        // Partition by bucket assignment along best_axis.
        const Vec3f centroid_extent = Extent(centroid_bounds);
        const float extent_axis =
            (best_axis == 0) ? centroid_extent.x :
            (best_axis == 1) ? centroid_extent.y : centroid_extent.z;
        const float min_axis = AxisValue(centroid_bounds.min, best_axis);
        const int split_idx = best_split;

        auto partition_iter = std::partition(
            prims.begin() + begin, prims.begin() + end,
            [best_axis, min_axis, extent_axis, split_idx](const BvhPrimRef& p) {
                return CentroidToBucket(AxisValue(p.centroid, best_axis), min_axis, extent_axis) <= split_idx;
            }
        );
        mid = static_cast<int>(partition_iter - prims.begin());

        if (mid == begin || mid == end) {
            // Defensive: SAH should never pick a split that leaves a side
            // empty (the n_left==0 / n_right==0 skip in ChooseBestSahSplit
            // filters that), but float rounding in bucket mapping could
            // collapse the partition. Fall back to median.
            mid = begin + primitive_count / 2;
            std::nth_element(
                prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
                [best_axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                    return AxisValue(a.centroid, best_axis) < AxisValue(b.centroid, best_axis);
                }
            );
        }
    } else {
        // No valid SAH split anywhere → degenerate centroid bounds. Force an
        // even partition along the longest centroid axis (extent may be tiny
        // or zero; the nth_element is a no-op when all centroids are equal,
        // and the index-based midpoint still partitions deterministically).
        const int axis = LongestAxis(centroid_bounds);
        mid = begin + primitive_count / 2;
        std::nth_element(
            prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
            [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
            }
        );
    }

    if (mid <= begin || mid >= end) {
        errors.push_back("BVH SAH split produced an empty child range");
        return -1;
    }

    const int left_child = BuildRecursiveSah(
        prims, begin, mid, depth + 1, max_leaf_triangles, bvh, errors);
    const int right_child = BuildRecursiveSah(
        prims, mid, end, depth + 1, max_leaf_triangles, bvh, errors);
    if (left_child < 0 || right_child < 0) {
        return -1;
    }

    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds, left_child, right_child, 0, 0
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
    return node_index;
}
```

Now replace the dispatch stub from Task 1. Delete:

```cpp
    if (options.split_method == BvhSplitMethod::SahBucketBinning) {
        result.errors.push_back("SahBucketBinning builder not yet implemented");
        return result;
    }
```

Then locate the existing `BuildRecursive` call at the bottom of `BuildBvh`:

```cpp
    const int root = BuildRecursive(
        prims,
        0,
        static_cast<int>(prims.size()),
        1,
        options.max_leaf_triangles,
        result.bvh,
        result.errors
    );
```

Replace with a switch on `options.split_method`:

```cpp
    int root = -1;
    switch (options.split_method) {
        case BvhSplitMethod::SahBucketBinning:
            root = BuildRecursiveSah(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                result.bvh,
                result.errors
            );
            break;
        case BvhSplitMethod::LongestAxisMedian:
            root = BuildRecursive(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                result.bvh,
                result.errors
            );
            break;
    }
```

- [ ] **Step 4: Build and verify all tests pass**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: 173 existing tests + 5 new `bvh_sah_*` tests = **178/178 PASS**.

Then run CTest:

```bash
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **8/8 PASS**. (The CTest scenes still use the default `LongestAxisMedian` since we haven't flipped the default yet — they exercise the median path.)

If any of the SAH-specific tests fail:

- `bvh_sah_builds_valid_bvh_for_five_triangles` failing means the basic SAH happy path is broken — check `ChooseBestSahSplit` and the SAH `BuildRecursiveSah` leaf path.
- `bvh_sah_chooses_better_split_than_median_for_clusters` failing means SAH isn't picking a tighter split than median — check the cost formula and the per-axis loop in `ChooseBestSahSplit`.
- `bvh_sah_and_median_return_same_hit_for_simple_ray` failing means the SAH BVH isn't correctly partitioning triangles — likely a partition bug in `BuildRecursiveSah`.
- `bvh_sah_handles_degenerate_centroid_bounds` failing means the `found_split == false` fallback isn't producing a valid BVH — check that branch.
- `bvh_sah_respects_max_leaf_triangles` failing means leaf criterion (a) isn't being applied first.

Fix in `bvh.cpp`; do not loosen the test assertions.

- [ ] **Step 5: Commit**

```bash
git add src/render/bvh.cpp tests/bvh_tests.cpp
git commit -m "$(cat <<'EOF'
feat(bvh): binned SAH builder with cost-based leaf criterion

Implements the Surface Area Heuristic BVH builder per the M2 design:
- 12 buckets per axis, evaluated across X / Y / Z
- Traversal-vs-intersection cost ratio c_T = 0.5
- Leaf criterion: count <= max_leaf_triangles OR SAH cost >= count
- Median fallback when centroid bounds is degenerate on every axis
  or floating-point rounding collapses the SAH partition

The default split method is still LongestAxisMedian; the flip lands
in the next commit. Five new tests cover SAH validity, cluster split
quality, hit equivalence with median split, the degenerate fallback,
and the max_leaf_triangles short-circuit.

Existing 173 tests stay green; new total is 178/178.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Flip the default to SAH

**Files:**

- Modify: `include/yaoray/render/bvh.hpp` — change the default value of `split_method`.

This task makes SAH the default. All 173 existing unit tests + the 5 new SAH tests + 8 CTest scenes must still pass — the existing tests assert generic invariants (node count thresholds, ray-hit results) that hold for either builder.

- [ ] **Step 1: Change the default**

In `include/yaoray/render/bvh.hpp`, change:

```cpp
struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::LongestAxisMedian;
    int max_leaf_triangles = 4;
};
```

to:

```cpp
struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::SahBucketBinning;
    int max_leaf_triangles = 4;
};
```

- [ ] **Step 2: Build and run the full unit test suite**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: **178/178 PASS**.

If a test fails, identify which one and the root cause:

- `bvh_builder_splits_five_triangles` expects `nodes.size() >= 3`. SAH on 5 triangles spaced 1.0 apart along X should split into at least one internal + two leaves → ≥ 3 nodes. If SAH instead returns a single leaf, the test fails — but that should only happen if SAH cost ≥ leaf cost, which on this scene it isn't (sparse triangles have low split cost). Investigate `ChooseBestSahSplit`.
- `bvh_builder_builds_single_leaf_for_one_triangle` — split-method-agnostic.
- `bvh_builder_returns_empty_bvh_for_empty_scene` — split-method-agnostic.
- `bvh_traversal_*` tests trace rays; ray hits don't depend on which builder ran.

Do not loosen the existing tests. Fix SAH if a real bug surfaces.

- [ ] **Step 3: Run the full CTest suite**

```bash
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **8/8 PASS**.

The CTest entries render committed scenes (`hello_emissive`, `cornell_box_pbrt`, `material_studio`, `texture_test`, etc.) and verify pixel output. SAH is a drop-in replacement at the BVH boundary; ray-trace results should be identical or near-identical (FP tolerance, since RNG seeding and ray order are unchanged) to the median-split outputs.

If a CTest scene fails:

- Determine whether the diff is numerical (a handful of pixels off by 1 LSB) — that's expected from traversal-order differences and the assertions should already tolerate it.
- If the diff is structural (missing geometry, wrong shading, NaN region), there's a real SAH bug. Localize by:
  1. Set the scene's BVH build options explicitly to `LongestAxisMedian` in a temporary edit; re-run; confirm pass.
  2. Switch back to `SahBucketBinning`; re-run; confirm fail.
  3. The diff between those two runs isolates the bug to the SAH algorithm.

- [ ] **Step 4: Commit**

```bash
git add include/yaoray/render/bvh.hpp
git commit -m "$(cat <<'EOF'
feat(bvh): default BvhSplitMethod to SahBucketBinning

Flips the default split method on BvhBuildOptions from
LongestAxisMedian to SahBucketBinning. LongestAxisMedian remains
available as a fallback / debugging value but no production code
path selects it now.

All 178 unit tests + 8 CTest entries pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Measure the `dining-room` performance gate

**Files:** none. This task is a manual measurement step. The result lands in the PR description (Task 5).

The Slice 1 performance gate (per the M2 spec §"Performance gates"): `dining-room` renders ≥ 2× faster than the M1 baseline (~210 s at 1280×720 / 64 spp on the operator's reference machine). Concrete target: **≤ 105 s** on the same machine, same build flags. The gate is **Slice 1's responsibility alone** — Slice 2 (parallel BVH build) doesn't affect render time.

The `dining-room` asset is gitignored — the operator must have it downloaded under `external/assets/pbrt/dining-room/` per `scenes/pbrt/dining_room/README.md`.

- [ ] **Step 1: Confirm asset is present**

```bash
ls external/assets/pbrt/dining-room/
```

Expected: Bitterli's PBRT v4 dining-room directory tree (`.pbrt`, `.ply`, `.exr` files). If absent, download per `scenes/pbrt/dining_room/README.md` and re-run.

- [ ] **Step 2: Build Release**

```bash
cmake -S . -B build
cmake --build build --config Release
ls build/Release/yaoray.exe
```

The binary path is `build/Release/yaoray.exe` on Windows / `build/yaoray` on Unix-like systems — adjust the rest of this task accordingly.

- [ ] **Step 3: Render the M1 baseline (median-split)**

Two options for capturing the baseline:

1. **Trust the spec value** (~210 s). Skip this step; record "M1 baseline: ~210 s (per M2 spec)" in the PR table.
2. **Re-measure on the worktree branch**. Temporarily change `split_method` default back to `LongestAxisMedian` in `include/yaoray/render/bvh.hpp`, rebuild Release, run:

   ```bash
   ./build/Release/yaoray.exe \
       --scene external/assets/pbrt/dining-room/scene.pbrt \
       --output /tmp/dining-room-median.png \
       --width 1280 --height 720 --spp 64
   ```

   Record the wall-clock render time printed at the end. **Revert** the default flip after measuring — do not let the temporary edit slip into a commit. (Use `git checkout -- include/yaoray/render/bvh.hpp` to revert.)

Option 1 is faster; option 2 gives a same-machine same-build comparison and is the rigorous choice. Pick based on how much the reference machine has changed since the M1 baseline was captured.

- [ ] **Step 4: Render the SAH build**

With `split_method` default at `SahBucketBinning` (Task 3 committed state), run:

```bash
./build/Release/yaoray.exe \
    --scene external/assets/pbrt/dining-room/scene.pbrt \
    --output /tmp/dining-room-sah.png \
    --width 1280 --height 720 --spp 64
```

Record the wall-clock render time. Target: **≤ 105 s**.

- [ ] **Step 5: Visual-compare the two renders**

If you ran option 2 in Step 3 (re-measured baseline), open both PNGs side-by-side:

- Composition must match (same scene layout, lighting, materials).
- Per-pixel differences are expected at 64 spp (different BVH traversal order changes RNG sequence in MIS sampling) but the noise envelope should be visually indistinguishable.

If the SAH render looks structurally wrong (missing geometry, wrong shading, NaN/Inf pixels), do **not** proceed. There's a SAH bug — debug in Task 2's territory before continuing.

- [ ] **Step 6: Decide gate status**

Compute the ratio: `M1 baseline time / SAH time`.

- **Ratio ≥ 2.0**: gate PASS. Proceed to Task 5.
- **Ratio between 1.0 and 2.0**: gate FAIL. The Slice 1 spec is explicit: "Slice 1 alone must achieve ≥ 2× speedup". Options before merging:
  1. Profile the SAH render. If BVH traversal still dominates, the SAH algorithm needs more work (e.g., bigger `max_leaf_triangles`, or a different `c_T`).
  2. Try `c_T = 0.125` (PBRT v3 default) and re-measure.
  3. Try `max_leaf_triangles = 8` and re-measure.
  4. If still under 2× after tuning: **open a follow-up Slice 1b** (e.g., ray packets, SIMD) BEFORE declaring Slice 1 complete. Do not merge with a gate failure.
- **Ratio < 1.0**: regression. SAH made things worse. Investigate immediately — likely a bug in `BuildRecursiveSah` producing a much deeper-than-necessary tree.

If you tuned `c_T` or `max_leaf_triangles` from the spec defaults (0.5 and 4), document the new value and the rationale in the PR description.

- [ ] **Step 7: Clean up local artifacts**

```bash
rm -f /tmp/dining-room-median.png /tmp/dining-room-sah.png
git status   # should show only the planned commits (Tasks 1, 2, 3)
```

If `git status` shows changes that didn't come from this plan (e.g., an accidentally committed `bvh.hpp` flip from Step 3 option 2), `git checkout --` them.

---

## Task 5: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline 7046d3c..HEAD
```

Expected three commits, in order:

1. `feat(bvh): add SahBucketBinning enum value + dispatch stub`
2. `feat(bvh): binned SAH builder with cost-based leaf criterion`
3. `feat(bvh): default BvhSplitMethod to SahBucketBinning`

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin m2-slice1-sah-bvh
gh pr create --title "feat(bvh): M2 Slice 1 — SAH BVH (>=2x dining-room speedup)" --body "$(cat <<'EOF'
## Summary

- Add `BvhSplitMethod::SahBucketBinning` and make it the default on `BvhBuildOptions`.
- Implement binned SAH per the M2 design (12 buckets per axis, c_T = 0.5, leaf criterion `SAH cost >= count` OR `count <= max_leaf_triangles`).
- Keep `LongestAxisMedian` available as a fallback / debugging value.
- 173 existing unit tests + 5 new SAH tests = 178/178 PASS.
- 8/8 CTest scenes PASS.

## Performance gate (Slice 1)

| Scene | Median split (M1 baseline) | SAH (this PR) | Speedup |
|---|---|---|---|
| `dining-room` (1280x720, 64 spp) | <REPLACE_WITH_BASELINE>s | <REPLACE_WITH_SAH>s | <REPLACE_WITH_RATIO>x |

Reference machine: <REPLACE_WITH_MACHINE>. Gate: >= 2x. <REPLACE_WITH_PASS_OR_FAIL>.

<REPLACE_WITH_TUNING_NOTES_IF_ANY>

## Test plan

- [x] `yaoray_tests.exe` — 178/178 PASS
- [x] `ctest --output-on-failure -C Release` — 8/8 PASS
- [x] `dining-room` SAH render visually matches the M1 baseline (no structural diff)
- [x] `dining-room` perf gate measured per the table above

## Out of scope (deferred to subsequent slices)

- Parallel BVH construction (Slice 2)
- Barcelona Pavilion scene integration (Slice 3)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Replace every `<REPLACE_WITH_*>` placeholder with the actual Task 4 measurements before submitting.

- [ ] **Step 3: Address review feedback**

If review surfaces issues, fix on the worktree branch with new commits (no force-push, no amend). Re-run `yaoray_tests.exe` + `ctest` after each fix and update the PR body if perf numbers shifted.

- [ ] **Step 4: Merge**

When the PR is approved and the operator confirms, merge via the GitHub UI. Then locally:

```bash
git checkout main
git pull origin main
git worktree remove <worktree-path>
git branch -D m2-slice1-sah-bvh
```

The worktree branch is gone; Slice 1 is in `main`. Slice 2 (parallel BVH construction) can be planned next.

---

## Self-Review Notes

- **Spec coverage:** every Slice 1 deliverable from §"Slice 1 — SAH BVH" of the M2 spec maps to a task here: enum addition (Task 1), algorithm + leaf criterion + new tests (Task 2), default flip (Task 3), perf gate measurement (Task 4).
- **No placeholders:** all code blocks in Tasks 1-3 are complete and self-contained. Task 4's PR body has explicit `<REPLACE_WITH_*>` markers that must be filled with measurements — these are intentional, not placeholders.
- **TDD ordering:** Task 1's dispatch stub makes Task 2's new SAH tests fail at first run. Task 3 flips the default only after the SAH path is fully covered by tests.
- **Type / name consistency:** `BvhSplitMethod`, `BvhBuildOptions`, `BvhPrimRef`, `RenderBvhNode`, `UnionBounds`, `Extent`, `AxisValue`, `IsFinite`, `LongestAxis`, `ParallelEpsilon`, `Union(Bounds3f, Point3f)` all referenced as they exist in the current `bvh.hpp` / `bvh.cpp` / `core/bounds.hpp` surface. New names: `SahBucket`, `SurfaceArea`, `CentroidToBucket`, `ChooseBestSahSplit`, `BuildRecursiveSah`, `kSahBucketCount`, `kSahTraversalCost`. No clashes with existing identifiers.
- **Cluster-test assertion:** `sah.bvh.nodes.size() < median.bvh.nodes.size()` is the meaningful SAH-vs-median signal. Expected concrete values: SAH = 5 nodes (root + leaf{4} + internal + leaf{4} + leaf{4}), median = 7 nodes (root + 2 internal + 4 leaves). If Task 4 tunes `c_T`, re-verify the assertion still holds against the 12-triangle cluster fixture.
- **Worktree branch name:** `m2-slice1-sah-bvh` everywhere (Setup, Task 5 Step 2 push, Task 5 Step 4 cleanup).
