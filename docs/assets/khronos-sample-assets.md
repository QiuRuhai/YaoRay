# Khronos glTF Sample Assets

YaoRay keeps selected Khronos glTF Sample Assets for importer and render-pipeline
compatibility validation.

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
copies the same `SimpleTexture` CC0 files so the CLI can exercise a small glTF
asset without depending on test fixture paths.

## Committed Validation Asset

- `scenes/examples/assets/gltf/FlightHelmet/`
  - Source: `Models/FlightHelmet/glTF/`
  - Purpose: in-repo glTF PBR compatibility target with external PNG textures,
    tangent data, normal maps, metallic-roughness maps, emissive/base-color
    texture slots, node transforms, and real-world mesh cleanup cases.
  - License: model files are CC0-1.0. `LICENSE.md`, `README.md`, and
    `metadata.json` are copied with the model and document the upstream
    licensing and attribution metadata.

FlightHelmet is intentionally larger than the unit-test fixtures. It is still
small enough to keep in the repository, and it gives the compiler and CPU
renderer a concrete target before testing much larger local-only scenes.

## Current Import Scope

YaoRay support covers static `.gltf` files with external buffers and external
PNG images, binary `.glb` files when they do not require embedded image loading,
default or first scenes, node transforms, triangle mesh primitives, indexed and
non-indexed geometry, positions, optional normals, optional UVs, imported or
generated tangents, base-color RGBA factors and textures, metallic/roughness
factors and metallic-roughness textures, normal texture scale, occlusion texture
strength, emissive factors and textures, alpha mode/cutoff metadata, and
double-sided metadata.

The render compiler stores texture color-space policy in render-owned textures:
base-color and emissive PNGs are loaded as sRGB, while metallic-roughness,
normal, and occlusion PNGs are loaded as linear data. CPU material resolution
uses base-color alpha, metallic-roughness maps, emissive maps, and tangent-space
normal maps. CPU visibility skips `MASK` alpha texels for camera, indirect, and
shadow rays. `BLEND` alpha is preserved as metadata but rendered opaque in this
slice.

Unsupported features include animation, skinning, morph targets, glTF cameras or
lights, sparse accessors, Draco or meshopt compression, embedded image buffers,
`data:` image URIs, texture transforms, occlusion darkening, alpha blending,
material extensions such as `KHR_materials_transmission`, exact glTF PBR BRDF
parity, mipmaps, anisotropic filtering, and CUDA/OptiX texture or material
parity.
