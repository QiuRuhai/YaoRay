# YaoRay Glass Shadows v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transparent and colored direct-light shadow visibility for dielectric glass in the CPU path tracer.

**Architecture:** Keep this slice local to the CPU path tracer: introduce one internal `TraceShadowVisibility()` helper that returns colored transmittance instead of binary occlusion, then call it from both area-light and HDRI direct-light estimators. Reuse existing render material fields and Beer-Lambert helpers; do not add scene schema, caustic transport, nested medium stacks, or CUDA code.

**Tech Stack:** C++20, existing YaoRay test harness (`yaoray_tests` via CTest), CPU path tracer, TOML examples and docs.

---

## File Structure

- Modify `tests/cpu_path_tracer_tests.cpp`: add deterministic direct-light shadow fixtures and tests for opaque blockers, clear glass, absorbing thick glass, thin glass, HDRI direct-light glass visibility, and mixed glass-plus-opaque blockers.
- Modify `src/backends/cpu/cpu_path_tracer.cpp`: add `ShadowVisibility`, transparent shadow tracing helpers, and replace duplicated binary shadow checks in `EstimateDirectLight()` and `EstimateDirectEnvironmentLight()`.
- Modify `README.md`: mention transparent/colored direct shadows and keep caustics/nested media/CUDA as future work.
- Modify `docs/architecture/overview.md`: document the shared shadow visibility helper and its straight-line approximation.
- Modify `docs/superpowers/specs/2026-05-21-yaoray-glass-shadows-v1-design.md`: append implementation status after the feature is complete.

Do not modify or stage the existing unrelated dirty files:

- `docs/superpowers/specs/2026-05-21-yaoray-dielectric-material-pack-v3-design.md`
- `scenes/examples/assets/gltf/Duck/`
- `scenes/examples/duck_gltf.toml`

## Pre-Flight

- [ ] **Step 1: Confirm current branch and dirty state**

Run:

```powershell
git -c core.fsmonitor=false -c core.untrackedCache=false status --short --branch --untracked-files=normal
```

Expected: branch is `main`; unrelated dirty Duck/glTF files and the older dielectric spec may be present. Do not revert or stage them.

- [ ] **Step 2: Build and test baseline**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build succeeds and `yaoray_tests` passes before feature changes.

---

### Task 1: Add Area-Light Transparent Shadow Tests

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`

- [ ] **Step 1: Add deterministic shadow fixture helpers**

In `tests/cpu_path_tracer_tests.cpp`, add these helpers after `MakeBlockedDiffuseFloorScene()` and before `MakeDiffuseToExplicitEmitterScene()`:

```cpp
void AddHorizontalQuadOccluder(yr::RenderScene& scene, float y, int material_index) {
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, 20.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        material_index
    });
    scene.triangles.push_back(yr::RenderTriangle{
        yr::Point3f{-20.0f, y, -20.0f},
        yr::Point3f{20.0f, y, 20.0f},
        yr::Point3f{-20.0f, y, 20.0f},
        yr::Vec3f{0.0f, -1.0f, 0.0f},
        material_index
    });
}

yr::RenderMaterial MakeShadowGlassMaterial(
    bool thin,
    yr::Color3f albedo = yr::Color3f{1.0f, 1.0f, 1.0f},
    yr::Color3f absorption_color = yr::Color3f{1.0f, 1.0f, 1.0f},
    float absorption_distance = 1.0f
) {
    yr::RenderMaterial material;
    material.type = yr::MaterialKind::Dielectric;
    material.albedo = albedo;
    material.ior = 1.5f;
    material.roughness = 0.0f;
    material.thin = thin;
    material.absorption_color = absorption_color;
    material.absorption_distance = absorption_distance;
    return material;
}

yr::RenderScene MakeAreaShadowSceneWithPane(const yr::RenderMaterial& pane_material) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(pane_material);
    AddHorizontalQuadOccluder(scene, 1.0f, 1);
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeAreaShadowSceneWithSlab(const yr::RenderMaterial& slab_material) {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(slab_material);
    AddHorizontalQuadOccluder(scene, 0.75f, 1);
    AddHorizontalQuadOccluder(scene, 1.25f, 1);
    RebuildBvh(scene);
    return scene;
}

yr::RenderScene MakeAreaShadowSceneWithPaneAndOpaqueBlocker() {
    yr::RenderScene scene = MakeDiffuseFloorScene(7);
    scene.spp = 1;
    scene.max_depth = 1;
    scene.threads = 1;
    scene.light_samples = 8;
    scene.materials.push_back(MakeShadowGlassMaterial(true));
    scene.materials.push_back(yr::RenderMaterial{
        yr::MaterialKind::Diffuse,
        yr::Color3f{1.0f, 1.0f, 1.0f},
        yr::Color3f{}
    });
    AddHorizontalQuadOccluder(scene, 0.75f, 1);
    AddHorizontalQuadOccluder(scene, 1.25f, 2);
    RebuildBvh(scene);
    return scene;
}
```

- [ ] **Step 2: Add failing area-light behavior tests**

Add these tests after the existing `cpu_path_tracer_counts_shadow_occlusion_and_dims_direct_light` test:

```cpp
YR_TEST(cpu_path_tracer_opaque_shadow_occluder_still_blocks_direct_light) {
    const yr::CpuPathTraceResult open_result = yr::RenderCpuPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult blocked_result = yr::RenderCpuPathTrace(MakeBlockedDiffuseFloorScene());

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float blocked_luminance = Luminance(blocked_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(blocked_luminance < open_luminance);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}

YR_TEST(cpu_path_tracer_clear_glass_pane_transmits_area_light_shadow) {
    const yr::CpuPathTraceResult open_result = yr::RenderCpuPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult glass_result =
        yr::RenderCpuPathTrace(MakeAreaShadowSceneWithPane(MakeShadowGlassMaterial(true)));

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float glass_luminance = Luminance(glass_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(glass_luminance > open_luminance * 0.8f);
    YR_EXPECT_EQ(glass_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_absorbing_glass_slab_tints_area_light_shadow) {
    const yr::CpuPathTraceResult clear_result =
        yr::RenderCpuPathTrace(MakeAreaShadowSceneWithSlab(MakeShadowGlassMaterial(false)));
    const yr::CpuPathTraceResult tinted_result = yr::RenderCpuPathTrace(MakeAreaShadowSceneWithSlab(
        MakeShadowGlassMaterial(false, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{0.25f, 0.70f, 1.0f}, 0.5f)
    ));

    const yr::Color3f clear = clear_result.film.LinearPixel(1, 1);
    const yr::Color3f tinted = tinted_result.film.LinearPixel(1, 1);

    YR_EXPECT_TRUE(clear.x > 0.0f);
    YR_EXPECT_TRUE(tinted.x < clear.x * 0.8f);
    YR_EXPECT_TRUE(tinted.y < clear.y);
    YR_EXPECT_TRUE(tinted.z > tinted.x * 1.5f);
    YR_EXPECT_TRUE(tinted.z > tinted.y);
    YR_EXPECT_EQ(tinted_result.stats.occluded_shadow_rays, std::uint64_t{0});
}

YR_TEST(cpu_path_tracer_thin_glass_shadow_ignores_absorption_distance) {
    const yr::CpuPathTraceResult neutral_result =
        yr::RenderCpuPathTrace(MakeAreaShadowSceneWithPane(MakeShadowGlassMaterial(true)));
    const yr::CpuPathTraceResult absorbing_result = yr::RenderCpuPathTrace(MakeAreaShadowSceneWithPane(
        MakeShadowGlassMaterial(true, yr::Color3f{1.0f, 1.0f, 1.0f}, yr::Color3f{0.1f, 0.2f, 1.0f}, 0.25f)
    ));

    const yr::Color3f neutral = neutral_result.film.LinearPixel(1, 1);
    const yr::Color3f absorbing = absorbing_result.film.LinearPixel(1, 1);

    YR_EXPECT_NEAR(absorbing.x, neutral.x, 1e-5);
    YR_EXPECT_NEAR(absorbing.y, neutral.y, 1e-5);
    YR_EXPECT_NEAR(absorbing.z, neutral.z, 1e-5);
}

YR_TEST(cpu_path_tracer_glass_then_opaque_shadow_still_blocks_area_light) {
    const yr::CpuPathTraceResult open_result = yr::RenderCpuPathTrace(MakeDiffuseFloorScene(7));
    const yr::CpuPathTraceResult blocked_result = yr::RenderCpuPathTrace(MakeAreaShadowSceneWithPaneAndOpaqueBlocker());

    const float open_luminance = Luminance(open_result.film.LinearPixel(1, 1));
    const float blocked_luminance = Luminance(blocked_result.film.LinearPixel(1, 1));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(blocked_luminance < open_luminance);
    YR_EXPECT_TRUE(blocked_result.stats.occluded_shadow_rays > 0);
}
```

- [ ] **Step 3: Verify area-light tests fail for transparent glass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build succeeds. The clear glass, absorbing slab, and thin glass shadow tests fail because current binary shadow rays still treat glass as occluding. Opaque blocker tests may already pass.

---

### Task 2: Add Shared Transparent Shadow Visibility For Area Lights

**Files:**
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add include for finite/infinite shadow distances**

In `src/backends/cpu/cpu_path_tracer.cpp`, add `<limits>` with the other standard includes:

```cpp
#include <limits>
```

- [ ] **Step 2: Add shadow visibility helper types**

In the anonymous namespace, after `PathMediumState`, add:

```cpp
constexpr int MaxTransparentShadowHits = 16;

struct ShadowVisibility {
    bool visible = true;
    Color3f transmittance{1.0f, 1.0f, 1.0f};
};
```

- [ ] **Step 3: Add shadow transmittance helpers**

After `ApplyMediumAttenuation()`, add:

```cpp
bool IsShadowTransmittanceBlack(Color3f color) {
    return MaxComponent(color) <= AbsorptionEpsilon;
}

float ClampTransmittanceChannel(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

Color3f ClampTransmittance(Color3f value) {
    return Color3f{
        ClampTransmittanceChannel(value.x),
        ClampTransmittanceChannel(value.y),
        ClampTransmittanceChannel(value.z)
    };
}

bool IsShadowTransparentMaterial(const RenderMaterial& material) {
    return material.type == MaterialKind::Dielectric;
}

Color3f ThinGlassShadowTransmittance(const RenderMaterial& material) {
    return ClampTransmittance(material.albedo);
}

void ToggleShadowMedium(PathMediumState& medium, const RenderMaterial& material) {
    if (medium.active) {
        medium = PathMediumState{};
        return;
    }
    medium.active = true;
    medium.absorption_color = material.absorption_color;
    medium.absorption_distance = material.absorption_distance;
}
```

- [ ] **Step 4: Add `TraceShadowVisibility()`**

After `DirectLightSampleCount()` and before `SurviveRussianRoulette()`, add:

```cpp
ShadowVisibility TraceShadowVisibility(
    const RenderScene& scene,
    Ray3f ray,
    float max_distance,
    CpuPathTraceStats& stats
) {
    ShadowVisibility visibility;
    PathMediumState medium;
    float remaining_distance = max_distance;

    for (int transparent_hit_count = 0; transparent_hit_count < MaxTransparentShadowHits; ++transparent_hit_count) {
        BvhTraceStats shadow_trace;
        const BvhHit hit = IntersectBvh(scene, ray, shadow_trace);
        AccumulateTraceStats(stats, shadow_trace);

        const bool finite_segment = std::isfinite(remaining_distance);
        if (!hit.hit || hit.triangle == nullptr || (finite_segment && hit.t >= remaining_distance)) {
            if (medium.active) {
                if (!finite_segment) {
                    return ShadowVisibility{false, Color3f{}};
                }
                visibility.transmittance = Multiply(
                    visibility.transmittance,
                    BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, remaining_distance)
                );
            }
            visibility.visible = !IsShadowTransmittanceBlack(visibility.transmittance);
            return visibility;
        }

        if (medium.active) {
            visibility.transmittance = Multiply(
                visibility.transmittance,
                BeerLambertTransmittance(medium.absorption_color, medium.absorption_distance, hit.t)
            );
            if (IsShadowTransmittanceBlack(visibility.transmittance)) {
                return ShadowVisibility{false, Color3f{}};
            }
        }

        if (!IsValidMaterialIndex(scene, hit.triangle->material_index)) {
            return ShadowVisibility{false, Color3f{}};
        }

        const RenderTriangle& triangle = *hit.triangle;
        const RenderMaterial& base_material = scene.materials[static_cast<std::size_t>(triangle.material_index)];
        const Point3f hit_point = ray.At(hit.t);
        const RenderMaterial material = ResolveHitMaterial(scene, triangle, base_material, hit_point);
        if (!IsShadowTransparentMaterial(material)) {
            return ShadowVisibility{false, Color3f{}};
        }

        if (material.thin) {
            visibility.transmittance = Multiply(visibility.transmittance, ThinGlassShadowTransmittance(material));
            if (IsShadowTransmittanceBlack(visibility.transmittance)) {
                return ShadowVisibility{false, Color3f{}};
            }
        } else {
            ToggleShadowMedium(medium, material);
        }

        const float bias = SurfaceBias(hit_point);
        if (finite_segment) {
            remaining_distance -= hit.t + bias;
            if (remaining_distance <= 0.0f) {
                visibility.visible = !IsShadowTransmittanceBlack(visibility.transmittance);
                return visibility;
            }
        }
        ray = Ray3f{hit_point + ray.direction * bias, ray.direction};
    }

    return ShadowVisibility{false, Color3f{}};
}
```

- [ ] **Step 5: Replace area-light binary shadow check**

Inside `EstimateDirectLight()`, replace this block:

```cpp
++stats.shadow_rays;
BvhTraceStats shadow_trace;
const Ray3f shadow_ray{shadow_origin, wi};
const BvhHit shadow_hit = IntersectBvh(scene, shadow_ray, shadow_trace);
AccumulateTraceStats(stats, shadow_trace);
if (shadow_hit.hit && shadow_hit.t < shadow_distance - shadow_bias) {
    ++stats.occluded_shadow_rays;
    continue;
}
```

with:

```cpp
++stats.shadow_rays;
const Ray3f shadow_ray{shadow_origin, wi};
const ShadowVisibility visibility =
    TraceShadowVisibility(scene, shadow_ray, shadow_distance - shadow_bias, stats);
if (!visibility.visible) {
    ++stats.occluded_shadow_rays;
    continue;
}
```

Then replace:

```cpp
light_radiance = light_radiance + Multiply(bsdf, sample->radiance) * (cos_surface * mis_weight / pdf_light);
```

with:

```cpp
const Color3f visible_radiance = Multiply(visibility.transmittance, sample->radiance);
light_radiance = light_radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / pdf_light);
```

- [ ] **Step 6: Verify area-light tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: new area-light transparent shadow tests pass. Existing tests should still pass except possible HDRI transparent shadow tests, which have not been added yet.

- [ ] **Step 7: Commit area-light transparent shadow support**

Run:

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add transparent area light shadows"
```

---

### Task 3: Add HDRI Direct-Light Transparent Shadow Coverage

**Files:**
- Modify: `tests/cpu_path_tracer_tests.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Add HDRI transparent occluder helper**

In `tests/cpu_path_tracer_tests.cpp`, after `MakeDiffusePlaneUnderHdriScene()`, add:

```cpp
yr::RenderScene MakeDiffusePlaneUnderHdriWithGlassOccluder(bool thin) {
    yr::RenderScene scene = MakeDiffusePlaneUnderHdriScene(false);
    scene.materials.push_back(MakeShadowGlassMaterial(thin));
    AddHorizontalQuadOccluder(scene, 0.5f, 1);
    RebuildBvh(scene);
    return scene;
}
```

- [ ] **Step 2: Add failing HDRI direct-light test**

Add this test after `cpu_path_tracer_occluder_blocks_direct_hdri_light`:

```cpp
YR_TEST(cpu_path_tracer_clear_glass_pane_transmits_hdri_direct_light_shadow) {
    const yr::CpuPathTraceResult open_result = yr::RenderCpuPathTrace(MakeDiffusePlaneUnderHdriScene(false));
    const yr::CpuPathTraceResult glass_result =
        yr::RenderCpuPathTrace(MakeDiffusePlaneUnderHdriWithGlassOccluder(true));

    const float open_luminance = Luminance(open_result.film.LinearPixel(0, 0));
    const float glass_luminance = Luminance(glass_result.film.LinearPixel(0, 0));

    YR_EXPECT_TRUE(open_luminance > 0.0f);
    YR_EXPECT_TRUE(glass_luminance > open_luminance * 0.8f);
    YR_EXPECT_EQ(glass_result.stats.occluded_shadow_rays, std::uint64_t{0});
}
```

- [ ] **Step 3: Verify HDRI test fails**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: build succeeds. `cpu_path_tracer_clear_glass_pane_transmits_hdri_direct_light_shadow` fails because `EstimateDirectEnvironmentLight()` still uses binary shadow visibility.

- [ ] **Step 4: Replace HDRI binary shadow check**

Inside `EstimateDirectEnvironmentLight()`, replace this block:

```cpp
const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
++stats.shadow_rays;
BvhTraceStats shadow_trace;
const BvhHit shadow_hit = IntersectBvh(scene, Ray3f{shadow_origin, wi}, shadow_trace);
AccumulateTraceStats(stats, shadow_trace);
if (shadow_hit.hit) {
    ++stats.occluded_shadow_rays;
    continue;
}
```

with:

```cpp
const Point3f shadow_origin = hit_point + normal * SurfaceBias(hit_point);
++stats.shadow_rays;
const ShadowVisibility visibility =
    TraceShadowVisibility(scene, Ray3f{shadow_origin, wi}, std::numeric_limits<float>::infinity(), stats);
if (!visibility.visible) {
    ++stats.occluded_shadow_rays;
    continue;
}
```

Then replace:

```cpp
radiance = radiance + Multiply(bsdf, sample.radiance) * (cos_surface * mis_weight / sample.pdf_solid_angle);
```

with:

```cpp
const Color3f visible_radiance = Multiply(visibility.transmittance, sample.radiance);
radiance = radiance + Multiply(bsdf, visible_radiance) * (cos_surface * mis_weight / sample.pdf_solid_angle);
```

- [ ] **Step 5: Verify all path tracer tests pass**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R yaoray_tests
```

Expected: `yaoray_tests` passes.

- [ ] **Step 6: Commit HDRI transparent shadow support**

Run:

```powershell
git add src/backends/cpu/cpu_path_tracer.cpp tests/cpu_path_tracer_tests.cpp
git commit -m "feat: add transparent environment shadows"
```

---

### Task 4: Verify Showcase And Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture/overview.md`
- Modify: `docs/superpowers/specs/2026-05-21-yaoray-glass-shadows-v1-design.md`

- [ ] **Step 1: Run glass showcase targeted tests before deciding whether to edit the scene**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -R "yaoray_cli_render_glass_showcase|yaoray_cli_render_glass_showcase_visual_sanity"
```

Expected: both tests pass. If either test fails, stop and inspect the rendered output and failure message before changing thresholds or scene content.

- [ ] **Step 2: Update README path tracer description**

In `README.md`, update the path tracer paragraph so the first sentence includes transparent colored shadow visibility:

```markdown
`render.integrator = "path"` selects the CPU path tracer with diffuse bounce, perfect mirror reflection, smooth/rough/thin dielectric glass, Beer-Lambert absorption for thick dielectric paths, transparent colored direct shadows through dielectric glass, deterministic sampling, random or stratified XZ-rectangle area-light surface sampling, area-light MIS, MIS-weighted BSDF-sampled emissive hits, HDRI environment lookup, HDRI direct environment sampling through a luminance-weighted equirectangular distribution, BSDF-to-environment MIS, fixed-policy Russian roulette path termination with `render.max_depth` as a hard limit, diffuse texture sampling, optional sample radiance clamping through `render.radiance_clamp`, and tile-threaded CPU execution controlled by `render.threads` (`0` auto, `1` single-thread reference, `N` fixed worker count).
```

Keep the existing follow-up sentences about `render.sampler`, `render.light_samples`, and CLI stats.

- [ ] **Step 3: Update README limitations**

In `README.md`, ensure the limitations paragraph still says caustics are not implemented:

```markdown
Dielectric materials do not yet include nested medium stacks, caustic-specific sampling, glTF glass extension import, or CUDA parity.
```

- [ ] **Step 4: Update architecture overview**

In `docs/architecture/overview.md`, update the CPU path tracer paragraph to mention transparent direct-light shadow visibility:

```markdown
`path` is the CPU path tracer: it adds deterministic multi-sample camera jitter, diffuse and glossy bounce sampling, perfect mirror scattering, dielectric glass scattering, Beer-Lambert absorption for one active thick dielectric medium, transparent colored direct-light visibility through dielectric glass, emissive hits, random or stratified XZ-rectangle area-light sampling, direct area-light MIS, HDRI environment lookup, HDRI direct environment sampling through a luminance-weighted equirectangular distribution, BSDF-to-environment MIS, Russian roulette, diffuse texture sampling, optional sample radiance clamping, and row-major tile threading.
```

Keep the rest of the paragraph unchanged.

- [ ] **Step 5: Update architecture direct-light paragraph**

In `docs/architecture/overview.md`, replace the direct-light MIS paragraph with:

```markdown
Direct-light MIS is split into render-level helpers and CPU path tracer orchestration. `yaoray_render` owns `PowerHeuristic()`, current XZ-rectangle area-light sampling and solid-angle PDF math, plus HDRI environment evaluation, equirectangular mapping, importance distribution, sampling, and PDF helpers. The CPU path tracer owns random sample generation, shadow visibility, previous-bounce state, and path throughput. Shadow visibility is a straight-line transmittance query: opaque materials block, while dielectric glass can transmit and tint direct area-light or environment-light samples without producing true refractive caustics.
```

- [ ] **Step 6: Append implementation status to Glass Shadows spec**

At the end of `docs/superpowers/specs/2026-05-21-yaoray-glass-shadows-v1-design.md`, append:

```markdown
## Implementation Status

Implemented in Glass Shadows v1:

- Shared CPU path tracer shadow visibility helper for area-light and HDRI direct-light samples.
- Straight-line transparent direct shadows through dielectric materials.
- Beer-Lambert tinting for thick dielectric shadow segments.
- Thin-glass surface transmittance without thickness absorption.
- Opaque fallback for non-dielectric blockers and transparent-hit guard failures.

Remaining limitations:

- No refracted shadow rays or true caustics.
- No nested medium stack or overlapping medium correctness.
- No glTF transmission/volume import.
- No CUDA parity.
```

- [ ] **Step 7: Run documentation keyword check**

Run:

```powershell
rg -n "transparent|colored|shadow|Beer-Lambert|caustic|nested medium|CUDA parity" README.md docs\architecture\overview.md docs\superpowers\specs\2026-05-21-yaoray-glass-shadows-v1-design.md
```

Expected: docs mention transparent/colored shadows as implemented and still list caustics, nested media, glTF import, and CUDA parity as future work.

- [ ] **Step 8: Commit docs**

Run:

```powershell
git add README.md docs/architecture/overview.md docs/superpowers/specs/2026-05-21-yaoray-glass-shadows-v1-design.md
git commit -m "docs: document glass shadows v1"
```

---

### Task 5: Full Verification

**Files:**
- No planned edits unless verification exposes a defect in files already touched by this plan.

- [ ] **Step 1: Run full build**

Run:

```powershell
cmake --build build --config Debug
```

Expected: build succeeds.

- [ ] **Step 2: Run full test suite**

Run:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

Expected: all tests pass, including `yaoray_tests`, CLI render tests, and glass showcase visual sanity.

- [ ] **Step 3: Run manual glass showcase render**

Run:

```powershell
.\build\yaoray.exe render .\scenes\examples\glass_showcase.toml --backend cpu
```

Expected: exits `0`; output includes `Integrator: path`, `Rendered image: scenes/examples/out/glass_showcase.png`, `Samples/sec:`, and `Rays/sec:`.

- [ ] **Step 4: Inspect final git state**

Run:

```powershell
git -c core.fsmonitor=false -c core.untrackedCache=false status --short --branch --untracked-files=normal
git -c core.fsmonitor=false -c core.untrackedCache=false log --oneline -6
```

Expected: feature commits are on the current branch. Only unrelated pre-existing dirty files remain unstaged.

## Final Handoff

- [ ] **Step 1: Summarize implemented behavior**

Mention:

- Area-light direct shadows now use colored transparent visibility.
- HDRI direct shadows now use the same helper.
- Thick glass shadow segments use Beer-Lambert tinting.
- Thin glass uses surface transmittance only.
- Opaque materials still block.
- Verification commands and results.

- [ ] **Step 2: Ask whether to push**

If the implementation ran inline on `main`, ask before pushing unless the user explicitly says `merge and push`.
