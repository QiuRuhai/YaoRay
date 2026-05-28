# YaoRay M2: Barcelona Pavilion — Design

**Date:** 2026-05-28
**Status:** Approved for implementation planning
**Predecessor:** `2026-05-28-yaoray-post-m1-roadmap-design.md` (post-M1 roadmap)

## North Star

M2 is complete when YaoRay can render the
[`mmp/pbrt-v4-scenes`](https://github.com/mmp/pbrt-v4-scenes)
conversion of Mies van der Rohe's Barcelona Pavilion straight from
its unmodified `.pbrt` file, and the existing `dining-room` scene
renders **at least twice as fast** as it does today.

The Pavilion is a recognizable architectural reach goal: marble
floor, glass walls, a chrome cross-shaped column, a reflective water
pool, and an HDRI sky together exercise the dielectric / conductor /
coateddiffuse BSDFs and the environment importance sampler harder
than the dining-room does. Its triangle count (50–100 K) is enough
larger than dining-room (~270 K — actually heavier, but more
uniformly distributed) that the current median-split BVH becomes the
visible bottleneck.

The accompanying performance work is the first half of "making
YaoRay engineering-grade." A Surface Area Heuristic (SAH) BVH plus
parallel BVH construction replace the median-split builder, give
≥ 2× render speedup on the dining-room baseline, and lay foundations
the future CUDA backend (M4) can reuse.

## Why this scene

* **Available with a clean PBRT v4 conversion.** The official
  `mmp/pbrt-v4-scenes` repo is maintained by PBRT's authors;
  conversions there have higher fidelity than third-party Tungsten
  exports.
* **Mid-size reach goal.** Bigger than `dining-room` by enough margin
  to expose BVH quality issues, but small enough that 3-slice M2
  finishes in a few weeks.
* **Visually iconic.** The Pavilion's polished marble, water pool,
  glass walls, and chrome cross are widely-recognized references —
  a successful render is portfolio-worthy.
* **Re-uses existing features.** No new BSDFs, no new light types
  beyond the PBRT v4 surface already supported. Performance and
  Pavilion-specific compatibility patches are the only new work.

## Quality bar

### Scene correctness (must)

* The unmodified `scene.pbrt` (the asset's primary file; final name
  confirmed at integration) renders to an image whose composition
  matches the asset's bundled reference render — polished marble
  floor, glass walls, reflective water pool, chrome cross-piece, and
  HDRI-lit sky all recognizable.
* Every surface declared `conductor`, `dielectric`, `coateddiffuse`,
  `coatedconductor` uses its proper BSDF.
* No `Error` compiler diagnostics. Any `Warning` is either anticipated
  (`subsurface` / `measured` / `hair` / `mix` fallback, or wrap-mode
  `black` degrade) or has a new explicit reason documented in the
  Pavilion README.
* No NaN / Inf pixels (verified by automated check).

### Performance gates (must)

* **Slice 1 alone** must achieve **≥ 2×** render speedup on the
  `dining-room` smoke scene relative to its current renderer (current
  baseline: ~210 s at 1280×720 / 64 spp on the operator's reference
  machine). Concrete target: **≤ 105 s** on the same machine, same
  build flags.
* The Pavilion full render completes in **≤ 30 minutes** at
  1280×720 / 64 spp.

### Performance stretch goal

* `dining-room` render time **≥ 3×** faster (≤ 70 s). Achievable only
  if SAH gives bigger BVH-side gains than the shading-dominated cost
  profile suggests; not blocking.

## Architecture

No new IR types. No new public interfaces. The work localizes to:

* **`src/render/bvh.cpp`** gains the SAH split implementation and a
  parallel build path. The existing `RenderBvh` data layout stays the
  same — both flat triangle indices and the node array keep their
  shapes. The change is internal to `BuildBvh()`.
* **`include/yaoray/render/bvh.hpp`** keeps the `BvhBuildOptions`
  shape but gives `split_method` a real second value (SAH) and makes
  it the default. The `LongestAxisMedian` enumerator stays available
  as a fallback / regression-comparison tool.
* **`scenes/pbrt/barcelona_pavilion/README.md`** (new) documents the
  download + render workflow, mirroring `scenes/pbrt/dining_room/`.
* **`docs/architecture/barcelona-pavilion.png`** (new) is the
  reference render captured at the end of Slice 3.

The Pavilion asset itself stays out of git under `external/assets/`
(already gitignored from M1).

## SAH BVH algorithm

Standard binned SAH with cost-based stop, modeled on PBRT v3 §4.3.2:

* For each candidate axis (X, Y, Z), partition the primitives into
  **12 bins** along the axis of the centroid bounding box.
* For each bin-boundary, compute the SAH cost:
  ```
  cost(split) = c_T + (count_L * area(bbox_L) + count_R * area(bbox_R)) / area(bbox_parent)
  ```
  with `c_T = 0.5` (the traversal-vs-intersection cost ratio; PBRT
  uses 0.125, but with our relatively cheap shading we tune higher
  to favor fewer-but-deeper nodes — final value pinned during
  Slice 1 measurement).
* Pick the axis × split position with the lowest cost.
* **Leaf criterion**: create a leaf when *either* the SAH split cost
  exceeds `count * 1.0` (the cost of testing all primitives in a leaf
  with no traversal overhead), *or* `count ≤ max_leaf_triangles`
  (current value: 4).
* On tie / no useful split (all centroids in one bin), fall back to
  splitting at the median by centroid along the longest axis.

The output `RenderBvh.nodes` and `RenderBvh.triangle_indices` keep
the same shape as the median-split builder so no downstream code (CPU
surface resolver, traversal, etc.) needs to change.

## Parallel BVH build

Parallelize the top-down recursion: the root node's two children are
built on separate threads, and deeper recursion branches keep
spawning until the work-per-task is small (e.g., the subtree has
fewer than 1024 primitives), at which point recursion proceeds
serially.

* Thread spawn uses `std::thread` directly (no new dependency).
  Worker count caps at the path tracer's existing thread budget
  (`scene.threads`, defaulted to hardware concurrency).
* **Determinism**: the parallel build must produce a `RenderBvh`
  *bitwise identical* to the serial SAH build for the same input.
  Two strategies guarantee this:
  1. Each parallel subtree builds into a thread-local
     `std::vector<RenderBvhNode>` and `std::vector<int>
     triangle_indices`. After both subtrees finish, the root thread
     concatenates the children's node arrays in a fixed order
     (always left subtree first), rewriting child indices into
     the merged numbering.
  2. Alternatively, pre-allocate node slots in DFS order before
     parallel construction. Marginally more complex; pick (1)
     unless profiling shows the merge step is the bottleneck.
* A unit test compares the parallel build's output (nodes +
  triangle_indices) against the serial build for `dining-room`'s
  geometry, byte-for-byte.

If determinism turns out to cost more than expected, we accept
"deterministic given identical thread count" as a relaxation — but
not less.

## Slice 1 — SAH BVH

**Goal:** Replace the median-split BVH with a binned SAH builder.
`dining-room` must render at least 2× faster than today on the same
hardware. All M1 unit tests and CTest entries remain green.

**Files:**
* Modify: `src/render/bvh.cpp` — implement `BuildSahBvh()` (or
  refactor `BuildRecursive` to dispatch on `options.split_method`).
* Modify: `include/yaoray/render/bvh.hpp` — add `SahBucketBinning` to
  the `BvhSplitMethod` enum; default `BvhBuildOptions::split_method`
  to it.
* Modify: `tests/bvh_tests.cpp` — add tests covering: (1) SAH
  produces a valid BVH (same correctness contract as median-split);
  (2) SAH renders a small fixture identically (within fp tolerance);
  (3) SAH leaf criterion is honored.

**Deliverables:**
* All existing BVH and CPU path-tracer tests still pass.
* All existing CTest entries still pass.
* A documented before/after measurement in the PR description: the
  exact `dining-room` render time at the M1 baseline and the new SAH
  number, with the ratio. Must show ≥ 2×.
* The `BvhBuildOptions::split_method` enum gains a value but the
  default switches to SAH; nothing else changes.

**Out-of-scope for this slice:** parallel construction; Pavilion;
ray packets / SIMD.

## Slice 2 — Parallel BVH construction

**Goal:** Build the BVH on multiple threads, deterministically.
Render output is unaffected (the gate for that is Slice 1); the
slice is verified by build-time speedup on a large reference scene
and by a determinism unit test.

**Files:**
* Modify: `src/render/bvh.cpp` — extract subtree build into a
  function callable from multiple threads; introduce the merge step
  that keeps node ordering deterministic.
* Modify: `tests/bvh_tests.cpp` — add a test that asserts parallel
  vs serial builds produce identical `RenderBvh.nodes` and
  `triangle_indices` arrays.

**Deliverables:**
* Parallel build wins on `dining-room`'s geometry (build time
  reduction documented in PR description; target: ≥ 2× build-time
  speedup on the operator's reference machine, but no hard gate
  since build time is small relative to render time).
* Determinism test passes (the parallel build's `RenderBvh` is
  bitwise identical to the serial build's output for the same
  input).
* All existing unit tests and CTest entries still pass.
* Render output is bitwise identical to Slice 1's output for the
  same scene.

**Out-of-scope for this slice:** Pavilion; SAH algorithm changes.

## Slice 3 — Barcelona Pavilion integration

**Goal:** Render the unmodified `scene.pbrt` (or whatever the
canonical entrypoint name is in mmp's repo) end to end, capture the
reference image, document the workflow.

**Files:**
* Create: `scenes/pbrt/barcelona_pavilion/README.md` — download +
  render workflow.
* Create: `docs/architecture/barcelona-pavilion.png` — final
  reference render.
* Modify: `README.md` — add Pavilion to the showcase list.
* Modify: `docs/architecture/overview.md` — bump M2 status in the
  roadmap table (planned → done; or leave for the merge commit).
* Modify (likely): `src/render/scene_compiler.cpp` or
  `src/pbrt/pbrt_scene.cpp` — whatever compatibility patches the
  Pavilion forces (this is the M1 Slice 4 pattern — patches
  discovered during integration, not pre-planned).

**Process:**
1. Download the Pavilion archive from `mmp/pbrt-v4-scenes` to
   `external/assets/pbrt/barcelona-pavilion/`.
2. Smoke-render at low resolution (e.g., 160×90, 4 spp) to discover
   any unsupported PBRT v4 directives that need patching.
3. For each surfaced issue: triage. If small (a missing param
   handler, a texture extension we don't yet load, etc.), patch and
   re-smoke. If large (something requiring a new BSDF, volumetrics,
   etc.), degrade gracefully with a Warning and document the gap.
4. Once the smoke render shows correct composition, run the full
   1280×720 / 64 spp render. Target completion: ≤ 30 minutes.
5. Save the result to `docs/architecture/barcelona-pavilion.png`.
6. Write `scenes/pbrt/barcelona_pavilion/README.md` following the
   `dining_room` template.

**Deliverables:**
* The Pavilion `.pbrt` renders to completion at production quality.
* The reference image is committed.
* The Pavilion's per-scene README is committed (asset stays out of
  git; gitignored under `external/assets/`).
* `README.md` and `docs/architecture/overview.md` reflect M2
  completion.

**Out-of-scope for this slice:** advanced materials (M3), additional
showcase scenes, polish items, CUDA work.

## Files & modules summary

```
include/yaoray/render/bvh.hpp                  ∼   (BvhSplitMethod enum, default)
src/render/bvh.cpp                              ∼   (SAH + parallel build)
tests/bvh_tests.cpp                             ∼   (SAH tests + parallel determinism test)

src/render/scene_compiler.cpp                  ∼?  (Pavilion compatibility patches)
src/pbrt/pbrt_scene.cpp                        ∼?  (Pavilion compatibility patches)
src/render/texture.cpp                         ∼?  (new texture format support if Pavilion needs it)

scenes/pbrt/barcelona_pavilion/README.md       +   (Pavilion download + render workflow)
docs/architecture/barcelona-pavilion.png       +   (reference render)
docs/architecture/overview.md                  ∼   (roadmap table: M2 → done)
README.md                                      ∼   (showcase list: Pavilion entry)
```

## Testing strategy

* **Unit tests** stay green: all BVH tests, path-tracer tests,
  scene-compiler tests, etc. The SAH build is a drop-in replacement
  at the `BuildBvh()` boundary.
* **Determinism test** (new, in `tests/bvh_tests.cpp`): build a
  fixed scene serially and in parallel; assert
  `RenderBvh.nodes` and `RenderBvh.triangle_indices` are bitwise
  identical. This catches non-determinism regressions.
* **Render-correctness test** (new, in `tests/bvh_tests.cpp`):
  for a small fixture scene, the SAH-built BVH produces the same
  ray-trace hits (per-pixel) as the median-split BVH. This catches
  geometry-misorganization bugs.
* **Performance measurement** is a **manual** step documented in
  the M2 PR description; no automated CTest perf gate (CI hardware
  is too variable for stable assertions). Operator runs the
  `dining-room` smoke variant locally, records the timing, and
  pastes before/after.
* **CTest entry for Pavilion** is *not* added (asset is gitignored,
  same pattern as `dining-room`).

## Risk register

* **Risk:** SAH BVH gives < 2× speedup on `dining-room`. **Likely
  cause:** shading dominates; BVH is not the bottleneck. **Mitigation:**
  profile to confirm; if SAH config (binning, cost ratio) tuning
  doesn't close the gap, open a Slice 1b for additional perf work
  (ray packets, SIMD intrinsics) before declaring M2 complete.
  Do not silently widen the gate.
* **Risk:** Parallel build introduces nondeterminism the unit test
  doesn't catch. **Mitigation:** the determinism test asserts
  bitwise equality of the entire node + triangle_indices arrays
  for a real scene (`dining-room`). If a more pathological scene
  exposes a missed corner case post-M2, log and fix.
* **Risk:** Pavilion uses a PBRT v4 directive we don't support and
  the patch is large (e.g., spectral parameters, custom samplers,
  loopsubdiv). **Mitigation:** triage at first occurrence. If small,
  add in Slice 3. If large, degrade with a documented Warning and
  push the proper fix to M3 / M5+.
* **Risk:** Pavilion exposes a BVH correctness bug (e.g., a scene
  with mixed sphere + trianglemesh geometry that the M1 BVH +
  surface-resolver pipeline mishandled). **Mitigation:** keep
  median-split available via `BvhSplitMethod::LongestAxisMedian` as
  a debugging fallback. If a render diverges, swap split method and
  diff the output to localize.
* **Risk:** Pavilion render time exceeds 30 minutes at production
  spp. **Mitigation:** profile, identify hot path, address in
  Slice 3 if it's a clear M2-scope issue (e.g., BVH traversal is
  still slow at scale); otherwise document as an M5+ optimization
  target and ship M2 with whatever the actual render time is.

## Out of scope

* New BSDFs, new light types — all of M2's PBRT v4 features come
  from the existing M1 surface unless Pavilion's compatibility
  triage requires a new addition.
* SIMD ray packets, Embree-class acceleration. Deferred.
* Texture streaming / memory budget improvements. Added only if
  Pavilion's textures don't fit memory.
* CUDA work. Deferred to M4.
* Pre-planned polish items (`Halton` / `Sobol` samplers, EXR
  output, auto-tangent, `Texture "mix"`, true black-border wrap,
  per-vertex normal smoothing, multi-infinite-light combining).
  Each lands as its own small PR between M2 and M3 if motivated.
* Automated performance regression in CTest. Manual measurement
  only.
