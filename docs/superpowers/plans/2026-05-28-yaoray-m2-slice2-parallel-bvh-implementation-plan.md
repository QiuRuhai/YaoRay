# YaoRay M2 Slice 2 — Parallel BVH Construction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the BVH on multiple threads with a bitwise-deterministic output that matches the serial builder, refactor the median and SAH builders to share a generic recursive framework, and document `dining-room` BVH build-time speedup in the PR description. Render output and Slice 1's `dining-room` render time stay unchanged.

**Architecture:** Inside `src/render/bvh.cpp`, refactor the two existing recursive builders (`BuildRecursive` for median, `BuildRecursiveSah` for SAH) into a single generic `BuildSubtreeSerial<ChooserFn>` template parameterized by a `SplitDecision`-returning chooser function (`ChooseMedianSplit`, `ChooseSahSplit`). Add a parallel sibling `BuildSubtreeParallel<ChooserFn>` that, above a configurable subtree-size threshold and with remaining fork budget, builds left and right children on separate `std::thread`s into thread-local `RenderBvh` buffers and merges them into the parent BVH in deterministic left-first order (rewriting child indices and `first_triangle` offsets during the merge). `BuildBvh` selects between serial and parallel based on `BvhBuildOptions::thread_count` (default 0 = `std::thread::hardware_concurrency()`).

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. Only new standard-library headers (`<thread>`, `<atomic>`). No third-party dependencies.

---

## Spec Coverage

This plan implements **Slice 2** from `docs/superpowers/specs/2026-05-28-yaoray-m2-barcelona-pavilion-design.md`:

1. **Parallel BVH construction** — top-down `std::thread` fork; thread-local subtree buffers; deterministic merge in left-first order.
2. **Determinism (must)** — parallel output's `RenderBvh.nodes`, `triangle_indices`, `max_depth`, `total_triangles` are bitwise identical to the serial output for the same input.
3. **Worker cap** — fork budget capped at `std::thread::hardware_concurrency()` (or user-supplied `thread_count`). No exponential thread spawning.
4. **Subtree threshold** — recursive parallel fork stops when subtree size drops below `parallel_min_subtree_size` (default 1024 per spec).
5. **Render output unchanged** — all 178 unit tests + 8 CTest entries still pass; Slice 1's `dining-room` render time stays within run-to-run variance.
6. **Soft perf goal** — `dining-room` BVH build time speedup documented in PR description.

This plan also incorporates the Slice 1 final reviewer's recommended cleanup:

7. **Extract shared framework** — eliminate the ~50 lines of duplication between `BuildRecursive` and `BuildRecursiveSah` by routing both through `BuildSubtreeSerial`.

**Out of scope (per spec or user direction):**

- Pavilion scene integration (Slice 3).
- SAH algorithm tuning (`c_T`, `max_leaf_triangles`).
- Removing `LongestAxisMedian` enum value (spec keeps it as a debug fallback).
- Documentation refresh of `docs/architecture/overview.md` (spec defers to Slice 3).
- Test for the partition-collapse FP-edge fallback (Slice 1 reviewer noted this as future hardening; not blocking).

---

## File Structure

**Modified files:**

| Path | Change |
|------|--------|
| `include/yaoray/render/bvh.hpp` | Add `BvhBuildOptions::thread_count` and `BvhBuildOptions::parallel_min_subtree_size` with default values. |
| `src/render/bvh.cpp` | Replace `BuildRecursive` + `BuildRecursiveSah` with shared helpers (`EmitLeafNode`, `ComputeRangeBounds`, `SplitDecision`, `ChooseMedianSplit`, `ChooseSahSplit`) + a generic `BuildSubtreeSerial<ChooserFn>`. Add `MergeSubtree` and `BuildSubtreeParallel<ChooserFn>`. Update `BuildBvh` dispatch to select serial vs parallel. |
| `tests/bvh_tests.cpp` | Add a `MakeGridTriangleScene(int side)` helper. Add `bvh_parallel_matches_serial_byte_for_byte` test (lowered threshold; uses cluster scene). Add `bvh_parallel_handles_below_threshold_serially` test. |

**New files:** None.

---

## Setting up the worktree

Create an isolated worktree off local `main` (HEAD `b7251d9`, post-PR-#5). Use the harness-native `EnterWorktree` tool with name `m2-slice2-parallel-bvh` (or have a controller run `superpowers:using-git-worktrees`).

Verify the baseline before any change:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe        # expect 178/178 PASS
cd build && ctest --output-on-failure -C Release   # expect 8/8 PASS
cd ..
```

If counts don't match, stop and investigate — this plan assumes the post-PR-#5 baseline (178 unit tests, 8 CTest entries) is intact. All commits in this plan land on the worktree branch.

---

## Task 1: Refactor — extract shared framework

**Files:**

- Modify: `src/render/bvh.cpp` — extract helpers, introduce `SplitDecision`, introduce `BuildSubtreeSerial<ChooserFn>`, rewire `BuildBvh` to use the new path. Remove the old `BuildRecursive` and `BuildRecursiveSah`.

This task contains no behavior change. The BVH output for any input scene must be bitwise identical to pre-refactor (the existing CTest pixel-diffs and the 5 SAH-specific Slice 1 tests verify this). The refactor's purpose is to:

1. Eliminate the ~50 lines of duplication that the Slice 1 final reviewer flagged.
2. Establish the single recursion point that Task 3's parallel infrastructure can wrap.

The new shape:

- `SplitDecision { kind: Leaf|Split, mid: int }` — what to do with a range.
- `EmitLeafNode(prims, begin, end, node_bounds, depth, node_index, bvh)` — writes a leaf at a pre-allocated node slot.
- `ComputeRangeBounds(prims, begin, end, &node_bounds, &centroid_bounds, errors) -> bool` — computes both bboxes; returns false on non-finite input.
- `ChooseMedianSplit(prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles) -> SplitDecision` — median chooser; partitions in place via `std::nth_element`.
- `ChooseSahSplit(prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles) -> SplitDecision` — SAH chooser; partitions in place via `std::partition` with the bucket predicate, falls back to median on collapse.
- `BuildSubtreeSerial<ChooserFn>(prims, begin, end, depth, max_leaf, chooser, bvh, errors) -> int` — generic recursive builder.

- [ ] **Step 1: Add `SplitDecision`, `EmitLeafNode`, `ComputeRangeBounds`**

Open `src/render/bvh.cpp`. Inside the existing anonymous namespace (after `LongestAxis` at around line 80, before `BuildRecursive` at line 82), add:

```cpp
struct SplitDecision {
    enum class Kind { MakeLeaf, Split };
    Kind kind = Kind::MakeLeaf;
    int mid = -1;  // partition point; only valid when kind == Split
};

void EmitLeafNode(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    int depth,
    int node_index,
    RenderBvh& bvh
) {
    const int first_triangle = static_cast<int>(bvh.triangle_indices.size());
    for (int i = begin; i < end; ++i) {
        bvh.triangle_indices.push_back(prims[static_cast<std::size_t>(i)].flat_index);
    }
    bvh.nodes[static_cast<std::size_t>(node_index)] = RenderBvhNode{
        node_bounds,
        -1,
        -1,
        first_triangle,
        end - begin
    };
    bvh.max_depth = std::max(bvh.max_depth, depth);
}

bool ComputeRangeBounds(
    const std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    Bounds3f* out_node_bounds,
    Bounds3f* out_centroid_bounds,
    std::vector<std::string>& errors
) {
    Bounds3f node_bounds;
    Bounds3f centroid_bounds;
    for (int i = begin; i < end; ++i) {
        node_bounds = UnionBounds(node_bounds, prims[static_cast<std::size_t>(i)].bounds);
        centroid_bounds = Union(centroid_bounds, prims[static_cast<std::size_t>(i)].centroid);
    }
    if (!IsFinite(node_bounds) || !IsFinite(centroid_bounds)) {
        errors.push_back("BVH build encountered non-finite primitive bounds");
        return false;
    }
    *out_node_bounds = node_bounds;
    *out_centroid_bounds = centroid_bounds;
    return true;
}
```

- [ ] **Step 2: Replace `BuildRecursive` with `ChooseMedianSplit`**

Delete the entire `BuildRecursive` function (lines 82–159 in the original). Add in its place:

```cpp
SplitDecision ChooseMedianSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& /*node_bounds*/,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
) {
    const int primitive_count = end - begin;
    if (primitive_count <= max_leaf_triangles) {
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
    }

    const int axis = LongestAxis(centroid_bounds);
    const int mid = begin + primitive_count / 2;
    std::nth_element(
        prims.begin() + begin,
        prims.begin() + mid,
        prims.begin() + end,
        [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
            return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
        }
    );
    return SplitDecision{SplitDecision::Kind::Split, mid};
}
```

- [ ] **Step 3: Replace `BuildRecursiveSah` with `ChooseSahSplit`**

Delete the entire `BuildRecursiveSah` function (lines 275–405 in the original). Add in its place (right after `ChooseBestSahSplit`, which stays unchanged):

```cpp
SplitDecision ChooseSahSplit(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    const Bounds3f& node_bounds,
    const Bounds3f& centroid_bounds,
    int max_leaf_triangles
) {
    const int primitive_count = end - begin;

    // Leaf criterion (a): small enough -> leaf regardless of SAH cost.
    if (primitive_count <= max_leaf_triangles) {
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
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
        return SplitDecision{SplitDecision::Kind::MakeLeaf, -1};
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
            // Defensive: float-rounding collapse. Fall back to median on best_axis.
            mid = begin + primitive_count / 2;
            std::nth_element(
                prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
                [best_axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                    return AxisValue(a.centroid, best_axis) < AxisValue(b.centroid, best_axis);
                }
            );
        }
    } else {
        // No valid SAH split anywhere -> degenerate centroid bounds. Force an
        // even partition along the longest centroid axis.
        const int axis = LongestAxis(centroid_bounds);
        mid = begin + primitive_count / 2;
        std::nth_element(
            prims.begin() + begin, prims.begin() + mid, prims.begin() + end,
            [axis](const BvhPrimRef& a, const BvhPrimRef& b) {
                return AxisValue(a.centroid, axis) < AxisValue(b.centroid, axis);
            }
        );
    }

    return SplitDecision{SplitDecision::Kind::Split, mid};
}
```

- [ ] **Step 4: Add the generic `BuildSubtreeSerial` template**

Right after `ChooseSahSplit`, before the closing `} // namespace`, add:

```cpp
template <typename ChooserFn>
int BuildSubtreeSerial(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    ChooserFn chooser,
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
    if (!ComputeRangeBounds(prims, begin, end, &node_bounds, &centroid_bounds, errors)) {
        return -1;
    }

    const SplitDecision decision =
        chooser(prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles);

    if (decision.kind == SplitDecision::Kind::MakeLeaf) {
        EmitLeafNode(prims, begin, end, node_bounds, depth, node_index, bvh);
        return node_index;
    }

    const int mid = decision.mid;
    if (mid <= begin || mid >= end) {
        errors.push_back("BVH split produced an empty child range");
        return -1;
    }

    const int left_child = BuildSubtreeSerial(
        prims, begin, mid, depth + 1, max_leaf_triangles, chooser, bvh, errors);
    const int right_child = BuildSubtreeSerial(
        prims, mid, end, depth + 1, max_leaf_triangles, chooser, bvh, errors);
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
```

- [ ] **Step 5: Rewire `BuildBvh` to use `BuildSubtreeSerial` + the choosers**

In `BuildBvh`, replace the existing `switch (options.split_method) { ... }` block (lines 465–489 in the original) with:

```cpp
    int root = -1;
    switch (options.split_method) {
        case BvhSplitMethod::SahBucketBinning:
            root = BuildSubtreeSerial(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                ChooseSahSplit,
                result.bvh,
                result.errors
            );
            break;
        case BvhSplitMethod::LongestAxisMedian:
            root = BuildSubtreeSerial(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                ChooseMedianSplit,
                result.bvh,
                result.errors
            );
            break;
    }
```

- [ ] **Step 6: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **178/178 unit tests PASS** + **8/8 CTest entries PASS**.

If any test fails: the refactor changed behavior somewhere. Most likely culprits:
- Leaf-criterion ordering inside `ChooseSahSplit` — make sure the `count <= max_leaf_triangles` short-circuit fires BEFORE the SAH evaluation.
- `EmitLeafNode` writing wrong values — verify all five fields match the old `BuildRecursive`/`BuildRecursiveSah` leaf emission.
- `ComputeRangeBounds` semantics — the old code computed bounds inside each builder; new code computes once in `BuildSubtreeSerial`. Output should be identical.
- The `mid <= begin || mid >= end` guard moved from inside each builder to `BuildSubtreeSerial`. Verify the error message stays the same string-wise if any test asserts on it (none of the existing tests do, but be aware).

Do not loosen any test. Fix the refactor to match the pre-refactor behavior exactly.

- [ ] **Step 7: Commit**

```bash
git add src/render/bvh.cpp
git commit -m "$(cat <<'EOF'
refactor(bvh): extract shared framework for median and SAH builders

Replaces BuildRecursive and BuildRecursiveSah with:
- SplitDecision struct (Leaf | Split with partition point)
- EmitLeafNode and ComputeRangeBounds helpers
- ChooseMedianSplit and ChooseSahSplit chooser functions
- BuildSubtreeSerial<ChooserFn> generic recursive builder

The two builders are now skinny pure-function choosers; the recursion
framework lives in one place. Behavior is unchanged: the same BVH
is produced for any input, and all 178 unit tests + 8 CTest entries
pass.

This sets up the parallel construction path in the next commits — the
parallel builder will wrap this same chooser interface.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Add API knobs (`thread_count`, `parallel_min_subtree_size`)

**Files:**

- Modify: `include/yaoray/render/bvh.hpp` — add two fields to `BvhBuildOptions` with default values.
- Modify: `src/render/bvh.cpp` — add validation in `BuildBvh`. The new fields are otherwise inert in this task.

The defaults match the M2 spec's recommendations:

- `thread_count = 0` → at build time, treated as `std::thread::hardware_concurrency()` (auto-detect).
- `parallel_min_subtree_size = 1024` → subtrees below this size build serially.

- [ ] **Step 1: Add the new fields to `BvhBuildOptions`**

In `include/yaoray/render/bvh.hpp`, change:

```cpp
struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::SahBucketBinning;
    int max_leaf_triangles = 4;
};
```

to:

```cpp
struct BvhBuildOptions {
    BvhSplitMethod split_method = BvhSplitMethod::SahBucketBinning;
    int max_leaf_triangles = 4;
    // Number of worker threads for parallel BVH construction.
    // 0 = auto (std::thread::hardware_concurrency()).
    // 1 = serial (no threading).
    // >=2 = parallel with up to that many threads.
    int thread_count = 0;
    // Subtrees smaller than this many primitives build serially even when
    // parallel construction is active.
    int parallel_min_subtree_size = 1024;
};
```

- [ ] **Step 2: Add validation in `BuildBvh`**

In `src/render/bvh.cpp`, find the existing validation block at the top of `BuildBvh`:

```cpp
    if (options.max_leaf_triangles < 1) {
        result.errors.push_back("BVH max_leaf_triangles must be at least 1");
        return result;
    }
```

Insert immediately after:

```cpp
    if (options.thread_count < 0) {
        result.errors.push_back("BVH thread_count must be >= 0");
        return result;
    }
    if (options.parallel_min_subtree_size < 1) {
        result.errors.push_back("BVH parallel_min_subtree_size must be >= 1");
        return result;
    }
```

- [ ] **Step 3: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **178/178 unit tests PASS** + **8/8 CTest entries PASS**. (The new fields exist but no code uses them yet — they're inert.)

- [ ] **Step 4: Commit**

```bash
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp
git commit -m "$(cat <<'EOF'
feat(bvh): add thread_count + parallel_min_subtree_size options

Adds BvhBuildOptions::thread_count (default 0 = auto) and
BvhBuildOptions::parallel_min_subtree_size (default 1024) per the
M2 design spec. Both fields are validated in BuildBvh (must be
non-negative / positive respectively).

The fields are inert at this commit. The next commit wires them
to the parallel construction path.

All 178 unit tests + 8 CTest entries pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Parallel infrastructure + determinism test

**Files:**

- Modify: `src/render/bvh.cpp` — add `MergeSubtree` and `BuildSubtreeParallel<ChooserFn>` helpers; rewire `BuildBvh` to dispatch on `thread_count`.
- Modify: `tests/bvh_tests.cpp` — add the determinism test and a serial-vs-parallel byte-identity test.

The parallel builder:

1. At each level, if `primitive_count > parallel_min_subtree_size` and `fork_budget > 1`: spawn two threads, each building its assigned subtree into a thread-local `RenderBvh`. After both join, merge into the parent's BVH in left-first order via `MergeSubtree`, which rewrites all child indices by `+= base_node_offset` and all `first_triangle` offsets by `+= base_tri_offset`.
2. Otherwise: fall back to `BuildSubtreeSerial`.

Fork-budget management: at every fork, left gets `(fork_budget + 1) / 2` and right gets `fork_budget - left_budget`. This keeps total thread count bounded by the initial budget. Starting budget = `thread_count` (or `hardware_concurrency()` if `thread_count == 0`).

Determinism argument: with the same input `prims`, both children's `chooser` calls produce identical split decisions and identical partitions (the partition operates only on the child's disjoint range; `std::partition` and `std::nth_element` are deterministic). Left-first merging then produces the same DFS node order as the serial builder. Output is bitwise identical.

- [ ] **Step 1: Add `<thread>` include**

In `src/render/bvh.cpp`, add to the standard-library include block (after `<string>` and before `<vector>`):

```cpp
#include <thread>
```

- [ ] **Step 2: Add `MergeSubtree`**

In `src/render/bvh.cpp`, inside the anonymous namespace, after `BuildSubtreeSerial` and before the closing `}`, add:

```cpp
// Merges a thread-local subtree's BVH into the parent BVH at the current
// position. Rewrites child indices (+= base_node_offset) and first_triangle
// offsets (+= base_tri_offset). Returns the merged subtree's root index in
// the parent BVH (= base_node_offset).
int MergeSubtree(const RenderBvh& subtree, RenderBvh& bvh) {
    const int base_node_offset = static_cast<int>(bvh.nodes.size());
    const int base_tri_offset = static_cast<int>(bvh.triangle_indices.size());

    bvh.nodes.reserve(bvh.nodes.size() + subtree.nodes.size());
    for (const RenderBvhNode& node : subtree.nodes) {
        RenderBvhNode adjusted = node;
        if (adjusted.triangle_count > 0) {
            adjusted.first_triangle += base_tri_offset;
        } else {
            if (adjusted.left_child >= 0) {
                adjusted.left_child += base_node_offset;
            }
            if (adjusted.right_child >= 0) {
                adjusted.right_child += base_node_offset;
            }
        }
        bvh.nodes.push_back(adjusted);
    }

    bvh.triangle_indices.reserve(bvh.triangle_indices.size() + subtree.triangle_indices.size());
    for (int idx : subtree.triangle_indices) {
        bvh.triangle_indices.push_back(idx);
    }

    bvh.max_depth = std::max(bvh.max_depth, subtree.max_depth);
    return base_node_offset;
}
```

- [ ] **Step 3: Add `BuildSubtreeParallel`**

Right after `MergeSubtree`, add:

```cpp
template <typename ChooserFn>
int BuildSubtreeParallel(
    std::vector<BvhPrimRef>& prims,
    int begin,
    int end,
    int depth,
    int max_leaf_triangles,
    int parallel_min_subtree_size,
    int fork_budget,
    ChooserFn chooser,
    RenderBvh& bvh,
    std::vector<std::string>& errors
) {
    const int primitive_count = end - begin;

    // Below threshold or out of budget -> fall back to the serial builder.
    if (primitive_count <= parallel_min_subtree_size || fork_budget <= 1) {
        return BuildSubtreeSerial(
            prims, begin, end, depth, max_leaf_triangles, chooser, bvh, errors);
    }

    if (begin >= end) {
        errors.push_back("BVH build produced an empty primitive range");
        return -1;
    }

    const int node_index = static_cast<int>(bvh.nodes.size());
    bvh.nodes.push_back(RenderBvhNode{});

    Bounds3f node_bounds;
    Bounds3f centroid_bounds;
    if (!ComputeRangeBounds(prims, begin, end, &node_bounds, &centroid_bounds, errors)) {
        return -1;
    }

    const SplitDecision decision =
        chooser(prims, begin, end, node_bounds, centroid_bounds, max_leaf_triangles);

    // The chooser may still decide to make a leaf even at large sizes
    // (e.g., SAH cost >= leaf cost). Honor that.
    if (decision.kind == SplitDecision::Kind::MakeLeaf) {
        EmitLeafNode(prims, begin, end, node_bounds, depth, node_index, bvh);
        return node_index;
    }

    const int mid = decision.mid;
    if (mid <= begin || mid >= end) {
        errors.push_back("BVH split produced an empty child range");
        return -1;
    }

    // Spawn one thread per child. Each builds into its own thread-local BVH.
    // The prims vector slot is shared but the children touch disjoint ranges
    // [begin, mid) and [mid, end), so no synchronization is needed.
    RenderBvh left_subtree;
    RenderBvh right_subtree;
    std::vector<std::string> left_errors;
    std::vector<std::string> right_errors;

    const int left_budget = (fork_budget + 1) / 2;
    const int right_budget = fork_budget - left_budget;

    std::thread left_thread([&] {
        BuildSubtreeParallel(
            prims, begin, mid, depth + 1, max_leaf_triangles,
            parallel_min_subtree_size, left_budget,
            chooser, left_subtree, left_errors);
    });
    std::thread right_thread([&] {
        BuildSubtreeParallel(
            prims, mid, end, depth + 1, max_leaf_triangles,
            parallel_min_subtree_size, right_budget,
            chooser, right_subtree, right_errors);
    });

    left_thread.join();
    right_thread.join();

    // Aggregate errors in left-first order (matches serial DFS error order).
    errors.insert(errors.end(), left_errors.begin(), left_errors.end());
    errors.insert(errors.end(), right_errors.begin(), right_errors.end());
    if (!left_errors.empty() || !right_errors.empty()) {
        return -1;
    }
    if (left_subtree.nodes.empty() || right_subtree.nodes.empty()) {
        errors.push_back("BVH parallel subtree returned empty");
        return -1;
    }

    // Merge left first, then right -- deterministic DFS order.
    const int left_child = MergeSubtree(left_subtree, bvh);
    const int right_child = MergeSubtree(right_subtree, bvh);

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
```

- [ ] **Step 4: Wire `BuildBvh` to dispatch on `thread_count`**

In `src/render/bvh.cpp`, find the `switch (options.split_method)` block in `BuildBvh` (the one you rewrote in Task 1 Step 5). Replace it with a dispatch that also accounts for the thread count:

```cpp
    // Resolve effective thread count: 0 means auto-detect.
    int effective_thread_count = options.thread_count;
    if (effective_thread_count == 0) {
        effective_thread_count = static_cast<int>(std::thread::hardware_concurrency());
        if (effective_thread_count <= 0) {
            effective_thread_count = 1;  // defensive: hardware_concurrency may return 0
        }
    }

    auto run_builder = [&](auto chooser) -> int {
        if (effective_thread_count <= 1) {
            return BuildSubtreeSerial(
                prims,
                0,
                static_cast<int>(prims.size()),
                1,
                options.max_leaf_triangles,
                chooser,
                result.bvh,
                result.errors
            );
        }
        return BuildSubtreeParallel(
            prims,
            0,
            static_cast<int>(prims.size()),
            1,
            options.max_leaf_triangles,
            options.parallel_min_subtree_size,
            effective_thread_count,
            chooser,
            result.bvh,
            result.errors
        );
    };

    int root = -1;
    switch (options.split_method) {
        case BvhSplitMethod::SahBucketBinning:
            root = run_builder(ChooseSahSplit);
            break;
        case BvhSplitMethod::LongestAxisMedian:
            root = run_builder(ChooseMedianSplit);
            break;
    }
```

- [ ] **Step 5: Add the determinism test**

In `tests/bvh_tests.cpp`, append the following two new tests at the bottom (after the last existing `YR_TEST` block):

```cpp
YR_TEST(bvh_parallel_matches_serial_byte_for_byte) {
    // Same scene, two builds: one forced serial (thread_count=1), one
    // forced parallel with a lowered subtree-threshold so the 12-triangle
    // cluster scene actually engages the parallel code path. The two
    // builds must produce bitwise-identical RenderBvh output.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();

    yr::BvhBuildOptions serial_opts;
    serial_opts.split_method = yr::BvhSplitMethod::SahBucketBinning;
    serial_opts.thread_count = 1;
    serial_opts.parallel_min_subtree_size = 4;
    const yr::BvhBuildResult serial = yr::BuildBvh(scene, serial_opts);

    yr::BvhBuildOptions parallel_opts;
    parallel_opts.split_method = yr::BvhSplitMethod::SahBucketBinning;
    parallel_opts.thread_count = 4;
    parallel_opts.parallel_min_subtree_size = 4;
    const yr::BvhBuildResult parallel = yr::BuildBvh(scene, parallel_opts);

    YR_EXPECT_TRUE(serial.errors.empty());
    YR_EXPECT_TRUE(parallel.errors.empty());
    YR_EXPECT_EQ(serial.bvh.nodes.size(), parallel.bvh.nodes.size());
    YR_EXPECT_EQ(serial.bvh.triangle_indices.size(), parallel.bvh.triangle_indices.size());
    YR_EXPECT_EQ(serial.bvh.max_depth, parallel.bvh.max_depth);
    YR_EXPECT_EQ(serial.bvh.total_triangles, parallel.bvh.total_triangles);

    for (std::size_t i = 0; i < serial.bvh.nodes.size(); ++i) {
        const yr::RenderBvhNode& s = serial.bvh.nodes[i];
        const yr::RenderBvhNode& p = parallel.bvh.nodes[i];
        YR_EXPECT_EQ(s.bounds.min.x, p.bounds.min.x);
        YR_EXPECT_EQ(s.bounds.min.y, p.bounds.min.y);
        YR_EXPECT_EQ(s.bounds.min.z, p.bounds.min.z);
        YR_EXPECT_EQ(s.bounds.max.x, p.bounds.max.x);
        YR_EXPECT_EQ(s.bounds.max.y, p.bounds.max.y);
        YR_EXPECT_EQ(s.bounds.max.z, p.bounds.max.z);
        YR_EXPECT_EQ(s.left_child, p.left_child);
        YR_EXPECT_EQ(s.right_child, p.right_child);
        YR_EXPECT_EQ(s.first_triangle, p.first_triangle);
        YR_EXPECT_EQ(s.triangle_count, p.triangle_count);
    }
    for (std::size_t i = 0; i < serial.bvh.triangle_indices.size(); ++i) {
        YR_EXPECT_EQ(serial.bvh.triangle_indices[i], parallel.bvh.triangle_indices[i]);
    }
}

YR_TEST(bvh_parallel_handles_below_threshold_serially) {
    // With parallel_min_subtree_size larger than the scene, the parallel
    // builder must fall through to the serial path on the first call and
    // still produce a valid BVH. This verifies the fast-path early-out
    // doesn't break small-scene behavior.
    yr::RenderSceneIR scene = MakeClusteredTriangleScene();  // 12 triangles

    yr::BvhBuildOptions options;
    options.split_method = yr::BvhSplitMethod::SahBucketBinning;
    options.thread_count = 8;
    options.parallel_min_subtree_size = 1024;  // > 12, so no parallel
    const yr::BvhBuildResult result = yr::BuildBvh(scene, options);

    YR_EXPECT_TRUE(result.errors.empty());
    YR_EXPECT_EQ(result.bvh.total_triangles, 12);
    YR_EXPECT_TRUE(!result.bvh.nodes.empty());
}
```

- [ ] **Step 6: Build and run the full test suite**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
cd build && ctest --output-on-failure -C Release
cd ..
```

Expected: **180/180 unit tests PASS** (178 prior + 2 new) + **8/8 CTest entries PASS**.

If `bvh_parallel_matches_serial_byte_for_byte` fails:
- Check `MergeSubtree`'s index rewriting — `first_triangle` for leaves, `left_child`/`right_child` for interior nodes.
- Confirm the parallel builder spawns threads in left-first order and merges in left-first order. Any swap breaks determinism.
- Confirm both threads see the same `chooser` function pointer (passed by value through the lambda capture).
- Confirm the prims vector is not reallocated during the build (no `push_back` during recursion — only at the top-level scene-to-prims conversion).
- Pre-emit of the root interior node placeholder happens BEFORE forking; both threads start fresh and don't write to that slot.

If `bvh_parallel_handles_below_threshold_serially` fails:
- The `primitive_count <= parallel_min_subtree_size` early-out in `BuildSubtreeParallel` may have an off-by-one. Verify the condition matches the spec: "fewer than 1024 primitives" → use `<= parallel_min_subtree_size` for the serial fall-through.

If existing tests fail:
- Most likely the dispatch in `BuildBvh` regressed serial behavior. Run with `thread_count = 1` explicitly to confirm serial path still works as before Task 1's refactor.

- [ ] **Step 7: Commit**

```bash
git add src/render/bvh.cpp tests/bvh_tests.cpp
git commit -m "$(cat <<'EOF'
feat(bvh): parallel construction with deterministic merge

Implements the top-down parallel BVH builder per the M2 design spec:
- BuildSubtreeParallel spawns std::thread for left and right children
  when subtree size > parallel_min_subtree_size and fork_budget > 1
- Thread-local RenderBvh buffers; deterministic left-first merge via
  MergeSubtree rewrites child indices and first_triangle offsets
- Fork budget halves at each split; total threads capped near
  hardware concurrency without exponential spawning
- BuildBvh dispatches on options.thread_count (0 = auto)

Two new tests:
- bvh_parallel_matches_serial_byte_for_byte: same input, thread_count=1
  vs thread_count=4 (with low parallel_min_subtree_size to engage the
  parallel path on a 12-triangle scene); asserts byte-identical output
  across nodes, triangle_indices, max_depth, and total_triangles.
- bvh_parallel_handles_below_threshold_serially: thread_count=8 but
  threshold=1024 forces serial; verifies the fast-path early-out.

All 180 unit tests + 8 CTest entries pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Measure `dining-room` BVH build + render time

**Files:** none. Manual measurement; numbers land in the PR description (Task 5).

Render output and total render time should NOT regress versus Slice 1 (~103 s). BVH build time (the `Prepare seconds` line in yaoray's stdout) should drop noticeably with parallel build active.

The `dining-room` asset must be present at `external/assets/pbrt/dining-room/scene-v4.pbrt` (downloaded during Slice 1 Task 4; if missing, follow `scenes/pbrt/dining_room/README.md`).

- [ ] **Step 1: Confirm asset is present**

```bash
ls external/assets/pbrt/dining-room/scene-v4.pbrt
```

If missing, download per `scenes/pbrt/dining_room/README.md`.

- [ ] **Step 2: Build Release**

```bash
cmake -S . -B build
cmake --build build --config Release
ls build/Release/yaoray.exe
```

- [ ] **Step 3: Render with `thread_count = 1` (forced serial)**

The yaoray CLI takes the build options from a default-constructed `BvhBuildOptions`. To force serial, we need to temporarily override the default. The cleanest path: edit `src/backends/cpu/cpu_prepared_scene.cpp` line 29 (the `BuildBvh(scene)` call) to pass an explicit options object:

```cpp
BvhBuildOptions opts;
opts.thread_count = 1;  // TEMPORARY: serial baseline measurement
BvhBuildResult build = BuildBvh(scene, opts);
```

Rebuild and render:

```bash
cmake --build build --config Release
./build/Release/yaoray.exe render external/assets/pbrt/dining-room/scene-v4.pbrt --backend cpu 2>&1 | grep -E "Prepare seconds|Elapsed seconds"
```

Record the two numbers (`Prepare seconds` = BVH build time + small overhead; `Elapsed seconds` = render time).

Revert the temporary edit:

```bash
git checkout -- src/backends/cpu/cpu_prepared_scene.cpp
cmake --build build --config Release
```

- [ ] **Step 4: Render with default (parallel auto-detect)**

```bash
./build/Release/yaoray.exe render external/assets/pbrt/dining-room/scene-v4.pbrt --backend cpu 2>&1 | grep -E "Prepare seconds|Elapsed seconds"
```

Record both numbers again.

- [ ] **Step 5: Verify render time hasn't regressed**

Compare the two `Elapsed seconds` values. They should be within run-to-run variance (~1–3%) of each other AND of Slice 1's 103 s. Big deviation = unexpected regression — investigate before proceeding.

The "BVH nodes" and "BVH max depth" lines from both runs should also be identical (the BVH topology is deterministic).

- [ ] **Step 6: Compute speedup ratio**

```
BVH build speedup = serial Prepare seconds / parallel Prepare seconds
```

Target: ≥ 2×. Soft goal (not a merge gate). Document the actual ratio in the PR description (Task 5).

If the speedup is far less than expected (e.g., < 1.5×) on a machine with > 4 cores:
- Profile to see if threads actually spawn — add temporary `std::cout << "fork at depth " << depth` if needed.
- Check `parallel_min_subtree_size` — 1024 may be too high for `dining-room`'s ~270k triangles + the per-cluster distribution. Try lowering to 256 or 512 and re-measure. If a lower threshold helps significantly, document the change in the PR description; do not commit the lowered default unless the perf evidence is strong enough to justify breaking the M2 spec's recommended value.

---

## Task 5: PR + merge

- [ ] **Step 1: Verify the worktree state**

```bash
git log --oneline b7251d9..HEAD
```

Expected three commits:

1. `refactor(bvh): extract shared framework for median and SAH builders`
2. `feat(bvh): add thread_count + parallel_min_subtree_size options`
3. `feat(bvh): parallel construction with deterministic merge`

- [ ] **Step 2: Push and open the PR**

```bash
git push -u origin m2-slice2-parallel-bvh
gh pr create --title "feat(bvh): M2 Slice 2 — parallel construction (deterministic)" --body "$(cat <<'EOF'
## Summary

- Refactor: extract `BuildRecursive` and `BuildRecursiveSah` into shared `BuildSubtreeSerial<ChooserFn>` + `ChooseMedianSplit` / `ChooseSahSplit`. Cleans up the ~50 lines of duplication flagged by the Slice 1 final reviewer.
- Add `BvhBuildOptions::thread_count` (default 0 = `std::thread::hardware_concurrency()`) and `BvhBuildOptions::parallel_min_subtree_size` (default 1024).
- Implement `BuildSubtreeParallel<ChooserFn>` using `std::thread` directly: top-down fork into thread-local subtree buffers, deterministic left-first `MergeSubtree`, fork-budget capping to prevent exponential thread spawn.
- Two new tests:
  - `bvh_parallel_matches_serial_byte_for_byte` — same input, serial vs parallel builds produce bitwise-identical `RenderBvh.nodes`, `triangle_indices`, `max_depth`, `total_triangles`.
  - `bvh_parallel_handles_below_threshold_serially` — below-threshold scenes fall through to serial path cleanly.

## BVH determinism (must)

The parallel build's output is bitwise identical to the serial build's output for the same input. Verified by the new `bvh_parallel_matches_serial_byte_for_byte` test (180/180 unit tests pass) and by the 8 existing CTest pixel-compare scenes (8/8 pass — same BVH topology means same ray-trace results).

## Performance (soft goal)

Measured on the development sandbox (Windows, MSVC 19.51, Release build, hardware_concurrency = 11):

| Mode | `Prepare seconds` (BVH build) | `Elapsed seconds` (render) |
|---|---|---|
| `thread_count = 1` (serial) | <REPLACE_WITH_SERIAL_PREPARE>s | <REPLACE_WITH_SERIAL_RENDER>s |
| `thread_count = 0` (auto parallel) | <REPLACE_WITH_PARALLEL_PREPARE>s | <REPLACE_WITH_PARALLEL_RENDER>s |
| Speedup | **<REPLACE_WITH_PREPARE_RATIO>×** | ~1× (unchanged) |

Render time stays within run-to-run variance of Slice 1's 103 s baseline (parallel BVH does not affect ray traversal). BVH topology (nodes + max depth) is identical across serial and parallel runs.

## Test plan

- [x] `yaoray_tests.exe` — 180/180 PASS (178 pre-existing + 2 new parallel-determinism tests)
- [x] `ctest --output-on-failure -C Release` — 8/8 PASS
- [x] `dining-room` BVH build speedup measured per the table above
- [x] `dining-room` render output bit-stable (same BVH nodes, same max depth, render time within noise)

## Out of scope (deferred to subsequent slices)

- Barcelona Pavilion scene integration (Slice 3)
- SAH algorithm tuning (`c_T`, `max_leaf_triangles`)
- Documentation refresh of `docs/architecture/overview.md` (Slice 3)

## Commits

- `<REPLACE_WITH_SHA_1>` refactor(bvh): extract shared framework for median and SAH builders
- `<REPLACE_WITH_SHA_2>` feat(bvh): add thread_count + parallel_min_subtree_size options
- `<REPLACE_WITH_SHA_3>` feat(bvh): parallel construction with deterministic merge

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Replace every `<REPLACE_WITH_*>` placeholder with the actual Task 4 measurements and commit SHAs before submitting.

- [ ] **Step 3: Address review feedback**

If review surfaces issues, fix on the worktree branch with new commits (no force-push, no amend). Re-run `yaoray_tests.exe` + `ctest` after each fix.

- [ ] **Step 4: Merge**

When the PR is approved and the operator confirms, merge via the GitHub UI. Then locally:

```bash
git checkout main
git pull origin main
git worktree remove .worktrees/m2-slice2-parallel-bvh
git branch -D m2-slice2-parallel-bvh
```

Slice 2 is in `main`. Slice 3 (Pavilion integration) can be planned next.

---

## Self-Review Notes

- **Spec coverage:** every Slice 2 deliverable from §"Slice 2 — Parallel BVH construction" of the M2 spec is mapped to a task: parallel infrastructure (Task 3), determinism test (Task 3 Step 5), thread-count cap (Task 2 + Task 3 dispatch logic), 1024 threshold (Task 2 default), render-output equivalence (Task 4 Step 5).
- **TDD ordering:** Task 3's determinism test is a regression test, not a TDD failing-first test — with Task 2's inert options, the test passes trivially (both builds are serial). It only becomes meaningful AFTER Task 3 Step 3 wires the parallel path. The plan flow puts the test addition AFTER the parallel implementation, with the run in Step 6 confirming determinism. Acceptable deviation from strict TDD because the test's primary value is regression detection rather than driving design.
- **Refactor risk:** Task 1 is the highest-risk task (touches both builders without behavior change). The 178 existing tests + 8 CTest pixel-compares are the safety net. If any fails after Step 6, the refactor introduced a behavioral diff — fix the refactor, do not loosen the test.
- **Thread safety of `prims`:** the shared `prims` vector is sized once during the scene-to-prims conversion and never resized during recursion. Child threads access disjoint ranges `[begin, mid)` and `[mid, end)`. No synchronization is needed; no atomics. This is correct per C++ memory model for `std::vector` (concurrent writes to non-overlapping elements without resize are safe).
- **Determinism by construction:** parallel split decisions are identical to serial because the chooser is deterministic and operates on the same primitive subset. Left-first merge preserves DFS order. The plan does not rely on `std::thread`'s OS-level scheduling — only on `join()` semantics.
- **`parallel_min_subtree_size = 1024` default justification:** spec value; cuts thread overhead for small subtrees while still parallelizing the bulk of `dining-room`'s 269k-triangle work. Task 4 Step 6 has a contingency for adjusting if dining-room speedup falls short.
- **Type consistency:** `ChooserFn`, `SplitDecision`, `EmitLeafNode`, `ComputeRangeBounds`, `MergeSubtree`, `BuildSubtreeSerial`, `BuildSubtreeParallel`, `ChooseMedianSplit`, `ChooseSahSplit` are all used consistently across tasks. The `BvhBuildOptions` field names (`thread_count`, `parallel_min_subtree_size`) match between hpp and cpp.
- **Worktree branch name** `m2-slice2-parallel-bvh` used in Setup, Task 5 Step 2 push, and Task 5 Step 4 cleanup.
