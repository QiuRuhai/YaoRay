# YaoRay M1 Slice 4 — `dining-room` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `Shape "trianglemesh"`'s tangent (`S`) pass-through, `LightSource "distant"` and `LightSource "spot"` end-to-end, install a documented material-degradation policy table, and render Benedikt Bitterli's `dining-room` PBRT v4 scene to a "visually close" quality bar (definition in the M1 spec §"Quality Bar").

**Architecture:** Four focused compiler/renderer additions, each independent. (1) `CompileTriangleMeshShape` learns to read the `"normal S"` array and pass it through to `RenderVertex.tangent` plus the per-primitive `has_tangents` flag, mirroring the PLY-mesh path. (2) `SampleAnalyticDistant` joins `SampleAnalyticPoint` (Slice 1) as a delta-light sampler, wired into the path tracer's existing analytic-light loop. (3) `SampleAnalyticSpot` adds cone-falloff multiplied onto a point-style 1/r² emitter. (4) The catch-all material fallback in `CompileMaterial` (introduced as a single Warning emitter in Slice 1) is replaced by a kind-specific substitution table — subsurface keeps its `reflectance`, measured becomes the default conductor, hair is grey diffuse, mix averages its components, deep-layered collapses to `coateddiffuse`. Then a manual asset download + a small smoke fixture + a curated full render finish the milestone.

**Tech Stack:** C++20, CMake 3.24, custom `yr_test.hpp` test harness, CTest. No new third-party libraries. The `dining-room` asset is a one-time download of Bitterli's PBRT-v4 conversion (~10 MB compressed); the unpacked tree lives in `external/assets/pbrt/dining-room/` and is **gitignored** (the existing `.gitignore` already covers `external/assets/`'s descendants via the project's standard ignore rules — confirm in Task 5 and patch `.gitignore` if necessary).

---

## Spec Coverage

This plan implements Slice 4 of `docs/superpowers/specs/2026-05-27-yaoray-m1-dining-room-design.md`:

1. **Trianglemesh `S` (tangent) pass-through** with the existing PLY pattern.
2. **`LightSource "distant"`** — direction transformed via the light's CTM, delta emitter with no MIS weight against BSDF.
3. **`LightSource "spot"`** — point-light radiance × cone falloff between `conedeltaangle` and `coneangle`.
4. **Material degradation policy substitutions** for `subsurface`, `measured`, `hair`, `mix`, and `layered` (depth > 2 — represented in PBRT v4 as nested materials beyond what coateddiffuse/coatedconductor handle).
5. **dining-room** download, smoke + full renders, README + arch-overview refresh.

### Note on facet normals when `N` is missing

The M1 spec says "Default-generate a facet-normal vertex normal only when none was supplied." YaoRay's M0 surface resolver (`src/render/shading.cpp` — `GeometricNormal`) already computes the facet normal on the fly from the triangle's positions when `prim.has_normals == false`, and `cpu_surface.cpp` uses it as the geometric normal for shading. The behavior the spec wants is therefore **already in place**; Task 1 only adds the `S` pass-through. If a later slice (or the user) wants stored per-vertex normals computed at compile time, that work is out of scope here and would benefit from per-vertex normal smoothing (averaging incident face normals) rather than the per-triangle facet copy. Document this in Task 1's commit message.

### Out of scope (deferred to M2+)

- Per-vertex normal smoothing in `CompileTriangleMeshShape` for `N`-missing meshes (M0's facet fallback suffices for Slice 4).
- MikkT-style auto-tangent generation for trianglemeshes that ship `N` but not `S`. (Bitterli's dining-room ships both; if a downstream scene needs tangents but lacks `S`, the normal map silently does nothing — same behavior as Slice 3.)
- IES light profiles, photometric data, blackbody for distant/spot.
- True black-border wrap mode in textures (still degrades to clamp).
- Subdivision surfaces, displacement, hair primitives.

---

## File Structure

**Modified files:**

| Path | Change |
|------|--------|
| `src/render/scene_compiler.cpp` | Add `S` reading to `CompileTriangleMeshShape`. Extend `CompileAnalyticLights` with `"distant"` and `"spot"` branches. Replace catch-all material fallback with the kind-specific substitution table. |
| `include/yaoray/render/light_sampling.hpp` | Declare `SampleAnalyticDistant` and `SampleAnalyticSpot`. |
| `src/render/light_sampling.cpp` | Implement both. |
| `src/backends/cpu/cpu_path_tracer.cpp` | Extend the analytic-light loop in `EstimateDirectLight` to dispatch on `AnalyticLightKind::Distant` / `::Spot`. |
| `tests/analytic_light_tests.cpp` | Add unit tests for distant + spot sampling math (already exists from Slice 1). |
| `tests/render_scene_tests.cpp` | Add compile tests for `LightSource "distant"` / `"spot"`. |
| `tests/material_degradation_tests.cpp` | Replace the Slice 1 "catch-all → diffuse" expectations with the new per-kind table assertions (preserving any tests still relevant). |
| `tests/scene_compiler_tangent_tests.cpp` | NEW — verify trianglemesh tangents land in `RenderVertex.tangent` and `prim.has_tangents`. |
| `CMakeLists.txt` | Register the new test file. The CTest entry for `dining-room` is **not** added (the asset is gitignored; the render is a manual / documentation artifact rather than CI). |
| `README.md` | Add a "Showcase: dining-room" section pointing at the manual workflow. |
| `docs/architecture/overview.md` | Refresh to reflect the post-M1 two-layer pipeline + supported PBRT directive surface. |

**New files:**

| Path | Responsibility |
|------|----------------|
| `tests/scene_compiler_tangent_tests.cpp` | Tangent pass-through unit tests for trianglemesh. |
| `scenes/pbrt/dining_room/README.md` | Step-by-step instructions for downloading the Bitterli asset, where to put it, and how to invoke the render. (The asset itself stays gitignored under `external/assets/pbrt/dining-room/`.) |
| `docs/architecture/overview.md` | Project architecture overview (the file may already exist as a stub; if so, replace; if missing, create.) |

---

## Setting up the worktree

Create an isolated worktree off of local `main` (HEAD `e638621`, post-Slice-3). Use the harness-native `EnterWorktree` tool with name `m1-dining-room` (or have a controller run `superpowers:using-git-worktrees`). After entering the worktree, run `git merge main --ff-only` if needed to pick up the post-Slice-3 commits (the worktree base may default to `origin/main`).

Verify baseline:

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/yaoray_tests.exe       # 159/159 PASS (post-Slice-3 baseline).
cd build && ctest --output-on-failure -C Release  # 8/8 PASS.
```

All commits in this plan land on the worktree branch.

---

## Task 1: Trianglemesh `S` (tangent) pass-through

**Files:**
- Modify: `src/render/scene_compiler.cpp` — extend `CompileTriangleMeshShape` with `S` parameter reading.
- Create: `tests/scene_compiler_tangent_tests.cpp` — verify pass-through.
- Modify: `CMakeLists.txt` — register the new test file.

PBRT v4 trianglemesh supports `"normal S"` (per-vertex tangents). M0's `CompileTriangleMeshShape` reads `P`, `N`, `uv` but ignores `S`. The PlyMesh path already handles tangents — this task mirrors that pattern.

Tangent handedness (the `tangent_handedness` field in `RenderVertex`) is needed for normal mapping. PBRT doesn't carry per-vertex handedness; we default to `+1.0` for all vertices when `S` is provided. This is a reasonable approximation — most exported scenes use right-handed tangent frames consistently.

- [ ] **Step 1: Write the failing test**

Create `tests/scene_compiler_tangent_tests.cpp`:

```cpp
#include "yr_test.hpp"

#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/render/render_scene.hpp>

namespace {

yr::PbrtScene MinimalSceneWithTangentTriangle() {
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

    yr::PbrtShapeRecord record;
    record.shape.type = "trianglemesh";
    record.shape.params.push_back(yr::PbrtParam{
        "point3", "P",
        {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "normal", "N",
        {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "point2", "uv",
        {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{
        "normal", "S",
        {1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f},
        {}, {}, {}
    });
    record.shape.params.push_back(yr::PbrtParam{"integer", "indices", {}, {0, 1, 2}, {}, {}});
    record.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(record);
    return pbrt;
}

} // namespace

YR_TEST(scene_compiler_trianglemesh_S_param_populates_vertex_tangents) {
    const yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->primitives.size(), std::size_t{1});
    YR_EXPECT_TRUE(result.scene->primitives[0].has_tangents);
    YR_EXPECT_EQ(result.scene->vertices.size(), std::size_t{3});
    for (std::size_t vi = 0; vi < 3; ++vi) {
        const yr::Vec3f t = result.scene->vertices[vi].tangent;
        YR_EXPECT_NEAR(t.x, 1.0f, 1.0e-6);
        YR_EXPECT_NEAR(t.y, 0.0f, 1.0e-6);
        YR_EXPECT_NEAR(t.z, 0.0f, 1.0e-6);
        YR_EXPECT_NEAR(result.scene->vertices[vi].tangent_handedness, 1.0f, 1.0e-6);
    }
}

YR_TEST(scene_compiler_trianglemesh_without_S_param_has_no_tangents) {
    yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    // Strip the S param.
    auto& params = pbrt.shapes[0].shape.params;
    params.erase(
        std::remove_if(params.begin(), params.end(), [](const yr::PbrtParam& p) {
            return p.name == "S";
        }),
        params.end()
    );
    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(!result.scene->primitives[0].has_tangents);
}

YR_TEST(scene_compiler_trianglemesh_S_transformed_by_object_to_world) {
    yr::PbrtScene pbrt = MinimalSceneWithTangentTriangle();
    // Set object_to_world to a 90-degree rotation about Y: (1,0,0) -> (0,0,-1).
    yr::Mat4f m{};
    m.m[0] = 0.0f; m.m[2] = -1.0f;
    m.m[8] = 1.0f; m.m[10] = 0.0f;
    pbrt.shapes[0].object_to_world = m;

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(result.scene->primitives[0].has_tangents);
    const yr::Vec3f t = result.scene->vertices[0].tangent;
    YR_EXPECT_NEAR(t.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(t.y, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(t.z, -1.0f, 1.0e-5);
}
```

The test needs `#include <algorithm>` at the top for `std::remove_if`.

Register the new test file in `CMakeLists.txt`:

```cmake
add_executable(yaoray_tests
    ...
    tests/scene_compiler_tangent_tests.cpp   # <-- new
    ...
)
```

- [ ] **Step 2: Run tests, verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the three new tests fail because `CompileTriangleMeshShape` currently ignores `S` and leaves `has_tangents == false`.

- [ ] **Step 3: Implement S pass-through**

In `src/render/scene_compiler.cpp`, locate `CompileTriangleMeshShape`. Just after the UV detection block (around line 235 — search for `if (uv_param == nullptr) uv_param = FindParam(params, "st");`), add the tangent detection block:

```cpp
    // Tangents (PBRT v4 "normal S"). Per-vertex handedness defaults to +1.
    const PbrtParam* s_param = FindParam(params, "S");
    bool has_tangents = (s_param != nullptr && s_param->floats.size() == vertex_count * 3);
```

Inside the per-vertex loop, just after the UV assignment block (search for `if (has_uvs) { v.uv = Vec2f{...}; }`), add:

```cpp
        if (has_tangents) {
            Vec3f tangent{s_param->floats[vi * 3], s_param->floats[vi * 3 + 1], s_param->floats[vi * 3 + 2]};
            v.tangent = TransformVector(record.object_to_world, tangent);
            v.tangent_handedness = 1.0f;
        }
```

In the `RenderPrimitive` initialization block (search for `prim.has_uvs = has_uvs;`), add immediately after:

```cpp
    prim.has_tangents = has_tangents;
```

- [ ] **Step 4: Run tests and verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all 3 new tangent tests pass. Previous tests still pass. New total: 159 + 3 = 162.

- [ ] **Step 5: Commit**

```bash
git add include/yaoray/render/render_scene.hpp \
        src/render/scene_compiler.cpp \
        tests/scene_compiler_tangent_tests.cpp \
        CMakeLists.txt
git commit -m "feat: trianglemesh passes \"normal S\" through to RenderVertex.tangent

Mirrors the PLY-mesh tangent pass-through pattern. Per-vertex
handedness defaults to +1 because PBRT v4 trianglemesh does not carry
per-vertex handedness. When N is missing the surface resolver already
falls back to GeometricNormal (M0 behavior, unchanged)."
```

(The header file is in the `git add` only if you touch it; `RenderVertex` already has the `tangent` and `tangent_handedness` fields from M0, so this is normally unnecessary.)

---

## Task 2: `LightSource "distant"` end-to-end

**Files:**
- Modify: `include/yaoray/render/light_sampling.hpp` — declare `SampleAnalyticDistant`.
- Modify: `src/render/light_sampling.cpp` — implement.
- Modify: `src/render/scene_compiler.cpp` — extend `CompileAnalyticLights` to handle `"distant"`.
- Modify: `src/backends/cpu/cpu_path_tracer.cpp` — dispatch on `AnalyticLightKind::Distant` in the analytic-light loop.
- Modify: `tests/analytic_light_tests.cpp` — sampling math tests.
- Modify: `tests/render_scene_tests.cpp` — compile-side test.

PBRT v4 `LightSource "distant"` parameters:
- `from`, `to` (point3) — the light direction is `from - to`, normalized, then transformed by the light's CTM. (PBRT's convention is unusual: the direction "from" the light source TO the scene. We sample wi = `(to - from)` normalized then negated to point toward the light, OR equivalently sample wi = `(from - to)` normalized.)
- `L` (rgb) — radiance, default `(1,1,1)`.
- `scale` (rgb or float) — multiplier, default `1`.

Distant light is a delta in DIRECTION (not in position). Every point in the scene receives the same incoming direction. Sampling pattern:
- `wi` = unit vector toward the light (the opposite of light's emission direction).
- `distance` = `infinity` (use a very large finite value to keep the shadow ray's `t_max` sensible — e.g. 1e6).
- `radiance` = `L * scale` (irradiance per unit solid angle, but for a Dirac-in-direction delta we just pass it through).
- `is_delta = true`.

The path tracer's existing analytic-light loop (Slice 1) already handles `is_delta` lights with no MIS weight against BSDF — we just need to feed it the right `AnalyticLightSample`.

- [ ] **Step 1: Write failing sampling test**

Append to `tests/analytic_light_tests.cpp`:

```cpp
YR_TEST(sample_analytic_distant_returns_normalized_wi_toward_light_and_infinite_distance) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    // Light shines DOWN the +Y axis (from above to below); wi at any point
    // should point UP toward the light, i.e. +Y.
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{2.0f, 3.0f, 4.0f};

    const yr::AnalyticLightSample s = yr::SampleAnalyticDistant(light, yr::Point3f{1.0f, 2.0f, 3.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.wi.x, 0.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.wi.y, 1.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.wi.z, 0.0f, 1.0e-6);
    YR_EXPECT_TRUE(s.distance > 1.0e5f);
    YR_EXPECT_NEAR(s.radiance.x, 2.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.radiance.y, 3.0f, 1.0e-6);
    YR_EXPECT_NEAR(s.radiance.z, 4.0f, 1.0e-6);
}

YR_TEST(sample_analytic_distant_is_independent_of_shading_point) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{1.0f, 1.0f, 1.0f};

    const yr::AnalyticLightSample s_a = yr::SampleAnalyticDistant(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    const yr::AnalyticLightSample s_b = yr::SampleAnalyticDistant(light, yr::Point3f{10.0f, 20.0f, 30.0f});
    YR_EXPECT_NEAR(s_a.wi.x, s_b.wi.x, 1.0e-6);
    YR_EXPECT_NEAR(s_a.wi.y, s_b.wi.y, 1.0e-6);
    YR_EXPECT_NEAR(s_a.wi.z, s_b.wi.z, 1.0e-6);
}
```

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: build fails (`SampleAnalyticDistant` not declared yet).

- [ ] **Step 3: Declare + implement `SampleAnalyticDistant`**

In `include/yaoray/render/light_sampling.hpp`, append (right after `SampleAnalyticPoint`'s declaration):

```cpp
AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point);
```

In `src/render/light_sampling.cpp`, append the implementation:

```cpp
AnalyticLightSample SampleAnalyticDistant(const AnalyticLight& light, Point3f shading_point) {
    (void)shading_point;  // Distant lights are direction-only; position is irrelevant.
    AnalyticLightSample s;
    // The compiler stores `direction` as the propagation direction of light (from
    // the source toward the scene). At any shading point, wi points back at the
    // light source, i.e. opposite of the propagation direction.
    Vec3f wi{-light.direction.x, -light.direction.y, -light.direction.z};
    if (LengthSquared(wi) == 0.0f) {
        return s;
    }
    s.wi = Normalize(wi);
    s.distance = 1.0e6f;             // Effectively infinite for shadow-ray t_max.
    s.radiance = light.intensity;   // For a Dirac-in-direction delta, this is the
                                     // radiance directly.
    s.is_delta = true;
    s.valid = true;
    return s;
}
```

- [ ] **Step 4: Run sampling tests, verify pass**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the two `sample_analytic_distant_*` tests pass.

- [ ] **Step 5: Write failing compile test**

Append to `tests/render_scene_tests.cpp` (alongside the Slice 1 point-light compile test):

```cpp
YR_TEST(scene_compiler_compiles_lightsource_distant) {
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

    // Distant light shining straight down. PBRT v4 syntax: "from" and "to";
    // direction is (to - from).
    yr::PbrtLightRecord lr;
    lr.light.type = "distant";
    lr.light.params.push_back(yr::PbrtParam{"point3", "from", {0.0f, 1.0f, 0.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"point3", "to",   {0.0f, 0.0f, 0.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "L", {5.0f, 5.0f, 5.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"float", "scale", {0.5f}, {}, {}, {}});
    lr.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(lr);

    // At least one shape so the empty-scene check passes.
    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->analytic_lights.size(), std::size_t{1});

    const yr::AnalyticLight& al = result.scene->analytic_lights[0];
    YR_EXPECT_TRUE(al.kind == yr::AnalyticLightKind::Distant);
    // direction = (to - from) = (0,-1,0), normalized.
    YR_EXPECT_NEAR(al.direction.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.direction.y, -1.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.direction.z, 0.0f, 1.0e-5);
    // intensity = L * scale.
    YR_EXPECT_NEAR(al.intensity.x, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(al.intensity.y, 2.5f, 1.0e-5);
    YR_EXPECT_NEAR(al.intensity.z, 2.5f, 1.0e-5);
}
```

- [ ] **Step 6: Verify failure**

Expected: test fails — `CompileAnalyticLights` currently warns and skips `"distant"`.

- [ ] **Step 7: Implement compile-side dispatch**

In `src/render/scene_compiler.cpp`, find `CompileAnalyticLights`. The `"distant"` branch currently emits a Warning. Replace it with:

```cpp
        } else if (record.light.type == "distant") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Distant;
            // PBRT v4: direction = to - from, then transformed by light_to_world's
            // linear part (translation is irrelevant for a directional light).
            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            const Point3f to_local = Point3FromParam(FindParam(record.light.params, "to"),
                                                     Point3f{0.0f, 0.0f, 1.0f});
            Vec3f dir_local{to_local.x - from_local.x, to_local.y - from_local.y, to_local.z - from_local.z};
            if (LengthSquared(dir_local) == 0.0f) {
                diagnostics.push_back(Warning(scene, "LightSource.distant",
                    "distant light has zero direction; defaulting to (0,-1,0)"));
                dir_local = Vec3f{0.0f, -1.0f, 0.0f};
            }
            const Vec3f dir_world = TransformVector(record.light_to_world, dir_local);
            al.direction = Normalize(dir_world);

            const Color3f L = RgbParam(FindParam(record.light.params, "L"), Color3f{1.0f, 1.0f, 1.0f});
            Color3f scale{1.0f, 1.0f, 1.0f};
            const PbrtParam* scale_param = FindParam(record.light.params, "scale");
            if (scale_param != nullptr) {
                if (scale_param->floats.size() >= 3) {
                    scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
                } else if (!scale_param->floats.empty()) {
                    const float s = scale_param->floats[0];
                    scale = Color3f{s, s, s};
                }
            }
            al.intensity = Color3f{L.x * scale.x, L.y * scale.y, L.z * scale.z};
            al.position = Point3f{};
            al.cone_angle = 0.0f;
            ir.analytic_lights.push_back(al);
```

(Splice this into the existing `else if` chain — it replaces only the `"distant"` arm.)

- [ ] **Step 8: Verify the compile test passes**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: `scene_compiler_compiles_lightsource_distant` passes; baseline still green.

- [ ] **Step 9: Wire distant light into path tracer's direct-light loop**

In `src/backends/cpu/cpu_path_tracer.cpp`, locate the analytic-light loop inside `EstimateDirectLight` (introduced in Slice 1). The current code probably reads something like:

```cpp
        if (light.kind == AnalyticLightKind::Point) {
            sample = SampleAnalyticPoint(light, hit_point);
        } else {
            // Slice 1 only handles Point. Distant/Spot land in later slices.
            continue;
        }
```

Replace the `else { continue; }` with:

```cpp
        } else if (light.kind == AnalyticLightKind::Distant) {
            sample = SampleAnalyticDistant(light, hit_point);
        } else {
            // Spot lands in the next task.
            continue;
        }
```

Don't forget to add `#include <yaoray/render/light_sampling.hpp>` if it's not already at the top.

- [ ] **Step 10: Write an integration test that a sphere is lit by a distant light**

Append to `tests/cpu_path_tracer_tests.cpp` (the file already exists with similar tests from Slice 1):

```cpp
YR_TEST(cpu_path_tracer_lights_a_sphere_with_a_distant_light) {
    yr::RenderSceneIR scene;
    scene.width = 32;
    scene.height = 32;
    scene.spp = 8;
    scene.max_depth = 2;
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

    // Strong distant light from above so the top of the sphere is well-lit.
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Distant;
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};  // propagating down
    light.intensity = yr::Color3f{5.0f, 5.0f, 5.0f};
    scene.analytic_lights.push_back(light);

    const yr::CpuPathTraceResult result = RunPathTrace(std::move(scene));
    YR_EXPECT_TRUE(result.ok);
    YR_EXPECT_TRUE(result.stats.hits > 0);
    // The top of the sphere (pixel near (16, 13)) should be brighter than the
    // bottom (pixel near (16, 19)) under a downward-shining distant light.
    const yr::Color3f top = result.film.LinearPixel(16, 13);
    const yr::Color3f bottom = result.film.LinearPixel(16, 19);
    YR_EXPECT_TRUE(top.x > bottom.x);
}
```

- [ ] **Step 11: Run all tests, verify**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the new integration test passes. Total: 162 + 2 (sampling) + 1 (compile) + 1 (integration) = 166.

- [ ] **Step 12: Commit**

```bash
git add include/yaoray/render/light_sampling.hpp \
        src/render/light_sampling.cpp \
        src/render/scene_compiler.cpp \
        src/backends/cpu/cpu_path_tracer.cpp \
        tests/analytic_light_tests.cpp \
        tests/render_scene_tests.cpp \
        tests/cpu_path_tracer_tests.cpp
git commit -m "feat: LightSource \"distant\" — analytic delta-direction light"
```

---

## Task 3: `LightSource "spot"` end-to-end

**Files:**
- Modify: `include/yaoray/render/light_sampling.hpp` — declare `SampleAnalyticSpot`.
- Modify: `src/render/light_sampling.cpp` — implement.
- Modify: `src/render/scene_compiler.cpp` — extend `CompileAnalyticLights` for `"spot"`.
- Modify: `src/backends/cpu/cpu_path_tracer.cpp` — dispatch on `AnalyticLightKind::Spot`.
- Modify: `tests/analytic_light_tests.cpp`, `tests/render_scene_tests.cpp` — tests.

PBRT v4 `LightSource "spot"`:
- `from` (point3, default `(0,0,0)`) — position.
- `to` (point3, default `(0,0,1)`) — cone axis target. Cone axis = normalize(to - from).
- `I` (rgb) — intensity, default `(1,1,1)`.
- `scale` (rgb or float, default 1).
- `coneangle` (float, degrees, default `30`) — full half-angle of the cone (the outer cutoff).
- `conedeltaangle` (float, degrees, default `5`) — width of the smooth falloff *inside* the cone (between `coneangle - conedeltaangle` and `coneangle`).

Sampling:
- `wi` = unit vector from shading point toward `from`.
- `distance` = distance from shading point to `from`.
- Angle between `-wi` (light → shading) and cone axis `(to - from)` determines falloff. Within the inner cone (angle ≤ `coneangle - conedeltaangle`): full intensity. Within the outer band (between inner and `coneangle`): smooth falloff from 1 to 0 (use smoothstep or a linear ramp; PBRT uses a smoothstep variant). Outside `coneangle`: zero.
- Radiance = `(I * scale / distance²) * falloff`.

`AnalyticLight` already has `position`, `direction` (cone axis), `intensity`, and `cone_angle` fields from M0; we need to store both `coneangle` and `conedeltaangle`. Since `cone_angle` is the only existing slot, store `cos(coneangle)` there. For `conedeltaangle` we'd need either a new field or a packed encoding — adding a `cone_cos_inner` field to `AnalyticLight` is cleanest. Verify the field doesn't already exist; add if missing.

- [ ] **Step 1: Confirm or extend `AnalyticLight` struct**

Read `include/yaoray/render/render_scene.hpp`. Find `AnalyticLight`:

```cpp
struct AnalyticLight {
    AnalyticLightKind kind = AnalyticLightKind::Point;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    float cone_angle = 0.0f;
};
```

This is M0's shape. We need a separate field for the inner cone cosine. Add `cone_cos_inner`. The struct becomes:

```cpp
struct AnalyticLight {
    AnalyticLightKind kind = AnalyticLightKind::Point;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    // For Spot lights: store the cosines of the inner and outer cone half-angles.
    // `cone_angle` stores cos(outer_half_angle); `cone_cos_inner` stores cos(inner).
    float cone_angle = 0.0f;        // = cos(coneangle_radians) for spot
    float cone_cos_inner = 0.0f;    // = cos((coneangle - conedeltaangle)_radians) for spot
};
```

Add the field; existing code that touches `cone_angle` keeps working (Slice 1's point compile doesn't read it).

- [ ] **Step 2: Write failing sampling tests**

Append to `tests/analytic_light_tests.cpp`:

```cpp
YR_TEST(sample_analytic_spot_returns_full_intensity_inside_inner_cone) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};   // shining straight down
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);              // cos(30°)
    light.cone_cos_inner = std::cos(0.4363f);          // cos(25°)

    // Shading point directly below the light.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{0.0f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    YR_EXPECT_TRUE(s.is_delta);
    YR_EXPECT_NEAR(s.distance, 1.0f, 1.0e-5);
    // Inside the inner cone — full intensity / distance² = 4.0.
    YR_EXPECT_NEAR(s.radiance.x, 4.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.y, 4.0f, 1.0e-5);
    YR_EXPECT_NEAR(s.radiance.z, 4.0f, 1.0e-5);
}

YR_TEST(sample_analytic_spot_returns_zero_outside_outer_cone) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);   // 30°
    light.cone_cos_inner = std::cos(0.4363f); // 25°

    // Shading point off to the side, well outside the 30° cone.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{2.0f, 0.0f, 0.0f});
    // Either the sample is invalid (because radiance is zero) or radiance is exactly zero.
    if (s.valid) {
        YR_EXPECT_NEAR(s.radiance.x, 0.0f, 1.0e-5);
    }
}

YR_TEST(sample_analytic_spot_falloff_band_returns_intermediate_value) {
    yr::AnalyticLight light;
    light.kind = yr::AnalyticLightKind::Spot;
    light.position = yr::Point3f{0.0f, 1.0f, 0.0f};
    light.direction = yr::Vec3f{0.0f, -1.0f, 0.0f};
    light.intensity = yr::Color3f{4.0f, 4.0f, 4.0f};
    light.cone_angle = std::cos(0.5236f);     // 30°
    light.cone_cos_inner = std::cos(0.4363f); // 25°

    // Shading point in the falloff band — ~27.5° off the cone axis.
    // tan(27.5°) ≈ 0.521; with y=1 the horizontal offset is 0.521.
    const yr::AnalyticLightSample s = yr::SampleAnalyticSpot(light, yr::Point3f{0.521f, 0.0f, 0.0f});
    YR_EXPECT_TRUE(s.valid);
    // Should be strictly between 0 and the full-intensity peak.
    YR_EXPECT_TRUE(s.radiance.x > 0.0f);
    YR_EXPECT_TRUE(s.radiance.x < 4.0f);
}
```

The implementation needs `<cmath>` for `std::cos`.

- [ ] **Step 3: Verify failure**

Expected: build fails (`SampleAnalyticSpot` not declared) OR tests fail (if the function returns invalid for everything).

- [ ] **Step 4: Declare + implement `SampleAnalyticSpot`**

In `include/yaoray/render/light_sampling.hpp`, after `SampleAnalyticDistant`:

```cpp
AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point);
```

In `src/render/light_sampling.cpp`:

```cpp
AnalyticLightSample SampleAnalyticSpot(const AnalyticLight& light, Point3f shading_point) {
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
    const Vec3f wi = Vec3f{delta.x / dist, delta.y / dist, delta.z / dist};

    // Angle between the light's cone axis (light.direction, propagating from
    // light into scene) and the direction FROM light to shading point (-wi).
    // The cosine of that angle determines the falloff.
    const Vec3f axis = LengthSquared(light.direction) > 0.0f
        ? Normalize(light.direction)
        : Vec3f{0.0f, 0.0f, 1.0f};
    const float cos_angle = -Dot(wi, axis);  // (-wi) . axis

    float falloff;
    if (cos_angle >= light.cone_cos_inner) {
        falloff = 1.0f;
    } else if (cos_angle <= light.cone_angle) {
        falloff = 0.0f;
    } else {
        // Smoothstep between cone_angle (outer cos, lower) and cone_cos_inner (higher).
        const float t = (cos_angle - light.cone_angle) /
                        (light.cone_cos_inner - light.cone_angle);
        falloff = t * t * (3.0f - 2.0f * t);
    }

    s.wi = wi;
    s.distance = dist;
    s.radiance = Color3f{
        light.intensity.x * falloff / dist_sq,
        light.intensity.y * falloff / dist_sq,
        light.intensity.z * falloff / dist_sq
    };
    s.is_delta = true;
    s.valid = true;
    return s;
}
```

- [ ] **Step 5: Run sampling tests, verify pass**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all three `sample_analytic_spot_*` tests pass.

- [ ] **Step 6: Write failing compile test**

Append to `tests/render_scene_tests.cpp`:

```cpp
YR_TEST(scene_compiler_compiles_lightsource_spot) {
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

    yr::PbrtLightRecord lr;
    lr.light.type = "spot";
    lr.light.params.push_back(yr::PbrtParam{"point3", "from", {0.0f, 5.0f, 0.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"point3", "to",   {0.0f, 0.0f, 0.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"rgb", "I", {10.0f, 10.0f, 10.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"float", "coneangle", {30.0f}, {}, {}, {}});
    lr.light.params.push_back(yr::PbrtParam{"float", "conedeltaangle", {5.0f}, {}, {}, {}});
    lr.light_to_world = yr::Mat4f{};
    pbrt.lights.push_back(lr);

    yr::PbrtShapeRecord shape;
    shape.shape.type = "sphere";
    shape.shape.params.push_back(yr::PbrtParam{"float", "radius", {0.5f}, {}, {}, {}});
    shape.object_to_world = yr::Mat4f{};
    pbrt.shapes.push_back(shape);

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_EQ(result.scene->analytic_lights.size(), std::size_t{1});

    const yr::AnalyticLight& al = result.scene->analytic_lights[0];
    YR_EXPECT_TRUE(al.kind == yr::AnalyticLightKind::Spot);
    // position = from = (0, 5, 0)
    YR_EXPECT_NEAR(al.position.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.position.y, 5.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.position.z, 0.0f, 1.0e-5);
    // direction = normalize(to - from) = (0, -1, 0)
    YR_EXPECT_NEAR(al.direction.x, 0.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.direction.y, -1.0f, 1.0e-5);
    YR_EXPECT_NEAR(al.direction.z, 0.0f, 1.0e-5);
    // cone_angle = cos(30°), cone_cos_inner = cos(25°).
    YR_EXPECT_NEAR(al.cone_angle, std::cos(0.5236f), 1.0e-4);
    YR_EXPECT_NEAR(al.cone_cos_inner, std::cos(0.4363f), 1.0e-4);
}
```

- [ ] **Step 7: Verify failure, then implement compile-side**

In `src/render/scene_compiler.cpp`, find the `"spot"` Warning branch in `CompileAnalyticLights` and replace it with:

```cpp
        } else if (record.light.type == "spot") {
            AnalyticLight al;
            al.kind = AnalyticLightKind::Spot;

            const Point3f from_local = Point3FromParam(FindParam(record.light.params, "from"),
                                                       Point3f{0.0f, 0.0f, 0.0f});
            const Point3f to_local = Point3FromParam(FindParam(record.light.params, "to"),
                                                     Point3f{0.0f, 0.0f, 1.0f});
            al.position = TransformPoint(record.light_to_world, from_local);

            Vec3f dir_local{to_local.x - from_local.x, to_local.y - from_local.y, to_local.z - from_local.z};
            if (LengthSquared(dir_local) == 0.0f) {
                diagnostics.push_back(Warning(scene, "LightSource.spot",
                    "spot light has zero direction; defaulting to (0,0,1)"));
                dir_local = Vec3f{0.0f, 0.0f, 1.0f};
            }
            const Vec3f dir_world = TransformVector(record.light_to_world, dir_local);
            al.direction = Normalize(dir_world);

            const Color3f I = RgbParam(FindParam(record.light.params, "I"), Color3f{1.0f, 1.0f, 1.0f});
            Color3f scale{1.0f, 1.0f, 1.0f};
            const PbrtParam* scale_param = FindParam(record.light.params, "scale");
            if (scale_param != nullptr) {
                if (scale_param->floats.size() >= 3) {
                    scale = Color3f{scale_param->floats[0], scale_param->floats[1], scale_param->floats[2]};
                } else if (!scale_param->floats.empty()) {
                    const float s = scale_param->floats[0];
                    scale = Color3f{s, s, s};
                }
            }
            al.intensity = Color3f{I.x * scale.x, I.y * scale.y, I.z * scale.z};

            constexpr float Pi = 3.14159265358979323846f;
            const float cone_deg = FloatParam(FindParam(record.light.params, "coneangle"), 30.0f);
            const float delta_deg = FloatParam(FindParam(record.light.params, "conedeltaangle"), 5.0f);
            const float outer_rad = cone_deg * Pi / 180.0f;
            const float inner_rad = std::max(0.0f, (cone_deg - delta_deg)) * Pi / 180.0f;
            al.cone_angle = std::cos(outer_rad);
            al.cone_cos_inner = std::cos(inner_rad);

            ir.analytic_lights.push_back(al);
```

- [ ] **Step 8: Verify compile test passes**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: `scene_compiler_compiles_lightsource_spot` passes.

- [ ] **Step 9: Wire spot light into path tracer**

In `src/backends/cpu/cpu_path_tracer.cpp`, extend the analytic-light loop dispatch from Task 2:

```cpp
        } else if (light.kind == AnalyticLightKind::Distant) {
            sample = SampleAnalyticDistant(light, hit_point);
        } else if (light.kind == AnalyticLightKind::Spot) {
            sample = SampleAnalyticSpot(light, hit_point);
        } else {
            continue;
        }
```

- [ ] **Step 10: Run all tests**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: 166 + 3 (sampling) + 1 (compile) = 170. No regressions.

- [ ] **Step 11: Commit**

```bash
git add include/yaoray/render/render_scene.hpp \
        include/yaoray/render/light_sampling.hpp \
        src/render/light_sampling.cpp \
        src/render/scene_compiler.cpp \
        src/backends/cpu/cpu_path_tracer.cpp \
        tests/analytic_light_tests.cpp \
        tests/render_scene_tests.cpp
git commit -m "feat: LightSource \"spot\" — analytic spot with cone falloff"
```

---

## Task 4: Material degradation policy table

**Files:**
- Modify: `src/render/scene_compiler.cpp` — replace the catch-all `MaterialFallbackWarning + Diffuse` branch with per-kind substitutions.
- Modify: `tests/material_degradation_tests.cpp` — update assertions to match the new table.

The Slice 1 catch-all (anything unknown → diffuse-grey) is replaced by an explicit table per the M1 spec §3:

| Declared material        | Substitution                                                                              |
| ------------------------ | ----------------------------------------------------------------------------------------- |
| `subsurface`             | `diffuse`, copying `reflectance` from the declared material.                               |
| `measured`               | `conductor` with default `eta` `(0.2, 0.2, 0.2)` and `k` `(1.0, 1.0, 1.0)`.                |
| `hair`                   | `diffuse`, `reflectance` `(0.5, 0.5, 0.5)`.                                                |
| `mix` (two-way)          | `diffuse`, `reflectance` = average of the two component materials' resolved reflectance values. |
| Any other unknown kind   | `diffuse`, `reflectance` `(0.5, 0.5, 0.5)` — catch-all safety net.                          |

Each substitution emits a Warning naming the original kind. The "layered (depth > 2)" case from the spec is degenerate in PBRT v4 syntax — PBRT v4 doesn't expose arbitrary layered nesting via the `Material` directive, so we treat it as a non-issue and only handle the kinds listed above.

The `mix` case is harder: it references two named materials via `"string materials" ["a" "b"]` (PBRT v4 syntax). To compute the average we'd need to resolve "a" and "b" by name. For M1, simplify: read the first material's reflectance if it's a `"texture"` or `"rgb"` value at the top-level `mix` material params (PBRT sometimes flattens this); otherwise default to `(0.5, 0.5, 0.5)`. Document this approximation.

- [ ] **Step 1: Update the existing degradation tests**

Read `tests/material_degradation_tests.cpp`. The Slice 1 tests look something like:

```cpp
YR_TEST(material_degradation_subsurface_falls_back_to_diffuse_grey) {
    // ...
    YR_EXPECT_NEAR(material.reflectance.value.x, 0.5f, 1.0e-6);  // catch-all grey
    YR_EXPECT_TRUE(DiagnosticsContain(diagnostics, ..., "subsurface"));
}
```

Rewrite the Slice 1 tests so the assertions match the new table:

```cpp
YR_TEST(material_degradation_subsurface_keeps_declared_reflectance) {
    // PbrtScene with Material "subsurface" "rgb reflectance" [0.8 0.2 0.1]
    // ... [set up pbrt as in the existing test] ...
    yr::PbrtEntity mat;
    mat.type = "subsurface";
    mat.params.push_back(yr::PbrtParam{"rgb", "reflectance", {0.8f, 0.2f, 0.1f}, {}, {}, {}});
    pbrt.named_materials["s"] = mat;
    pbrt.shapes[0].material_name = "s";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.8f, 1.0e-6);
    YR_EXPECT_NEAR(m.reflectance.value.y, 0.2f, 1.0e-6);
    YR_EXPECT_NEAR(m.reflectance.value.z, 0.1f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "subsurface"));
}

YR_TEST(material_degradation_measured_becomes_default_conductor) {
    // ... [set up pbrt with Material "measured"] ...
    yr::PbrtEntity mat;
    mat.type = "measured";
    pbrt.named_materials["m"] = mat;
    pbrt.shapes[0].material_name = "m";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& mat_out = result.scene->materials[0];
    YR_EXPECT_TRUE(mat_out.kind == yr::RenderMaterialKind::Conductor);
    YR_EXPECT_NEAR(mat_out.eta.value.x, 0.2f, 1.0e-6);
    YR_EXPECT_NEAR(mat_out.k.value.x, 1.0f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "measured"));
}

YR_TEST(material_degradation_hair_becomes_grey_diffuse) {
    yr::PbrtEntity mat;
    mat.type = "hair";
    pbrt.named_materials["h"] = mat;
    pbrt.shapes[0].material_name = "h";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    const yr::RenderMaterial& m = result.scene->materials[0];
    YR_EXPECT_TRUE(m.kind == yr::RenderMaterialKind::Diffuse);
    YR_EXPECT_NEAR(m.reflectance.value.x, 0.5f, 1.0e-6);
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning, "hair"));
}

YR_TEST(material_degradation_unknown_kind_still_emits_catch_all_warning) {
    yr::PbrtEntity mat;
    mat.type = "totally_made_up_kind";
    pbrt.named_materials["x"] = mat;
    pbrt.shapes[0].material_name = "x";

    const yr::SceneCompileResult result = yr::CompilePbrtScene(pbrt);
    YR_EXPECT_TRUE(result.scene.has_value());
    YR_EXPECT_TRUE(DiagnosticsContain(result.diagnostics, yr::DiagnosticSeverity::Warning,
                                       "totally_made_up_kind"));
}
```

Keep the existing `MinimalSceneWithMaterial` helper / similar fixture functions from the Slice 1 test file. Add `<cmath>` for `std::cos` if you reused it from earlier tests.

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: the rewritten tests fail because today's catch-all returns grey regardless of declared kind.

- [ ] **Step 3: Implement the table**

In `src/render/scene_compiler.cpp`, replace the catch-all branch of `CompileMaterial` (the final `else`). The current code looks like:

```cpp
    } else {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    }
```

Replace it with a chain of explicit per-kind substitutions:

```cpp
    } else if (type == "subsurface") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    } else if (type == "measured") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Conductor;
        material.eta.value = Color3f{0.2f, 0.2f, 0.2f};
        material.k.value = Color3f{1.0f, 1.0f, 1.0f};
        material.uroughness.value = 0.0f;
        material.vroughness.value = 0.0f;
    } else if (type == "hair") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = Color3f{0.5f, 0.5f, 0.5f};
    } else if (type == "mix") {
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        // PBRT v4 mix material references two named materials, which we don't
        // resolve recursively in M1. Use the inline-reflectance param if the
        // mix declares one; otherwise default grey.
        material.reflectance = TexParam3fFromParams(params, "reflectance",
            Color3f{0.5f, 0.5f, 0.5f}, bindings, scene, diagnostics);
    } else {
        // Catch-all: any other unrecognized kind.
        diagnostics.push_back(MaterialFallbackWarning(scene, type));
        material.kind = RenderMaterialKind::Diffuse;
        material.reflectance.value = Color3f{0.5f, 0.5f, 0.5f};
    }
```

- [ ] **Step 4: Run tests**

```bash
cmake --build build --config Release
./build/Release/yaoray_tests.exe
```

Expected: all degradation tests pass. Previous tests still pass. Total grows by the number of new + modified tests (4 modified in place from Slice 1).

- [ ] **Step 5: Commit**

```bash
git add src/render/scene_compiler.cpp \
        tests/material_degradation_tests.cpp
git commit -m "feat: documented material degradation table for unsupported kinds

subsurface -> diffuse (preserves reflectance), measured -> default
conductor, hair -> grey diffuse, mix -> approximate diffuse, all with
named Warning diagnostics. Replaces the Slice 1 catch-all that mapped
every unknown kind to grey."
```

---

## Task 5: dining-room integration

**Files:**
- Create: `scenes/pbrt/dining_room/README.md` — manual download instructions.
- Modify: `README.md` (root) — "Showcase" section pointing at dining-room.
- Modify: `docs/architecture/overview.md` — refresh post-M1 architecture description.
- **No CTest entry** — the dining-room asset is gitignored and the render is documentation-only, not CI.

This task is mostly a curation exercise rather than code work. The Bitterli dining-room is downloaded once to `external/assets/pbrt/dining-room/`, rendered at a small smoke spp to confirm the pipeline survives, then debugged + tuned until it produces a clean image at production spp. Any compile / runtime issues that surface here become small fixes routed back into the affected file (parser / compiler / shader).

- [ ] **Step 1: Confirm `external/assets/` is gitignored**

```bash
git check-ignore -v external/assets/anything
```

Expected: a hit on `.gitignore`. If not, add this line to `.gitignore`:

```
external/assets/
```

Commit the change as a separate small commit:

```bash
git add .gitignore
git commit -m "chore: ensure external/assets/ is gitignored"
```

- [ ] **Step 2: Download the Bitterli dining-room asset**

This is a **manual step** the operator runs once. From the worktree root:

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
# Download (~10 MB). The URL pattern is Bitterli's standard.
curl -L -o dining-room.zip "https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip"
# Extract.
unzip dining-room.zip
# Verify the layout — should contain a top-level .pbrt file and asset folders:
ls dining-room/
```

Expected after unzip: a `dining-room/` directory containing the scene's main `.pbrt` file (the exact name varies between Bitterli releases; common ones include `dining-room.pbrt`, `scene.pbrt`, or `breakfast.pbrt`), plus `geometry/`, `textures/`, and possibly `materials/` subdirectories.

If `curl` or `unzip` aren't on the operator's machine, document the equivalent manual download from `https://benedikt-bitterli.me/resources/` (look for the "dining room" entry, click the PBRTv4 link).

- [ ] **Step 3: First smoke render — does the pipeline survive?**

```bash
./build/Release/yaoray.exe render external/assets/pbrt/dining-room/<MAIN_SCENE>.pbrt --backend cpu 2>&1 | head -200
```

Replace `<MAIN_SCENE>` with whatever the top-level `.pbrt` file is named.

Expected: the parser may emit Warnings for unsupported PBRT directives (Halton sampler, Sensor types, etc.) — those are fine. The compiler may emit Warnings for our degradation table (subsurface, measured, etc.). What we want:

- No `Error:` diagnostics (or, if some surface, document them as M2 follow-ups).
- A non-zero `Hits:` count (the rays are reaching the geometry).
- A `Rendered image: ...` line at the end.

**Override the resolution and spp temporarily** to keep this iteration fast. The CLI doesn't currently accept `--xresolution` / `--spp` overrides, so the easiest path is to copy the main `.pbrt` to a `<MAIN_SCENE>_smoke.pbrt` next to it and edit the `Film` / `Sampler` lines to e.g. `xresolution 128 yresolution 128 pixelsamples 4`. The `*_smoke.pbrt` lives inside the gitignored `external/assets/pbrt/dining-room/` so no git tracking is needed.

- [ ] **Step 4: Iterate on issues that surface during the smoke render**

Common categories of issues you may need to address:
- **Unsupported PBRT directive** (e.g., a sampler name we don't know). The parser already emits a Warning and continues; if the warning produces incorrect behavior, look at the parser's `else` branch around line 510 of `pbrt_scene.cpp` and consider whether we need to silently accept this directive or do better.
- **Missing geometry file** (e.g. a `.ply` reference that didn't unpack). Re-extract the zip; verify the path.
- **Materials with an extension we don't support** (e.g., a `displacement` parameter on diffuse). The compiler likely already warns and ignores it; confirm.
- **A spectrum-typed parameter we can't parse as RGB.** The parser's `floats` storage should handle `"spectrum"` types as 3-float RGB; if a particular spectrum literal in dining-room fails to parse, add a small fixup to the parser as needed.

If a regression surfaces that affects the M1 unit tests, fix the root cause in the compiler/parser and add a small test case before continuing.

- [ ] **Step 5: Production render**

Once the smoke render produces a coherent image (no `Error:`, recognizable composition), launch the full render. Use the original (un-edited) `.pbrt`:

```bash
./build/Release/yaoray.exe render external/assets/pbrt/dining-room/<MAIN_SCENE>.pbrt --backend cpu
```

Expected runtime: minutes to tens of minutes depending on resolution and spp declared in the scene file. The output PNG lands wherever the scene's `Film "string filename"` directive points (typically `out/<scene-name>.png` relative to the scene file).

Examine the rendered PNG against the M1 spec's Quality Bar:
- Geometry all present.
- HDRI window light reaches the table.
- Distant / spot lights (if used) cast expected shadows.
- Materials don't fall back silently — every metal/glass surface uses its proper BSDF (verify by spot-checking material declarations in the scene file against the build-log Warnings).
- No fireflies in more than 0.5 % of pixels.
- No NaN / Inf pixels.
- Side-by-side against the Bitterli reference image: no obvious composition / color / energy difference.

If the image fails one or more of these, identify the responsible subsystem (lighting, BSDF, texture) and either fix it in this slice or escalate as an M2 follow-up.

- [ ] **Step 6: Capture a reference render for `README.md`**

Save the production render to `docs/architecture/dining-room.png` (or a similar repo-relative path under `docs/`). The size should be reasonable — if the production render is multi-megabyte, downscale to ~1500px wide via `stb_image_write` or an external tool.

- [ ] **Step 7: Write `scenes/pbrt/dining_room/README.md`**

Create the file:

```markdown
# Dining Room (Bitterli PBRT v4)

YaoRay's M1 anchor scene. This directory intentionally stays empty in git —
the asset is large and licensed CC-BY by Benedikt Bitterli; we link to
his resources instead of redistributing.

## Download

```bash
mkdir -p external/assets/pbrt
cd external/assets/pbrt
curl -L -o dining-room.zip "https://benedikt-bitterli.me/resources/pbrt-v4/dining-room.zip"
unzip dining-room.zip
```

Or visit https://benedikt-bitterli.me/resources/ and grab the PBRTv4 link
manually.

## Render

```bash
# From repo root, after building:
./build/Release/yaoray render external/assets/pbrt/dining-room/<MAIN_SCENE>.pbrt --backend cpu
```

Replace `<MAIN_SCENE>` with the top-level `.pbrt` filename in the unpacked
asset (commonly `scene.pbrt` or `dining-room.pbrt`).

The output PNG lands at the path declared in the scene's `Film "string
filename"` directive, typically `out/<name>.png` next to the scene file.

## Expected output

A lit dining room with the table set for breakfast under window light.
See `docs/architecture/dining-room.png` for the M1 reference render.
```

- [ ] **Step 8: Update `README.md` (root)**

Append a "Showcase" section that references the new render and the dining-room workflow. Keep it short — the canonical instructions live in `scenes/pbrt/dining_room/README.md`.

- [ ] **Step 9: Refresh `docs/architecture/overview.md`**

Replace the file's contents with a current snapshot of the post-M1 architecture:

```markdown
# YaoRay Architecture Overview

YaoRay is a physically-based offline path tracer that consumes PBRT v4
scene files and produces HDR images via a multi-threaded CPU backend.
A CUDA backend is planned for M2+.

## Two-Layer Pipeline

```text
PBRT v4 scene  ──CompilePbrtScene──▶  RenderSceneIR  ──Backend.Prepare──▶  Renderable
   (.pbrt)                              (flat tables)                       (BVH + buffers)
```

The PBRT layer parses a `.pbrt` file into `PbrtScene` — a faithful
representation of the directives and parameters as written. The render
layer consumes `PbrtScene` and emits `RenderSceneIR`, a flat,
GPU-friendly layout: indexed vertices, primitives, materials, textures,
and lights.

## Supported PBRT v4 Surface (M1)

**Geometry:** `trianglemesh` (P / N / uv / S), `plymesh`, `sphere`.

**Materials:** `diffuse` / `matte`, `conductor` / `metal`, `dielectric` /
`glass`, `thindielectric`, `coateddiffuse`, `coatedconductor`,
`diffusetransmission`. Texture binding via `"texture <param>" ["name"]`
on `reflectance`, `eta`, `k`, `uroughness`, `vroughness`, and the
coating-layer parameters. Normal maps via `"string normalmap"`.

**Materials with documented degradation:** `subsurface` (→ diffuse with
declared reflectance), `measured` (→ default conductor), `hair` (→ grey
diffuse), `mix` (→ approximate diffuse). Each emits a named Warning at
compile time.

**Lights:** `LightSource "infinite"` (HDRI environment with importance
sampling), `LightSource "point"`, `LightSource "distant"`, `LightSource
"spot"`, `AreaLightSource "diffuse"`.

**Textures:** `Texture "imagemap"` (PNG / JPEG / HDR), `Texture
"constant"`. Wrap modes: `repeat` and `clamp` (`black` degrades to
clamp). Color space auto-detected, explicit `"encoding"` overrides.

## Backend

Single-threaded reference renderer (`src/backends/cpu/cpu_debug_renderer.cpp`)
plus a production multi-threaded path tracer
(`src/backends/cpu/cpu_path_tracer.cpp`) with:

- Surface-area-heuristic BVH for trianglemeshes.
- Analytic sphere primitives intersected linearly outside the BVH.
- Multiple importance sampling combining BSDF, area-light, and
  environment-light samples.
- Delta-light handling for point, distant, and spot.
- Russian-roulette path termination.
- ACES, Reinhard, and identity tone mappers.

## What's not in M1

- Spectral rendering (RGB only).
- Volumetrics / media.
- Adaptive sampling.
- CUDA backend.
- IES light profiles.
- Subdivision and displacement.
- Sampler alternatives (Halton / Sobol / PMJ02BN).
```

- [ ] **Step 10: Final commit**

```bash
git add scenes/pbrt/dining_room/README.md \
        README.md \
        docs/architecture/overview.md \
        docs/architecture/dining-room.png
git commit -m "docs: dining-room workflow + post-M1 architecture overview"
```

---

## Wrap-up checklist

After all 5 tasks:

- [ ] `./build/Release/yaoray_tests.exe` — unit-test count grows by ~12-14 tests (Task 1: 3, Task 2: 4, Task 3: 4, Task 4: 4 rewritten in place). Total ≈ 170-172 PASS.
- [ ] `cd build && ctest --output-on-failure -C Release` — still **8/8** (no new CTest entry — dining-room is not in CI).
- [ ] `./build/Release/yaoray.exe render external/assets/pbrt/dining-room/<MAIN>.pbrt --backend cpu` produces an image meeting the M1 spec's Quality Bar §"Quality Bar (Operational Definition of "Visually Close")".
- [ ] A reference render lives at `docs/architecture/dining-room.png`.
- [ ] `git log --oneline | head -8` shows 5 atomic commits (one per task), plus the optional `.gitignore` touch from Task 5 Step 1.
- [ ] Local `main` has not been touched — all commits live on the slice's worktree branch.

After verification, the controller invokes `superpowers:finishing-a-development-branch` to merge the slice into local `main`. Per the user's instructions, the four M1 slices are pushed to `origin/main` together once Slice 4 lands — see the post-M1 PR step.
