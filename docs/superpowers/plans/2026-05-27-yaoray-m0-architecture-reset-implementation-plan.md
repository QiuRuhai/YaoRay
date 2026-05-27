# M0 Architecture Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the structural migration from the three-layer TOML+PBRT architecture to a two-layer PBRT-only architecture (PbrtScene → RenderSceneIR → Backend), with no rendering regression on the existing PBRT test scene.

**Architecture:** The parser outputs `PbrtScene` (parameterized scene description with string references). A new scene compiler resolves references, loads PLY meshes, and produces `RenderSceneIR` with PBRT-aligned material types, unified table geometry, and emissive primitives for area lights. The CPU backend adapts BVH construction and path tracing to use table geometry.

**Tech Stack:** C++20, CMake 3.24+, MSVC on Windows 11, stb_image for textures

**Spec:** `docs/superpowers/specs/2026-05-27-yaoray-pbrt-architecture-reset-design.md`

**Note on compilability:** This is a large architectural reset. The project will not compile between Tasks 2–9. Full compilation resumes at the end of Task 10. This is by design — the reset changes interconnected types across every module.

---

## File Structure

### New files

| File | Responsibility |
|------|---------------|
| `include/yaoray/core/diagnostic.hpp` | Diagnostic types (migrated from scene/) |
| `src/core/diagnostic.cpp` | Diagnostic implementation |
| `include/yaoray/core/transform.hpp` | Mat4f and transform utilities (shared by parser + compiler) |
| `src/core/transform.cpp` | Mat4f implementation |
| `include/yaoray/pbrt/pbrt_scene.hpp` | PbrtScene data model (PbrtParam, PbrtEntity, PbrtShapeRecord, etc.) |

### Rewritten files (major changes)

| File | What changes |
|------|-------------|
| `include/yaoray/render/render_scene.hpp` | New RenderMaterialKind, TexParam1f/3f, RenderMaterial, EmissivePrimitive; remove RenderTriangle, RenderAreaLight, old MaterialKind |
| `include/yaoray/render/scene_compiler.hpp` | New signature: `CompilePbrtScene(const PbrtScene&)` |
| `src/render/scene_compiler.cpp` | Full rewrite: consume PbrtScene |
| `src/pbrt/pbrt_scene.cpp` | Refactor: output PbrtScene instead of SceneWorld |
| `include/yaoray/render/bvh.hpp` | Remove RenderTriangle references, use table geometry |
| `src/render/bvh.cpp` | Build from vertices/indices/primitives |
| `include/yaoray/render/shading.hpp` | Remove RenderTriangle params, use table geometry lookups |
| `src/render/shading.cpp` | Table geometry barycentric/UV/normal interpolation |
| `include/yaoray/render/light_sampling.hpp` | EmissivePrimitive sampling instead of RenderAreaLight |
| `src/render/light_sampling.cpp` | Triangle surface point sampling |
| `src/render/bsdf.cpp` | New material kind dispatch |
| `include/yaoray/backends/cpu/cpu_material.hpp` | Remove RenderTriangle param |
| `src/backends/cpu/cpu_material.cpp` | Table geometry material resolution |
| `src/backends/cpu/cpu_surface.cpp` | Table geometry surface tracing |
| `src/backends/cpu/cpu_prepared_scene.cpp` | BVH build from table geometry |
| `src/backends/cpu/cpu_path_tracer.cpp` | Adapt to new material/light types |
| `src/app/main.cpp` | Direct PbrtScene pipeline, remove SceneWorld |
| `CMakeLists.txt` | Remove old modules, update sources |

### Deleted files

| File | Reason |
|------|--------|
| `include/yaoray/scene/scene.hpp` | Enums migrate to render_scene.hpp |
| `include/yaoray/scene/scene_parser.hpp` | TOML parser |
| `include/yaoray/scene/scene_world.hpp` | SceneWorld layer |
| `include/yaoray/scene/diagnostic.hpp` | Migrated to core/ |
| `src/scene/scene.cpp` | Enums migrate |
| `src/scene/scene_parser.cpp` | TOML parser |
| `src/scene/scene_world.cpp` | SceneWorld layer |
| `src/scene/diagnostic.cpp` | Migrated to core/ |
| `include/yaoray/assets/asset_resource.hpp` | No longer needed |
| `include/yaoray/assets/obj_loader.hpp` | OBJ removed |
| `include/yaoray/assets/gltf_loader.hpp` | glTF removed |
| `src/assets/obj_loader.cpp` | OBJ removed |
| `src/assets/gltf_loader.cpp` | glTF removed |
| `include/yaoray/frontends/scene_frontend.hpp` | Frontends removed |
| `src/frontends/scene_frontend.cpp` | Frontends removed |
| `scenes/examples/*.toml` | TOML scenes |

### Migrated files

| File | Destination |
|------|------------|
| `src/assets/ply_loader.cpp` | stays in-place, `yaoray_pbrt` links it |
| `include/yaoray/assets/ply_loader.hpp` | stays in-place, `yaoray_pbrt` includes it |

---

### Task 1: Core Infrastructure — Diagnostics and Transform

**Files:**
- Create: `include/yaoray/core/diagnostic.hpp`
- Create: `src/core/diagnostic.cpp`
- Create: `include/yaoray/core/transform.hpp`
- Create: `src/core/transform.cpp`

- [ ] **Step 1: Create `include/yaoray/core/diagnostic.hpp`**

Copy the diagnostic types from `include/yaoray/scene/diagnostic.hpp` to the new location:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace yr {

enum class DiagnosticSeverity {
    Error,
    Warning,
};

struct SceneDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::filesystem::path file;
    std::string field;
    std::string message;
};

bool HasSceneErrors(const std::vector<SceneDiagnostic>& diagnostics);
std::string FormatSceneDiagnostic(const SceneDiagnostic& diagnostic);
std::string FormatSceneDiagnostics(const std::vector<SceneDiagnostic>& diagnostics);

} // namespace yr
```

- [ ] **Step 2: Create `src/core/diagnostic.cpp`**

Copy the implementation from `src/scene/diagnostic.cpp` to the new location. Update the include to `#include <yaoray/core/diagnostic.hpp>`.

- [ ] **Step 3: Create `include/yaoray/core/transform.hpp`**

Extract the `Mat4f` type and transform utilities used by both the parser and compiler. Both `pbrt_scene.cpp` and `scene_compiler.cpp` currently define their own local `Mat4` + `Multiply` + `TransformPoint` + etc. Unify these into a shared header:

```cpp
#pragma once

#include <array>
#include <yaoray/core/vec.hpp>

namespace yr {

struct Mat4f {
    std::array<float, 16> m{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

Mat4f Multiply(Mat4f a, Mat4f b);
Point3f TransformPoint(Mat4f transform, Point3f point);
Vec3f TransformVector(Mat4f transform, Vec3f value);
Vec3f TransformNormal(Mat4f transform, Vec3f normal);

Mat4f TranslationMatrix(Vec3f translation);
Mat4f ScaleMatrix(Vec3f scale);
Mat4f RotationAxisMatrix(float angle_degrees, Vec3f axis);
Mat4f LookAtMatrix(Point3f eye, Point3f target, Vec3f up);

} // namespace yr
```

- [ ] **Step 4: Create `src/core/transform.cpp`**

Implement all transform functions. Port the implementations from the current `pbrt_scene.cpp` (lines 46–109) and `scene_compiler.cpp` (lines 52–109). These are the same math — `Multiply`, `TransformPoint`, `TransformVector`, `TransformNormal`, `TranslationMatrix`, `ScaleMatrix`, `RotationAxisMatrix`. Add `LookAtMatrix` for PBRT `LookAt` handling.

- [ ] **Step 5: Commit**

```
git add include/yaoray/core/diagnostic.hpp src/core/diagnostic.cpp include/yaoray/core/transform.hpp src/core/transform.cpp
git commit -m "refactor: extract diagnostics and transform to yaoray_core"
```

---

### Task 2: PbrtScene Data Model

**Files:**
- Create: `include/yaoray/pbrt/pbrt_scene.hpp`

- [ ] **Step 1: Create `include/yaoray/pbrt/pbrt_scene.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/core/transform.hpp>
#include <yaoray/core/vec.hpp>

namespace yr {

struct PbrtParam {
    std::string type;
    std::string name;
    std::vector<float> floats;
    std::vector<int> ints;
    std::vector<std::string> strings;
    std::vector<bool> bools;
};

struct PbrtEntity {
    std::string type;
    std::vector<PbrtParam> params;
};

struct PbrtShapeRecord {
    PbrtEntity shape;
    std::string material_name;
    std::optional<PbrtEntity> inline_material;
    std::optional<PbrtEntity> area_light;
    Mat4f object_to_world;
};

struct PbrtLightRecord {
    PbrtEntity light;
    Mat4f light_to_world;
};

struct PbrtObjectInstance {
    std::string name;
    Mat4f instance_to_world;
};

struct PbrtScene {
    std::filesystem::path source_path;
    std::filesystem::path source_root;

    PbrtEntity camera;
    PbrtEntity sampler;
    PbrtEntity integrator;
    PbrtEntity film;
    PbrtEntity filter;
    Mat4f camera_transform;

    std::unordered_map<std::string, PbrtEntity> named_materials;
    std::unordered_map<std::string, PbrtEntity> named_textures;
    std::vector<PbrtShapeRecord> shapes;
    std::vector<PbrtLightRecord> lights;

    std::unordered_map<std::string, std::vector<PbrtShapeRecord>> object_definitions;
    std::vector<PbrtObjectInstance> instances;
};

struct PbrtSceneLoadResult {
    std::optional<PbrtScene> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

PbrtSceneLoadResult LoadPbrtScene(const std::filesystem::path& path);

} // namespace yr
```

- [ ] **Step 2: Commit**

```
git add include/yaoray/pbrt/pbrt_scene.hpp
git commit -m "feat: add PbrtScene data model"
```

---

### Task 3: Render Types Rewrite

**Files:**
- Rewrite: `include/yaoray/render/render_scene.hpp`
- Modify: `include/yaoray/render/texture.hpp`

This task replaces the old type system. After this task, downstream code (BVH, BSDF, backends) will not compile until they are updated in subsequent tasks.

- [ ] **Step 1: Rewrite `include/yaoray/render/render_scene.hpp`**

Replace the entire file with the new PBRT-aligned types. Key changes:
- `MaterialKind` → `RenderMaterialKind` with PBRT v4 types
- Old `RenderMaterial` → new struct with `TexParam1f`/`TexParam3f` fields
- Remove `RenderTriangle` and `RenderAreaLight`
- Update `RenderVertex` (remove per-vertex boolean flags)
- Update `RenderPrimitive` (add per-primitive boolean flags)
- Add `EmissivePrimitive`
- Move render enums here (RenderBackendKind, RenderIntegratorKind, etc.) — these were in `scene/scene.hpp`
- Add `AnalyticLightKind` and `AnalyticLight` for future use

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/texture.hpp>

namespace yr {

// --- Render enums (migrated from scene/scene.hpp) ---

enum class RenderBackendKind { Cpu, Cuda };
enum class RenderIntegratorKind { DebugDirect, Path };
enum class RenderSamplerKind { Independent, Stratified };
enum class ToneMapperKind { None, Reinhard, Aces };

std::string_view RenderBackendName(RenderBackendKind backend);
std::optional<RenderBackendKind> ParseRenderBackendName(std::string_view name);
std::string_view RenderIntegratorName(RenderIntegratorKind integrator);
std::optional<RenderIntegratorKind> ParseRenderIntegratorName(std::string_view name);
std::string_view RenderSamplerName(RenderSamplerKind sampler);
std::optional<RenderSamplerKind> ParseRenderSamplerName(std::string_view name);
std::string_view ToneMapperName(ToneMapperKind mapper);
std::optional<ToneMapperKind> ParseToneMapperName(std::string_view name);

// --- Camera ---

struct RenderCamera {
    Point3f origin;
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f right{1.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    float fov_y_radians = 0.785398185f;
};

// --- Environment ---

struct RenderEnvironment {
    bool active = false;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};

struct RenderEnvironmentDistribution {
    int width = 0;
    int height = 0;
    std::vector<float> texel_weights;
    std::vector<float> row_weights;
    std::vector<float> row_cdf;
    std::vector<float> conditional_cdfs;
    float total_weight = 0.0f;
    bool uniform = false;
};

// --- Materials ---

enum class RenderMaterialKind {
    Diffuse,
    Conductor,
    Dielectric,
    ThinDielectric,
    CoatedDiffuse,
    CoatedConductor,
    DiffuseTransmission,
    Mix,
};

struct TexParam1f {
    float value = 0.0f;
    int texture = -1;
};

struct TexParam3f {
    Color3f value{0.0f, 0.0f, 0.0f};
    int texture = -1;
};

struct RenderMaterial {
    RenderMaterialKind kind = RenderMaterialKind::Diffuse;

    TexParam3f reflectance{{0.5f, 0.5f, 0.5f}};

    TexParam3f eta;
    TexParam3f k;

    float ior = 1.5f;

    TexParam1f uroughness{0.0f};
    TexParam1f vroughness{0.0f};
    bool remap_roughness = true;

    int mix_material_a = -1;
    int mix_material_b = -1;
    TexParam1f mix_amount{0.5f};

    float coating_ior = 1.5f;
    TexParam1f coating_roughness{0.0f};

    int normal_map = -1;
    float normal_scale = 1.0f;

    Color3f emission{0.0f, 0.0f, 0.0f};

    TexParam1f alpha{1.0f};

    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};

// --- Geometry ---

struct RenderVertex {
    Point3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent;
    float tangent_handedness = 1.0f;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool has_tangents = false;
};

// --- Lights ---

struct EmissivePrimitive {
    int primitive_index = 0;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float area = 0.0f;
};

enum class AnalyticLightKind { Point, Spot, Distant };

struct AnalyticLight {
    AnalyticLightKind kind = AnalyticLightKind::Point;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    float cone_angle = 0.0f;
};

// --- Film settings ---

struct FilmSettings {
    std::filesystem::path output;
    ToneMapperKind tone_mapper = ToneMapperKind::Aces;
    float exposure = 0.0f;
};

// --- Complete Render Scene IR ---

struct RenderSceneIR {
    RenderBackendKind requested_backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::Path;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;
    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    float radiance_clamp = 0.0f;

    RenderCamera camera;

    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;

    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;

    std::vector<EmissivePrimitive> emissive_primitives;
    RenderEnvironment environment;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<AnalyticLight> analytic_lights;

    FilmSettings film;
};

} // namespace yr
```

- [ ] **Step 2: Update `include/yaoray/render/texture.hpp`**

Add `RenderTextureKind` enum and `constant_value` field to `RenderTexture`:

```cpp
enum class RenderTextureKind {
    Image,
    Constant,
    Scale,
    Checkerboard,
};
```

Add `RenderTextureKind kind = RenderTextureKind::Image;` and `Color4f constant_value;` fields to the existing `RenderTexture` struct. Keep all existing fields and functions.

- [ ] **Step 3: Create `src/render/render_scene.cpp`**

Implement the enum name/parse functions (port from `src/scene/scene.cpp`). The new functions use `RenderBackendKind`, `RenderIntegratorKind`, `RenderSamplerKind`, `ToneMapperKind` — same logic, just moved here.

- [ ] **Step 4: Commit**

```
git add include/yaoray/render/render_scene.hpp include/yaoray/render/texture.hpp src/render/render_scene.cpp
git commit -m "refactor: PBRT-aligned render types"
```

---

### Task 4: PBRT Parser Rewrite

**Files:**
- Rewrite: `include/yaoray/pbrt/pbrt_scene.hpp` (add LoadPbrtScene impl declaration — already done)
- Rewrite: `src/pbrt/pbrt_scene.cpp`

- [ ] **Step 1: Rewrite `src/pbrt/pbrt_scene.cpp` to output PbrtScene**

The current parser (~935 lines) outputs to `SceneWorld`. Refactor it to output `PbrtScene` instead. The tokenizer, parameter reading, and transform stack handling remain the same. Key changes:

1. Replace all `#include` of scene/scene_world headers with `#include <yaoray/pbrt/pbrt_scene.hpp>`
2. Use `Mat4f` from `yaoray/core/transform.hpp` instead of local `Mat4`
3. Remove all local transform function definitions (they're now in `transform.cpp`)
4. Replace parser state:
   - Old: `SceneWorld world; CameraDescription camera; MaterialDescription current_material;`
   - New: `PbrtScene scene; std::string current_material_name; std::optional<PbrtEntity> current_area_light;`
5. For `Camera "perspective"`: store as `state.scene.camera = PbrtEntity{"perspective", params};`
6. For `Film "rgb"`: store as `state.scene.film = PbrtEntity{"rgb", params};`
7. For `LookAt`: compute and store `state.scene.camera_transform`
8. For `MakeNamedMaterial`: store as `state.scene.named_materials[name] = PbrtEntity{type, params};`
9. For `Material`: store as inline material on the current state
10. For `NamedMaterial`: set `state.current_material_name = name;`
11. For `AreaLightSource`: store as `state.current_area_light = PbrtEntity{type, params};`
12. For `Shape`: create `PbrtShapeRecord{PbrtEntity{shape_type, params}, current_material_name, current_inline_material, current_area_light, current_transform}` and push to `state.scene.shapes`
13. For `Texture`: store in `state.scene.named_textures`
14. For `Include`: recursive parse into same PbrtScene (already works similarly)

The function signature changes from:
```cpp
SceneWorldLoadResult LoadPbrtSceneFile(const std::filesystem::path& path);
```
to:
```cpp
PbrtSceneLoadResult LoadPbrtScene(const std::filesystem::path& path);
```

- [ ] **Step 2: Commit**

```
git add src/pbrt/pbrt_scene.cpp include/yaoray/pbrt/pbrt_scene.hpp
git commit -m "refactor: PBRT parser outputs PbrtScene"
```

---

### Task 5: Scene Compiler Rewrite

**Files:**
- Rewrite: `include/yaoray/render/scene_compiler.hpp`
- Rewrite: `src/render/scene_compiler.cpp`

- [ ] **Step 1: Rewrite `include/yaoray/render/scene_compiler.hpp`**

```cpp
#pragma once

#include <optional>
#include <vector>

#include <yaoray/core/diagnostic.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct SceneCompileResult {
    std::optional<RenderSceneIR> scene;
    std::vector<SceneDiagnostic> diagnostics;
};

SceneCompileResult CompilePbrtScene(const PbrtScene& scene);

} // namespace yr
```

- [ ] **Step 2: Rewrite `src/render/scene_compiler.cpp`**

The compiler resolves the `PbrtScene` into a `RenderSceneIR`. Key responsibilities:

**Helper: param lookup functions**
```cpp
const PbrtParam* FindParam(const std::vector<PbrtParam>& params, const std::string& name);
float FloatParam(const PbrtParam* param, float fallback);
Color3f RgbParam(const PbrtParam* param, Color3f fallback);
std::string StringParam(const PbrtParam* param, const std::string& fallback);
int IntParam(const PbrtParam* param, int fallback);
```

**Film/camera/integrator resolution:**
- `scene.film` → extract `xresolution`, `yresolution`, `filename` → set `ir.width`, `ir.height`, `ir.film.output`
- `scene.camera` → extract `fov`, apply `scene.camera_transform` → set `ir.camera`
- `scene.integrator` → extract type string → map to `RenderIntegratorKind`
- `scene.sampler` → extract type → map to `RenderSamplerKind`
- Extract `maxdepth`, `pixelsamples` from integrator/sampler params

**Material compilation:**
- Iterate `scene.named_materials`
- Map PBRT material type strings to `RenderMaterialKind`:
  - `"matte"` or `"diffuse"` → `Diffuse`, read `"reflectance"` or `"Kd"` RGB param → `reflectance`
  - `"conductor"` or `"metal"` → `Conductor`, read `"eta"`, `"k"`, `"roughness"` params
  - `"dielectric"` or `"glass"` → `Dielectric`, read `"eta"` float param → `ior`
  - Unknown type → warning diagnostic, default to Diffuse
- Build material name → index map for shape references

**Shape compilation (trianglemesh):**
- For each `PbrtShapeRecord` where `shape.type == "trianglemesh"`:
  - Extract `"point3 P"` → positions, `"integer indices"` → indices
  - Optional: `"normal N"` → normals, `"float uv"` or `"point2 uv"` → UVs
  - Apply `object_to_world` transform to positions and normals
  - Create `RenderPrimitive` referencing into the scene-wide vertex/index buffers
  - Resolve material: look up `material_name` in the name→index map
  - If `area_light` is present: compute triangle areas, create `EmissivePrimitive` entries with `AreaLightSource` radiance

**Shape compilation (plymesh):**
- For each `PbrtShapeRecord` where `shape.type == "plymesh"`:
  - Extract `"string filename"` param
  - Resolve path relative to `scene.source_root`
  - Call `LoadPlyAsset()` from `yaoray/assets/ply_loader.hpp`
  - Convert PLY positions/normals/UVs to `RenderVertex` entries
  - Apply `object_to_world` transform
  - Create `RenderPrimitive`, resolve material and area light same as trianglemesh

**Object instancing:**
- For each `PbrtObjectInstance`: look up `object_definitions[name]`, expand all shapes with the instance transform composed with each shape's local transform

- [ ] **Step 3: Commit**

```
git add include/yaoray/render/scene_compiler.hpp src/render/scene_compiler.cpp
git commit -m "feat: scene compiler consumes PbrtScene"
```

---

### Task 6: BVH and Shading — Table Geometry

**Files:**
- Rewrite: `include/yaoray/render/bvh.hpp`
- Rewrite: `src/render/bvh.cpp`
- Rewrite: `include/yaoray/render/shading.hpp`
- Rewrite: `src/render/shading.cpp`

- [ ] **Step 1: Rewrite `include/yaoray/render/bvh.hpp`**

Remove all `RenderTriangle` references. `BvhHit` now stores a flat triangle index instead of a triangle pointer:

```cpp
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include <yaoray/core/bounds.hpp>
#include <yaoray/core/ray.hpp>

namespace yr {

struct RenderSceneIR;

enum class BvhSplitMethod { LongestAxisMedian };

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
    int total_triangles = 0;
};

struct BvhTraceStats {
    std::uint64_t node_tests = 0;
    std::uint64_t triangle_tests = 0;
};

struct BvhHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    int triangle_index = -1;
    int primitive_index = -1;
    float bary_u = 0.0f;
    float bary_v = 0.0f;
};

struct BvhBuildResult {
    RenderBvh bvh;
    std::vector<std::string> errors;
};

BvhBuildResult BuildBvh(const RenderSceneIR& scene, const BvhBuildOptions& options = {});

BvhHit IntersectBvh(
    const RenderSceneIR& scene,
    const RenderBvh& bvh,
    const Ray3f& ray,
    BvhTraceStats& stats,
    float t_min = 1.0e-5f,
    float t_max = std::numeric_limits<float>::infinity()
);

} // namespace yr
```

Key changes:
- `BuildBvh` takes `const RenderSceneIR&` instead of `const std::vector<RenderTriangle>&`
- `BvhHit` stores `triangle_index` (flat index across all primitives), `primitive_index`, and barycentric `bary_u`/`bary_v` (computed during intersection, avoids recomputation in shading)
- `RenderBvh` gains `total_triangles` for validation

- [ ] **Step 2: Rewrite `src/render/bvh.cpp`**

The BVH build now iterates all primitives and expands triangles:

```cpp
// Build triangle list from table geometry
struct BvhPrimitive {
    int triangle_index = -1;
    int primitive_index = -1;
    Bounds3f bounds;
    Point3f centroid;
};

// For each primitive, for each triangle in that primitive:
int flat_tri = 0;
for (int pi = 0; pi < primitives.size(); ++pi) {
    const auto& prim = primitives[pi];
    for (uint32_t ti = 0; ti < prim.index_count / 3; ++ti) {
        uint32_t i0 = indices[prim.first_index + ti * 3 + 0];
        uint32_t i1 = indices[prim.first_index + ti * 3 + 1];
        uint32_t i2 = indices[prim.first_index + ti * 3 + 2];
        Point3f p0 = vertices[i0].position;
        Point3f p1 = vertices[i1].position;
        Point3f p2 = vertices[i2].position;
        // compute bounds, centroid, store BvhPrimitive
        flat_tri++;
    }
}
```

The intersection function looks up triangle vertices through the index buffer. Store barycentric coordinates in `BvhHit` during intersection (Moller-Trumbore gives u,v directly).

The `IntersectBvh` intersection loop changes from:
```cpp
// OLD: const RenderTriangle& tri = scene.triangles[index];
// NEW:
const auto [prim_idx, local_tri] = LocateTriangle(scene, tri_index);
const auto& prim = scene.primitives[prim_idx];
uint32_t base = prim.first_index + local_tri * 3;
Point3f p0 = scene.vertices[scene.indices[base + 0]].position;
Point3f p1 = scene.vertices[scene.indices[base + 1]].position;
Point3f p2 = scene.vertices[scene.indices[base + 2]].position;
```

Build a precomputed lookup table (`std::vector<std::pair<int,int>>` mapping flat triangle index → (primitive_index, local_triangle_index)) during BVH construction and store it in `RenderBvh` for fast lookup during intersection.

- [ ] **Step 3: Rewrite `include/yaoray/render/shading.hpp`**

```cpp
#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct TriangleRef {
    int primitive_index = -1;
    int local_triangle = -1;
};

TriangleRef LocateTriangle(const RenderSceneIR& scene, int flat_triangle_index);

Vec2f InterpolateUv(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateNormal(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f InterpolateTangent(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
float InterpolateHandedness(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v);
Vec3f GeometricNormal(const RenderSceneIR& scene, TriangleRef tri);

Vec3f ResolveShadingNormal(
    const RenderSceneIR& scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal);

} // namespace yr
```

- [ ] **Step 4: Rewrite `src/render/shading.cpp`**

Implement all functions using table geometry lookups:

```cpp
TriangleRef LocateTriangle(const RenderSceneIR& scene, int flat_triangle_index) {
    int flat = 0;
    for (int pi = 0; pi < static_cast<int>(scene.primitives.size()); ++pi) {
        int tri_count = static_cast<int>(scene.primitives[pi].index_count / 3);
        if (flat_triangle_index < flat + tri_count) {
            return TriangleRef{pi, flat_triangle_index - flat};
        }
        flat += tri_count;
    }
    return TriangleRef{};
}
```

For interpolation, use barycentric coordinates (w = 1 - u - v, u, v) to interpolate vertex attributes via the index buffer:

```cpp
Vec2f InterpolateUv(const RenderSceneIR& scene, TriangleRef tri, float bary_u, float bary_v) {
    const auto& prim = scene.primitives[tri.primitive_index];
    if (!prim.has_uvs) return Vec2f{};
    uint32_t base = prim.first_index + tri.local_triangle * 3;
    const auto& v0 = scene.vertices[scene.indices[base + 0]];
    const auto& v1 = scene.vertices[scene.indices[base + 1]];
    const auto& v2 = scene.vertices[scene.indices[base + 2]];
    float w = 1.0f - bary_u - bary_v;
    return Vec2f{
        v0.uv.x * w + v1.uv.x * bary_u + v2.uv.x * bary_v,
        v0.uv.y * w + v1.uv.y * bary_u + v2.uv.y * bary_v
    };
}
```

- [ ] **Step 5: Commit**

```
git add include/yaoray/render/bvh.hpp src/render/bvh.cpp include/yaoray/render/shading.hpp src/render/shading.cpp
git commit -m "refactor: BVH and shading use table geometry"
```

---

### Task 7: Light Sampling — Emissive Primitives

**Files:**
- Rewrite: `include/yaoray/render/light_sampling.hpp`
- Rewrite: `src/render/light_sampling.cpp`

- [ ] **Step 1: Rewrite `include/yaoray/render/light_sampling.hpp`**

```cpp
#pragma once

#include <optional>

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>

namespace yr {

struct EmissiveSample {
    Point3f point;
    Vec3f normal;
    Color3f radiance;
    float pdf = 0.0f;
    int emissive_index = -1;
};

std::optional<EmissiveSample> SampleEmissivePrimitive(
    const RenderSceneIR& scene,
    int emissive_index,
    Vec2f sample_triangle,
    Vec2f sample_select);

std::optional<EmissiveSample> SampleEmissiveLights(
    const RenderSceneIR& scene,
    float select_sample,
    Vec2f triangle_sample);

float PdfEmissiveLightSolidAngle(
    const RenderSceneIR& scene,
    Point3f shading_point,
    Point3f light_point,
    Vec3f light_normal);

} // namespace yr
```

- [ ] **Step 2: Rewrite `src/render/light_sampling.cpp`**

Implement triangle surface point sampling for emissive primitives:

**`SampleEmissivePrimitive`:** Given an emissive primitive index, uniformly sample a triangle within that primitive, then uniformly sample a point on that triangle using the standard square-to-triangle mapping:
```cpp
// Square-to-triangle mapping
float su = std::sqrt(sample_triangle.x);
float u = 1.0f - su;
float v = sample_triangle.y * su;
Point3f point = p0 * (1.0f - u - v) + p1 * u + p2 * v;
```

**`SampleEmissiveLights`:** Uniformly select one emissive primitive (using `select_sample`), then sample a point on it. The PDF includes the 1/N selection probability.

**`PdfEmissiveLightSolidAngle`:** Convert area PDF to solid angle PDF: `pdf = distance^2 / (cos_theta * area) * (1/N_emissive)`. Used by MIS.

- [ ] **Step 3: Commit**

```
git add include/yaoray/render/light_sampling.hpp src/render/light_sampling.cpp
git commit -m "refactor: emissive primitive light sampling"
```

---

### Task 8: BSDF Adaptation

**Files:**
- Modify: `include/yaoray/render/bsdf.hpp`
- Rewrite: `src/render/bsdf.cpp`

- [ ] **Step 1: Update `include/yaoray/render/bsdf.hpp`**

The header interface stays the same — `EvaluateBsdf`, `PdfBsdf`, `SampleBsdf`, `IsDeltaBsdf` all take `const RenderMaterial&`. The material struct changed type, so the implementations need updating.

- [ ] **Step 2: Rewrite `src/render/bsdf.cpp`**

Update the material kind dispatch. For M0, only three material kinds need working BSDF code:

- **`RenderMaterialKind::Diffuse`** — port the existing Lambertian code. Use `material.reflectance.value` instead of `material.albedo`.
- **`RenderMaterialKind::Conductor`** — port the existing Metal/GGX code. Use `material.uroughness.value` and `material.vroughness.value`. For complex Fresnel, use `material.eta.value` and `material.k.value`. If eta/k are zero (unset), fall back to perfect mirror behavior.
- **`RenderMaterialKind::Dielectric`** — port the existing Dielectric code. Use `material.ior` for the index of refraction.

For all other material kinds (`ThinDielectric`, `CoatedDiffuse`, `CoatedConductor`, `DiffuseTransmission`, `Mix`): fall back to Diffuse behavior with a fallback reflectance. These will be implemented in M1/M2.

Update `IsDeltaBsdf`:
```cpp
bool IsDeltaBsdf(const RenderMaterial& material) {
    switch (material.kind) {
        case RenderMaterialKind::Conductor:
            return material.uroughness.value == 0.0f && material.vroughness.value == 0.0f;
        case RenderMaterialKind::Dielectric:
        case RenderMaterialKind::ThinDielectric:
            return material.uroughness.value == 0.0f && material.vroughness.value == 0.0f;
        default:
            return false;
    }
}
```

- [ ] **Step 3: Commit**

```
git add include/yaoray/render/bsdf.hpp src/render/bsdf.cpp
git commit -m "refactor: BSDF dispatch for PBRT material kinds"
```

---

### Task 9: CPU Backend Adaptation

**Files:**
- Rewrite: `include/yaoray/backends/cpu/cpu_material.hpp`
- Rewrite: `src/backends/cpu/cpu_material.cpp`
- Rewrite: `src/backends/cpu/cpu_surface.cpp`
- Rewrite: `src/backends/cpu/cpu_prepared_scene.cpp`
- Modify: `src/backends/cpu/cpu_path_tracer.cpp`

- [ ] **Step 1: Rewrite `include/yaoray/backends/cpu/cpu_material.hpp`**

```cpp
#pragma once

#include <yaoray/core/vec.hpp>
#include <yaoray/render/render_scene.hpp>
#include <yaoray/render/shading.hpp>
#include <yaoray/render/bvh.hpp>

namespace yr {

struct ResolvedMaterialSample {
    RenderMaterial material;
    Vec3f shading_normal{0.0f, 0.0f, 1.0f};
    Vec2f uv;
    float alpha = 1.0f;
};

ResolvedMaterialSample ResolveCpuMaterialSample(
    const RenderSceneIR& scene,
    TriangleRef tri,
    float bary_u,
    float bary_v,
    Vec3f geometric_normal,
    Vec3f wo);

bool IsAlphaVisible(const ResolvedMaterialSample& sample);

} // namespace yr
```

Key change: the function takes `TriangleRef` + barycentric coordinates instead of `const RenderTriangle&`.

- [ ] **Step 2: Rewrite `src/backends/cpu/cpu_material.cpp`**

Port the material resolution logic to use table geometry lookups:
- UV interpolation: `InterpolateUv(scene, tri, bary_u, bary_v)`
- Texture sampling: use `material.reflectance.texture` instead of `material.albedo_texture`
- Normal mapping: `InterpolateTangent(scene, tri, bary_u, bary_v)` instead of per-triangle tangent access
- Shading normal: `ResolveShadingNormal(scene, tri, bary_u, bary_v, geometric_normal)`
- Alpha: read from `material.alpha.value`; if `material.alpha.texture >= 0`, sample it

- [ ] **Step 3: Update `include/yaoray/backends/cpu/cpu_surface.hpp`**

Update `CpuSurfaceHit` to use `BvhHit` with the new fields (no triangle pointer, has bary_u/bary_v):

```cpp
struct CpuSurfaceHit {
    bool hit = false;
    bool exhausted = false;
    BvhHit geometry_hit;
    ResolvedMaterialSample sample;
};
```

Remove `barycentric` and `uv` fields — barycentric is in `BvhHit`, UV is in `ResolvedMaterialSample`.

- [ ] **Step 4: Rewrite `src/backends/cpu/cpu_surface.cpp`**

Update `TraceVisibleSurface` to use the new `BvhHit` fields:

```cpp
// OLD: surface_hit.barycentric = BarycentricCoordinates(ray.At(geometry_hit.t), *geometry_hit.triangle);
// NEW: barycentric is already in geometry_hit.bary_u / bary_v from Moller-Trumbore

TriangleRef tri = LocateTriangle(scene, geometry_hit.triangle_index);
// Or use geometry_hit.primitive_index directly

// OLD: const RenderMaterial& base = scene.materials[geometry_hit.triangle->material_index];
// NEW:
const RenderMaterial& base = scene.materials[scene.primitives[tri.primitive_index].material_index];

surface_hit.sample = ResolveCpuMaterialSample(
    scene, tri, geometry_hit.bary_u, geometry_hit.bary_v, geometric_normal, -ray.direction);
```

- [ ] **Step 5: Rewrite `src/backends/cpu/cpu_prepared_scene.cpp`**

Update `PrepareCpuScene` to call the new `BuildBvh(scene)` instead of `BuildBvh(scene.triangles)`:

```cpp
CpuPrepareResult PrepareCpuScene(RenderSceneIR scene) {
    CpuPrepareResult result;
    const auto start = std::chrono::steady_clock::now();
    BvhBuildResult build = BuildBvh(scene);
    const auto end = std::chrono::steady_clock::now();
    result.elapsed_seconds = std::chrono::duration<double>(end - start).count();
    if (!build.errors.empty()) {
        result.ok = false;
        result.error = build.errors[0];
        return result;
    }
    result.ok = true;
    result.scene.emplace(std::move(scene), std::move(build.bvh));
    return result;
}
```

- [ ] **Step 6: Update `src/backends/cpu/cpu_path_tracer.cpp`**

Key changes throughout the path tracer:

1. **Material type checks:** Replace `material.type == MaterialKind::Dielectric` with `material.kind == RenderMaterialKind::Dielectric`
2. **Albedo access:** Replace `material.albedo` with `material.reflectance.value`
3. **Emission check:** Replace `material.emission` access — same field name, no change needed
4. **Area light sampling:** Replace `SampleAreaLight` calls with `SampleEmissiveLights`. Replace `PdfAreaLightsForPointSolidAngle` with `PdfEmissiveLightSolidAngle`.
5. **Direct light loop:** Replace iteration over `scene.area_lights` with sampling from `scene.emissive_primitives`
6. **Shadow material transparency:** Replace `material.type == MaterialKind::Dielectric` with `material.kind == RenderMaterialKind::Dielectric`
7. **Absorption:** `material.absorption_color` and `material.absorption_distance` field names are the same
8. **Thin check:** `material.thin` → check `material.kind == RenderMaterialKind::ThinDielectric`
9. **BvhHit access:** Replace `geometry_hit.triangle->material_index` with `scene.primitives[geometry_hit.primitive_index].material_index`
10. **light_samples:** Replace `scene.light_samples` — use a fixed default (1) or compute from emissive count
11. **Emissive hit MIS:** Update `EmissiveHitMisWeight` to use `PdfEmissiveLightSolidAngle` with the light normal from the hit

- [ ] **Step 7: Commit**

```
git add include/yaoray/backends/cpu/cpu_material.hpp src/backends/cpu/cpu_material.cpp
git add include/yaoray/backends/cpu/cpu_surface.hpp src/backends/cpu/cpu_surface.cpp
git add src/backends/cpu/cpu_prepared_scene.cpp src/backends/cpu/cpu_path_tracer.cpp
git commit -m "refactor: CPU backend uses table geometry and PBRT materials"
```

---

### Task 10: App, CMake, and Cleanup

**Files:**
- Rewrite: `src/app/main.cpp`
- Rewrite: `CMakeLists.txt`
- Delete: all files listed in the "Deleted files" section above

- [ ] **Step 1: Rewrite `src/app/main.cpp`**

Replace the SceneWorld/frontends pipeline with direct PbrtScene usage:

```cpp
#include <yaoray/backends/backend.hpp>
#include <yaoray/core/version.hpp>
#include <yaoray/film/image_writer.hpp>
#include <yaoray/film/tone_mapping.hpp>
#include <yaoray/pbrt/pbrt_scene.hpp>
#include <yaoray/render/scene_compiler.hpp>
#include <yaoray/core/diagnostic.hpp>

// In RunRender():
// 1. Parse PBRT scene
yr::PbrtSceneLoadResult parse_result = yr::LoadPbrtScene(scene_path);
if (yr::HasSceneErrors(parse_result.diagnostics) || !parse_result.scene.has_value()) {
    std::cerr << yr::FormatSceneDiagnostics(parse_result.diagnostics) << '\n';
    return 1;
}

// 2. Compile to RenderSceneIR
const yr::SceneCompileResult compile_result = yr::CompilePbrtScene(parse_result.scene.value());
if (yr::HasSceneErrors(compile_result.diagnostics) || !compile_result.scene.has_value()) {
    std::cerr << yr::FormatSceneDiagnostics(compile_result.diagnostics) << '\n';
    return 1;
}

// 3. Apply backend override if provided
// (backend override changes ir.requested_backend)

// 4. Prepare + Render + Output (same as before, using compile_result.scene.value())
```

Remove all `SceneWorld`, `SceneDescription`, `LoadSceneWorldFile`, `CompileSceneWorld` references. Remove the `ToFilmToneMapper` adapter — use `ir.film.tone_mapper` directly. Remove `OfflineRequested`/`ValidateOfflineWorkflow` — offline workflow will be re-added in a future milestone when PBRT scene compilation supports it.

The CLI now only accepts `.pbrt` files:
```
Usage:
  yaoray render <scene.pbrt> [--backend cpu|cuda]
```

- [ ] **Step 2: Delete old files**

Delete all files listed in the "Deleted files" section of the file structure. Use `git rm` for tracked files:

```bash
git rm include/yaoray/scene/scene.hpp include/yaoray/scene/scene_parser.hpp
git rm include/yaoray/scene/scene_world.hpp include/yaoray/scene/diagnostic.hpp
git rm src/scene/scene.cpp src/scene/scene_parser.cpp
git rm src/scene/scene_world.cpp src/scene/diagnostic.cpp
git rm include/yaoray/assets/asset_resource.hpp
git rm include/yaoray/assets/obj_loader.hpp include/yaoray/assets/gltf_loader.hpp
git rm src/assets/obj_loader.cpp src/assets/gltf_loader.cpp
git rm include/yaoray/frontends/scene_frontend.hpp src/frontends/scene_frontend.cpp
```

Also remove external dependencies that are no longer needed:
```bash
git rm -r external/tinyobjloader external/tinygltf external/tomlplusplus
```

Remove TOML scene files:
```bash
git rm scenes/examples/*.toml
```

Keep `scenes/examples/assets/` — some assets (like the FlightHelmet glTF) might be useful for future milestones, and the HDRI/OBJ files might be referenced by future PBRT scenes.

- [ ] **Step 3: Rewrite `CMakeLists.txt`**

Remove the deleted libraries and update source lists. The new structure:

```cmake
cmake_minimum_required(VERSION 3.24)
project(YaoRay VERSION 0.1.0 DESCRIPTION "Physically based offline path tracer" LANGUAGES CXX)
option(YAORAY_ENABLE_CUDA "Build the CUDA backend when available" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

# --- yaoray_core ---
add_library(yaoray_core STATIC
    src/core/version.cpp
    src/core/diagnostic.cpp
    src/core/transform.cpp
)
target_include_directories(yaoray_core PUBLIC include)

# --- yaoray_film ---
add_library(yaoray_film STATIC
    src/film/film.cpp
    src/film/film_checkpoint.cpp
    src/film/image_writer.cpp
    src/film/tone_mapping.cpp
)
target_include_directories(yaoray_film PUBLIC include)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)
target_link_libraries(yaoray_film PUBLIC yaoray_core PRIVATE stb)

# --- yaoray_pbrt ---
add_library(yaoray_pbrt STATIC
    src/pbrt/pbrt_scene.cpp
    src/assets/ply_loader.cpp
)
target_include_directories(yaoray_pbrt PUBLIC include)
target_link_libraries(yaoray_pbrt PUBLIC yaoray_core)

# --- yaoray_render ---
add_library(yaoray_render STATIC
    src/render/render_scene.cpp
    src/render/bvh.cpp
    src/render/bsdf.cpp
    src/render/environment.cpp
    src/render/light_sampling.cpp
    src/render/mis.cpp
    src/render/scene_compiler.cpp
    src/render/shading.cpp
    src/render/texture.cpp
)
target_include_directories(yaoray_render PUBLIC include)
target_link_libraries(yaoray_render PUBLIC yaoray_core yaoray_pbrt PRIVATE stb)

# --- yaoray_backends ---
add_library(yaoray_backends STATIC
    src/backends/backend.cpp
    src/backends/cpu/cpu_debug_backend.cpp
    src/backends/cpu/cpu_debug_renderer.cpp
    src/backends/cpu/cpu_material.cpp
    src/backends/cpu/cpu_prepared_scene.cpp
    src/backends/cpu/cpu_tile_scheduler.cpp
    src/backends/cpu/cpu_sampler.cpp
    src/backends/cpu/cpu_surface.cpp
    src/backends/cpu/cpu_path_tracer.cpp
    src/backends/cuda/cuda_backend.cpp
    src/backends/cuda/cuda_prepared_scene.cpp
)
target_include_directories(yaoray_backends PUBLIC include)
target_link_libraries(yaoray_backends PUBLIC yaoray_core yaoray_film yaoray_render)

# --- yaoray executable ---
add_executable(yaoray src/app/main.cpp)
target_link_libraries(yaoray PRIVATE yaoray_core yaoray_film yaoray_pbrt yaoray_render yaoray_backends)
```

Update the test executable and CLI tests — see Task 11.

- [ ] **Step 4: Update remaining include references**

Search all `.cpp` and `.hpp` files for includes of deleted headers. Update them:
- `#include <yaoray/scene/diagnostic.hpp>` → `#include <yaoray/core/diagnostic.hpp>`
- `#include <yaoray/scene/scene.hpp>` → `#include <yaoray/render/render_scene.hpp>`
- `#include <yaoray/scene/scene_world.hpp>` → remove (should not be needed)
- `#include <yaoray/frontends/scene_frontend.hpp>` → remove
- `#include <yaoray/assets/obj_loader.hpp>` → remove
- `#include <yaoray/assets/gltf_loader.hpp>` → remove

Also update `src/render/environment.cpp` — it currently uses `RenderEnvironment` from `render_scene.hpp` and `EnvironmentKind` from `scene.hpp`. The `EnvironmentKind` enum is no longer needed; `RenderEnvironment` uses a boolean `active` field instead.

Also update `src/backends/cpu/cpu_debug_backend.cpp` and `src/backends/cpu/cpu_debug_renderer.cpp` — these may reference `RenderTriangle` or old material types. The debug renderer can use table geometry lookups. If too complex to adapt, stub it out (return an error saying "debug renderer not yet adapted to table geometry").

Also update `src/backends/backend.cpp` — it uses `RenderBackendKind` which is now in `render_scene.hpp`.

- [ ] **Step 5: Commit**

```
git add -A
git commit -m "refactor: complete M0 architecture reset — PBRT-only pipeline"
```

---

### Task 11: Tests and Validation

**Files:**
- Modify: `CMakeLists.txt` (test section)
- Modify: various test files
- Delete: TOML-related test files

- [ ] **Step 1: Update test executable source list in `CMakeLists.txt`**

Remove tests for deleted modules:
- Remove: `tests/scene_tests.cpp`, `tests/scene_world_tests.cpp`, `tests/frontends_tests.cpp`, `tests/assets_tests.cpp` (OBJ/glTF parts)
- Keep: `tests/pbrt_tests.cpp`, `tests/bvh_tests.cpp`, `tests/bsdf_tests.cpp`, `tests/film_tests.cpp`, `tests/core_tests.cpp`, `tests/version_tests.cpp`, `tests/render_scene_tests.cpp`, `tests/environment_tests.cpp`, `tests/mis_tests.cpp`, `tests/light_sampling_tests.cpp`, `tests/texture_tests.cpp`, `tests/backend_tests.cpp`, `tests/cpu_*_tests.cpp`

Update test link libraries:
```cmake
target_link_libraries(yaoray_tests PRIVATE yaoray_core yaoray_film yaoray_pbrt yaoray_render yaoray_backends)
```

- [ ] **Step 2: Update test files for new types**

Update all kept test files that reference old types:
- Replace `MaterialKind::` with `RenderMaterialKind::`
- Replace `material.albedo` with `material.reflectance.value`
- Replace `material.type` with `material.kind`
- Replace `RenderTriangle` usage with table geometry setup (vertices + indices + primitives)
- Replace `RenderAreaLight` usage with `EmissivePrimitive`
- Replace `BuildBvh(triangles)` with `BuildBvh(scene)` passing a `RenderSceneIR`
- Update includes from `scene/` to `core/` or `render/`

For `tests/pbrt_tests.cpp`: update to test `LoadPbrtScene()` returning `PbrtSceneLoadResult` with `PbrtScene` instead of `SceneWorldLoadResult` with `SceneWorld`.

- [ ] **Step 3: Update CLI render tests in `CMakeLists.txt`**

Remove all TOML-based CLI tests:
```cmake
# REMOVE these tests:
# yaoray_cli_render_cpu (uses builtin_triangle.toml)
# yaoray_cli_render_obj (uses obj_quad.toml)
# yaoray_cli_render_textured_obj (uses textured_quad.toml)
# yaoray_cli_render_gltf (uses gltf_textured_asset.toml)
# yaoray_cli_render_cornell_box (uses cornell_box.toml)
# yaoray_cli_render_path (uses path_tracer_bounce.toml)
# yaoray_cli_render_hdri_showcase (uses hdri_lighting_showcase.toml)
# yaoray_cli_render_glass_showcase (uses glass_showcase.toml)
# yaoray_cli_render_glass_showcase_visual_sanity
# yaoray_cli_render_offline_* (uses offline_*.toml)
# yaoray_cli_render_cuda (uses builtin_triangle.toml)
# yaoray_cli_render_unsupported_asset (uses unsupported_asset.toml)
# yaoray_cli_render_missing_file — can keep this one, just change to a .pbrt path
# yaoray_cli_render_invalid_scene (uses invalid_width.toml)
```

Keep and update:
```cmake
# Keep the PBRT minimal test:
add_yaoray_cli_render_test(yaoray_cli_render_pbrt_minimal
    SCENE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/pbrt/minimal_triangle.pbrt"
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scene/out/minimal_pbrt.png"
    BACKEND cpu
    EXPECT_REGEX
        "Integrator: path"
        "Compiled triangles: 1"
        "Rendered image:"
)
```

Also update `yaoray_cli_help` and `yaoray_cli_version` — these should still pass as-is.

- [ ] **Step 4: Build and fix compilation errors**

Run: `cmake --build build --target yaoray_tests yaoray 2>&1 | head -100`

Fix any remaining compilation errors. Common issues:
- Missing includes
- Type mismatches from old → new material/geometry types
- Functions that still reference deleted types

- [ ] **Step 5: Run tests**

Run: `cd build && ctest --output-on-failure`

The critical test is `yaoray_cli_render_pbrt_minimal`. It must render `tests/fixtures/pbrt/minimal_triangle.pbrt` and output an image. Expected output includes "Integrator: path", "Compiled triangles: 1", "Rendered image:".

- [ ] **Step 6: Verify rendered output**

Open `tests/fixtures/scene/out/minimal_pbrt.png` and verify it shows a white triangle on a black background. This confirms the full pipeline works: PBRT parsing → PbrtScene → scene compilation → RenderSceneIR → BVH build → CPU path tracing → Film → PNG output.

- [ ] **Step 7: Commit**

```
git add -A
git commit -m "test: update tests for M0 architecture reset"
```

- [ ] **Step 8: Update architecture documentation**

Update `docs/architecture/overview.md` to reflect the new two-layer architecture. Remove references to SceneDescription, SceneWorld, TOML, OBJ, glTF, frontends. Describe the new PbrtScene → RenderSceneIR → Backend pipeline.

```
git add docs/architecture/overview.md
git commit -m "docs: update architecture overview for PBRT-only pipeline"
```
