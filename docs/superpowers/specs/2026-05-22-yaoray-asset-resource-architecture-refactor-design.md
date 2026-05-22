# YaoRay Asset Resource Architecture Refactor Design

## Summary

This refactor prepares YaoRay for fuller glTF asset support and future CUDA or
OptiX backends by separating asset resources, backend-neutral render input, and
backend-specific prepared data.

The current renderer compiles TOML scenes directly into a flat `RenderScene`
that contains render input and a CPU BVH. That works for the CPU path tracer but
puts CPU runtime data in the shared render contract. It also makes
`scene_compiler.cpp` responsible for asset loading, format dispatch, material
mapping, instance expansion, texture caching, and BVH construction.

The new direction keeps the current CPU renderer behavior but changes the
architecture in stages:

```text
TOML authoring layer
Asset resource layer
Render compile layer
Backend layer
```

## Goals

- Introduce a backend-neutral render scene IR that does not contain CPU BVH,
  CUDA device pointers, or OptiX handles.
- Move CPU acceleration data into a CPU-specific prepared scene.
- Introduce a formal `AssetResource` model that can represent glTF-style scene
  graphs, nodes, meshes, primitives, material slots, textures, images, and
  samplers.
- Keep current TOML asset and instance syntax compatible in the first
  implementation stages.
- Let OBJ and glTF loaders output the same asset resource model.
- Keep CPU debug and path tracer output behavior equivalent after each stage.
- Leave room for later CUDA and OptiX backends to prepare their own runtime
  layouts from the same backend-neutral IR.

## Non-Goals

- No CUDA renderer implementation in this refactor.
- No OptiX renderer implementation in this refactor.
- No full glTF PBR material rewrite.
- No glTF animation, skinning, morph targets, cameras, or lights.
- No asset database, asynchronous loading, persistent resource cache, or hot
  reload system.
- No TOML syntax redesign in the first stage.
- No per-material-slot scene override syntax in the first stage.
- No image-output or integrator behavior change.

## Current Problems

`RenderScene` currently mixes two concepts:

- backend-neutral render input such as camera, triangles, materials, textures,
  lights, environment settings, and render settings;
- CPU runtime data, specifically `RenderBvh`.

That coupling is acceptable while CPU is the only real backend, but it is the
wrong long-term contract for CUDA and OptiX. CUDA will likely need compact GPU
buffers and device memory. OptiX will need GAS, IAS, SBT records, and OptiX
handles. Those structures should not leak into the shared render compiler
output.

The asset path has a similar issue. OBJ and glTF loaders already share
`ImportedMesh`, but that type is a flat triangle package shaped around the
current renderer. It cannot naturally represent fuller glTF scene graphs,
primitive material slots, images, samplers, or future node-level features.

## Target Architecture

### TOML Authoring Layer

The TOML layer remains the user-facing scene description. In the first stages,
existing `[[assets]]` and `[[instances]]` syntax should keep working.

Instances continue to reference whole assets. They do not reference internal
glTF nodes or primitives in this refactor. The asset resource layer stores the
internal node hierarchy, and the render compiler traverses it from the default
asset scene.

### Asset Resource Layer

The asset layer owns file-format import. OBJ and glTF loaders both output an
`AssetResource`. File-format details stay inside `yaoray_assets`.

The resource model should be close enough to glTF to avoid another rewrite when
fuller glTF support arrives, but it should use YaoRay names and types rather
than exposing tinygltf or tinyobjloader types.

Proposed shape:

```cpp
struct AssetResource {
    std::vector<AssetScene> scenes;
    int default_scene = 0;

    std::vector<AssetNode> nodes;
    std::vector<AssetMesh> meshes;
    std::vector<AssetMaterial> materials;
    std::vector<AssetTexture> textures;
    std::vector<AssetImage> images;
    std::vector<AssetSampler> samplers;
};

struct AssetScene {
    std::string name;
    std::vector<int> root_nodes;
};

struct AssetNode {
    std::string name;
    AssetTransform transform;
    int mesh = -1;
    std::vector<int> children;
};

struct AssetTransform {
    std::array<float, 16> local_to_parent;
};

struct AssetMesh {
    std::string name;
    std::vector<AssetPrimitive> primitives;
};

struct AssetPrimitive {
    AssetPrimitiveTopology topology = AssetPrimitiveTopology::Triangles;
    std::vector<Point3f> positions;
    std::vector<Vec3f> normals;
    std::vector<Vec2f> texcoords0;
    std::vector<std::uint32_t> indices;
    int material = -1;
};

struct AssetMaterial {
    std::string name;
    MaterialKind approximate_type = MaterialKind::Diffuse;
    Color3f base_color{0.8f, 0.8f, 0.8f};
    Color3f emission;
    float metallic = 0.0f;
    float roughness = 1.0f;
    float specular = 0.04f;
    int base_color_texture = -1;
};

struct AssetTexture {
    int image = -1;
    int sampler = -1;
};

struct AssetImage {
    std::filesystem::path path;
};

struct AssetSampler {
    TextureWrap wrap_s = TextureWrap::Repeat;
    TextureWrap wrap_t = TextureWrap::Repeat;
};
```

Implementation may adjust names to match local style, but the implementation
must preserve these boundaries:

- `AssetResource` describes the asset itself, not a TOML instance.
- Nodes store local transforms and child links. `AssetTransform` stores a
  4x4 local-to-parent matrix so glTF node matrices and TRS transforms can be
  represented without decomposition.
- Meshes contain primitives.
- Primitives own topology, attributes, indices, and material slot references.
- Materials remain closer to imported asset semantics than to current
  `RenderMaterial`.
- Textures reference image and sampler records separately so glTF image and
  sampler state can survive import before render texture compilation.

OBJ import can produce a simple resource: one scene, one root node, one mesh,
triangle primitives, and imported materials.

### Render Compile Layer

The render compiler owns conversion from TOML scene plus asset resources to a
backend-neutral render input:

```text
SceneDescription + AssetResource -> RenderSceneIR
```

`RenderSceneIR` should contain camera, render settings, materials, textures,
environment data, area lights, and renderable geometry. It should not contain
backend runtime structures.

Proposed shape:

```cpp
struct RenderSceneIR {
    RenderBackendKind requested_backend = RenderBackendKind::Cpu;
    RenderIntegratorKind integrator = RenderIntegratorKind::DebugDirect;
    RenderSamplerKind sampler = RenderSamplerKind::Independent;

    int width = 0;
    int height = 0;
    int spp = 1;
    int max_depth = 5;
    std::uint64_t seed = 0;
    int threads = 0;
    int light_samples = 1;
    float radiance_clamp = 0.0f;

    RenderCamera camera;
    RenderEnvironment environment;

    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;
    std::vector<RenderEnvironmentDistribution> environment_distributions;
    std::vector<RenderTriangle> triangles;
    std::vector<RenderAreaLight> area_lights;
};
```

The compiler traverses asset scenes from root nodes, accumulates transforms,
expands mesh primitives into the current flat render geometry, maps asset
materials to render materials, and applies scene instance material overrides.

Material override semantics stay simple:

- Without `instance.material`, each primitive uses its asset material slot.
- With `instance.material`, every primitive under that instance uses the scene
  material override.

Per-slot material remapping is future work. The resource model should not block
it, but the TOML syntax and implementation are out of scope for the first
stages.

### Backend Layer

Backends receive `RenderSceneIR` and prepare their own runtime representation.

For CPU:

```cpp
struct CpuPreparedScene {
    const RenderSceneIR* scene = nullptr;
    RenderBvh bvh;
};

CpuPreparedScene PrepareCpuScene(const RenderSceneIR& scene);
```

CPU debug and path tracing read geometry, materials, lights, textures, and
settings from `RenderSceneIR`, while BVH traversal uses `CpuPreparedScene`.

Future CUDA and OptiX backends should follow the same pattern:

- CUDA prepares device buffers and any GPU acceleration data.
- OptiX prepares GAS, IAS, SBT records, and OptiX handles.

None of those backend-prepared structures belong in `RenderSceneIR`.

## Phased Plan

### Phase 1: RenderSceneIR And CPUPreparedScene

Create the backend-neutral render contract before changing asset import.

Scope:

- Rename or introduce `RenderSceneIR` for backend-neutral data.
- Remove `RenderBvh` from the shared render compiler output.
- Add `CpuPreparedScene` and CPU prepare logic that builds the BVH.
- Update `RenderBackend::Render()` to accept the backend-neutral IR.
- Update CPU debug and path tracer entry points to use prepared CPU data.
- Preserve current TOML syntax, asset import behavior, rendered output, and
  stats semantics.

This phase establishes the backend boundary needed for CUDA and OptiX.

### Phase 2: AssetResource

Introduce the formal asset resource model while keeping render output
equivalent.

Scope:

- Add `AssetResource` and related asset structs.
- Convert OBJ loader to output a simple asset resource.
- Convert glTF loader to output asset scenes, nodes, meshes, primitives, and
  materials.
- Update the render compiler to traverse asset resources into `RenderSceneIR`.
- Keep TOML instances referencing whole assets.
- Preserve current scene examples and tests.

This phase establishes the asset boundary needed for fuller glTF support.

### Phase 3: Compiler Cleanup

Split compiler responsibilities after the new contracts are in place.

Scope:

- Separate asset cache and loader dispatch from the top-level scene compiler.
- Separate asset-resource-to-render-geometry compilation.
- Separate asset and scene material mapping.
- Centralize transform and normal-transform helpers.
- Add focused diagnostics for unsupported resource features.
- Preserve existing rendered output.

This phase reduces file-level coupling and makes later glTF material and node
features easier to add.

## Testing Strategy

Phase 1 tests:

- Existing render scene compiler tests should still verify camera, material,
  texture, environment, light, and triangle output.
- CPU backend tests should verify that BVH stats are produced from CPU prepared
  data.
- CLI smoke tests should keep the same output expectations except where wording
  must reflect the new boundary.

Phase 2 tests:

- OBJ importer should produce a one-scene asset resource with expected mesh,
  primitive, material, UV, and normal data.
- glTF importer should preserve node hierarchy, transforms, primitive material
  slots, normals, UVs, indices, and texture references.
- Scene compiler tests should verify default asset scene traversal and instance
  transform accumulation.
- Material override tests should verify whole-instance override behavior.

Phase 3 tests:

- Existing scene and render compiler tests should pass unchanged.
- Add diagnostics tests for unsupported topology and invalid node, mesh,
  primitive, material, texture, or sampler references.

## Risks And Mitigations

- **Large mechanical churn:** Split the work into phases. Phase 1 does not
  introduce `AssetResource`; Phase 2 does not clean every compiler file.
- **Behavior drift:** Keep rendered output and current tests equivalent after
  each phase.
- **Overbuilding the asset layer:** Model only static scenes, nodes, meshes,
  primitives, materials, textures, images, and samplers. Leave animation,
  skinning, morphs, cameras, and lights out of scope.
- **Premature GPU assumptions:** Keep `RenderSceneIR` neutral. Do not add CUDA
  or OptiX-specific fields until those backends exist.
- **Material architecture sprawl:** Keep asset material import and render
  material evaluation separate. Fuller glTF PBR can be designed as a later
  material slice.

## Success Criteria

- `RenderSceneIR` contains no CPU BVH, CUDA pointer, or OptiX handle.
- CPU rendering still works through a CPU-specific prepared scene.
- `AssetResource` can represent static glTF scenes with nodes, meshes,
  primitives, material slots, textures, images, and samplers.
- OBJ and glTF loaders share the same asset resource output.
- The render compiler consumes asset resources rather than format-specific mesh
  packages.
- Current scenes and tests keep their behavior.
- The architecture provides a clear path for future CUDA and OptiX prepared
  scenes.

## Implementation Status

Design approved for phased planning.

Phase 1 implementation status: `RenderSceneIR` and `CpuPreparedScene` have been implemented. The render compiler now outputs backend-neutral render input, and the CPU backend builds its own BVH during scene preparation.
