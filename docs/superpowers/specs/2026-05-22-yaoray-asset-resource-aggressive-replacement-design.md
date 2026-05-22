# YaoRay AssetResource Aggressive Replacement Design

## Summary

This phase replaces the current flat imported asset interface with a formal
`AssetResource` model. The goal is to make OBJ and glTF import share one
asset-layer contract that can represent static glTF scene structure without
leaking tinygltf, tinyobjloader, or render-backend details into the renderer.

This is an aggressive replacement, not a compatibility bridge:

- `ImportedMesh`, `ImportedTriangle`, and `ImportedMaterial` are removed as the
  main public asset API.
- OBJ and glTF loaders return `AssetResource`.
- The render compiler consumes `AssetResource` directly and expands it into the
  existing `RenderSceneIR`.

The functional scope stays narrow. This phase does not add full glTF PBR,
animation, skinning, morph targets, imported cameras, imported lights, or
per-material-slot TOML overrides.

## Previous Problem

Before Phase 2, the asset layer returned `ImportedMesh`, a renderer-shaped flat
triangle package. It was workable for OBJ and simple glTF fixtures, but it lost
important asset structure:

- glTF scenes and default-scene selection are flattened in the loader.
- glTF node hierarchy and local transforms are baked before the render compiler
  sees them.
- meshes and primitives are not represented as stable asset resources.
- material slots, texture references, images, and samplers are compressed into
  render-oriented imported material fields.

That makes fuller glTF support harder because every new glTF feature has to
fight a flat triangle API.

## Goals

- Introduce `AssetResource` as the public imported-asset model.
- Preserve current render behavior for existing OBJ, glTF, inline, and builtin
  scenes.
- Keep TOML syntax unchanged.
- Preserve whole-instance material override semantics.
- Preserve current CPU debug and path tracer outputs within existing test
  tolerances.
- Keep asset loading format-specific, but make renderer compilation
  format-neutral.
- Make glTF node, mesh, primitive, material, texture, image, and sampler data
  explicit enough for later glTF work.

## Non-Goals

- No CUDA or OptiX implementation.
- No changes to `RenderSceneIR` other than what is needed to consume assets.
- No glTF animation, skinning, morph target, camera, or light import.
- No full glTF PBR material system.
- No alpha mode, normal map, occlusion map, emissive texture, metallic-roughness
  texture, or texture-transform import.
- No per-primitive or per-material-slot TOML override syntax.
- No persistent asset database, async loading, hot reload, or global cache.
- No render output format changes.

## Target Asset Model

The asset layer owns imported asset structure. It should use YaoRay types and
names, not tinygltf or tinyobjloader types.

```cpp
enum class AssetPrimitiveTopology {
    Triangles,
};

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

struct AssetTransform {
    std::array<float, 16> local_to_parent;
};

struct AssetNode {
    std::string name;
    AssetTransform transform;
    int mesh = -1;
    std::vector<int> children;
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

The exact header name can be `include/yaoray/assets/asset_resource.hpp`.
`AssetLoadResult` should hold `std::optional<AssetResource> resource`.

## Loader Contracts

### OBJ Loader

The OBJ loader outputs a simple `AssetResource`:

- one `AssetScene`;
- one root `AssetNode`;
- one `AssetMesh`;
- one or more triangle `AssetPrimitive` records;
- OBJ material records mapped into `AssetMaterial`;
- OBJ diffuse texture paths represented through `AssetTexture`,
  `AssetImage`, and `AssetSampler`.

OBJ has no meaningful scene graph in this implementation. Its root node uses
identity transform, and its mesh stores OBJ geometry in asset-local space.

The loader groups OBJ faces by material slot. Each group becomes one triangle
primitive with a single `AssetPrimitive::material` value. It must preserve
positions, optional UVs, optional vertex normals, and material slot references.

### glTF Loader

The glTF loader outputs the static glTF scene structure:

- one `AssetScene` per glTF scene;
- `default_scene` from `model.defaultScene`, falling back to scene 0;
- one `AssetNode` per glTF node, preserving name, local transform, mesh index,
  and child links;
- one `AssetMesh` per glTF mesh;
- one `AssetPrimitive` per supported glTF primitive;
- one `AssetMaterial` per glTF material;
- separate `AssetTexture`, `AssetImage`, and `AssetSampler` records.

The loader must not bake node transforms into positions. Local transforms stay
on nodes and are composed by the render compiler.

Supported primitive mode remains `TRIANGLES` only. Unsupported modes produce
diagnostics. Accessor support remains limited to the existing supported static
position, normal, UV, and index cases.

## Render Compiler Contract

The render compiler loads and caches `AssetResource` values by asset path. It
then converts semantic scene instances into `RenderSceneIR` geometry:

```text
SceneDescription instance
  -> asset default scene
  -> root nodes
  -> recursive node traversal
  -> mesh primitive expansion
  -> RenderSceneIR triangles
```

Transform order is:

```text
world = instance_transform * accumulated_node_local_transform
```

This preserves current TOML instance transform behavior while letting glTF
nodes remain local until compilation.

For each triangle:

- positions are transformed by the accumulated world transform;
- geometric normals are derived from transformed positions;
- vertex normals are transformed by the normal transform and normalized;
- UVs are copied when all referenced vertices have `TEXCOORD_0`;
- the primitive material slot maps through `AssetMaterial` unless overridden by
  `instance.material`;
- if no asset material is valid and no override exists, the compiler creates or
  reuses a fallback default render material for that asset expansion.

## Material And Texture Mapping

The asset layer stores imported material semantics. The render compiler maps
them to the current `RenderMaterial` model.

Current mapping:

- `AssetMaterial::approximate_type` maps to `RenderMaterial::type`;
- `base_color` maps to `albedo`;
- `emission` maps to `emission`;
- `roughness` maps to `roughness`;
- `specular` maps to `specular`;
- `base_color_texture` maps to `albedo_texture` after loading the referenced
  PNG image with the referenced sampler wrap modes.

glTF metallic remains an import hint for choosing the current approximate
material type. It is not a full PBR shading implementation in this phase.

Texture caching remains in the render compiler for now. The cache key should
still include image path and sampler wrap modes so duplicate texture use does
not duplicate render textures.

## Diagnostics

Loader diagnostics stay in `AssetLoadResult` as `errors` and `warnings`.
The render compiler converts those into scene diagnostics using `assets.path`.

Compiler-side diagnostics should cover invalid resource references:

- invalid default scene;
- invalid root node;
- invalid child node;
- invalid mesh index;
- invalid material index;
- invalid texture, image, or sampler reference;
- unsupported primitive topology;
- malformed primitive attribute or index data.

## Migration

This phase removes the old imported asset API rather than keeping a compatibility
bridge:

- delete `ImportedMesh`, `ImportedTriangle`, and `ImportedMaterial`;
- replace `AssetLoadResult::mesh` with `AssetLoadResult::resource`;
- replace `LoadObjMesh()` with `LoadObjResource()`;
- replace `LoadGltfMesh()` with `LoadGltfResource()`;
- update tests to assert `AssetResource` structure;
- update `scene_compiler.cpp` to cache and traverse `AssetResource`;
- update docs that mention flat imported meshes.

The old mesh loader names should not remain in production code after this
phase. Tests and compiler code should call the resource-named loader APIs.

## Testing Strategy

Asset model tests:

- default `AssetTransform` is identity;
- default `AssetResource` has no scenes and `default_scene == 0`;
- default primitive topology is triangles.

OBJ tests:

- triangle OBJ produces one scene, one root node, one mesh, and one triangle
  primitive;
- quad OBJ triangulates to two triangles through indices or primitive data;
- UV and vertex normal fixtures preserve attributes;
- MTL diffuse material and base-color texture import through material, texture,
  image, and sampler records;
- duplicate material names still produce errors.

glTF tests:

- indexed and non-indexed triangle fixtures import as resources;
- default scene and root nodes are preserved;
- node local transform is preserved instead of baked into positions;
- base-color texture, image path, and sampler wrap modes are preserved;
- unsupported wrap modes still warn and default to repeat;
- GLB fixture still loads.

Compiler tests:

- OBJ and glTF assets still compile to the same expected render triangle counts;
- instance transforms compose with asset node transforms;
- asset material slots map to render materials;
- whole-instance material override still overrides all primitives;
- duplicated asset texture usage is cached.

End-to-end tests:

- full `ctest --test-dir build --output-on-failure -C Debug` passes on macOS;
- CLI examples continue to render PNG outputs.

## Risks And Mitigations

- **Large churn:** Keep feature scope fixed. Do not add new glTF features while
  replacing the asset contract.
- **Transform regressions:** Add compiler tests that compare transformed vertex
  positions for glTF nodes and TOML instance transforms.
- **Material drift:** Keep current approximate material mapping and assert
  existing texture/material fixtures.
- **Texture duplication:** Preserve path plus sampler wrap cache keys.
- **Diagnostics becoming vague:** Keep loader errors format-specific and map
  compiler resource errors to `assets.path`.

## Success Criteria

- No production code depends on `ImportedMesh`, `ImportedTriangle`, or
  `ImportedMaterial`.
- OBJ and glTF loaders return `AssetResource`.
- glTF node hierarchy and local transforms survive asset loading.
- Render compiler expands `AssetResource` to `RenderSceneIR`.
- Current TOML scenes and examples render successfully.
- Full CTest passes on macOS.
- Architecture docs describe the asset resource layer as implemented.

## Implementation Status

Implemented in Phase 2. OBJ and glTF loaders now return AssetResource, the render compiler traverses asset resources directly, and the old ImportedMesh asset API has been removed.
