# YaoRay Renderer Rewrite Design

Date: 2026-05-12

## Purpose

YaoRay is a rewrite of the current ToyRender project into a learning-oriented but engineering-grade offline path tracer. The project should stay approachable for studying rendering architecture while being capable of producing realistic, polished images from common model assets.

The rewrite is not a small refactor of the existing code. Existing ToyRender code is treated as reference material for algorithms and experiments. The new project starts from a clean architecture and a new project identity.

## Goals

- Build a physically based offline renderer with a clear CPU reference backend and a real CUDA backend.
- Use a PBRT-inspired architecture without initially implementing PBRT scene syntax.
- Load common assets, with glTF/.glb as the primary model path and OBJ as a secondary path.
- Use a two-layer scene architecture: semantic scene descriptions for humans, flat render scenes for CPU/CUDA.
- Provide a Film pipeline for HDR accumulation, progressive output, tone mapping, and final image writing.
- Keep dependencies light in the first stage while leaving importer interfaces open for Assimp or other future asset systems.
- Keep Windows + NVIDIA RTX 4060 as the first target while preserving Linux-friendly CMake structure.
- Add enough tests to make large-scale rewriting and later CUDA work safe.

## Non-Goals For The First Stage

- No GUI editor or full interactive renderer.
- No complete PBRT parser.
- No first-stage OptiX dependency.
- No bidirectional path tracing, MLT, spectral rendering, or advanced volume rendering.
- No attempt to fully import arbitrary glTF cameras, lights, animation, skinning, or scene behavior.

## Project Naming

- Project name: `YaoRay`
- Repository/top-level directory target: `yaoray`
- CLI executable: `yaoray`
- C++ namespace: `yr`
- CMake project name: `YaoRay`

## Architecture

YaoRay uses a two-layer design:

```text
scene.toml + glTF/OBJ assets
        |
        v
SceneDescription
  camera, lights, asset references, instances, material overrides, render settings
        |
        v
SceneCompiler
  asset loading, instance expansion, material conversion, texture preparation, BVH build
        |
        v
RenderScene
  flat arrays of triangles, BVH nodes, materials, textures, lights, camera, environment
        |
        v
CPU Backend / CUDA Backend
        |
        v
Film Pipeline
  HDR accumulation, progressive checkpoints, tone mapping, PNG and optional EXR output
```

The upper layer is for readability and scene authoring. The lower layer is for rendering and GPU upload. `RenderScene` should avoid virtual objects, shared ownership, and pointer-heavy structures. It should favor arrays, indices, handles, and plain data types that CPU and CUDA can both consume.

## Directory Layout

```text
yaoray/
  CMakeLists.txt
  cmake/

  include/yaoray/
    core/
    scene/
    assets/
    render/
    materials/
    film/
    backends/
    util/

  src/
    core/
    scene/
    assets/
    render/
    materials/
    film/
    backends/cpu/
    app/

  cuda/
    kernels/
    runtime/

  external/
    stb/
    tinygltf/
    tinyobjloader/

  scenes/
    examples/
    tests/

  assets/
    examples/

  tests/
    unit/
    render/
    assets/

  docs/
    architecture/
    superpowers/specs/
```

Module boundaries:

- `core`: math, ray, bounds, transform, color and future spectrum foundations. It should not depend on scene, asset, material, or backend modules.
- `scene`: semantic scene descriptions, including camera, lights, instances, material overrides, render settings, and output settings.
- `assets`: importer interfaces and first-stage glTF/OBJ implementations.
- `render`: `SceneCompiler`, `RenderScene`, BVH, integrator interfaces, and shared rendering data structures.
- `materials`: BSDF and PBR material evaluation, texture references, and material conversion helpers.
- `film`: HDR accumulation, progressive checkpoints, tone mapping, PNG output, and optional EXR support.
- `backends`: common backend interface plus CPU/CUDA backend implementations.
- `cuda`: CUDA runtime wrappers, GPU buffers, device scene upload, and kernels.

The existing `core/pbrt_cpu.h` should be split and migrated only as useful reference code. It should not become part of the new long-term structure.

## Scene Files And Asset Import

The first-stage scene format is a hand-authored TOML file. TOML is preferred over JSON because it is easier to edit manually, and preferred over YAML because it has fewer ambiguous parsing rules.

Example shape:

```toml
[render]
backend = "cuda"
width = 1280
height = 720
spp = 256
max_depth = 12
seed = 42

[film]
output = "out/statue.png"
checkpoint_interval_s = 10
checkpoint_path = "out/statue_progress.png"
tone_mapper = "aces"
exposure = 0.0

[camera]
type = "perspective"
position = [0, 1.2, 4]
target = [0, 1.0, 0]
fov_y = 45
aperture = 0.0
focus_distance = 4.0

[[assets]]
name = "statue"
path = "assets/models/statue.glb"

[[instances]]
asset = "statue"
transform.translate = [0, 0, 0]
transform.scale = [1, 1, 1]

[[lights]]
type = "area"
position = [0, 4, 2]
size = [2, 2]
radiance = [8, 7, 6]

[environment]
type = "hdri"
path = "assets/hdri/studio.hdr"
strength = 1.0
```

glTF/.glb is the primary model path. OBJ is supported as a secondary geometry path. First-stage dependencies are `tinygltf`, `tinyobjloader`, and `stb` family libraries. The public import surface is an `AssetImporter` interface that returns `ImportedAsset`; render modules do not call tinygltf or tinyobjloader directly.

First-stage glTF support:

- mesh primitives
- positions, normals, tangents, UVs
- indices
- node transforms
- base color factor and texture
- metallic and roughness factors
- metallic-roughness texture
- normal texture
- emissive factor and texture
- opaque and masked alpha handling

First-stage OBJ support:

- positions, normals, UVs
- face triangulation
- basic `.mtl` diffuse/specular/texture loading where practical
- OBJ material support is best-effort and not treated as physically complete

glTF metallic-roughness materials map into YaoRay PBR material descriptions:

```text
baseColor       -> diffuse/albedo or conductor tint input
metallic        -> dielectric/conductor blend input
roughness       -> microfacet roughness
normal map      -> shading normal perturbation
emissive        -> emissive material and possible mesh-light source
```

Camera, light, render, and Film settings are controlled by YaoRay scene files rather than imported from glTF. Downloaded assets often contain cameras and lights that are not suitable for offline rendering; hand-authored scene files make lighting and composition reproducible.

PBRT file syntax is a future importer target. If implemented later, it should convert PBRT input into `SceneDescription`, not bypass the compiler and backend layers.

## Rendering Core And Backends

The first-stage integrator is a unidirectional path tracer with:

- next event estimation
- multiple importance sampling
- area lights
- mesh emissive lights where practical
- HDRI environment light support
- Russian roulette after the basic path tracer is stable

CPU backend:

- The CPU backend is the correctness reference.
- It should support the complete first-stage feature set before CUDA catches up.
- It can use tile-based multithreading.
- It should be easy to debug and suitable for golden image tests.

CUDA backend:

- CUDA is a formal backend, not a side demo.
- It consumes the same `RenderScene` concept as the CPU backend.
- First-stage CUDA supports a focused subset: triangle meshes, BVH traversal, diffuse and metal-roughness materials, emissive materials, basic direct/environment lighting, and GPU accumulation.
- CUDA can start as a megakernel implementation for simplicity and later evolve toward wavefront path tracing if material divergence and scene complexity justify it.

Backend interface shape:

```cpp
class RenderBackend {
public:
    virtual BackendInfo Info() const = 0;
    virtual RenderResult Render(const RenderScene& scene,
                                Film& film,
                                const RenderOptions& options) = 0;
};
```

CLI backend selection:

```bash
yaoray render scenes/examples/statue.toml --backend cpu
yaoray render scenes/examples/statue.toml --backend cuda
```

OptiX is reserved as a future backend. First-stage architecture should not depend on OptiX, but `RenderBackend` and `RenderScene` should not block a future `OptixBackend` that uses NVIDIA acceleration structures and ray tracing programs.

Platform targets:

- First practical target: Windows, NVIDIA RTX 4060 8GB, CUDA architecture 8.9.
- CMake should remain friendly to Linux builds.
- CPU backend must build and run without CUDA.
- CUDA architecture should be configurable through CMake.

## Film Pipeline

Film is the renderer's image formation layer. It stores linear HDR radiance samples, not only display-ready pixels.

Core responsibilities:

- accumulate samples in linear HDR float buffers
- track sample counts or filter weights
- support fixed-seed reproducibility
- support progressive rendering and checkpoint images
- apply exposure
- apply tone mapping
- convert to sRGB for display output
- write final PNG output
- provide an EXR output interface, implemented when the dependency path is stable
- detect NaN/Inf samples and report bad-sample statistics

Output flow:

```text
backend sample
  -> Film::AddSample or backend-specific accumulation buffer
  -> linear HDR average
  -> exposure
  -> tone mapping
  -> sRGB conversion
  -> PNG checkpoint / final PNG
  -> optional linear EXR
```

Tone mapping first-stage support:

- `none`: debug mode with clamp and sRGB conversion
- `reinhard`: simple robust tone mapping
- `aces`: default display transform for polished images

CUDA should not transfer every sample to CPU. It should accumulate on the GPU and transfer per tile, per pass, checkpoint, or final image. CPU and CUDA output should share final tone mapping and image writing code to avoid display differences.

PNG output can use `stb_image_write` or another small writer. HDRI input can start with `.hdr` through `stb_image`; EXR input/output can be added through `tinyexr` later.

## Error Handling

User-facing input errors should be diagnosed clearly:

- missing or invalid scene fields
- missing model or texture files
- glTF/OBJ import failures
- invalid output path
- CUDA backend requested but unavailable
- insufficient GPU memory

Example:

```text
Scene error in scenes/statue.toml:
  [camera] missing required field: position
```

Internal errors should fail early in debug and be explicit in release diagnostics:

- invalid BVH nodes
- material index out of range
- Film size mismatch
- GPU upload size mismatch
- NaN/Inf explosion beyond tolerated bad-sample handling

## Testing And Verification

First-stage testing:

- `core` unit tests for vector, matrix, transform, ray, and AABB behavior
- `scene` tests for TOML parsing, default values, and error diagnostics
- `assets` tests using small glTF/OBJ fixtures
- `render` tests for BVH intersection and `SceneCompiler` output consistency
- `film` tests for accumulation, tone mapping, sRGB conversion, and NaN/Inf handling
- CLI smoke tests that render a tiny scene at 1 spp and verify output image creation
- golden image tests for small deterministic scenes using fixed seeds and error thresholds

CUDA tests are staged:

- initial CUDA smoke test for output size, process success, and absence of NaN/Inf
- later CPU/CUDA image error comparison on simple scenes
- later performance benchmarks for samples/sec, BVH build time, upload time, and GPU memory usage

## Rewrite And Git Strategy

YaoRay is a rewrite. The old ToyRender state should be preserved for reference, then the new project should start with clean history.

Recommended workflow before implementation:

1. Preserve the current repository state on an archive branch named `archive/toyrender-before-yaoray`.
2. Create a new orphan `main` branch for YaoRay.
3. Carry forward only intentionally chosen documents, assets, references, and source code.
4. Commit the YaoRay design as the first new-history commit.
5. Build the new project incrementally from the approved implementation plan.

This avoids permanently losing old experiments while making the new project history reflect the rewrite.

The design document may also be committed in the current history as a brainstorming checkpoint. That checkpoint is not the clean YaoRay project start; after the archive branch and orphan `main` are created, this approved design should be carried forward into the new history.

## Implementation Phases

1. Create the clean YaoRay project skeleton, CMake targets, namespace, CLI target, and test harness.
2. Implement `core` math, ray, bounds, transform, camera basics, and initial Film.
3. Implement TOML scene parsing and CLI command structure.
4. Implement `SceneDescription`, `ImportedAsset`, `SceneCompiler`, and initial `RenderScene`.
5. Implement CPU path tracing for basic geometry, BVH, area lights, and Film output.
6. Add glTF and OBJ import through `AssetImporter`.
7. Add PBR material mapping, textures, HDRI environment lighting, and tone mapping polish.
8. Add the CUDA backend core subset using the same render-scene model.
9. Add progressive checkpoints and example scenes.
10. Expand tests, golden images, CUDA smoke tests, and documentation.

## First-Stage Completion Criteria

- `yaoray render scenes/examples/*.toml --backend cpu` renders PNG output.
- `yaoray render scenes/examples/*.toml --backend cuda` renders supported subset scenes on the RTX 4060.
- At least one glTF/.glb and one OBJ example asset can be imported and rendered.
- Basic PBR materials, textures, area lighting, environment lighting, tone mapping, and progressive checkpoints work.
- Unit tests, asset tests, CLI smoke tests, and small golden image tests exist.
- README and architecture documentation explain build, render, scene authoring, and backend extension workflows.
