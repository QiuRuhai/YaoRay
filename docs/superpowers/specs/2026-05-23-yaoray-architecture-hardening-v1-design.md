# YaoRay Architecture Hardening v1 Design

## Overview

YaoRay already has the right high-level pipeline:

```text
SceneDescription
  -> AssetResource
  -> RenderSceneIR
  -> RenderBackend::Prepare()
  -> PreparedScene
  -> RenderBackend::Render()
  -> RenderResult
```

This design hardens that pipeline without replacing it with a new top-level
architecture. The goal is to make asset growth and future GPU work fit the
current system by strengthening ownership, backend contracts, render IR shape,
and asset/render semantic boundaries in small reversible slices.

## Goals

- Remove prepared-scene lifetime hazards.
- Make backend preparation and rendering contracts explicit and testable.
- Move `RenderSceneIR` toward a table-shaped, GPU-packable representation while
  preserving current CPU behavior during migration.
- Decouple `AssetResource` from scene/render material enums so asset import can
  preserve source semantics independently of current renderer capabilities.
- Add a CUDA prepared-scene and packing prototype that validates data layout
  before real CUDA rendering exists.
- Keep all existing CLI examples, unit tests, offline rendering workflows, and
  committed glTF compatibility targets working after every slice.

## Non-Goals

- Do not replace the current pipeline with a separate `RenderWorld` architecture.
- Do not implement real CUDA kernels in this hardening slice.
- Do not remove the CPU triangle path until a compatible table path is proven.
- Do not add new asset format features beyond what is needed to preserve current
  OBJ and static glTF/GLB behavior.
- Do not redesign the integrator API unless backend contract work exposes a
  concrete blocker.

## Current Problems

`PreparedScene` currently exposes `SourceScene()` but CPU and CUDA prepared
scene types store raw pointers to an externally owned `RenderSceneIR`. This is
safe in the current CLI flow because the compiled scene outlives rendering, but
it is not safe as a public backend contract.

`RenderSceneIR` is backend-neutral in intent, but it stores fully expanded
world-space `RenderTriangle` records. That works well for CPU traversal and
small scenes, but future GPU packing and large assets need stable vertex,
index, primitive, material, texture, image, and sampler tables.

`AssetResource` preserves useful imported asset semantics, but it still depends
on `yaoray_scene` material enums through fields such as `approximate_type`.
This couples import decisions to the current renderer material model.

CUDA is a named backend placeholder. That is useful for CLI and contract tests,
but it does not yet validate any future GPU data layout or prepared-scene
ownership behavior.

## Target Architecture

The stable ownership boundary is:

```text
RenderSceneIR value
  -> RenderBackend::Prepare(RenderSceneIR)
  -> backend-owned PreparedScene
```

`RenderSceneIR` is the backend-neutral input package. It contains render
settings, camera, environment, lights, materials, textures, and geometry data.
It does not contain CPU BVHs, CUDA buffers, OptiX handles, or other
backend-owned runtime structures.

`PreparedScene` is backend-owned runtime data. It must not depend on a caller
keeping a source `RenderSceneIR` alive after `Prepare()` returns.

The app and CLI only coordinate:

```text
LoadSceneFile()
CompileScene()
CreateRenderBackend()
backend->Prepare()
backend->Render()
WriteImage()
```

They do not inspect CPU BVHs, CUDA buffers, or backend-specific prepared scene
internals.

## Slice 1: Backend Lifetime Hardening

Change the backend preparation boundary so a prepared scene owns or otherwise
stabilizes all data it needs for rendering.

Preferred shape:

```cpp
class RenderBackend {
public:
    virtual BackendPrepareResult Prepare(RenderSceneIR scene) = 0;
    virtual RenderResult Render(const PreparedScene& scene, const RenderRequest& request) = 0;
};
```

`CompileScene()` returns a value. The CLI can move that value into
`Prepare()`. Tests can also pass temporary `RenderSceneIR` values to prove that
prepared scenes are independent of caller-owned IR lifetime.

CPU prepared scene shape:

```text
CpuPreparedScene
  owns RenderSceneIR
  owns RenderBvh
```

CUDA prepared scene shape for now:

```text
CudaPreparedScene
  owns RenderSceneIR
  later owns PackedRenderScene and device resources
```

Default construction of concrete prepared scene types should be removed unless
there is a real valid empty state. `SourceScene()` may remain as a read-only
debug/introspection API, but it should return owned data.

Acceptance criteria:

- CPU rendering works after preparing from a temporary `RenderSceneIR`.
- CPU prepared scene owns the render scene and the CPU BVH.
- CUDA prepared scene placeholder owns the render scene.
- Existing CLI and unit tests still pass.

## Slice 2: Backend Contract Clarification

After ownership is stable, make backend behavior explicit.

Add a small capability/query surface that describes what a backend can currently
prepare and render. Initial capabilities should cover:

- backend kind
- supported integrators
- whether offline resume/progress callbacks are supported
- whether texture-backed materials are supported
- whether the backend is a runnable renderer or a controlled placeholder

Prepare phase responsibilities:

- validate backend-supported scene features
- construct all backend runtime data
- return stable errors for unsupported or invalid input

Render phase responsibilities:

- accept only matching prepared scene types
- render from prepared runtime data
- report stable `RenderResult` errors without leaking implementation details

Acceptance criteria:

- CPU rejects non-CPU prepared scenes with a stable error.
- CUDA prepare/render placeholder errors remain controlled and tested.
- CLI unsupported-backend and offline/CUDA failure paths keep passing.
- Backend tests cover capability queries and prepare/render mismatch cases.

## Slice 3: Render IR Table Migration

Move `RenderSceneIR` toward a table layout suitable for CPU preparation and
future GPU packing.

Target table families:

```text
RenderVertex[]
RenderIndex[]
RenderPrimitive[]
RenderMaterial[]
RenderTexture[]
RenderImage[]
RenderSampler[]
RenderAreaLight[]
RenderEnvironment
```

Initial migration can keep existing `triangles` as a compatibility field:

```text
RenderSceneIR
  table geometry fields
  triangles compatibility view during migration
```

The compiler should emit table geometry for builtin triangles, inline quads,
OBJ assets, and glTF assets. CPU prepare may initially build its BVH from the
compatibility triangle view, then move to a table-to-triangle or direct table
path once tests prove parity.

Acceptance criteria:

- Builtin triangle and OBJ/glTF compiler tests assert table counts.
- CPU output remains unchanged through the compatibility path.
- No backend-specific acceleration structure is added to `RenderSceneIR`.
- Table layout is documented enough for CUDA packing work to consume.

## Slice 4: AssetResource Decoupling

Remove renderer material enums from `AssetResource`.

Asset material records should preserve source asset semantics:

```text
AssetMaterial
  base_color
  base_color_alpha
  metallic
  roughness
  specular_hint if needed
  base_color_texture
  metallic_roughness_texture
  normal_texture
  occlusion_texture
  emissive_texture
  normal_scale
  occlusion_strength
  alpha_mode
  alpha_cutoff
  double_sided
```

The scene compiler owns the lowering policy from asset semantics to current
`RenderMaterial`. For example, a metallic glTF material may lower to the
current `MaterialKind::Metal`, while non-metallic rough materials lower to
diffuse or plastic according to existing compatibility behavior.

Acceptance criteria:

- `include/yaoray/assets/asset_resource.hpp` no longer includes
  `yaoray/scene/scene.hpp`.
- OBJ and glTF loader tests still prove preserved source material fields.
- Scene compiler tests prove current render material lowering behavior.
- Existing FlightHelmet compatibility tests still pass.

## Slice 5: CUDA Prepared Scene And Packer Prototype

Introduce a CPU-side CUDA packing prototype without real CUDA rendering.

Suggested shape:

```text
PackedRenderScene
  settings metadata
  geometry buffer counts
  material buffer counts
  texture/image metadata counts
  light/environment metadata
```

`CudaBackend::Prepare()` can validate and build this packed metadata, return a
`CudaPreparedScene`, and still let `Render()` return a controlled
not-implemented error. This proves that future CUDA work has a concrete data
contract and avoids changing all upstream layers at the same time as kernel
development.

Acceptance criteria:

- CUDA prepare succeeds for scenes whose data can be packed.
- CUDA render still returns a controlled not-implemented error.
- Unsupported CUDA features report stable prepare errors.
- Packed metadata tests cover builtin, OBJ, glTF textured, and empty-scene
  cases.

## Testing Strategy

Every slice must run:

```text
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
build\yaoray_tests.exe
ctest --test-dir build --output-on-failure
```

Specific coverage to preserve:

- scene parser tests
- asset loader tests
- scene compiler OBJ/glTF/FlightHelmet tests
- backend prepare/render tests
- CPU material and surface tests
- CPU path tracer tests
- CLI render tests
- offline checkpoint/resume tests
- CUDA placeholder failure-path tests

## Migration Safety

Each slice should be independently reviewable and reversible. Avoid mixing
lifetime fixes, table IR changes, asset semantic decoupling, and CUDA packing in
one commit.

The first implementation plan should start with Slice 1 and Slice 2 only unless
the backend contract work exposes a reason to include table IR immediately.

## Risks And Mitigations

- Risk: copying `RenderSceneIR` into prepared scenes increases memory use.
  Mitigation: accept the copy for correctness first; table IR and move-based
  prepare reduce cost later.
- Risk: table IR duplicates `triangles` during migration.
  Mitigation: keep the overlap temporary and add tests that identify the
  compatibility path.
- Risk: asset decoupling changes material appearance.
  Mitigation: preserve current lowering behavior through compiler tests before
  removing old fields.
- Risk: CUDA prepare succeeding could imply CUDA render support.
  Mitigation: keep CLI and error text explicit: preparation/packing is supported,
  rendering is not implemented yet.

## Success Criteria

- Prepared scenes are independent of caller-owned `RenderSceneIR` lifetime.
- Backend capabilities and failure paths are explicit and tested.
- Render IR has a migration path toward GPU-packable table data.
- Asset import semantics are separated from renderer material enums.
- CUDA has a prepared-scene packing contract before real rendering begins.
- The full current test suite passes after each slice.
