# YaoRay Asset, Render, Backend IR Architecture Design

## Summary

YaoRay has reached the point where the current `RenderScene` shape is too
small for the next renderer phase. The CPU path tracer, asset importers,
material lowering, direct-light sampling, environment sampling, texture
resolution, medium state, and backend execution are all coupled through data
structures that were intentionally simple during the first renderer slices.

The next architecture direction is a larger boundary reset:

```text
Scene authoring and importers
        |
        v
Asset IR
        |
        v
GPU-ready Render IR
        |
        v
Backend runtime
```

The purpose of this design is to make asset growth and GPU work independent
instead of forcing every new asset feature to be shaped around the current CPU
path tracer. Importers should preserve source asset semantics. The render
compiler should lower those semantics into a backend-facing render world. CPU,
CUDA, and future OptiX backends should consume that render world without knowing
whether the scene came from TOML, OBJ, glTF, or a future format.

The first implementation should be a vertical slice: keep the existing CLI
behavior and current visible rendering behavior, but route a narrow end-to-end
path through `Asset IR -> Render IR -> CPU backend`. Follow-up slices then
migrate OBJ/glTF import, clean backend dependencies, and prepare GPU packing.

## Goals

- Introduce an `Asset IR` layer that preserves mesh, primitive, node, material,
  texture, sampler, and source-diagnostic semantics from scene-authored assets,
  OBJ, and glTF.
- Introduce a GPU-ready `Render IR` layer built from flat arrays, integer
  handles, stable record layouts, and no importer-specific concepts.
- Make the CPU path tracer consume the same render contract future CUDA and
  OptiX work will consume.
- Move material lowering out of importers. For example, glTF metallic/roughness
  data should not be converted to current `MaterialKind` choices inside the
  glTF loader.
- Split diagnostics by stage so scene parsing, asset loading, render
  compilation, and backend runtime failures can be reported without conflating
  responsibilities.
- Preserve the current command-line shape for users during the migration:
  `yaoray render scene.toml --backend cpu`.
- Keep the first implementation slice behavior-preserving enough that the
  current unit, CLI, and visual sanity tests remain meaningful.

## Non-Goals

- No CUDA path tracer implementation in the architecture slice.
- No complete glTF PBR shading implementation in the first vertical slice.
- No animation, skinning, morph target, or sparse accessor support in the first
  Asset IR migration.
- No denoising, adaptive sampling, spectral rendering, or caustic transport.
- No full material-node graph. The design keeps a structured PBR-oriented
  material record, not an arbitrary shader graph.
- No requirement that the first slice remove every old `RenderScene` type in one
  change. Transitional adapters are acceptable if the new path becomes the
  default pipeline and old dependencies are explicitly retired in later slices.

## Current Problems

`RenderScene` currently mixes several concepts:

- authoring-derived render settings,
- camera and environment settings,
- render materials,
- render-owned textures,
- flat world-space triangles,
- area lights,
- environment sampling distributions,
- and a built BVH.

That shape worked for early CPU milestones, but it does not scale cleanly:

- The glTF importer already approximates metallic/roughness into current
  `MaterialKind` values, losing source material semantics before a compiler can
  make an explicit backend decision.
- `ImportedMaterial` only has a diffuse texture slot, so normal, alpha,
  metallic-roughness, emissive, and future texture slots have no natural home.
- `cpu_path_tracer.cpp` owns path integration, camera rays, texture material
  resolution, direct lighting, environment lighting, MIS, transparent shadow
  visibility, single-medium state, stats, and tile execution. That makes the CPU
  loop the effective renderer contract.
- `RenderTriangle` is the primary geometry representation. A GPU backend will
  want buffer ranges and handles, not importer-shaped world-space triangle
  records.
- Light data is split into special arrays such as `area_lights`; future light
  types and environment sampling need a uniform backend contract.

The architecture needs a boundary where asset semantics can expand without
rewriting backend runtime code, and a second boundary where backend runtime code
can move to CPU, CUDA, or OptiX without importing scene-loader assumptions.

## Target Pipeline

The target pipeline is:

```text
SceneDescription
   -> SceneAssetGraph / AssetDocument
   -> RenderWorld
   -> RenderRequest
   -> Backend RenderResult
```

`scene_parser` remains responsible for TOML syntax, scene authoring validation,
asset references, instance references, and render settings.

The asset layer loads inline geometry, OBJ, glTF, and future asset formats into
`AssetDocument` values. This layer preserves source semantics and reports
asset-level diagnostics. It does not decide how the CPU or CUDA backend should
shade imported materials.

The render compiler consumes the parsed scene, asset documents, scene instances,
and backend-independent render settings. It produces a `RenderWorld` with flat
buffers, stable records, integer handles, light records, texture tables, and
acceleration inputs.

Backends consume `RenderWorld` through a `RenderRequest`. Backends do not include
scene parser or asset importer headers.

## Asset IR

Asset IR is the semantic asset boundary. It should remain close enough to common
asset concepts that glTF and OBJ data can be represented without premature
renderer-specific loss.

The first complete shape should be organized around documents, nodes, meshes,
primitives, materials, images, samplers, and textures:

```cpp
struct AssetDocument {
    std::vector<AssetNode> nodes;
    std::vector<AssetMesh> meshes;
    std::vector<AssetMaterial> materials;
    std::vector<AssetImage> images;
    std::vector<AssetSampler> samplers;
    std::vector<AssetTexture> textures;
    std::vector<AssetDiagnostic> diagnostics;
};

struct AssetNode {
    std::string name;
    int mesh = -1;
    int parent = -1;
    std::vector<int> children;
    AssetTransform local_transform;
};

struct AssetMesh {
    std::string name;
    std::vector<AssetPrimitive> primitives;
};

struct AssetPrimitive {
    AssetAttributeSet attributes;
    AssetIndexBuffer indices;
    int material = -1;
    AssetPrimitiveTopology topology = AssetPrimitiveTopology::Triangles;
};
```

`AssetAttributeSet` should preserve positions, normals, UVs, and future
attributes by slot. The first migration can store typed vectors rather than a
fully generic buffer view model, but the API should avoid assuming there is only
one triangle list and one UV channel forever.

Asset material records should preserve current hand-authored materials and glTF
PBR semantics:

```cpp
struct AssetMaterial {
    std::string name;
    AssetMaterialModel model = AssetMaterialModel::PbrMetallicRoughness;
    Color4f base_color_factor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;
    Color3f emissive_factor{};
    AssetTextureSlot base_color;
    AssetTextureSlot metallic_roughness;
    AssetTextureSlot normal;
    AssetTextureSlot emissive;
    AssetAlphaMode alpha_mode = AssetAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    float ior = 1.5f;
    bool thin = false;
    Color3f absorption_color{1.0f, 1.0f, 1.0f};
    float absorption_distance = 1.0f;
};
```

The material model enum should include at least:

- `PbrMetallicRoughness` for glTF-style imported materials.
- `DiffuseLegacy` for OBJ/MTL diffuse materials and simple current TOML
  diffuse materials.
- `EmissiveLegacy` for current direct emissive materials.
- `MirrorLegacy`, `MetalLegacy`, `PlasticLegacy`, and `DielectricLegacy` for
  current TOML-authored material kinds that do not map exactly onto glTF PBR.

This does not mean the final renderer has separate shader families for every
legacy type. It means the semantic layer records what the author or importer
provided. The render compiler decides how to lower each semantic material into
the current backend-facing model.

Texture slots should preserve usage, UV set, sampler, texture handle, factor,
and color-space expectations where applicable:

```cpp
struct AssetTextureSlot {
    int texture = -1;
    int uv_set = 0;
    float scale = 1.0f;
};
```

Asset IR should not store world-space instance baking as its primary form. Scene
instances provide transforms. The render compiler applies those transforms when
building `RenderWorld`.

## Render IR

Render IR is the backend contract. It should be designed as if it may be packed
to GPU buffers, even while the first consumer is still the CPU backend.

The top-level render world should use tables and integer handles:

```cpp
struct RenderWorld {
    RenderSettings settings;
    RenderCamera camera;

    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<RenderPrimitive> primitives;

    std::vector<RenderMaterialRecord> materials;
    std::vector<RenderImageRecord> images;
    std::vector<RenderSamplerRecord> samplers;
    std::vector<RenderTextureRecord> textures;
    std::vector<Color4f> texels;

    std::vector<RenderLightRecord> lights;
    RenderEnvironmentRecord environment;

    RenderBvh bvh;
};
```

Core constraints:

- Flat arrays are the source of truth.
- Records use integer handles and index ranges.
- Records do not own pointers.
- Records do not know about TOML, OBJ, glTF, or importer diagnostics.
- The CPU backend consumes this IR directly.
- CUDA may later consume this IR through a thin packer that preserves handles and
  ranges.

Geometry should move from `std::vector<RenderTriangle>` as the only execution
shape to indexed buffers:

```cpp
struct RenderVertex {
    Point3f position;
    Vec3f geometric_normal;
    Vec3f shading_normal;
    Vec2f uv0;
    std::uint32_t flags = 0;
};

struct RenderPrimitive {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int material = -1;
    Bounds3f bounds;
    std::uint32_t flags = 0;
};
```

The first slice may build a triangle-level BVH from these buffers and keep a
CPU-friendly hit helper, but the authoritative geometry input should be
`vertices + indices + primitives`.

Material records should be fixed-layout and handle-based:

```cpp
struct RenderMaterialRecord {
    RenderMaterialModel model = RenderMaterialModel::Diffuse;
    Color4f base_color_factor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic_factor = 0.0f;
    float roughness_factor = 1.0f;
    Color3f emissive_factor{};

    int base_color_texture = -1;
    int metallic_roughness_texture = -1;
    int normal_texture = -1;
    int emissive_texture = -1;

    float ior = 1.5f;
    float alpha_cutoff = 0.5f;
    RenderAlphaMode alpha_mode = RenderAlphaMode::Opaque;
    std::uint32_t flags = 0;
};
```

The first CPU implementation can lower current materials into the existing BSDF
behavior, but the record shape should already have stable slots for
metallic-roughness, normal, emissive, alpha, thin glass, and absorption flags.

Textures should be represented as image, sampler, and texture records:

```cpp
struct RenderImageRecord {
    int width = 0;
    int height = 0;
    RenderImageFormat format = RenderImageFormat::LinearRgb32f;
    std::uint32_t texel_offset = 0;
    std::uint32_t texel_count = 0;
};

struct RenderSamplerRecord {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
    TextureFilter min_filter = TextureFilter::Bilinear;
    TextureFilter mag_filter = TextureFilter::Bilinear;
};

struct RenderTextureRecord {
    int image = -1;
    int sampler = -1;
    RenderTextureColorSpace color_space = RenderTextureColorSpace::Linear;
};
```

Lights should use a single record table:

```cpp
struct RenderLightRecord {
    RenderLightType type = RenderLightType::RectArea;
    Color3f radiance{1.0f, 1.0f, 1.0f};
    Point3f position;
    Vec3f u;
    Vec3f v;
    int primitive = -1;
    std::uint32_t flags = 0;
};
```

The first slice only needs rectangle area lights plus constant/HDRI environment
records. The unified table keeps point, sphere, distant, mesh, and other light
types from requiring backend API changes later.

## Material Lowering

Material lowering belongs in the render compiler, not importers and not
backends.

Examples:

- glTF base color factor and base color texture become Asset IR PBR data.
  The render compiler lowers them into `RenderMaterialRecord` fields.
- glTF metallic/roughness values remain numeric PBR semantics in Asset IR.
  The compiler decides whether the current CPU model becomes diffuse, metal,
  plastic, or a new PBR approximation.
- OBJ `Kd` and `map_Kd` become a diffuse-style Asset IR material. The compiler
  lowers it to the current diffuse render model with a base-color texture.
- TOML `glass`, `rough_glass`, and `thin_glass` become dielectric semantic
  materials. The compiler lowers them into dielectric render records with IOR,
  thin, absorption, and roughness fields.
- Unsupported slots such as normal maps and alpha modes are preserved in Asset
  IR. If the active render compiler cannot lower them, it emits render compile
  diagnostics instead of silently discarding them.

This makes unsupported feature handling explicit. It also lets future backends
support a feature without changing every importer.

## Backend API

The backend boundary should accept only the render world:

```cpp
struct RenderRequest {
    const RenderWorld& world;
};

struct RenderResult {
    Film film;
    RenderStats stats;
    std::vector<RenderDiagnostic> diagnostics;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual RenderBackendKind Kind() const = 0;
    virtual RenderResult Render(const RenderRequest& request) = 0;
};
```

CPU internals can still be split into helpers:

- `CpuPathTracer`
- `CpuDebugDirect`
- `CpuRenderWorldAccessor`
- `CpuTextureSampler`
- `CpuLightSampler`
- `CpuVisibility`
- `CpuMediumState`

Those helpers are implementation details. They should not alter the Render IR
contract.

The CUDA backend can remain a not-implemented backend in the first architecture
slices, but it should receive the same `RenderRequest` shape. This keeps the
future CUDA entry point honest.

## Diagnostics

Diagnostics should be separated by stage:

- Scene diagnostics: TOML syntax, missing camera, unknown asset reference,
  unknown material reference, invalid authoring fields.
- Asset diagnostics: failed file load, unsupported asset extension, unsupported
  glTF accessor, missing texture image, unsupported wrap mode fallback.
- Render compile diagnostics: unsupported material slot, alpha mode not lowered,
  normal map ignored, unsupported topology, backend feature not supported by the
  current render model.
- Backend diagnostics: CUDA unavailable, device memory failure, runtime launch
  failure, unsupported backend option.

The first slice may preserve the current `SceneDiagnostic` result type for CLI
plumbing, but the implementation should make diagnostic source stages explicit
internally so they can be split cleanly later.

## Migration Slices

### Slice 1: IR Scaffolding And Vertical Slice

Build the new data structures and route one complete path through them.

Scope:

- Add Asset IR type definitions.
- Add Render IR type definitions.
- Convert inline triangles, inline quads, and current hand-authored simple
  materials into Asset IR.
- Compile that Asset IR into `RenderWorld`.
- Make the CLI default pipeline use `SceneDescription -> Asset IR -> RenderWorld
  -> CPU backend` for the covered path.
- Let the CPU path tracer consume `RenderWorld` through helpers or a temporary
  adapter.

Acceptance:

- Existing CTest suite passes.
- `minimal`, `cornell_box_path`, and `glass_showcase` render through the new
  default pipeline.
- No temporary untracked example asset is required or included in this slice.
- Any remaining old `RenderScene` dependency is documented as transitional.

### Slice 2: OBJ And glTF Importer Migration

Move importers from renderer-shaped output to Asset IR output.

Scope:

- Make OBJ/MTL produce `AssetDocument`.
- Make glTF/GLB produce `AssetDocument`.
- Preserve glTF base color, metallic, roughness, normal, emissive, alpha, and
  double-sided semantics where they exist in supported static mesh data.
- Move current metallic/roughness approximation out of the glTF loader and into
  render compilation.
- Emit render compile diagnostics for preserved but unsupported slots.

Acceptance:

- Existing OBJ and glTF tests pass.
- New tests prove imported metallic/roughness semantics survive importer output.
- Normal and alpha slots can be represented and diagnosed even before they
  affect shading.

### Slice 3: Backend Contract Cleanup

Make CPU backend code depend on Render IR, not scene or asset layers.

Scope:

- Remove backend includes of scene parser and asset importer headers.
- Make `debug_direct` and `path` both consume `RenderWorld`.
- Move material resolution, texture sampling, light sampling, and environment
  sampling helpers to Render IR accessors or backend-local helpers.
- Return stats and diagnostics through `RenderResult`.
- Keep CUDA as a not-implemented backend that accepts the same request shape.

Acceptance:

- CPU unit and CLI tests pass.
- Include boundaries show backend code does not depend on scene or importer
  headers.
- Existing render behavior remains close enough for current visual sanity tests.

### Slice 4: GPU Packer Preparation

Prepare the final host-side step needed for CUDA without writing the CUDA
integrator.

Scope:

- Define `PackedRenderWorld` or `GpuRenderPackage` as a thin packing output from
  `RenderWorld`.
- Validate handle integrity, offsets, buffer ranges, and record alignment.
- Decide how texture texels, BVH nodes, primitives, materials, lights, and
  environment distributions map to linear buffers.
- Add tests for pack offsets and reference integrity.

Acceptance:

- No rendering behavior change.
- Packing tests prove records can be linearized without pointer ownership.
- CUDA follow-up work has a concrete input package.

## Testing Strategy

The architecture change needs tests at each boundary:

- Asset IR tests: parser/importer output preserves material slots, texture
  slots, sampler state, primitive material assignment, and static node
  transforms.
- Render compile tests: buffer ranges, material handles, texture handles, light
  records, environment records, and lowering diagnostics are correct.
- Backend smoke tests: current CLI render tests continue to exercise the default
  render command.
- Visual sanity tests: existing glass showcase visual sanity remains in place;
  future material and asset showcase sanity tests can be added after the IR
  migration stabilizes.
- Include boundary checks: backend code should not include scene parser or asset
  importer headers after Slice 3.
- Regression tests: all current unit tests and CTest CLI tests should continue
  to pass after each slice.

The first slice should prefer small deterministic tests over broad screenshot
approval. Visual sanity tests are valuable for renderer regressions, but the new
boundaries need structural unit tests.

## Risks And Mitigations

- **Risk: The refactor becomes too large to verify.** Mitigation: implement as
  vertical slices with existing CTest passing after each slice.
- **Risk: Render IR overfits current CPU behavior.** Mitigation: enforce flat
  arrays, integer handles, stable record layouts, and no importer concepts in
  Render IR.
- **Risk: Asset IR becomes a full arbitrary shader graph too early.**
  Mitigation: use structured PBR and legacy material records; leave shader
  graphs out of scope.
- **Risk: Imported features are silently lost.** Mitigation: preserve semantic
  slots in Asset IR and emit render compile diagnostics when lowering ignores a
  slot.
- **Risk: CPU migration hides old dependencies behind adapters forever.**
  Mitigation: allow adapters only in Slice 1 and make backend dependency cleanup
  the explicit goal of Slice 3.
- **Risk: GPU packing requirements force another redesign.** Mitigation: design
  Render IR with GPU constraints from the start and add packing validation before
  CUDA integration.

## Success Criteria

- The architecture has explicit `Asset IR`, `Render IR`, and backend runtime
  boundaries.
- Importers preserve asset material semantics instead of lowering directly to
  CPU renderer material choices.
- The CPU backend consumes the same `RenderWorld` contract intended for future
  CUDA and OptiX work.
- Render IR uses flat arrays and integer handles suitable for later GPU packing.
- Existing tests continue to pass after each migration slice.
- Unsupported imported features become diagnostics, not silent data loss.

## Future Work

- Real CUDA path tracing backend consuming packed Render IR.
- Full glTF PBR material shading parity.
- Normal maps, alpha modes, emissive textures, metallic-roughness textures, and
  texture transform support.
- Mesh lights and additional analytic light types.
- Animation, skinning, morph target, and sparse accessor import.
- GPU BVH construction or CPU-built BVH upload format.
- Integrator API cleanup after the Render IR and backend boundaries are stable.

## Implementation Status

Planned. This document defines the architecture target and migration slices for
the next YaoRay refactor. No implementation slice has started yet.
