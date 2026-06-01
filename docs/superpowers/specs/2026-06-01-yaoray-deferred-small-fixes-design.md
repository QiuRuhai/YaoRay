# YaoRay Deferred Small Fixes — Design

**Date:** 2026-06-01
**Status:** Approved for implementation planning
**Roadmap source:** `2026-05-29-yaoray-roadmap-refresh-design.md` deferred small fixes

## Goal

Clean up four known post-M3 quality issues without changing the main
M4 direction. These are narrow correctness and robustness fixes:

1. Light-sampling PDF mismatch.
2. Include per-source-root search paths.
3. Input bounds validation.
4. `ObjectInstance` sphere and default-material handling.

This cleanup is not a new milestone. It prepares the renderer for M4 by
making existing supported behavior more predictable, better diagnosed,
and easier to trust.

## Scope

Each fix is independent and should land as its own slice and commit. The
implementation must follow existing YaoRay patterns: warning-based
degradation where possible, focused regression tests, and no external
asset downloads.

Out of scope:

* New PBRT material or geometry classes.
* CUDA.
* M4 subsurface / BSSRDF work.
* Large-scene integration renders.
* A broad parser or compiler refactor.

## Architecture

The cleanup keeps the current two-layer pipeline:

```text
PBRT v4 scene -> PbrtScene -> RenderSceneIR -> Backend
```

The changes stay local to parser path metadata, scene compilation
helpers, and render-layer light sampling. No backend API changes are
expected.

### Slice 1 — Area-Light Sampling PDF Consistency

Current behavior has a distribution mismatch: `SampleEmissiveLights`
selects emissive primitives uniformly by primitive count, while
`PdfEmissiveLightSolidAngle` computes the area PDF as if all emissive
surface area were sampled uniformly.

The fix is to make sampling and PDF use the same distribution. The
preferred design is area-weighted primitive selection:

* Pick an emissive primitive with probability `primitive_area / total_area`.
* Pick a triangle inside that primitive consistently with the primitive's
  internal triangle sampling.
* Report the matching area PDF and convert to solid-angle PDF at the
  shading point.

If triangle-level area variation inside one primitive becomes material,
the implementation may refine the primitive sampler to area-weight
triangles as well. The required acceptance criterion is that the PDF
reported by the sampling path and the PDF queried for the sampled point
describe the same probability density.

### Slice 2 — Include-Root Resource Resolution

`Include` parsing currently resolves included PBRT files relative to the
including file, which is correct. Resource loading during compilation
uses only `scene.source_root`, so resources referenced from an included
file can fail if they live next to that include.

The parser will record all source roots that contributed parsed content:

* The main scene root.
* Each included file's parent directory.

The compiler will resolve resource paths through a shared helper:

1. Absolute paths are used directly.
2. Try the main `source_root`.
3. Try each recorded include root in parse order.
4. If nothing exists, return the main-root candidate so diagnostics keep
   their current shape.

This helper should be used for imagemap textures, normal maps, measured
`.bsdf` files, PLY meshes, and infinite-light environment maps. It must
not change output film filename resolution; film output remains relative
to the main scene root.

### Slice 3 — Input Bounds Validation

Scene settings and shape parameters should not allow non-finite values,
zero dimensions, negative sample counts, or invalid geometry to enter
the render IR.

Use warning plus correction when the user's intent is recoverable:

* Film width and height clamp to at least `1`.
* Sampler spp clamps to at least `1`.
* Integrator max depth clamps to at least `0`.
* Camera fov falls back to a sane default if it is non-finite or outside
  a renderable range.

Use warning plus skip when geometry cannot be made meaningful:

* Sphere radius must be positive and finite after transform scale.
* Non-finite or non-positive effective sphere radius skips that sphere.

This policy keeps malformed input diagnosable without turning small
scene-authoring mistakes into fatal compile failures. Existing fatal
errors remain fatal when the scene cannot compile or contains no
geometry.

### Slice 4 — ObjectInstance Sphere and Default Material

`ObjectInstance` compilation already handles several shape kinds but
does not mirror top-level shape behavior completely.

The fix is to make instance compilation use the same material and shape
rules as top-level shapes:

* Support `Shape "sphere"` inside object definitions.
* Ensure a default material exists when an instance shape has no named or
  inline material.
* Warn when an instance references an undefined material, then use the
  default material.
* Preserve existing support for trianglemesh, plymesh, disk, and
  loopsubdiv instances.

This should keep `ObjectInstance` behavior boring in the best possible
way: if a top-level shape compiles, the same shape inside an object
definition should compile after transform composition.

## Testing Strategy

Each slice gets focused regression coverage.

* `light_sampling_tests.cpp`: build a scene with multiple emissive
  primitives of different areas. Verify sampled PDF and queried PDF are
  consistent for sampled points, and verify primitive selection follows
  the intended area-weighted thresholds.
* `pbrt_tests.cpp` or a new parser/compiler fixture: create a root scene
  that includes a file from another directory, with that include
  referencing a resource next to itself. Verify the parser records the
  include root and the compiler resolves the resource through it.
* `scene_compiler_input_validation_tests.cpp`: cover invalid film
  resolution, spp, max depth, fov, and sphere radius. Assert warnings,
  corrected values, and no NaN or Inf in the resulting IR.
* `scene_compiler_object_instance_tests.cpp`: cover object-instanced
  spheres, no-material default behavior, and undefined-material warning
  behavior.

Validation after implementation:

* Run the targeted new tests after each slice.
* Run the full unit test executable.
* Run CTest for the configured build.

## Acceptance Criteria

The cleanup is complete when:

* Area-light sample PDFs and queried PDFs are consistent for the same
  sampling distribution.
* Included files can resolve their adjacent resources without breaking
  main-root resource lookup.
* Bad-but-recoverable scene settings produce warnings and stable clamped
  values.
* Invalid sphere radius does not create invalid IR.
* Object-instanced spheres compile, and instance shapes without a valid
  material use the default material with diagnostics where appropriate.
* All existing tests remain green.

## Risk Register

* **Risk:** area-weighted light selection changes image noise or
  brightness. **Mitigation:** assert PDF consistency directly and keep
  MIS estimator counts unchanged.
* **Risk:** include-root lookup finds a different file than before.
  **Mitigation:** try the main source root before include roots, and use
  include roots only as fallback for existing resource loaders.
* **Risk:** clamping hides bad input. **Mitigation:** every correction
  emits a warning with the affected field.
* **Risk:** object-instance logic diverges from top-level shape logic
  again. **Mitigation:** keep the instance dispatch structurally aligned
  with the top-level dispatch and test the missing sphere/default cases.

## Slice Order

Recommended implementation order:

1. Area-light PDF consistency.
2. ObjectInstance sphere/default-material handling.
3. Input bounds validation.
4. Include-root resource resolution.

The first two are pure in-memory compiler/render-layer fixes. Input
validation is similarly local. Include-root resolution touches parser
metadata plus multiple compiler resource call sites, so it is last.
