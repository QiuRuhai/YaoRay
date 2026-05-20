# Khronos glTF Sample Assets

YaoRay keeps a small subset of official Khronos glTF Sample Assets as importer
fixtures. These files are intentionally tiny and are used to verify file-format
plumbing, not final visual quality.

Source repository:
https://github.com/KhronosGroup/glTF-Sample-Assets

Khronos asset browser:
https://github.khronos.org/glTF-Assets/

## Imported Fixtures

- `tests/fixtures/assets/gltf/Triangle/glTF/`
  - Source: `Models/Triangle/glTF/`
  - Purpose: indexed triangle primitive.
  - License: CC0 1.0 Universal, credited to Marco Hutter.
- `tests/fixtures/assets/gltf/TriangleWithoutIndices/glTF/`
  - Source: `Models/TriangleWithoutIndices/glTF/`
  - Purpose: non-indexed triangle primitive.
  - License: CC0 1.0 Universal, credited to Marco Hutter.
- `tests/fixtures/assets/gltf/SimpleTexture/glTF/`
  - Source: `Models/SimpleTexture/glTF/`
  - Purpose: external `.bin` plus base-color PNG texture.
  - License: CC0 1.0 Universal, credited to Marco Hutter.
- `tests/fixtures/assets/gltf/BoxTextured/glTF-Binary/BoxTextured.glb`
  - Source: `Models/BoxTextured/glTF-Binary/BoxTextured.glb`
  - Purpose: binary `.glb` container smoke coverage.
  - License: CC-BY 4.0 International with Cesium trademark/logo limitations.

The render example under `scenes/examples/assets/gltf/SimpleTexture/glTF/`
copies the same `SimpleTexture` CC0 files so the CLI can exercise a real glTF
asset without depending on test fixture paths.

## Current Import Scope

YaoRay v1 support covers static `.gltf` files with external buffers and images,
binary `.glb` files, default or first scenes, node transforms, triangle mesh
primitives, indexed and non-indexed geometry, positions, optional normals,
optional UVs, base color factors, base color textures, roughness/metallic
factors mapped to existing material kinds, and emissive factors.

Unsupported features include animation, skinning, morph targets, glTF cameras or
lights, sparse accessors, Draco or meshopt compression, normal maps, alpha modes,
occlusion/emissive/roughness/metallic textures, KHR material extensions, and
exact glTF PBR shading.
