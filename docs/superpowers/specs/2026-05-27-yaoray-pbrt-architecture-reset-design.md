# YaoRay PBRT Architecture Reset Design

Date: 2026-05-27

## Purpose

YaoRay has accumulated organic architectural complexity through rapid feature-driven development. This design resets the architecture around a single clear goal: **a PBRT-aligned offline path tracer that produces beautiful images from pbrt-v4-scenes, with clean architecture suitable for learning rendering algorithms.**

The reset removes the custom TOML scene format, OBJ/glTF asset importers, and the three-layer scene representation. It replaces them with a two-layer pipeline built entirely around the PBRT scene format.

## Goals

- PBRT as the sole scene format, directly compatible with pbrt-v4-scenes
- Material system aligned with PBRT v4 types (diffuse, conductor, dielectric, coateddiffuse, mix, etc.)
- Two-layer data flow: PbrtScene (parsed scene description) to RenderSceneIR (flat GPU-friendly arrays)
- Unified table geometry, removing the dual RenderTriangle/indexed representation
- Area lights as emissive geometry, not separate entities
- Scene-driven development with clear milestones tied to specific PBRT scenes
- CPU backend first, CUDA at medium priority

## Non-Goals

- No custom scene format (TOML removed)
- No OBJ or glTF asset import (PBRT uses trianglemesh and plymesh)
- No GUI or interactive rendering
- No bidirectional path tracing, MLT, or spectral rendering in initial scope
- No full pbrt-v4 as a library dependency

## Data Flow Pipeline

```
PBRT file (.pbrt)
      |
      v
+---------------------+
|   pbrt_parser        |  Tokenizer + scene builder (reference pbrt-v4 for correctness)
|   (yaoray_pbrt)      |  Output: PbrtScene (parameterized scene description)
+---------------------+
      |
      v
+---------------------+
|   scene_compiler     |  Resolve material/texture refs, load PLY meshes,
|   (yaoray_render)    |  expand transforms, build table geometry
|                      |  Output: RenderSceneIR (flat GPU-friendly arrays)
+---------------------+
      |
      v
+---------------------+
|   Backend.Prepare()  |  CPU: build BVH
|   (yaoray_backends)  |  CUDA: upload buffers + build acceleration structure
|                      |  Output: PreparedScene (backend-private)
+---------------------+
      |
      v
+---------------------+
|   Backend.Render()   |  Path tracing, MIS, BSDF sampling
|   -> Film -> Output  |  Output: Film -> tone mapping -> PNG
+---------------------+
```

## CMake Module Structure

### Retained modules

| Module | Role |
|--------|------|
| `yaoray_core` | vec, ray, bounds, version, diagnostics |
| `yaoray_film` | Film accumulation, image writer, tone mapping, checkpoint |
| `yaoray_pbrt` | PBRT parser + PLY loader, outputs PbrtScene |
| `yaoray_render` | Scene compiler + render primitives (BVH, BSDF, MIS, texture, environment) |
| `yaoray_backends` | CPU and CUDA backends |

### Removed modules

| Module | Reason |
|--------|--------|
| `yaoray_scene` | TOML parser, SceneDescription, SceneWorld all removed |
| `yaoray_assets` | OBJ/glTF loaders removed; PLY loader migrates to yaoray_pbrt |
| `yaoray_frontends` | No frontend dispatch needed with single PBRT entry point |

### Dependency graph

```
yaoray_core <- yaoray_film
yaoray_core <- yaoray_pbrt (parser + PLY loader)
yaoray_core <- yaoray_render (compiler + render primitives)
yaoray_pbrt <- yaoray_render (compiler consumes PbrtScene)
yaoray_render <- yaoray_backends (backend consumes RenderSceneIR)
yaoray_film <- yaoray_backends (backend outputs to Film)
```

## PbrtScene Data Model

PbrtScene faithfully represents the PBRT file format as parsed. All references are symbolic strings. No files are loaded, no semantics are interpreted.

```cpp
struct PbrtParam {
    std::string type;    // "float", "rgb", "string", "point3", "integer", "bool", "texture"
    std::string name;
    std::vector<float> floats;
    std::vector<int> ints;
    std::vector<std::string> strings;
    std::vector<bool> bools;
};

struct PbrtEntity {
    std::string type;              // "perspective", "diffuse", "trianglemesh", etc.
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

    // Render options (before WorldBegin)
    PbrtEntity camera;
    PbrtEntity sampler;
    PbrtEntity integrator;
    PbrtEntity film;
    PbrtEntity filter;
    Mat4f camera_transform;

    // World definition (after WorldBegin)
    std::unordered_map<std::string, PbrtEntity> named_materials;
    std::unordered_map<std::string, PbrtEntity> named_textures;
    std::vector<PbrtShapeRecord> shapes;
    std::vector<PbrtLightRecord> lights;

    // Object definitions and instancing
    std::unordered_map<std::string, std::vector<PbrtShapeRecord>> object_definitions;
    std::vector<PbrtObjectInstance> instances;
};
```

### Parser strategy

The existing parser (~935 lines) is retained and refactored. Its output changes from SceneWorld to PbrtScene. The tokenizer and transform stack handling remain. New PBRT directives are added incrementally as milestones require them (Texture, ObjectBegin/End, ObjectInstance, AreaLightSource, LightSource, more Shape types).

pbrt-v4's parser source is referenced for correctness on edge cases but not linked as a dependency. pbrt-v4 is ~100k lines with tightly coupled internal types; importing it would make YaoRay a wrapper rather than an independent learning project.

## Material System

### Material types aligned with PBRT v4

```cpp
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
```

### Texture-parameterized materials

Any material parameter can be a constant value or a texture reference:

```cpp
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

    // Diffuse / CoatedDiffuse / DiffuseTransmission
    TexParam3f reflectance{{0.5f, 0.5f, 0.5f}};

    // Conductor / CoatedConductor
    TexParam3f eta;
    TexParam3f k;

    // Dielectric / ThinDielectric / coating IOR
    float ior = 1.5f;

    // Roughness (GGX microfacet)
    TexParam1f uroughness{0.0f};
    TexParam1f vroughness{0.0f};
    bool remap_roughness = true;

    // Mix
    int mix_material_a = -1;
    int mix_material_b = -1;
    TexParam1f mix_amount{0.5f};

    // Coating (CoatedDiffuse / CoatedConductor)
    float coating_ior = 1.5f;
    TexParam1f coating_roughness{0.0f};

    // Normal mapping
    int normal_map = -1;
    float normal_scale = 1.0f;

    // Emission (populated when AreaLightSource is bound to shape)
    Color3f emission{0.0f, 0.0f, 0.0f};

    // Alpha masking
    TexParam1f alpha{1.0f};
};
```

### BSDF layer changes

| Current | New |
|---------|-----|
| Lambertian diffuse | Retained |
| Perfect mirror | Removed (conductor + roughness=0) |
| GGX metal | Conductor BSDF (complex Fresnel with eta+k) |
| Plastic | CoatedDiffuse (dielectric coating model) |
| Dielectric glass | Retained, parameter source changes |
| -- | ThinDielectric (new) |
| -- | CoatedConductor (new) |
| -- | DiffuseTransmission (new) |
| -- | Mix dispatch (new) |

### Texture system

```cpp
enum class RenderTextureKind {
    Image,
    Constant,
    Scale,
    Checkerboard,
};

struct RenderTexture {
    RenderTextureKind kind = RenderTextureKind::Image;
    int width = 0;
    int height = 0;
    std::vector<Color4f> texels;
    Color4f constant_value;
    int texture_a = -1;
    int texture_b = -1;
};
```

## Geometry System

### Unified table geometry

The dual representation (RenderTriangle + vertices/indices/primitives) is unified into table geometry only.

```cpp
struct RenderVertex {
    Point3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent;
    float tangent_handedness = 1.0f;
};
// 48 bytes, GPU-alignment friendly

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material_index = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool has_tangents = false;
};
```

Attribute presence flags (has_normals, has_uvs, has_tangents) are per-primitive, not per-vertex. All vertices in a primitive share the same attribute availability, matching PBRT trianglemesh semantics.

### PBRT shape compilation

| PBRT Shape | Compilation | Initial Priority |
|------------|-------------|-----------------|
| trianglemesh | Direct to vertices/indices | Required |
| plymesh | Load PLY then vertices/indices | Required |
| sphere | Tessellate to triangles | High |
| disk | Tessellate to triangles | Medium |
| cylinder | Tessellate to triangles | Medium |
| bilinearmesh | Split to two triangles each | Medium |
| loopsubdiv | Loop subdivision to triangles | Low |
| curve | Future work | Low |

### BVH adaptation

BVH construction iterates all primitives and expands each into its component triangles. Leaf nodes store flat triangle indices. Intersection fetches vertices through the index buffer:

```
flat_triangle_id -> locate primitive -> get material_index
                 -> indices[first_index + local_tri * 3 + 0/1/2]
                 -> vertices[index] for position, normal, uv, tangent
```

## Light System

### Area lights as emissive geometry

PBRT has no separate area light entities. Area lights are shapes with bound AreaLightSource directives. The compiler identifies emissive primitives:

```cpp
struct EmissivePrimitive {
    int primitive_index;
    Color3f radiance;
    float area;
};
```

Light sampling changes from "random XZ-rectangle point" to "random emissive primitive, random triangle surface point". MIS logic remains unchanged; only the PDF computation changes.

### Environment lights

```cpp
struct RenderEnvironment {
    bool active = false;
    Color3f radiance{0.0f, 0.0f, 0.0f};
    float strength = 1.0f;
    float rotation_radians = 0.0f;
    int texture_index = -1;
    int distribution_index = -1;
};
```

### Analytic lights (future)

```cpp
enum class AnalyticLightKind { Point, Spot, Distant };
struct AnalyticLight {
    AnalyticLightKind kind;
    Point3f position;
    Vec3f direction;
    Color3f intensity;
    float cone_angle = 0.0f;
};
```

## Complete RenderSceneIR

```cpp
struct RenderSceneIR {
    // Render settings
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

    // Camera
    RenderCamera camera;

    // Geometry (scene-wide single buffer)
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;

    // Materials and textures
    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;

    // Lights
    std::vector<EmissivePrimitive> emissive_primitives;
    RenderEnvironment environment;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<AnalyticLight> analytic_lights;
};
```

Changes from current:
- Removed: `std::vector<RenderTriangle> triangles`
- Removed: `std::vector<RenderAreaLight> area_lights`
- Removed: `int light_samples` (compiler/integrator internal decision)
- Added: `std::vector<EmissivePrimitive> emissive_primitives`
- Changed: `RenderMaterial` to PBRT-aligned version with TexParam fields

## Development Milestones

### M0: Architecture Reset (no new rendering features)

Complete the structural migration. Existing rendering capability must not regress.

- Remove TOML frontend, OBJ/glTF loaders, SceneDescription, SceneWorld, yaoray_frontends
- Create PbrtScene data model
- Refactor PBRT parser to output PbrtScene
- Create scene compiler (PbrtScene to RenderSceneIR)
- Remove RenderTriangle, unify table geometry
- Adapt BVH to table geometry
- Switch material system to RenderMaterialKind (Diffuse + Conductor + Dielectric initially)
- Convert area lights to emissive primitives
- Validation: existing PBRT test scenes still render correctly

### M1: Cornell Box (PBRT version)

```
Features unlocked: diffuse material, emissive geometry light sampling, trianglemesh
Validation scene: pbrt-v4-scenes/cornell-box
```

### M2: Breakfast

```
Features unlocked: conductor (metals), dielectric (glass), coateddiffuse (coatings),
                   imagemap textures, plymesh, object instancing (ObjectBegin/End/Instance),
                   multi-emitter sampling
Validation scene: pbrt-v4-scenes/breakfast
```

This is the most important milestone. Breakfast is one of the most iconic PBRT showcase scenes. Rendering it means core rendering features are complete.

### M3: Environment Lighting + Large Scenes

```
Features unlocked: infinite light (HDRI environment), large-scale geometry handling,
                   sphere/disk analytic shapes, distant light (sun)
Validation scene: pbrt-v4-scenes/barcelona-pavilion or landscape
```

### M4: CUDA Backend

```
Features unlocked: CUDA path tracing, GPU buffer management, GPU BVH (or OptiX)
Validation: Breakfast and Barcelona rendered on GPU with correctness and speedup verification
```

### M5+: Advanced Features (choose by interest)

```
Options:
- coatedconductor -> sportscar (automotive paint)
- subsurface -> ganesha/head (SSS skin)
- curve geometry -> hair scenes
- volpath + homogeneous medium -> cloud/smoke
- loop subdivision -> teapot/killeroo
- denoiser (OIDN / OptiX denoiser)
```

### Definition of "done"

No fixed endpoint. M2 (Breakfast) is the first meaningful milestone where YaoRay renders a real, beautiful scene. After M3 the project is a "feature-complete learning renderer." M5+ is pure exploration by interest.

## Code Migration

### Files to remove

| File | Reason |
|------|--------|
| `src/scene/scene_parser.cpp` | TOML parser |
| `src/scene/scene_world.cpp` | SceneWorld layer |
| `include/yaoray/scene/scene_parser.hpp` | |
| `include/yaoray/scene/scene_world.hpp` | |
| `src/assets/obj_loader.cpp` | OBJ format removed |
| `src/assets/gltf_loader.cpp` | glTF format removed |
| `include/yaoray/assets/obj_loader.hpp` | |
| `include/yaoray/assets/gltf_loader.hpp` | |
| `include/yaoray/assets/asset_resource.hpp` | |
| `src/frontends/scene_frontend.cpp` | Single PBRT entry point |
| `include/yaoray/frontends/scene_frontend.hpp` | |
| `external/tinyobjloader/` | OBJ dependency |
| `external/tinygltf/` | glTF dependency |
| `external/tomlplusplus/` | TOML dependency |
| `scenes/examples/*.toml` | TOML scene files |

### Files to migrate

| File | Destination | Notes |
|------|-------------|-------|
| `src/scene/scene.cpp` | `yaoray_render` | Render enums (RenderBackendKind, RenderIntegratorKind, RenderSamplerKind, ToneMapperKind) and parse functions move to render_scene.hpp; PbrtScene uses strings, not enums |
| `src/scene/diagnostic.cpp` | `yaoray_core` | Diagnostic system (used by both yaoray_pbrt parser and yaoray_render compiler) |
| `src/assets/ply_loader.cpp` | `yaoray_pbrt` | PLY loader for plymesh |

### Files to retain and adapt

| File | Changes |
|------|---------|
| `src/render/bvh.cpp` | Adapt to table geometry |
| `src/render/bsdf.cpp` | Add conductor, coateddiffuse, thindielectric, mix dispatch |
| `src/render/scene_compiler.cpp` | Rewrite to consume PbrtScene instead of SceneWorld |
| `src/render/light_sampling.cpp` | Triangle surface sampling instead of XZ-rectangle |
| `src/render/mis.cpp` | PDF changes for triangle lights |
| `src/render/texture.cpp` | Add Constant/Scale/Checkerboard texture kinds |
| `src/render/shading.cpp` | Adapt to new material fields |
| `src/backends/cpu/cpu_path_tracer.cpp` | Adapt to new material/geometry/light types |
| `src/backends/cpu/cpu_material.cpp` | Rewrite for PBRT material sampling |
| `src/backends/cpu/cpu_surface.cpp` | Adapt to table geometry intersection |
| `src/backends/cpu/cpu_prepared_scene.cpp` | Adapt BVH build to table geometry |

## Testing Strategy

### M0 testing guarantees

1. Retain all PBRT CLI render tests as regression guards
2. Remove TOML-related tests (scene_tests, scene_world_tests, OBJ/glTF asset tests, frontends_tests)
3. Adapt unit tests (bvh_tests, bsdf_tests, cpu_*_tests) to new geometry/material types
4. Add PBRT parser tests covering directive parsing correctness
5. Add golden image tests for Cornell Box and other milestone scenes

### Per-milestone acceptance

- Target scene renders to completion
- Rendering result is visually consistent with pbrt-v4 reference (pixel-exact matching not required)
- All existing tests do not regress
