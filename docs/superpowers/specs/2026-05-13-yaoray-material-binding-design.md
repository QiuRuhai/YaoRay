# YaoRay Material Binding Design

Date: 2026-05-13

## Purpose

YaoRay now has CPU direct lighting that consumes `RenderMaterial::albedo` and `RenderMaterial::emission`, but scene authors cannot define materials in TOML or bind them to instances. The compiler still creates one default material per instance. That keeps old tests simple, but it prevents Cornell Box style scenes from expressing red walls, green walls, white walls, and emissive debug surfaces cleanly.

This slice adds the smallest scene-level material system needed for Cornell-style test scenes: named diffuse/emissive materials and optional instance material binding.

## Goals

- Add `[[materials]]` to TOML scene files.
- Support material fields:
  - `name`
  - `albedo`
  - `emission`
- Add optional `material = "name"` to each `[[instances]]` entry.
- Compile named material descriptions into `RenderScene::materials`.
- Assign referenced material indices to all triangles emitted for an instance.
- Preserve old scenes: instances without `material` still receive a default material.
- Validate duplicate material names.
- Validate instance references to unknown material names.
- Keep `RenderMaterial` unchanged: it already has `albedo` and `emission`.
- Keep material parsing independent of OBJ `.mtl` files and imported asset materials.

## Non-Goals

- No roughness, metallic, specular, glass, subsurface, or BRDF type system.
- No textures or UVs.
- No OBJ `.mtl` import.
- No glTF material import.
- No per-face material IDs.
- No material override inheritance or material libraries.
- No renderer changes; CPU direct lighting already consumes `RenderMaterial`.
- No Cornell Box scene in this slice. This slice prepares the material binding needed for it.

## Approved Decisions

First material model:

```toml
[[materials]]
name = "white"
albedo = [0.75, 0.75, 0.75]
emission = [0, 0, 0]
```

`albedo` defaults to `[0.8, 0.8, 0.8]`. `emission` defaults to `[0, 0, 0]`.

Instances may bind a material:

```toml
[[instances]]
asset = "left_wall"
material = "red"
translate = [0, 0, 0]
rotate_degrees = [0, 0, 0]
scale = [1, 1, 1]
```

If `material` is omitted, the compiler preserves current behavior by creating a default material for that instance. This avoids breaking all current fixtures and examples.

Allow emissive materials in the first slice. `RenderMaterial::emission` already exists and CPU direct lighting already adds it on hits. Keeping it in the TOML material model costs little and makes future emissive debug geometry possible.

## TOML Schema

Add root-level `materials` as an optional array of tables:

```toml
[[materials]]
name = "mat_name"
albedo = [0.8, 0.8, 0.8]
emission = [0, 0, 0]
```

Field rules:

- `name` is required and must not be empty.
- `albedo` is optional and must be a 3-number vector if present.
- `emission` is optional and must be a 3-number vector if present.
- Unknown fields inside `[[materials]]` are diagnostics.
- Duplicate material names are diagnostics.

`[[instances]]` gains one optional field:

```toml
material = "mat_name"
```

Field rules:

- `material` is optional.
- If present, it must be a string and must not be empty.
- If present, it must reference a material declared in `[[materials]]`.
- Unknown fields inside `[[instances]]` should accept `material`.

Root unknown-field validation must add `materials` to the allowed root keys.

## Scene Data Model

Add to `include/yaoray/scene/scene.hpp`:

```cpp
struct MaterialDescription {
    std::string name;
    Color3f albedo{0.8f, 0.8f, 0.8f};
    Color3f emission;
};
```

Extend `InstanceDescription`:

```cpp
struct InstanceDescription {
    std::string asset;
    TransformDescription transform;
    std::string material;
};
```

Extend `SceneDescription`:

```cpp
std::vector<MaterialDescription> materials;
```

This keeps semantic scene data separate from compiled render data. `RenderMaterial` remains in the render layer.

## Compiler Behavior

`CompileScene()` should build a local name-to-material-index map before expanding instances:

1. For each `MaterialDescription`, append a `RenderMaterial` to `compiled.materials`.
2. Store `material.name -> index`.
3. If an instance specifies `material`, look up the referenced name.
4. If the name is missing from the map, emit `instances.material` diagnostic and fail compilation.
5. If an instance omits `material`, append a default `RenderMaterial{}` and use that index.
6. Emit all triangles for that instance with the chosen material index.

This preserves the current one-default-material-per-unbound-instance behavior while letting bound instances share named material slots.

The compiler should not inspect asset-internal materials. OBJ geometry remains geometry-only.

## Error Handling

Parser diagnostics:

- `materials.name`: missing required field.
- `materials.name`: must not be empty.
- `materials`: duplicate material name.
- `materials.albedo`: vector validation errors from existing vector readers.
- `materials.emission`: vector validation errors from existing vector readers.
- `instances.material`: must be a string.
- `instances.material`: must not be empty.

Compiler diagnostics:

- `instances.material`: references unknown material.

The parser already validates `instances.asset` references. It should validate material references after parsing all materials and instances if possible. Keeping the unknown-material check in the compiler is acceptable because the compiler already resolves renderer-facing indices and returns scene diagnostics.

## Testing Strategy

Scene parser tests:

- Loads `[[materials]]` with name, albedo, and emission.
- Applies default albedo/emission when optional fields are omitted.
- Parses `instances.material`.
- Rejects duplicate material names.
- Rejects empty material names.
- Rejects empty instance material references.
- Rejects unknown fields inside `[[materials]]`.
- Rejects misdeclared `[materials]` table instead of `[[materials]]`.

Scene compiler tests:

- Compiles named materials into `RenderScene::materials`.
- Instance bound to a material uses the referenced material index.
- Two instances can share one named material index.
- Unbound instances still get default materials.
- Unknown material references fail with `instances.material`.
- Existing builtin triangle and OBJ tests keep passing.

Renderer tests do not need new behavior here because CPU direct lighting already tests `RenderMaterial::albedo` and `RenderMaterial::emission` directly.

CLI tests:

- Existing render fixtures keep passing.
- A render fixture may be updated to use material binding once parser/compiler coverage is in place.

## Documentation

Update `README.md` and `docs/architecture/overview.md` to state:

- TOML scenes support named diffuse/emissive materials.
- Instances can bind named materials.
- This prepares Cornell Box style scenes.
- Textures, imported materials, roughness/metallic, and full PBR remain future work.

## Completion Criteria

- `SceneDescription` contains material descriptions.
- `InstanceDescription` supports optional material names.
- Scene parser supports `[[materials]]` and `instances.material`.
- Duplicate/empty/misdeclared material inputs produce diagnostics.
- Scene compiler emits `RenderMaterial` records from named materials.
- Instance triangles receive correct material indices.
- Old scenes without material bindings still compile and render.
- Full Debug build and CTest pass.

## Future Work

Likely follow-up slices:

1. Add a Cornell-style debug scene using named materials.
2. Add reusable primitive or mesh assets for walls, floor, ceiling, and boxes.
3. Add path tracing so Cornell Box shows indirect bounce and color bleeding.
4. Add roughness/specular material fields after diffuse baseline is stable.
5. Add textures and UV handling.
6. Add OBJ `.mtl` or glTF material import.
