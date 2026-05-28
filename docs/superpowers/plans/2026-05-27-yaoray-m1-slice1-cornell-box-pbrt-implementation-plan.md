# YaoRay M1 Slice 1 — `cornell_box_pbrt` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `Shape "sphere"`, `LightSource "point"`, and a material-degradation diagnostic emitter through the YaoRay PBRT pipeline so a hand-authored `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt` renders correctly end-to-end on the CPU backend.

**Architecture:** Extends the two-layer pipeline (`PbrtScene → RenderSceneIR → Backend`) introduced in M0. The IR already has `RenderSceneIR.analytic_lights` (a `std::vector<AnalyticLight>`) and `AnalyticLight { kind, position, direction, intensity, cone_angle }` defined but unused; this slice fills them. Sphere geometry is added as a parallel `std::vector<RenderSphere>` table on `RenderSceneIR`. The BVH stays triangle-only — sphere intersection is a linear pass appended to `IntersectBvh`, which is sufficient at M1 scale (≤ 50 spheres) and avoids a BVH refactor. Direct lighting in the CPU path tracer gains an analytic-light loop alongside the existing emissive-primitive and environment loops; point lights are deltas, so no MIS weight against BSDF is needed.

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. All M0 dependencies (no new third-party libs).

---

## Spec Coverage

This plan implements the following items from `docs/superpowers/specs/2026-05-27-yaoray-m1-dining-room-design.md` §"Implementation Slices — Slice 1":

1. `Shape "sphere"` end-to-end (parser, compiler, BVH bounds, intersection, normal, UV).
2. `LightSource "point"` end-to-end (parser already done in M0, IR, compiler, CPU sampling, MIS).
3. Diagnostic warning infrastructure for material degradation (the emitter only — the explicit substitution table lands in Slice 4).
4. The hand-authored `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt` asset plus its CTest entry.

Out of scope (deferred to other slices): HDRI, textures, normal maps, vertex N/uv/S pass-through, `LightSource "distant"`/`"spot"`, dining-room.

---

## File Structure

**New files:**

| Path | Responsibility |
|------|----------------|
| `tests/sphere_tests.cpp` | Pure sphere math unit tests (intersect, bounds, normal, UV). |
| `tests/analytic_light_tests.cpp` | Point-light sampling math unit tests. |
| `tests/material_degradation_tests.cpp` | Unit tests that the warning emitter fires correctly for unknown material kinds. |
| `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt` | The Cornell box scene asset. |

**Modified files:**

| Path | Change |
|------|--------|
| `include/yaoray/render/render_scene.hpp` | Add `RenderSphere` struct + `std::vector<RenderSphere> spheres` to `RenderSceneIR`. |
| `include/yaoray/render/bvh.hpp` | Add `sphere_index` field to `BvhHit`. |
| `include/yaoray/render/shading.hpp` | Declare sphere math helpers (`IntersectSphere`, `SphereBounds`, `SphereNormal`, `SphereUv`). |
| `include/yaoray/render/light_sampling.hpp` | Add `AnalyticLightSample` struct + `SampleAnalyticPoint`. |
| `src/render/shading.cpp` | Implement sphere math helpers. |
| `src/render/bvh.cpp` | Append linear sphere intersection loop in `IntersectBvh`. |
| `src/render/scene_compiler.cpp` | Add `CompileSphereShape` dispatch, add `CompileAnalyticLights` step, add `MaterialFallbackWarning` helper and route unknown material kinds through it. |
| `src/render/light_sampling.cpp` | Implement `SampleAnalyticPoint`. |
| `src/backends/cpu/cpu_surface.cpp` | Resolve sphere hits: populate geometric normal + UV from the hit `sphere_index`. |
| `src/backends/cpu/cpu_path_tracer.cpp` | Extend `EstimateDirectLight` with an analytic-light loop. |
| `CMakeLists.txt` | Add the three new test files to the `yaoray_tests` executable; add the CTest entry for the new scene. |
| `README.md` | Add `cornell_box_pbrt` to the documented runnable scenes. (Light update only — full README rewrite is Slice 4.) |

---

## Task 1: Sphere geometry math

**Files:**
- Create: `tests/sphere_tests.cpp`
- Modify: `include/yaoray/render/shading.hpp` (add declarations)
- Modify: `src/render/shading.cpp` (add implementations)
- Modify: `CMakeLists.txt` (line 80–100 area — add `tests/sphere_tests.cpp` to the `yaoray_tests` executable)

This task adds pure analytic ray-sphere helpers with no integration anywhere else. The signatures match what later tasks will call.

**Math reference (analytic ray-sphere, unit sphere centered at `c`, radius `r`):**

A ray `o + t*d` hits the sphere when `|o + t*d − c|² = r²`. Solve the quadratic `a*t² + b*t + γ = 0` with `a = d·d`, `b = 2*(o−c)·d`, `γ = (o−c)·(o−c) − r²`. Pick the smallest positive root within `[t_min, t_max]`.

UV at a surface point `p` on the sphere (PBRT convention): `n = normalize(p − c)`, `u = (atan2(n.x, n.z) + π) / (2π)`, `v = (π − acos(clamp(n.y, −1, 1))) / π` so `v=0` at the south pole and `v=1` at the north pole.

- [ ] **Step 1: Add the declarations to `shading.hpp`**

Add at the end of the `namespace yr {` block, before the closing brace:

```cpp
struct SphereHit {
    bool hit = false;
    float t = 0.0f;
};

SphereHit IntersectSphere(Point3f center, float radius, const Ray3f& ray, float t_min, float t_max);
Bounds3f SphereBounds(Point3f center, float radius);
Vec3f SphereNormal(Point3f center, float radius, Point3f surface_point);
Vec2f SphereUv(Vec3f outward_normal);
```

The `#include <yaoray/core/ray.hpp>` and `#include <yaoray/core/bounds.hpp>` must be present in the header. Add them if missing.

- [ ] **Step 2: Implement the helpers in `shading.cpp`**

Append the implementations (inside `namespace yr {`):

```cpp
SphereHit IntersectSphere(Point3f center, float radius, const Ray3f& ray, float t_min, float t_max) {
    const Vec3f oc{ray.origin.x - center.x, ray.origin.y - center.y, ray.origin.z - center.z};
    const float a = Dot(ray.direction, ray.direction);
    const float b = 2.0f * Dot(oc, ray.direction);
    const float c = Dot(oc, oc) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) {
        return SphereHit{false, 0.0f};
    }
    const float sqrt_disc = std::sqrt(disc);
    const float inv_2a = 0.5f / a;
    const float t0 = (-b - sqrt_disc) * inv_2a;
    if (t0 > t_min && t0 < t_max) {
        return SphereHit{true, t0};
    }
    const float t1 = (-b + sqrt_disc) * inv_2a;
    if (t1 > t_min && t1 < t_max) {
        return SphereHit{true, t1};
    }
    return SphereHit{false, 0.0f};
}

Bounds3f SphereBounds(Point3f center, float radius) {
    const Point3f lo{center.x - radius, center.y - radius, center.z - radius};
    const Point3f hi{center.x + radius, center.y + radius, center.z + radius};
    return Bounds3f{lo, hi};
}

Vec3f SphereNormal(Point3f center, float radius, Point3f surface_point) {
    const Vec3f d{surface_point.x - center.x, surface_point.y - center.y, surface_point.z - center.z};
    const float inv_r = radius > 0.0f ? 1.0f / radius : 0.0f;
    return Vec3f{d.x * inv_r, d.y * inv_r, d.z * inv_r};
}

Vec2f SphereUv(Vec3f outward_normal) {
    constexpr float Pi = 3.14159265358979323846f;
    const float clamped_y = std::clamp(outward_normal.y, -1.0f, 1.0f);
    const float u = (std::atan2(outward_normal.x, outward_normal.z) + Pi) / (2.0f * Pi);
    const float v = (Pi - std::acos(clamped_y)) / Pi;
    return Vec2f{u, v};
}
```

The implementation needs `#include <algorithm>` (for `std::clamp`) and `#include <cmath>` if not already included.

- [ ] **Step 3: Write the failing tests**

Create `tests/sphere_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/core/ray.hpp>
#include <yaoray/render/shading.hpp>

YR_TEST(sphere_intersect_hits_unit_sphere_from_outside) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_NEAR(hit.t, 2.0f, 1.0e-5);
}

YR_TEST(sphere_intersect_misses_when_ray_passes_outside) {
    const yr::Ray3f ray{yr::Point3f{2.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(!hit.hit);
}

YR_TEST(sphere_intersect_picks_nearest_hit_from_inside) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 0.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.0e6f);
    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_NEAR(hit.t, 1.0f, 1.0e-5);
}

YR_TEST(sphere_intersect_respects_t_max) {
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::SphereHit hit = yr::IntersectSphere(yr::Point3f{0.0f, 0.0f, 0.0f}, 1.0f, ray, 1.0e-5f, 1.5f);
    YR_EXPECT_TRUE(!hit.hit);
}

YR_TEST(sphere_bounds_contain_extents) {
    const yr::Bounds3f b = yr::SphereBounds(yr::Point3f{1.0f, 2.0f, 3.0f}, 0.5f);
    YR_EXPECT_NEAR(b.min.x, 0.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.min.y, 1.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.min.z, 2.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.x, 1.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.y, 2.5f, 1.0e-6);
    YR_EXPECT_NEAR(b.max.z, 3.5f, 1.0e-6);
}

YR_TEST(sphere_normal_points_outward) {
    const yr::Vec3f n = yr::SphereNormal(yr::Point3f{0.0f, 0.0f, 0.0f}, 2.0f, yr::Point3f{2.0f, 0.0f, 0.0f});
    YR_EXPECT_NEAR(n.x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(n.y, 0.0f, 1.0e-6);
    YR_EXPECT_NEAR(n.z, 0.0f, 1.0e-6);
}

YR_TEST(sphere_uv_north_pole_is_v_one) {
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, 1.0f, 0.0f});
    YR_EXPECT_NEAR(uv.v, 1.0f, 1.0e-5);
}

YR_TEST(sphere_uv_south_pole_is_v_zero) {
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, -1.0f, 0.0f});
    YR_EXPECT_NEAR(uv.v, 0.0f, 1.0e-5);
}

YR_TEST(sphere_uv_front_seam_is_u_half) {
    // +z is the "front"; PBRT seam puts u=0.5 there.
    const yr::Vec2f uv = yr::SphereUv(yr::Vec3f{0.0f, 0.0f, 1.0f});
    YR_EXPECT_NEAR(uv.u, 0.5f, 1.0e-5);
}
```

Note: the existing `Vec2f` exposes its components as `.x`/`.y` *or* as `.u`/`.v` depending on the header. If the test fails to compile with `.u`/`.v`, change them to `.x`/`.y` to match the header.

- [ ] **Step 4: Register the new test file in CMake**

Modify `CMakeLists.txt`. In the `add_executable(yaoray_tests ...)` block (around line 80), add `tests/sphere_tests.cpp` to the source list, e.g. directly after `tests/bvh_tests.cpp`:

```cmake
add_executable(yaoray_tests
    tests/test_main.cpp
    tests/version_tests.cpp
    tests/core_tests.cpp
    tests/bvh_tests.cpp
    tests/sphere_tests.cpp   # <-- new
    tests/film_tests.cpp
    ...
)
```

- [ ] **Step 5: Build and run the tests**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: all `sphere_*` tests pass; no other tests broken.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/shading.hpp src/render/shading.cpp \
        tests/sphere_tests.cpp CMakeLists.txt
git commit -m "feat: analytic ray-sphere math (intersect, bounds, normal, UV)"
```

---

## Task 2: RenderSphere IR + Shape "sphere" compiler dispatch

**Files:**
- Modify: `include/yaoray/render/render_scene.hpp` (add `RenderSphere` and `RenderSceneIR.spheres`)
- Modify: `src/render/scene_compiler.cpp` (add `CompileSphereShape` and dispatch)
- Modify: `tests/render_scene_tests.cpp` (add tests)
- Modify: `tests/pbrt_tests.cpp` (optional — confirms PBRT parses `Shape "sphere"`)

After this task, the scene compiler turns `Shape "sphere"` into a populated `RenderSceneIR.spheres` entry, but nothing renders the sphere yet. That's Task 3 / 4.

PBRT v4 `Shape "sphere"` parameters: `radius` (float, default 1.0). `zmin`, `zmax`, `phimax` are partial-sphere parameters — M1 ignores them with a warning.

- [ ] **Step 1: Add `RenderSphere` to `render_scene.hpp`**

In `include/yaoray/render/render_scene.hpp`, add the struct just below `RenderPrimitive` (around line 134) and the vector to `RenderSceneIR` (around line 180, next to `primitives`):

```cpp
struct RenderSphere {
    Point3f center{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    int material_index = 0;
    int area_light_index = -1;   // -1 if no area light
    bool flip_normals = false;
};
```

And in `RenderSceneIR`:

```cpp
struct RenderSceneIR {
    // ... existing fields ...

    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;
    std::vector<RenderSphere> spheres;       // <-- new
    // ... rest ...
};
```

- [ ] **Step 2: Write the failing scene-compiler test**

Add to `tests/render_scene_tests.cpp` (find a good spot near other compile tests):

```cpp
YR_TEST(scene_compiler_compiles_shape_sphere) {
    // Build a minimal PbrtScene with a Shape "sphere" of radius 0.5 translated to (1, 2, 3).
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};  // identity
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtShapeRecord record;
    record.shape.type = "sphere";
    record.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    // object_to_world that translates by (1,2,3)
    record.object_to_world = yr::Mat4f{};
    record.object_to_world.m[12] = 1.0f;
    record.object_to_world.m[13] = 2.0f;
    record.object_to_world.m[14] = 3.0f;
    pbrt.shapes.push_back(record);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->spheres.size(), std::size_t{1});
    YR_EXPECT_NEAR(result.scene->spheres[0].center.x, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.y, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].center.z, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(result.scene->spheres[0].radius, 0.5f, 1.0e-6);
}
```

If `tests/render_scene_tests.cpp` doesn't yet include `<yaoray/render/scene_compiler.hpp>` and `<yaoray/pbrt/pbrt_scene.hpp>`, add those includes at the top.

- [ ] **Step 3: Run the test and verify it fails**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: `[FAIL] scene_compiler_compiles_shape_sphere` because the compiler currently emits `unsupported shape type: sphere` and produces zero spheres.

- [ ] **Step 4: Implement `CompileSphereShape` and dispatch**

In `src/render/scene_compiler.cpp`, just before `CompilePlyMeshShape` (around line 283), add:

```cpp
bool CompileSphereShape(
    const PbrtShapeRecord& record,
    int material_index,
    RenderSceneIR& ir,
    const PbrtScene& scene,
    std::vector<SceneDiagnostic>& diagnostics
) {
    const float radius = FloatParam(FindParam(record.shape.params, "radius"), 1.0f);

    // Warn on unsupported partial-sphere params.
    if (FindParam(record.shape.params, "zmin") != nullptr ||
        FindParam(record.shape.params, "zmax") != nullptr ||
        FindParam(record.shape.params, "phimax") != nullptr) {
        diagnostics.push_back(Warning(scene, "Shape.sphere",
            "partial sphere parameters (zmin/zmax/phimax) are not supported in M1; full sphere will be used"));
    }

    // Sphere center = object_to_world * (0,0,0). Radius is scaled by the uniform component
    // of the linear part; warn if non-uniform.
    const Mat4f& m = record.object_to_world;
    const Point3f center = TransformPoint(m, Point3f{0.0f, 0.0f, 0.0f});

    const Vec3f sx = TransformVector(m, Vec3f{1.0f, 0.0f, 0.0f});
    const Vec3f sy = TransformVector(m, Vec3f{0.0f, 1.0f, 0.0f});
    const Vec3f sz = TransformVector(m, Vec3f{0.0f, 0.0f, 1.0f});
    const float lx = std::sqrt(Dot(sx, sx));
    const float ly = std::sqrt(Dot(sy, sy));
    const float lz = std::sqrt(Dot(sz, sz));
    const float scale = (lx + ly + lz) / 3.0f;
    const float max_dev = std::max({
        std::fabs(lx - scale),
        std::fabs(ly - scale),
        std::fabs(lz - scale)
    });
    if (max_dev > 1.0e-3f * scale) {
        diagnostics.push_back(Warning(scene, "Shape.sphere",
            "non-uniform scale on sphere transform; using average scale"));
    }

    RenderSphere sphere;
    sphere.center = center;
    sphere.radius = radius * scale;
    sphere.material_index = material_index;
    sphere.area_light_index = -1;  // Area lights on spheres are handled in a later slice.

    // If an area light is attached, surface this as a warning for Slice 1 (we don't
    // yet sample analytic-shape emitters as area lights).
    if (record.area_light.has_value()) {
        diagnostics.push_back(Warning(scene, "Shape.sphere.AreaLightSource",
            "area light on a sphere is not yet supported in M1 Slice 1; the emission will be ignored"));
    }

    ir.spheres.push_back(sphere);
    return true;
}
```

Then in `CompilePbrtScene` (around line 456), extend the dispatch:

```cpp
if (record.shape.type == "trianglemesh") {
    CompileTriangleMeshShape(record, mat_idx, ir, scene, diagnostics);
} else if (record.shape.type == "plymesh") {
    CompilePlyMeshShape(record, mat_idx, ir, scene, diagnostics);
} else if (record.shape.type == "sphere") {                       // <-- new
    CompileSphereShape(record, mat_idx, ir, scene, diagnostics);  // <-- new
} else {
    diagnostics.push_back(Warning(scene, "Shape", "unsupported shape type: " + record.shape.type));
}
```

Also update the empty-geometry check at the end of `CompilePbrtScene` (around line 468) to account for spheres:

```cpp
if (ir.primitives.empty() && ir.spheres.empty()) {
    diagnostics.push_back(Error(scene, "Shape", "scene contains no geometry"));
}
```

- [ ] **Step 5: Build, run tests, verify pass**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: `scene_compiler_compiles_shape_sphere` passes; all previous tests still pass.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/render_scene.hpp src/render/scene_compiler.cpp \
        tests/render_scene_tests.cpp
git commit -m "feat: compile Shape \"sphere\" into RenderSceneIR.spheres"
```

---

## Task 3: BVH + surface resolver for sphere hits

**Files:**
- Modify: `include/yaoray/render/bvh.hpp` (add `sphere_index` to `BvhHit`)
- Modify: `src/render/bvh.cpp` (append sphere intersection loop in `IntersectBvh`)
- Modify: `src/backends/cpu/cpu_surface.cpp` (resolve sphere hits)
- Modify: `tests/bvh_tests.cpp` (add sphere hit tests)

This task makes rays actually hit spheres and the surface resolver populate the right normal + UV + material. After this, `cpu_path_tracer` will already render spheres correctly because it uses the surface resolver's outputs.

The BVH stays triangle-only. After the BVH walk in `IntersectBvh`, we linearly intersect each sphere. This is O(n_spheres) per ray — acceptable at M1 scale (≤ 50 spheres per scene). The future M2 slice can fold spheres into the BVH if profiling shows a bottleneck.

- [ ] **Step 1: Extend `BvhHit`**

In `include/yaoray/render/bvh.hpp`, add `sphere_index` to `BvhHit`:

```cpp
struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    int triangle_index = -1;
    int primitive_index = -1;
    int sphere_index = -1;       // <-- new; -1 for triangle hits
    float bary_u = 0.0f;         // For sphere hits, this is the U on the sphere (azimuth).
    float bary_v = 0.0f;         // For sphere hits, this is the V on the sphere (zenith).
};
```

- [ ] **Step 2: Write the failing BVH test**

Add to `tests/bvh_tests.cpp`:

```cpp
#include <yaoray/render/shading.hpp>   // For SphereUv if needed in assertions.

YR_TEST(bvh_intersect_finds_sphere_when_no_triangles) {
    yr::RenderSceneIR scene;
    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 1.0f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);
    scene.materials.push_back(yr::RenderMaterial{});

    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    YR_EXPECT_TRUE(build.errors.empty());

    yr::BvhTraceStats stats;
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere_index, 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.0f, 1.0e-5);
}

YR_TEST(bvh_intersect_picks_closer_of_sphere_and_triangle) {
    yr::RenderSceneIR scene;

    // Triangle at z = -1 (further from camera at z=3).
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-1.0f, -1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 1.0f, -1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 0.0f,  1.0f, -1.0f}, yr::Vec3f{0.0f, 0.0f, 1.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2};
    scene.primitives.push_back(yr::RenderPrimitive{0, 3, 0, true, false, false});

    // Sphere at z = 0 (closer to camera).
    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);
    scene.materials.push_back(yr::RenderMaterial{});

    const yr::BvhBuildResult build = yr::BuildBvh(scene);
    yr::BvhTraceStats stats;
    const yr::Ray3f ray{yr::Point3f{0.0f, 0.0f, 3.0f}, yr::Vec3f{0.0f, 0.0f, -1.0f}};
    const yr::BvhHit hit = yr::IntersectBvh(scene, build.bvh, ray, stats);

    YR_EXPECT_TRUE(hit.hit);
    YR_EXPECT_EQ(hit.sphere_index, 0);
    YR_EXPECT_EQ(hit.triangle_index, -1);
    YR_EXPECT_NEAR(hit.t, 2.5f, 1.0e-5);
}
```

- [ ] **Step 3: Verify the new tests fail**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: `bvh_intersect_finds_sphere_when_no_triangles` and `bvh_intersect_picks_closer_of_sphere_and_triangle` both fail because spheres aren't intersected yet.

- [ ] **Step 4: Implement sphere intersection in `IntersectBvh`**

In `src/render/bvh.cpp`, at the end of `IntersectBvh` just before `return nearest;` (around line 327), append a linear sphere loop:

```cpp
    // Linear pass over analytic spheres.  M1 keeps spheres out of the BVH because the
    // expected scene-level count is small (≤ ~50); revisit if profiling demands it.
    for (std::size_t si = 0; si < scene.spheres.size(); ++si) {
        const RenderSphere& sphere = scene.spheres[si];
        const SphereHit s = IntersectSphere(sphere.center, sphere.radius, ray, t_min, nearest.t);
        if (!s.hit || s.t >= nearest.t) {
            continue;
        }
        const Point3f hit_point = ray.At(s.t);
        const Vec3f n = SphereNormal(sphere.center, sphere.radius, hit_point);
        const Vec2f uv = SphereUv(n);

        nearest.hit = true;
        nearest.t = s.t;
        nearest.triangle_index = -1;
        nearest.primitive_index = -1;
        nearest.sphere_index = static_cast<int>(si);
        nearest.bary_u = uv.x;
        nearest.bary_v = uv.y;
    }
```

Add `#include <yaoray/render/shading.hpp>` near the top of `src/render/bvh.cpp` so `IntersectSphere`/`SphereNormal`/`SphereUv` are visible.

Note: `Vec2f` uses `.x` / `.y` (not `.u` / `.v`); adjust the call site if your `Vec2f` uses different names.

- [ ] **Step 5: Run tests and verify pass**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: both new sphere-BVH tests pass; previous tests still green.

- [ ] **Step 6: Resolve sphere hits in the CPU surface layer**

In `src/backends/cpu/cpu_surface.cpp`, find `TraceVisibleSurface` (the function that takes a `BvhHit` and turns it into a `CpuSurfaceHit`). It currently assumes `geometry_hit.triangle_index >= 0` and uses `LocateTriangle` / `GeometricNormal` to fill the surface fields. Add a parallel path for `geometry_hit.sphere_index >= 0`.

The implementation must be located in `cpu_surface.cpp` to keep the BVH-to-surface translation in one place. Read the file before editing — the existing structure is the source of truth for parameter names.

A representative sphere-resolution snippet (insert into the visible-surface resolution function, right after the existing miss / `triangle_index < 0` early-out, but before the triangle-only logic):

```cpp
    if (geometry_hit.sphere_index >= 0 &&
        static_cast<std::size_t>(geometry_hit.sphere_index) < scene.spheres.size()) {
        const RenderSphere& sphere = scene.spheres[static_cast<std::size_t>(geometry_hit.sphere_index)];
        const Point3f hit_point = ray.At(geometry_hit.t);
        const Vec3f geometric_normal = SphereNormal(sphere.center, sphere.radius, hit_point);
        const Vec3f shading_normal = sphere.flip_normals
            ? Vec3f{-geometric_normal.x, -geometric_normal.y, -geometric_normal.z}
            : geometric_normal;

        CpuSurfaceHit out;
        out.hit = true;
        out.geometry_hit = geometry_hit;
        out.sample.material = scene.materials[static_cast<std::size_t>(sphere.material_index)];
        out.sample.shading_normal = shading_normal;
        out.sample.alpha = 1.0f;  // Spheres are opaque in M1.
        return out;
    }
```

The exact field names on `CpuSurfaceHit` / `out.sample` come from the surrounding code — match them. Add `#include <yaoray/render/shading.hpp>` if not already included.

- [ ] **Step 7: Add an integration test that a sphere actually renders**

Append to `tests/cpu_path_tracer_tests.cpp`:

```cpp
YR_TEST(cpu_path_tracer_renders_sphere_in_an_emissive_room) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 3;
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    // Diffuse sphere material.
    yr::RenderMaterial diffuse;
    diffuse.kind = yr::RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = yr::Color3f{0.8f, 0.4f, 0.2f};
    scene.materials.push_back(diffuse);

    // Emissive material for a ceiling quad.
    yr::RenderMaterial emissive;
    emissive.kind = yr::RenderMaterialKind::Diffuse;
    emissive.emission = yr::Color3f{5.0f, 5.0f, 5.0f};
    scene.materials.push_back(emissive);

    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);

    // Big emissive ceiling at y = 2.
    scene.vertices = {
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f, -2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{ 2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
        yr::RenderVertex{yr::Point3f{-2.0f, 2.0f,  2.0f}, yr::Vec3f{0.0f, -1.0f, 0.0f}, {}, {}, 1.0f},
    };
    scene.indices = {0, 1, 2,  0, 2, 3};
    scene.primitives.push_back(yr::RenderPrimitive{0, 6, 1, true, false, false});

    yr::EmissivePrimitive ep;
    ep.primitive_index = 0;
    ep.radiance = yr::Color3f{5.0f, 5.0f, 5.0f};
    ep.area = 16.0f;
    scene.emissive_primitives.push_back(ep);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    // The center pixel must see the sphere (not the ceiling and not a miss).
    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
}
```

- [ ] **Step 8: Build + test**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: the new integration test passes; previous tests still pass.

- [ ] **Step 9: Commit**

```bash
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp \
        src/backends/cpu/cpu_surface.cpp \
        tests/bvh_tests.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: ray-sphere hits in BVH walk and CPU surface resolver"
```

---

## Task 4: AnalyticLight "point" sampling math

**Files:**
- Modify: `include/yaoray/render/light_sampling.hpp` (add `AnalyticLightSample` + `SampleAnalyticPoint`)
- Modify: `src/render/light_sampling.cpp` (implement `SampleAnalyticPoint`)
- Create: `tests/analytic_light_tests.cpp`
- Modify: `CMakeLists.txt` (register the new test file)

A point light is a Dirac delta in direction: it has zero solid-angle measure, so its sampled PDF is conceptually infinite and we use it as a delta source (no MIS against BSDF). The sample carries `is_delta = true`, the incident direction `wi = (light_pos − x) / distance`, the radiance reaching `x` from the unoccluded light (`intensity / distance²`), and the segment distance for shadow rays.

- [ ] **Step 1: Declare the sample struct + sampler**

Add to `include/yaoray/render/light_sampling.hpp`:

```cpp
struct AnalyticLightSample {
    Vec3f wi{0.0f, 0.0f, 0.0f};        // Direction from shading point to light.
    float distance = 0.0f;             // Shadow ray segment length.
    Color3f radiance{0.0f, 0.0f, 0.0f}; // Radiance reaching the shading point.
    bool is_delta = true;              // Point/Distant/Spot are deltas; no MIS vs BSDF.
    bool valid = false;
};

AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point);
```

- [ ] **Step 2: Write the failing tests**

Create `tests/analytic_light_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/render/light_sampling.hpp>
#include <yaoray/render/render_scene.hpp>

YR_TEST(sample_analytic_point_returns_direction_and_inverse_square_radiance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{0.0f, 2.0f, 0.0f};
    light.intensity = yr::Color3f{10.0f, 10.0f, 10.0f};

    const yr::AnalyticLightSample s = yr::SampleAnalyticPoint(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.distance, 2.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.y, 1.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.wi.z, 0.0f, 1.0e-5);
    // Radiance = intensity / distance² = 10 / 4 = 2.5.
    YR_EXPECT_NEAR(s.radiance.x, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.y, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.z, 2.5f, 1.0e-5);
}

YR_TEST(sample_analytic_point_returns_invalid_when_zero_distance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{1.0f, 2.0f, 3.0f};
    light.intensity = yr::Color3f{1.0f, 1.0f, 1.0f};
    const yr::AnalyticLightSample s = yr::SampleAnalyticPoint(light, yr::Point3f{1.0f, 2.0f, 3.0f});
    YR_EXPECT_TRUE(!s.valid);
}
```

Register the new test file in `CMakeLists.txt` (add `tests/analytic_light_tests.cpp` to `yaoray_tests`).

- [ ] **Step 3: Verify failure**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: build fails because `SampleAnalyticPoint` and `AnalyticLightSample` are not yet defined.

- [ ] **Step 4: Implement `SampleAnalyticPoint`**

Append to `src/render/light_sampling.cpp`:

```cpp
AnalyticLightSample SampleAnalyticPoint(const AnalyticLight& light, Point3f shading_point) {
    AnalyticLightSample s;
    const Vec3f delta{
        light.position.x - shading_point.x,
        light.position.y - shading_point.y,
        light.position.z - shading_point.z
    };
    const float dist_sq = Dot(delta, delta);
    if (dist_sq <= 0.0f) {
        return s;
    }
    const float dist = std::sqrt(dist_sq);
    const float inv_dist = 1.0f / dist;
    s.wi = Vec3f{delta.x * inv_dist, delta.y * inv_dist, delta.z * inv_dist};
    s.distance = dist;
    s.radiance = Color3f{
        light.intensity.x / dist_sq,
        light.intensity.y / dist_sq,
        light.intensity.z / dist_sq
    };
    s.is_delta = true;
    s.valid = true;
    return s;
}
```

- [ ] **Step 5: Build + test**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: both `sample_analytic_point_*` tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/yaoray/render/light_sampling.hpp src/render/light_sampling.cpp \
        tests/analytic_light_tests.cpp CMakeLists.txt
git commit -m "feat: AnalyticLightSample + SampleAnalyticPoint for point lights"
```

---

## Task 5: Scene compiler — `LightSource "point"` → `AnalyticLight`

**Files:**
- Modify: `src/render/scene_compiler.cpp` (add `CompileAnalyticLights` and call it from `CompilePbrtScene`)
- Modify: `tests/render_scene_tests.cpp` (add tests)

PBRT v4 `LightSource "point"` parameters:
- `from` (point3, default `(0,0,0)`) — position in **light** local space. Transformed by `light_to_world`.
- `I` (rgb spectrum, default `(1,1,1)`) — radiant intensity.
- `scale` (rgb spectrum, default `(1,1,1)`) — multiplier applied to `I`.

We extract `from`, transform it by `light_to_world`, multiply `I` by `scale`, and push into `ir.analytic_lights`.

- [ ] **Step 1: Write the failing test**

Add to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_compiles_lightsource_point) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.camera_transform = yr::Mat4f{};
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    // A point light at (1,2,3) in world space, intensity (10,20,30), scale (0.5,0.5,0.5).
    yr::PbrtLightRecord lr;
    lr.light.type = "point";
    lr.light.params.push_back(yr::PbrtParam{"point3", "from", {1.0f, 2.0f, 3.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "I", {10.0f, 20.0f, 30.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "scale", {0.5f, 0.5f, 0.5f}, {}, {}, {}});
    lr.light_to_world = yr::Mat4f{};   // identity
    pbrt.lights.push_back(lr);

    // Need at least one shape so the empty-scene check passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->analytic_lights.size(), std::size_t{1});

    const yr::AnalyticLight& al = result.scene->analytic_lights[0];
    YR_EXPECT_TRUE(al.kind == yr::AnalyticLightKind::Point);
    YR_EXPECT_NEAR(al.position.x, 1.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.position.y, 2.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.position.z, 3.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.intensity.x, 5.0f,  1.0e-5);   // 10 * 0.5
    YR_EXPECT_NEAR(al.intensity.y, 10.0f, 1.0e-5);   // 20 * 0.5
    YR_EXPECT_NEAR(al.intensity.z, 15.0f, 1.0e-5);   // 30 * 0.5
}
```

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: test fails because `ir.analytic_lights` is still empty after compilation.

- [ ] **Step 3: Implement `CompileAnalyticLights`**

In `src/render/scene_compiler.cpp`, just before `CompilePbrtScene` (around line 414, in the anonymous namespace section), add:

```cpp
Point3f Point3FromParam(const PbrtParam* param, Point3f fallback) {
    if (param == nullptr || param->floats.size() < 3) return fallback;
    return Point3f{param->floats[0], param->floats[1], param->floats[2]};
}

void CompileAnalyticLights(const PbrtScene& scene, RenderSceneIR& ir, std::vector<SceneDiagnostic>& diagnostics) {
    for (const PbrtLightRecord& record : scene.lights) {
        if (record.light.type == "point") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Point;

            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            al.position = TransformPoint(record.light_to_world, from_local);

            const Color3f intensity = RgbParam(FindParam(record.light.params, "I"),
                                               Color3f{1.0f, 1.0f, 1.0f});
            const Color3f scale = RgbParam(FindParam(record.light.params, "scale"),
                                           Color3f{1.0f, 1.0f, 1.0f});
            al.intensity = Color3f{
                intensity.x * scale.x,
                intensity.y * scale.y,
                intensity.z * scale.z
            };
            al.direction = Vec3f{};
            al.cone_angle = 0.0f;

            ir.analytic_lights.push_back(al);
        } else if (record.light.type == "distant" ||
                   record.light.type == "spot" ||
                   record.light.type == "infinite") {
            diagnostics.push_back(Warning(scene, "LightSource",
                "LightSource type '" + record.light.type + "' is not yet supported in M1 Slice 1 and was ignored"));
        } else {
            diagnostics.push_back(Warning(scene, "LightSource",
                "unsupported LightSource type: " + record.light.type));
        }
    }
}
```

Then in `CompilePbrtScene`, add the call right after `CompileInstances` (around line 466):

```cpp
    // 4. Compile object instances
    CompileInstances(scene, material_name_to_index, ir, diagnostics);

    // 5. Compile analytic light sources
    CompileAnalyticLights(scene, ir, diagnostics);   // <-- new

    if (ir.primitives.empty() && ir.spheres.empty()) {
        diagnostics.push_back(Error(scene, "Shape", "scene contains no geometry"));
    }
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: `scene_compiler_compiles_lightsource_point` passes; previous tests still green.

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp tests/render_scene_tests.cpp
git commit -m "feat: compile LightSource \"point\" into RenderSceneIR.analytic_lights"
```

---

## Task 6: Path tracer integrates analytic lights into direct lighting

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp` (extend `EstimateDirectLight`)
- Modify: `tests/cpu_path_tracer_tests.cpp` (add an integration test that a sphere lit by a point light is non-black)

`EstimateDirectLight` already handles emissive primitives and the environment. We add a loop over `scene.analytic_lights`. Point lights are deltas — the standard MIS treatment is to **not** weight them against BSDF sampling (the BSDF can never sample the delta direction). So each analytic light contributes:

```
contribution = bsdf * radiance * cos_surface * shadow_visibility
```

with no `pdf` division and no `mis_weight` multiplier. The standard `delta = true` previous-bounce flag (already used by mirrors and glass in M0) ensures the next bounce treats the BSDF sample as a delta if needed.

- [ ] **Step 1: Write the failing integration test**

Append to `tests/cpu_path_tracer_tests.cpp`:

```cpp
YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_point_light) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 2;  // direct only (no need for indirect to see the light)
    scene.camera.origin = yr::Point3f{0.0f, 0.0f, 3.0f};
    scene.camera.forward = yr::Vec3f{0.0f, 0.0f, -1.0f};
    scene.camera.right = yr::Vec3f{1.0f, 0.0f, 0.0f};
    scene.camera.up = yr::Vec3f{0.0f, 1.0f, 0.0f};
    scene.camera.fov_y_radians = 1.04719758f;

    yr::RenderMaterial diffuse;
    diffuse.kind = yr::RenderMaterialKind::Diffuse;
    diffuse.reflectance.value = yr::Color3f{0.8f, 0.8f, 0.8f};
    scene.materials.push_back(diffuse);

    yr::RenderSphere sphere;
    sphere.center = yr::Point3f{0.0f, 0.0f, 0.0f};
    sphere.radius = 0.5f;
    sphere.material_index = 0;
    scene.spheres.push_back(sphere);

    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Point;
    light.position = yr::Point3f{0.0f, 2.0f, 1.0f};
    light.intensity = yr::Color3f{40.0f, 40.0f, 40.0f};
    scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);

    // The pixel at the sphere center must receive non-zero direct illumination.
    const yr::Color3f center = result.film.LinearPixel(16, 16);
    YR_EXPECT_TRUE(center.x > 0.0f);
    YR_EXPECT_TRUE(center.y > 0.0f);
    YR_EXPECT_TRUE(center.z > 0.0f);
}
```

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: `cpu_path_tracer_lights_a_sphere_with_a_point_light` fails because the analytic-light loop does not exist yet — the sphere renders black.

- [ ] **Step 3: Add the analytic-light loop to `EstimateDirectLight`**

In `src/backends/cpu/cpu_path_tracer.cpp`, locate `EstimateDirectLight` (around line 389) and find the line `radiance = radiance + EstimateDirectEnvironmentLight(...)` near the end (around line 463). Insert the new analytic-light loop **before** that line:

```cpp
    // Analytic (non-area, non-environment) lights — deltas, no MIS vs BSDF.
    for (const AnalyticLight& light : scene.analytic_lights) {
        AnalyticLightSample sample;
        if (light.kind == AnalyticLightKind::Point) {
            sample = SampleAnalyticPoint(light, hit_point);
        } else {
            // Slice 1 only handles Point.  Distant/Spot land in later slices.
            continue;
        }
        if (!sample.valid || IsNearBlack(sample.radiance)) {
            continue;
        }
        const float cos_surface = std::max(0.0f, Dot(normal, sample.wi));
        if (cos_surface <= 0.0f) {
            continue;
        }
        const Color3f bsdf = EvaluateBsdf(material, wo, sample.wi, normal);
        if (IsNearBlack(bsdf)) {
            continue;
        }
        const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
        ++stats.shadow_rays;
        const ShadowVisibility visibility = TraceShadowVisibility(
            prepared_scene,
            Ray3f{shadow_origin, sample.wi},
            sample.distance - SurfaceBias(hit_point),
            stats
        );
        if (!visibility.visible) {
            ++stats.occluded_shadow_rays;
            continue;
        }
        const Color3f visible_radiance = Multiply(visibility.transmittance, sample.radiance);
        radiance = radiance + Multiply(bsdf, visible_radiance) * cos_surface;
    }
```

The reference to `AnalyticLight`, `AnalyticLightKind`, and `SampleAnalyticPoint` needs `#include <yaoray/render/light_sampling.hpp>` and `#include <yaoray/render/render_scene.hpp>` — both already present in this file.

- [ ] **Step 4: Build + test**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: the new integration test passes.

- [ ] **Step 5: Commit**

```bash
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: CPU path tracer samples AnalyticLight point sources"
```

---

## Task 7: Material degradation warning emitter

**Files:**
- Modify: `src/render/scene_compiler.cpp` (route unknown material types through a warning emitter)
- Create: `tests/material_degradation_tests.cpp`
- Modify: `CMakeLists.txt` (register the new test file)

**Scope clarification:** Slice 1 adds **only** the warning emitter and a catch-all fallback to `Diffuse`. The material-specific substitution table (subsurface → diffuse-with-reflectance, measured → conductor, etc.) lands in Slice 4 along with the dining-room integration. After Slice 1, a scene that declares `Material "subsurface" "rgb reflectance" [0.8 0.4 0.2]` parses, compiles to a default-grey Diffuse, and emits a Warning that names the original kind.

- [ ] **Step 1: Read `CompileMaterial` to see how the fall-through currently behaves**

Read `src/render/scene_compiler.cpp` lines ~124–~270 (the body of `CompileMaterial`). Identify the final `else` clause (it currently falls through to the default `RenderMaterial{}` without a diagnostic) — that's the spot we extend.

- [ ] **Step 2: Write the failing test**

Create `tests/material_degradation_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::PbrtScene MinimalSceneWithMaterial(const std::string& mat_type) {
    yr::PbrtScene pbrt;
    pbrt.source_path = "test.pbrt";
    pbrt.source_root = ".";
    pbrt.film.type = "rgb";
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "xresolution", {}, {16}, {}, {}});
    pbrt.film.params.push_back(yr::PbrtParam{"integer", "yresolution", {}, {16}, {}, {}});
    pbrt.camera.type = "perspective";
    pbrt.camera.params.push_back(yr::PbrtParam{"float", "fov", {45.0f}, {}, {}, {}});
    pbrt.integrator.type = "path";
    pbrt.sampler.type = "independent";

    yr::PbrtEntity mat;
    mat.type = mat_type;
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.8f, 0.4f, 0.2f}, {}, {}, {}});
    pbrt.named_materials["test_mat"] = mat;

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.material_name = "test_mat";
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    return pbrt;
}

bool DiagnosticsContain(
    const std::vector<yr::SceneDiagnostic>& diagnostics,
    yr::DiagnosticSeverity severity,
    const std::string& substring
) {
    for (const yr::SceneDiagnostic& d : diagnostics) {
        if (d.severity == severity && d.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

YR_TEST(material_degradation_emits_warning_for_subsurface) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("subsurface");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "subsurface"));
}

YR_TEST(material_degradation_emits_warning_for_unknown_kind) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("totally_made_up_material_name");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "totally_made_up_material_name"));
}

YR_TEST(material_degradation_falls_back_to_diffuse) {
    const yr::PbrtScene pbrt = MinimalSceneWithMaterial("hair");
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->materials.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->materials[0].kind == yr::RenderMaterialKind::Diffuse);
}
```

Register the new test file in `CMakeLists.txt`:

```cmake
add_executable(yaoray_tests
    ...
    tests/material_degradation_tests.cpp   # <-- new
    ...
)
```

- [ ] **Step 3: Verify failure**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: the new tests fail because `CompileMaterial` silently falls through.

- [ ] **Step 4: Implement the warning emitter**

In `src/render/scene_compiler.cpp`, add a helper near the top of the anonymous namespace (after `Warning` around line 30):

```cpp
SceneDiagnostic MaterialFallbackWarning(const PbrtScene& scene, const std::string& declared_kind) {
    return Warning(scene,
        "Material",
        "material kind '" + declared_kind + "' is not supported in M1 Slice 1; falling back to 'diffuse'. "
        "(Slice 4 will replace this catch-all with a per-kind substitution table.)");
}
```

Then locate the bottom of `CompileMaterial` — the final `else` (or equivalent fall-through) that emits no diagnostic today. Replace that branch with:

```cpp
} else {
    diagnostics.push_back(MaterialFallbackWarning(scene, type));
    material.kind = RenderMaterialKind::Diffuse;
    material.reflectance.value = RgbParam(FindParam(params, "reflectance"), Color3f{0.5f, 0.5f, 0.5f});
}
```

If the existing tail of `CompileMaterial` already sets `material.kind = RenderMaterialKind::Diffuse` silently, the change is to **add the diagnostic emission**, not to alter the fallback kind.

- [ ] **Step 5: Build + test**

```bash
cmake --build build --config Release
./build/yaoray_tests
```

Expected: the three `material_degradation_*` tests pass; previous tests stay green.

- [ ] **Step 6: Commit**

```bash
git add src/render/scene_compiler.cpp tests/material_degradation_tests.cpp \
        CMakeLists.txt
git commit -m "feat: warning emitter for unsupported material kinds"
```

---

## Task 8: Cornell box PBRT scene + CTest + README

**Files:**
- Create: `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt`
- Modify: `CMakeLists.txt` (add a CTest entry for the new scene)
- Modify: `README.md` (mention the new scene under runnable examples)

This task wires everything together. The scene contains:
- Cornell box walls (white floor / ceiling / back wall, red left wall, green right wall) as `Shape "trianglemesh"`.
- A mirror sphere on the left half (`Material "conductor"` with low roughness) and a glass sphere on the right half (`Material "dielectric"`) as `Shape "sphere"`.
- One `LightSource "point"` near the ceiling for direct illumination.
- One ceiling `AreaLightSource "diffuse"` quad to keep the scene moderately bright (already supported in M0 — useful as a sanity check that M0 features keep working).

- [ ] **Step 1: Create the scene file**

Create `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt`:

```pbrt
# Cornell box, PBRT v4 style.
#
# YaoRay M1 Slice 1 smoke test: exercises Shape "trianglemesh",
# Shape "sphere", LightSource "point", AreaLightSource "diffuse", and
# the three M0 materials (diffuse, conductor, dielectric).

LookAt 0 1 3.5  0 1 0  0 1 0
Camera "perspective" "float fov" [40]
Sampler "independent" "integer pixelsamples" [64]
Integrator "path" "integer maxdepth" [6]
Film "rgb"
    "integer xresolution" [256]
    "integer yresolution" [256]
    "string filename" ["out/cornell_box_pbrt.png"]

WorldBegin

# --- materials --------------------------------------------------------
MakeNamedMaterial "white"
    "string type" ["diffuse"]
    "rgb reflectance" [0.73 0.73 0.73]

MakeNamedMaterial "red"
    "string type" ["diffuse"]
    "rgb reflectance" [0.65 0.05 0.05]

MakeNamedMaterial "green"
    "string type" ["diffuse"]
    "rgb reflectance" [0.12 0.45 0.15]

MakeNamedMaterial "mirror"
    "string type" ["conductor"]
    "rgb eta" [0.16 0.42 1.37]
    "rgb k"   [3.98 2.41 1.60]
    "float uroughness" [0.02]
    "float vroughness" [0.02]

MakeNamedMaterial "glass"
    "string type" ["dielectric"]
    "float eta" [1.5]

# --- room (white floor / ceiling / back wall) -------------------------
AttributeBegin
NamedMaterial "white"
Shape "trianglemesh"
    "point3 P" [
        -1 0 -1   1 0 -1   1 0 1   -1 0 1
        -1 2 -1   1 2 -1   1 2 1   -1 2 1
        -1 0 -1   1 0 -1   1 2 -1  -1 2 -1
    ]
    "integer indices" [
        0 1 2  0 2 3
        4 6 5  4 7 6
        8 9 10 8 10 11
    ]
AttributeEnd

# --- left wall (red) --------------------------------------------------
AttributeBegin
NamedMaterial "red"
Shape "trianglemesh"
    "point3 P" [-1 0 -1   -1 0 1   -1 2 1   -1 2 -1]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- right wall (green) -----------------------------------------------
AttributeBegin
NamedMaterial "green"
Shape "trianglemesh"
    "point3 P" [1 0 -1   1 2 -1   1 2 1   1 0 1]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- mirror sphere (left) ---------------------------------------------
AttributeBegin
NamedMaterial "mirror"
Translate -0.4 0.35 0.0
Shape "sphere" "float radius" [0.35]
AttributeEnd

# --- glass sphere (right) ---------------------------------------------
AttributeBegin
NamedMaterial "glass"
Translate 0.4 0.35 0.4
Shape "sphere" "float radius" [0.35]
AttributeEnd

# --- ceiling area light (a small bright square) -----------------------
AttributeBegin
AreaLightSource "diffuse" "rgb L" [3 2.8 2.5]
NamedMaterial "white"
Shape "trianglemesh"
    "point3 P" [-0.3 1.99 -0.3   0.3 1.99 -0.3   0.3 1.99 0.3   -0.3 1.99 0.3]
    "integer indices" [0 1 2  0 2 3]
AttributeEnd

# --- point light (an accent fill from behind the camera-right) --------
AttributeBegin
LightSource "point"
    "point3 from" [1.4 1.7 1.5]
    "rgb I"       [3 3 3]
AttributeEnd

WorldEnd
```

Note the camera target `LookAt 0 1 3.5  0 1 0  0 1 0` — the `up` vector and `target` collinearity is a parser oddity; if the renderer produces a singular basis, change to `LookAt 0 1 3.5  0 0.95 0  0 1 0`.

- [ ] **Step 2: Render the scene manually and eyeball the result**

```bash
cmake --build build --config Release
./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu
```

Expected output: console reports `Compiled triangles: > 10`, `Compiled materials: 5`, `Compiled textures: 0`, `Rendered image: ...cornell_box_pbrt.png`, `Hits: > 0`. The output PNG at `scenes/pbrt/cornell_box_pbrt/out/cornell_box_pbrt.png` should look like a Cornell box: red wall left, green wall right, two spheres, soft shadows. If the image is mostly black, the most likely cause is a missing dispatch — re-check Tasks 2 and 5 first.

If the image is not recognizable (overexposed, geometry missing, etc.), iterate on the scene-file parameters (light intensity, sphere positions, camera) before continuing. The result does **not** need to be reference-perfect — Slice 1 only requires "runs, no errors, recognizable image".

- [ ] **Step 3: Add a CTest entry for the new scene**

In `CMakeLists.txt`, after the existing `yaoray_cli_render_pbrt_minimal` test (around line 165–172), add:

```cmake
    add_yaoray_cli_render_test(yaoray_cli_render_pbrt_cornell_box
        SCENE "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt"
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/scenes/pbrt/cornell_box_pbrt/out/cornell_box_pbrt.png"
        BACKEND cpu
        EXPECT_REGEX
            "Integrator: path"
            "Rendered image:"
            "Hits: [^0]"
    )
```

The `"Hits: [^0]"` regex guards against zero-hit regressions like the one that hit Task 11 of M0.

But CTest run-time should not be 256×256, 64 spp (~3 s). Override the scene's settings for the CI run by lowering spp temporarily — the easiest way is to copy the scene to a `*_smoke.pbrt` next to it with `pixelsamples [4]` and `xresolution [64]` `yresolution [64]`. If the existing CLI test helper has a way to override film resolution / spp via CLI args, prefer that and skip the duplicate scene file. (Look at `add_yaoray_cli_render_test` to see what options it exposes; if none, the duplicate-scene approach is simplest.)

Create a fast-rendering variant at `scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt_smoke.pbrt` by copying the main scene and changing `pixelsamples` to `[4]` and `xresolution`/`yresolution` to `[64]`. Point the CTest entry at that file instead. The "full" `cornell_box_pbrt.pbrt` stays for documentation renders.

- [ ] **Step 4: Run the new CTest and verify it passes**

```bash
cd build && ctest --output-on-failure -C Release
```

Expected: `yaoray_cli_render_pbrt_cornell_box` passes (and the existing 5 tests still pass — total 6 tests pass).

- [ ] **Step 5: Lightly update `README.md`**

The README is heavily out of date and a full rewrite is Slice 4's job. For Slice 1, just append a short paragraph under "Run" pointing at the new scene. Find the existing `./build/yaoray.exe render scenes/examples/minimal.toml ...` block (which references deleted TOML files) and **replace** the entire "Run" section's example list with:

```markdown
macOS/Linux:

```bash
./build/yaoray --help
./build/yaoray --version
./build/yaoray render scenes/pbrt/hello_emissive/hello_emissive.pbrt --backend cpu
./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu
```

Windows:

```powershell
build\Debug\yaoray.exe --help
build\Debug\yaoray.exe --version
build\Debug\yaoray.exe render scenes\pbrt\hello_emissive\hello_emissive.pbrt --backend cpu
build\Debug\yaoray.exe render scenes\pbrt\cornell_box_pbrt\cornell_box_pbrt.pbrt --backend cpu
```
```

(Just those two lines per OS. Do not attempt a full README rewrite in this slice — Slice 4 handles it.)

- [ ] **Step 6: Commit**

```bash
git add scenes/pbrt/cornell_box_pbrt/ CMakeLists.txt README.md
git commit -m "feat: cornell_box_pbrt PBRT scene + CTest + README pointer"
```

---

## Wrap-up checklist

After all 8 tasks:

- [ ] `./build/yaoray_tests` — all M0 tests + new sphere / analytic-light / material-degradation / sphere-integration tests pass.
- [ ] `cd build && ctest --output-on-failure -C Release` — `yaoray_tests`, `yaoray_cli_help`, `yaoray_cli_version`, `yaoray_cli_render_help`, `yaoray_cli_render_pbrt_minimal`, `yaoray_cli_render_pbrt_cornell_box` — 6/6.
- [ ] `./build/yaoray render scenes/pbrt/cornell_box_pbrt/cornell_box_pbrt.pbrt --backend cpu` produces a recognizable Cornell box image.
- [ ] `git log --oneline | head -10` shows ~8 atomic commits, each with a single concern.
