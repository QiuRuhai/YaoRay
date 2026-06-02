# M4 Subsurface Slice 3 — Exit-Point Sampling (Sample_Sp / Pdf_Sp + BVH probe) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the BSSRDF exit-point machinery — a BVH "all intersections along a ray that belong to one target object" probe query, the spatial pdf `Pdf_Sp` (MIS over 3 projection axes × 3 channels), and `SampleBssrdfProbe` (the disk/axis/channel-sampled probe ray that produces an exit point on the surface) — on top of Slice 2's `TabulatedBSSRDF`.

**Architecture:** Faithful port of pbrt-v4's `SeparableBSSRDF::Sample_Sp`/`Pdf_Sp` plus the probe-ray "interaction chain". The renderer has no all-hits API, so the probe query repeatedly calls the existing `IntersectBvh`, advancing `t_min` past each hit (the alpha-skip technique already used in `cpu_surface.cpp`), filtering to the entry object's `primitive_index`/`sphere_index`. The probe query and `SampleBssrdfProbe` live in the render layer (`bvh.cpp` / `bssrdf.cpp`, which can include `bvh.hpp`/`render_scene.hpp`/`shading.hpp`); `Pdf_Sp` is a pure-math method on `TabulatedBSSRDF`. No path-tracer wiring yet — that is Slice 4.

**Tech Stack:** C++20, `yr_test.hpp`, CMake + MSVC, CTest.

**Base branch:** local `main` (now at `788aaef`, after Slice 2 merged). **Worktree:** `m4-subsurface-slice3`.

**Build & test (from worktree root):**
```
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```
After Slice 2 the suite has **321 tests**. clangd "stale index" errors are FALSE POSITIVES — only MSVC build + ctest are authoritative.

---

## Context the implementer needs

- **Ray** (`include/yaoray/core/ray.hpp`): `struct Ray3f { Point3f origin; Vec3f direction; float time=0; Point3f At(float t) const; };`. No tMin/tMax stored — passed to `IntersectBvh`.
- **BVH** (`include/yaoray/render/bvh.hpp`):
  - `BvhBuildResult BuildBvh(const RenderSceneIR& scene, const BvhBuildOptions& = {});` → `{ RenderBvh bvh; std::vector<std::string> errors; }`.
  - `BvhHit IntersectBvh(const RenderSceneIR& scene, const RenderBvh& bvh, const Ray3f& ray, BvhTraceStats& stats, float t_min=1e-5f, float t_max=inf);` — **closest hit only**.
  - `struct BvhHit { bool hit; float t; int triangle_index; int primitive_index; int sphere_index; float bary_u; float bary_v; };` (triangle hit → `sphere_index==-1`; sphere hit → `triangle_index==primitive_index==-1`).
  - `struct BvhTraceStats { std::uint64_t node_tests, triangle_tests; };`.
- **Shading helpers** (`include/yaoray/render/shading.hpp`): `TriangleRef LocateTriangle(scene, int flat_triangle_index);`, `Vec3f GeometricNormal(scene, TriangleRef);`, `Vec3f SphereNormal(Point3f center, float radius, Point3f surface_point);`. `struct TriangleRef { int primitive_index; int local_triangle; };`.
- **Scene tables** (`include/yaoray/render/render_scene.hpp`): `RenderSceneIR` has `std::vector<RenderVertex> vertices; std::vector<std::uint32_t> indices; std::vector<RenderPrimitive> primitives; std::vector<RenderSphere> spheres; std::vector<RenderMaterial> materials;`. `RenderVertex{ Point3f position; Vec3f normal; Vec2f uv; Vec3f tangent; float tangent_handedness; }`. `RenderPrimitive{ uint32_t first_index, index_count; int material_index; bool has_normals, has_uvs, has_tangents; }`. `RenderSphere{ Point3f center; float radius; int material_index; int area_light_index; bool flip_normals; }`.
- **Vec math** (`include/yaoray/core/vec.hpp`): `Dot`, `Cross`, `Length`, `LengthSquared`, `Normalize`, scalar `*`/`/`/`+`/`-`, unary `-`. **No `operator[]`, no component-wise `Vec3f*Vec3f`.** `Point3f`/`Color3f` are `Vec3f`.
- **Slice 2 surface** (`include/yaoray/render/bssrdf.hpp`): `struct TabulatedBSSRDF { Color3f sigma_t, rho; float eta; const BSSRDFTable* table; …; Color3f Sr(float)/Sp(float); float Sw(float); Color3f S(...); float Sample_Sr(int,float); float Pdf_Sr(int,float); };`.
- **Test scene construction pattern** (from `tests/bvh_tests.cpp`): build `RenderSceneIR` by hand (push `vertices`, `indices`, one `RenderPrimitive{first_index, index_count, material_index, has_normals, has_uvs, has_tangents}`, a `RenderMaterial{}`), then `BuildBvh(scene)`.
- **pbrt faithfulness:** axis-selection probabilities `{ss:.25, ts:.25, ns:.5}`, channel prob `1/3`, the `Pdf_Sp` projection (`rProj` perpendicular radii + `|nLocal[axis]|` Jacobian), and `pdf = Pdf_Sp/nFound` must match pbrt so Slice 5 can compare against a reference render.

---

## File Structure

| File | Change |
|---|---|
| `include/yaoray/render/bvh.hpp` | Add `struct BvhProbeHits` + `IntersectBvhProbe(...)` declaration. |
| `src/render/bvh.cpp` | Implement `IntersectBvhProbe` (loop `IntersectBvh`, advance `t_min`, filter by target). |
| `include/yaoray/render/bssrdf.hpp` | Add `TabulatedBSSRDF::Pdf_Sp(...)`; add `struct BssrdfProbeSample` + `SampleBssrdfProbe(...)` decl; forward-declare `RenderSceneIR`/`RenderBvh`. |
| `src/render/bssrdf.cpp` | Implement `Pdf_Sp` and `SampleBssrdfProbe` (include `bvh.hpp`/`render_scene.hpp`/`shading.hpp`). |
| `tests/bvh_probe_tests.cpp` | New — probe all-hits query tests (Task 1). |
| `tests/bssrdf_pdf_sp_tests.cpp` | New — `Pdf_Sp` math tests (Task 2). |
| `tests/bssrdf_probe_tests.cpp` | New — `SampleBssrdfProbe` end-to-end tests (Task 3). |
| `CMakeLists.txt` | Register the three new test files. |

---

### Task 1: BVH probe — all intersections on a target object

**Files:**
- Modify: `include/yaoray/render/bvh.hpp`
- Modify: `src/render/bvh.cpp`
- Create: `tests/bvh_probe_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Declare the probe API in `include/yaoray/render/bvh.hpp`**

Add before the closing `} // namespace yr` (after the `IntersectBvh` declaration):
```cpp
// All intersections along `ray` (within [t_min, t_max]) that belong to one target
// object, collected by repeatedly calling IntersectBvh and advancing past each hit
// (no all-hits BVH traversal exists). A hit is kept when it is a triangle of
// `target_primitive_index` OR a hit on `target_sphere_index`; other geometry is
// transparent to the probe (skipped, not stopped). Hits are returned in
// increasing-t order, bounded by MaxHits.
struct BvhProbeHits {
    static constexpr int MaxHits = 64;
    int count = 0;
    bool exhausted = false;  // true if more than MaxHits target hits existed
    BvhHit hits[MaxHits];
};

BvhProbeHits IntersectBvhProbe(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    int target_primitive_index,   // -1 to not match triangles
    int target_sphere_index,      // -1 to not match spheres
    float t_min,
    float t_max);
```

- [ ] **Step 2: Write the failing tests — create `tests/bvh_probe_tests.cpp`**

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>
#include <cstdint>

namespace {

// Append one axis-aligned quad (2 triangles, 4 verts) in the z=z0 plane spanning
// [-1,1]^2, as a single primitive. Returns the new primitive index.
int AddQuad(yr::RenderSceneIR& scene, float z0) {
    const auto base = static_cast<std::uint32_t>(scene.vertices.size());
    auto V = [&](float x, float y) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, z0}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-1, -1); V(1, -1); V(1, 1); V(-1, 1);
    scene.indices.push_back(base + 0); scene.indices.push_back(base + 1); scene.indices.push_back(base + 2);
    scene.indices.push_back(base + 0); scene.indices.push_back(base + 2); scene.indices.push_back(base + 3);
    int prim = (int)scene.primitives.size();
    scene.primitives.push_back(yr::RenderPrimitive{base, 6, 0, true, false, false});
    return prim;
}

}  // namespace

// A vertical ray through one quad of the target primitive yields exactly 1 hit,
// tagged with that primitive index.
YR_TEST(bvh_probe_single_quad_one_hit) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    int prim = AddQuad(scene, 0.0f);
    auto built = yr::BuildBvh(scene);
    YR_EXPECT_TRUE(built.errors.empty());

    yr::Ray3f ray{yr::Point3f{0.2f, -0.1f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(scene, built.bvh, ray, prim, -1, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 1);
    YR_EXPECT_EQ(hits.hits[0].primitive_index, prim);
}

// Two parallel quads of the SAME primitive => the probe collects both crossings.
YR_TEST(bvh_probe_two_layers_two_hits) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    // One primitive made of two quads (z=0 and z=2) -> 4 triangles, 8 verts.
    const auto base = static_cast<std::uint32_t>(0);
    auto V = [&](float x, float y, float z) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, z}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-1, -1, 0); V(1, -1, 0); V(1, 1, 0); V(-1, 1, 0);
    V(-1, -1, 2); V(1, -1, 2); V(1, 1, 2); V(-1, 1, 2);
    auto Tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        scene.indices.push_back(a); scene.indices.push_back(b); scene.indices.push_back(c);
    };
    Tri(0, 1, 2); Tri(0, 2, 3); Tri(4, 5, 6); Tri(4, 6, 7);
    scene.primitives.push_back(yr::RenderPrimitive{base, 12, 0, true, false, false});
    scene.materials.push_back(yr::RenderMaterial{});
    auto built = yr::BuildBvh(scene);

    yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(scene, built.bvh, ray, 0, -1, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 2);
    YR_EXPECT_TRUE(hits.hits[0].t < hits.hits[1].t);  // sorted by t
}

// A second primitive in the ray's path is excluded when it is not the target.
YR_TEST(bvh_probe_filters_other_primitives) {
    yr::RenderSceneIR scene;
    scene.materials.push_back(yr::RenderMaterial{});
    int target = AddQuad(scene, 0.0f);  // primitive 0
    AddQuad(scene, 1.0f);               // primitive 1 (decoy in the path)
    auto built = yr::BuildBvh(scene);

    yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, -1.0f}, yr::Vec3f{0, 0, 1}};
    yr::BvhProbeHits hits = yr::IntersectBvhProbe(scene, built.bvh, ray, target, -1, 1e-5f, 10.0f);
    YR_EXPECT_EQ(hits.count, 1);
    YR_EXPECT_EQ(hits.hits[0].primitive_index, target);
}
```

- [ ] **Step 3: Register `tests/bvh_probe_tests.cpp` in `CMakeLists.txt`** (after `tests/bssrdf_sample_tests.cpp` in `add_executable(yaoray_tests …)`), build, confirm RED (unresolved `IntersectBvhProbe`).

- [ ] **Step 4: Implement `IntersectBvhProbe` in `src/render/bvh.cpp`**

Add at the end of the file, before the closing `} // namespace yr`:
```cpp
BvhProbeHits IntersectBvhProbe(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    int target_primitive_index,
    int target_sphere_index,
    float t_min,
    float t_max) {
    BvhProbeHits result;
    BvhTraceStats stats;

    // Safety cap on total traversals so a pathological scene cannot spin forever.
    constexpr int kMaxIterations = 4096;
    float cursor = t_min;
    for (int iter = 0; iter < kMaxIterations; ++iter) {
        BvhHit hit = IntersectBvh(scene, bvh, ray, stats, cursor, t_max);
        if (!hit.hit) break;

        const bool is_target =
            (target_primitive_index >= 0 && hit.triangle_index >= 0 &&
             hit.primitive_index == target_primitive_index) ||
            (target_sphere_index >= 0 && hit.sphere_index == target_sphere_index);

        if (is_target) {
            if (result.count < BvhProbeHits::MaxHits) {
                result.hits[result.count++] = hit;
            } else {
                result.exhausted = true;
                break;
            }
        }

        // Advance just past this hit to find the next one along the ray.
        float next = hit.t + 1.0e-4f * (1.0f + std::fabs(hit.t));
        if (!(next > cursor)) next = cursor + 1.0e-4f;  // guard against stalls
        cursor = next;
        if (cursor >= t_max) break;
    }
    return result;
}
```
If `<cmath>` is not already included in `bvh.cpp`, add it (for `std::fabs`).

- [ ] **Step 5: Build + run; confirm the 3 probe tests PASS and the full suite is green (324 total).**

- [ ] **Step 6: Commit**
```bash
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp tests/bvh_probe_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add BVH probe (all hits on a target object) (M4 slice 3)"
```

---

### Task 2: TabulatedBSSRDF::Pdf_Sp

**Files:**
- Modify: `include/yaoray/render/bssrdf.hpp`
- Modify: `src/render/bssrdf.cpp`
- Create: `tests/bssrdf_pdf_sp_tests.cpp`
- Modify: `CMakeLists.txt`

`Pdf_Sp` is the area-measure spatial pdf, combining the per-channel radial `Pdf_Sr` across the 3 projection axes with their MIS weights.

- [ ] **Step 1: Declare in `include/yaoray/render/bssrdf.hpp`**

Inside `struct TabulatedBSSRDF`, after the `float Pdf_Sr(int ch, float r) const;` line, add:
```cpp
    // Area-measure spatial pdf of sampling exit point `pi` (with geometric normal
    // `ni`) given entry point `po` and its orthonormal shading frame (ss, ts, ns).
    // MIS over the 3 projection axes (weights ss/ts/ns = .25/.25/.5) and the 3 RGB
    // channels (each 1/3). Mirrors SampleBssrdfProbe's axis/channel choices.
    float Pdf_Sp(const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
                 const Point3f& pi, const Vec3f& ni) const;
```

- [ ] **Step 2: Write the failing tests — create `tests/bssrdf_pdf_sp_tests.cpp`**

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <cmath>

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

// Pdf_Sp is finite and non-negative for a typical exit point, and the in-plane
// case (pi offset tangentially, ni == ns) is positive.
YR_TEST(bssrdf_pdf_sp_basic) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::Point3f pi{0.05f, 0.0f, 0.0f};
    yr::Vec3f ni{0, 0, 1};
    float pdf = s.Pdf_Sp(po, ss, ts, ns, pi, ni);
    YR_EXPECT_TRUE(std::isfinite(pdf) && pdf > 0.0f);
}

// Closer exit points have higher spatial pdf (the profile decays with radius).
YR_TEST(bssrdf_pdf_sp_decays_with_distance) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1}, ni{0, 0, 1};
    float near_pdf = s.Pdf_Sp(po, ss, ts, ns, yr::Point3f{0.01f, 0, 0}, ni);
    float far_pdf = s.Pdf_Sp(po, ss, ts, ns, yr::Point3f{0.3f, 0, 0}, ni);
    YR_EXPECT_TRUE(near_pdf > far_pdf);
}

// A non-scattering exit normal perpendicular to all axes' projections still yields
// a finite, non-negative pdf (no NaN from the |nLocal| Jacobian).
YR_TEST(bssrdf_pdf_sp_finite_for_grazing_normal) {
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::Point3f pi{0.05f, 0.05f, 0.0f};
    yr::Vec3f ni{1, 0, 0};  // grazing relative to ns
    float pdf = s.Pdf_Sp(po, ss, ts, ns, pi, ni);
    YR_EXPECT_TRUE(std::isfinite(pdf) && pdf >= 0.0f);
}
```

- [ ] **Step 3: Register `tests/bssrdf_pdf_sp_tests.cpp` in `CMakeLists.txt`**, build, confirm RED.

- [ ] **Step 4: Implement `Pdf_Sp` in `src/render/bssrdf.cpp`** (before the closing `}  // namespace yr`):

```cpp
float TabulatedBSSRDF::Pdf_Sp(const Point3f& po, const Vec3f& ss, const Vec3f& ts,
                              const Vec3f& ns, const Point3f& pi, const Vec3f& ni) const {
    // Express the entry->exit offset and the exit normal in the shading frame.
    Vec3f d = po - pi;
    float dLocal[3] = {Dot(ss, d), Dot(ts, d), Dot(ns, d)};
    float nLocal[3] = {Dot(ss, ni), Dot(ts, ni), Dot(ns, ni)};

    // Radius of the offset projected into the plane perpendicular to each axis.
    float rProj[3] = {
        std::sqrt(dLocal[1] * dLocal[1] + dLocal[2] * dLocal[2]),  // ss axis
        std::sqrt(dLocal[2] * dLocal[2] + dLocal[0] * dLocal[0]),  // ts axis
        std::sqrt(dLocal[0] * dLocal[0] + dLocal[1] * dLocal[1]),  // ns axis
    };

    const float axisProb[3] = {0.25f, 0.25f, 0.5f};
    const float chProb = 1.0f / 3.0f;

    float pdf = 0;
    for (int axis = 0; axis < 3; ++axis)
        for (int ch = 0; ch < 3; ++ch)
            pdf += Pdf_Sr(ch, rProj[axis]) * std::abs(nLocal[axis]) * chProb * axisProb[axis];
    return pdf;
}
```

- [ ] **Step 5: Build + run; confirm the 3 `bssrdf_pdf_sp_*` tests PASS; full suite green (327 total).**

- [ ] **Step 6: Commit**
```bash
git add include/yaoray/render/bssrdf.hpp src/render/bssrdf.cpp tests/bssrdf_pdf_sp_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add TabulatedBSSRDF::Pdf_Sp spatial pdf (M4 slice 3)"
```

---

### Task 3: SampleBssrdfProbe — disk/axis/channel-sampled exit point

**Files:**
- Modify: `include/yaoray/render/bssrdf.hpp`
- Modify: `src/render/bssrdf.cpp`
- Create: `tests/bssrdf_probe_tests.cpp`
- Modify: `CMakeLists.txt`

This ties Tasks 1 and 2 together exactly as pbrt's `Sample_Sp`: pick a projection axis (`u1 < .5` → main axis `ns`; `< .75` → `ss`; else `ts`), pick an RGB channel, sample a radius via `Sample_Sr`, build a probe ray of length `l = 2·sqrt(rMax²−r²)` centered on the entry, intersect via `IntersectBvhProbe`, pick one of the `nFound` crossings uniformly, and report it with `pdf = Pdf_Sp/nFound`.

- [ ] **Step 1: Declare in `include/yaoray/render/bssrdf.hpp`**

At the top of the file, after the existing includes, add forward declarations (do NOT include the heavy headers here):
```cpp
namespace yr {
struct RenderSceneIR;
struct RenderBvh;
}
```
(Place this `namespace yr { … }` forward-decl block right after the `#include` lines and before the main `namespace yr {` body, or simply add the two `struct` forward declarations inside the existing `namespace yr {` block above `struct BSSRDFTable`.)

After the `TabulatedBSSRDF` struct definition (before the closing `}  // namespace yr`), add:
```cpp
// Result of probing for a subsurface exit point. `hit == false` means the probe
// found no surface (the sample should be discarded by the caller). On success,
// `pi`/`ni` are the exit position and geometric normal, the *_index/bary fields
// identify the hit for later material/shading resolution, `sp` is the spatial
// BSSRDF term Sp(distance), and `pdf` is the area-measure sampling density
// (already divided by the number of probe crossings).
struct BssrdfProbeSample {
    bool hit = false;
    Point3f pi{0, 0, 0};
    Vec3f ni{0, 0, 1};
    int primitive_index = -1;
    int triangle_index = -1;
    int sphere_index = -1;
    float bary_u = 0.0f;
    float bary_v = 0.0f;
    Color3f sp{0, 0, 0};
    float pdf = 0.0f;
};

// Importance-sample a subsurface exit point on the entry object. `po` is the entry
// point; (ss, ts, ns) is its orthonormal shading frame. `u1` is consumed for the
// axis, channel, and crossing-selection choices (pbrt's reuse trick); `u2` =
// (radius-u, phi-u). The probe is restricted to `target_primitive_index` /
// `target_sphere_index` (pass -1 for the unused kind).
BssrdfProbeSample SampleBssrdfProbe(
    const TabulatedBSSRDF& bssrdf,
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
    int target_primitive_index, int target_sphere_index,
    float u1, const Vec2f& u2);
```

- [ ] **Step 2: Write the failing tests — create `tests/bssrdf_probe_tests.cpp`**

```cpp
#include "yr_test.hpp"
#include <yaoray/render/bssrdf.hpp>
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>
#include <cmath>
#include <cstdint>

static const yr::BSSRDFTable& Tbl() {
    static yr::BSSRDFTable t = [] {
        yr::BSSRDFTable table(100, 64);
        yr::ComputeBeamDiffusionBSSRDF(0.0f, 1.33f, table);
        return table;
    }();
    return t;
}

// A large flat quad in the z=0 plane, single primitive.
static yr::RenderSceneIR MakeQuadScene() {
    yr::RenderSceneIR scene;
    auto V = [&](float x, float y) {
        scene.vertices.push_back(yr::RenderVertex{yr::Point3f{x, y, 0.0f}, yr::Vec3f{0, 0, 1}, {}, {}, 1.0f});
    };
    V(-5, -5); V(5, -5); V(5, 5); V(-5, 5);
    scene.indices = {0, 1, 2, 0, 2, 3};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 0, true, false, false});
    scene.materials.push_back(yr::RenderMaterial{});
    return scene;
}

// Main-axis probe (u1 < .5) from a point on a big flat quad lands back on the quad,
// with a positive pdf consistent with Pdf_Sp/nFound.
YR_TEST(bssrdf_probe_lands_on_quad) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildBvh(scene);
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());

    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::BssrdfProbeSample r = yr::SampleBssrdfProbe(s, scene, built.bvh, po, ss, ts, ns,
                                                    /*prim=*/0, /*sphere=*/-1,
                                                    /*u1=*/0.2f, /*u2=*/yr::Vec2f{0.5f, 0.3f});
    YR_EXPECT_TRUE(r.hit);
    YR_EXPECT_NEAR(r.pi.z, 0.0f, 1e-3f);          // exit lies on the z=0 quad
    YR_EXPECT_EQ(r.primitive_index, 0);
    YR_EXPECT_TRUE(r.pdf > 0.0f && std::isfinite(r.pdf));
    YR_EXPECT_TRUE(r.sp.x >= 0.0f && std::isfinite(r.sp.x));
}

// The reported pdf equals Pdf_Sp(at the returned exit point) divided by the number
// of crossings (here nFound == 1 for a single flat quad).
YR_TEST(bssrdf_probe_pdf_matches_pdf_sp) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildBvh(scene);
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());

    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    yr::BssrdfProbeSample r = yr::SampleBssrdfProbe(s, scene, built.bvh, po, ss, ts, ns,
                                                    0, -1, 0.2f, yr::Vec2f{0.6f, 0.1f});
    YR_EXPECT_TRUE(r.hit);
    float pdf_sp = s.Pdf_Sp(po, ss, ts, ns, r.pi, r.ni);
    YR_EXPECT_NEAR(r.pdf, pdf_sp, 1e-3f * pdf_sp + 1e-6f);  // nFound == 1
}

// Determinism: identical inputs produce an identical sample.
YR_TEST(bssrdf_probe_deterministic) {
    yr::RenderSceneIR scene = MakeQuadScene();
    auto built = yr::BuildBvh(scene);
    yr::TabulatedBSSRDF s({0.02f, 0.02f, 0.02f}, {1.0f, 1.0f, 1.0f}, 1.33f, Tbl());
    yr::Point3f po{0, 0, 0};
    yr::Vec3f ss{1, 0, 0}, ts{0, 1, 0}, ns{0, 0, 1};
    auto a = yr::SampleBssrdfProbe(s, scene, built.bvh, po, ss, ts, ns, 0, -1, 0.33f, yr::Vec2f{0.4f, 0.7f});
    auto b = yr::SampleBssrdfProbe(s, scene, built.bvh, po, ss, ts, ns, 0, -1, 0.33f, yr::Vec2f{0.4f, 0.7f});
    YR_EXPECT_EQ(a.hit, b.hit);
    YR_EXPECT_NEAR(a.pi.x, b.pi.x, 1e-6f);
    YR_EXPECT_NEAR(a.pdf, b.pdf, 1e-6f);
}
```

- [ ] **Step 3: Register `tests/bssrdf_probe_tests.cpp` in `CMakeLists.txt`**, build, confirm RED (unresolved `SampleBssrdfProbe`).

- [ ] **Step 4: Implement in `src/render/bssrdf.cpp`**

Add these includes to the top include block (next to the existing `#include <yaoray/render/catmull_rom.hpp>`):
```cpp
#include <yaoray/render/bvh.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>
```
Then add, before the closing `}  // namespace yr`:
```cpp
BssrdfProbeSample SampleBssrdfProbe(
    const TabulatedBSSRDF& bssrdf,
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Point3f& po, const Vec3f& ss, const Vec3f& ts, const Vec3f& ns,
    int target_primitive_index, int target_sphere_index,
    float u1, const Vec2f& u2) {
    BssrdfProbeSample out;

    // Choose a projection axis (vz is the probe direction). Reuse u1 by rescaling.
    Vec3f vx, vy, vz;
    if (u1 < 0.5f) {
        vx = ss; vy = ts; vz = ns;
        u1 *= 2.0f;
    } else if (u1 < 0.75f) {
        vx = ts; vy = ns; vz = ss;
        u1 = (u1 - 0.5f) * 4.0f;
    } else {
        vx = ns; vy = ss; vz = ts;
        u1 = (u1 - 0.75f) * 4.0f;
    }

    // Choose an RGB channel, then rescale u1 again.
    int ch = (int)(u1 * 3.0f);
    if (ch < 0) ch = 0;
    if (ch > 2) ch = 2;
    u1 = u1 * 3.0f - (float)ch;

    // Sample a radius; reject non-scattering channels and out-of-support radii.
    float r = bssrdf.Sample_Sr(ch, u2.x);
    if (r < 0) return out;
    float phi = 2.0f * 3.14159265358979323846f * u2.y;

    float rMax = bssrdf.Sample_Sr(ch, 0.999f);
    if (r >= rMax) return out;
    float l = 2.0f * std::sqrt(std::max(0.0f, rMax * rMax - r * r));

    // Probe ray: a segment of length l centered on the entry, parallel to vz, offset
    // by the sampled disk position in the (vx, vy) plane.
    Vec3f disk = vx * (r * std::cos(phi)) + vy * (r * std::sin(phi));
    Point3f base = po + disk - vz * (l * 0.5f);
    Ray3f probe{base, vz};

    BvhProbeHits hits = IntersectBvhProbe(scene, bvh, probe, target_primitive_index,
                                          target_sphere_index, 1.0e-5f, l);
    int nFound = hits.count;
    if (nFound == 0) return out;

    // Pick one crossing uniformly, reusing u1.
    int selected = (int)(u1 * (float)nFound);
    if (selected < 0) selected = 0;
    if (selected >= nFound) selected = nFound - 1;
    const BvhHit& chosen = hits.hits[selected];

    // Resolve the exit position and geometric normal.
    out.pi = probe.At(chosen.t);
    if (chosen.sphere_index >= 0) {
        const RenderSphere& sph = scene.spheres[chosen.sphere_index];
        out.ni = SphereNormal(sph.center, sph.radius, out.pi);
        out.sphere_index = chosen.sphere_index;
    } else {
        TriangleRef tri = LocateTriangle(scene, chosen.triangle_index);
        out.ni = GeometricNormal(scene, tri);
        out.primitive_index = chosen.primitive_index;
        out.triangle_index = chosen.triangle_index;
        out.bary_u = chosen.bary_u;
        out.bary_v = chosen.bary_v;
    }

    // Spatial term and pdf (divided by the number of crossings, as in pbrt).
    Vec3f delta = po - out.pi;
    float dist = Length(delta);
    out.sp = bssrdf.Sp(dist);
    out.pdf = bssrdf.Pdf_Sp(po, ss, ts, ns, out.pi, out.ni) / (float)nFound;
    out.hit = true;
    return out;
}
```

- [ ] **Step 5: Build + run; confirm the 3 `bssrdf_probe_*` tests PASS; full suite green (330 total). If `bssrdf_probe_pdf_matches_pdf_sp` fails, check that `pdf == Pdf_Sp/nFound` and that the probe length / disk offset match pbrt — report your reasoning rather than altering the math.**

- [ ] **Step 6: Commit**
```bash
git add include/yaoray/render/bssrdf.hpp src/render/bssrdf.cpp tests/bssrdf_probe_tests.cpp CMakeLists.txt
git commit -m "feat(bssrdf): add SampleBssrdfProbe exit-point sampling (M4 slice 3)"
```

---

## Self-Review (completed by plan author)

**1. Spec coverage.** Slice 3 of the M4 spec asks for "Sample_Sp/Pdf_Sp + BVH probe-ray all-hits query". Task 1 builds the all-hits-on-target probe query (the BVH capability the spec calls out, implemented as a `t_min`-advancing loop since no native all-hits API exists). Task 2 implements `Pdf_Sp` (the 3-axis × 3-channel MIS spatial pdf). Task 3 implements `SampleBssrdfProbe` (= pbrt's `Sample_Sp`: axis/channel/disk sampling → probe → pick crossing → `pdf = Pdf_Sp/nFound`). Integrator wiring and white-furnace validation are correctly deferred to Slice 4.

**2. Placeholder scan.** No "TBD"/"handle edge cases"/"similar to". Every code step is complete and compilable; commands state expected pass/fail and counts (321 → 324 → 327 → 330).

**3. Type consistency.** `IntersectBvhProbe(scene, bvh, ray, int, int, float, float) → BvhProbeHits` is identical across header, impl, and tests. `BvhProbeHits{ int count; bool exhausted; BvhHit hits[64]; }` matches its uses. `TabulatedBSSRDF::Pdf_Sp(Point3f, Vec3f×3 frame, Point3f, Vec3f)` matches header/impl/tests and the call inside `SampleBssrdfProbe`. `BssrdfProbeSample` fields used in tests (`hit`, `pi`, `ni`, `primitive_index`, `pdf`, `sp`) match the struct. `SampleBssrdfProbe(...)`'s signature is identical in header, impl, and all three test call sites. All `Vec3f` access is `.x/.y/.z`; per-axis values use local `float[3]` arrays (no nonexistent `operator[]` on `Vec3f`). `RenderSceneIR`/`RenderBvh` are forward-declared in the header and fully included only in the `.cpp`.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-02-yaoray-m4-subsurface-slice3-implementation-plan.md`. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks. Sonnet is adequate for Task 1 (loop over an existing API) and Task 2 (direct formula); use opus for Task 3 (the axis/channel/disk sampling + probe assembly, the trickiest faithfulness surface).
2. **Inline Execution** — via `superpowers:executing-plans` with checkpoints.
