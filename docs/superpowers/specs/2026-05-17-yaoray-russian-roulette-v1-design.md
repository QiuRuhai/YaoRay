# YaoRay Russian Roulette v1 Design

YaoRay now has a CPU path tracer with direct-light MIS, BSDF-sampled emissive
MIS, configurable direct area-light samples, deterministic sampling, and
tile-threaded execution. The next path quality and scalability issue is fixed
path depth: raising `render.max_depth` improves indirect lighting but forces
every surviving path to run to that hard limit unless it hits the environment,
an invalid material, or a black BSDF sample.

This slice adds Russian roulette path termination to the CPU path tracer. The
goal is to make high `max_depth` values practical while preserving unbiased
energy estimates.

## Goals

- Add Russian roulette termination to the CPU `path` integrator.
- Keep `render.max_depth` as the hard upper bound.
- Start roulette only after a fixed minimum path depth so low-bounce lighting
  remains stable.
- Base survival probability on current path throughput.
- Compensate surviving paths by dividing throughput by survival probability.
- Preserve deterministic output for fixed scene seed, sampler, SPP, and thread
  count.
- Keep the design compatible with future sampler and integrator refactors.

## Non-Goals

- No scene schema changes.
- No new CLI flags or render stats.
- No user-configurable roulette parameters in TOML.
- No sampler architecture refactor.
- No environment MIS.
- No material model changes.
- No CUDA backend implementation.
- No full Integrator API refactor.

## Current State

`src/backends/cpu/cpu_path_tracer.cpp` owns the CPU path loop:

```text
for depth in [0, max_depth):
  trace ray
  add environment / emission / direct light
  stop at max_depth
  sample BSDF
  throughput *= bsdf_sample.weight
  spawn next ray
```

`render.max_depth` is currently the only general path depth control. This is
simple and deterministic, but it creates a poor tradeoff:

- low `max_depth` misses deeper indirect light and hurts glass later,
- high `max_depth` traces many low-throughput bounces,
- path cost grows toward the hard depth even when later bounces contribute
  little energy.

The CPU sampler already exposes `Next1D()`, which is sufficient for v1 roulette.

## Design

Add a small fixed-policy Russian roulette decision inside `TracePath()` after a
valid BSDF sample is produced and before the next ray is spawned.

Use these internal constants:

```cpp
constexpr int RussianRouletteStartDepth = 3;
constexpr float RussianRouletteMinSurvival = 0.05f;
constexpr float RussianRouletteMaxSurvival = 0.95f;
```

Depth is the existing zero-based loop index. The camera ray is `depth == 0`.
Roulette starts when preparing the next ray after a hit at `depth >= 3`.

The survival probability is:

```text
survival = clamp(max_component(throughput_after_bsdf),
                 RussianRouletteMinSurvival,
                 RussianRouletteMaxSurvival)
```

Decision:

```text
if depth >= RussianRouletteStartDepth:
  if sampler.Next1D() >= survival:
    terminate path
  throughput_after_bsdf /= survival
```

This is the standard compensation step: paths that survive become brighter by
`1 / survival`, so the expected contribution remains unchanged.

## Path Loop Placement

The roulette decision must happen after:

- direct lighting at the current hit,
- emissive hit accumulation,
- `max_depth` hard-limit check,
- valid BSDF sampling,
- `throughput = throughput * bsdf_sample.weight`.

It must happen before:

- storing the next ray as active work,
- tracing the next bounce.

This placement means:

- camera-visible emission is never randomly removed,
- first few bounces stay stable,
- delta paths can still continue before the start depth,
- black or invalid BSDF samples still terminate immediately without consuming a
  roulette random number,
- `max_depth` remains the hard upper bound.

## Fixed Policy Rationale

This slice intentionally does not add TOML fields such as `rr_min_depth` or
`rr_min_probability`.

The renderer still needs a broader integrator-parameter design. Adding public
knobs now would commit to names and semantics before the path tracer has glass,
environment sampling, better samplers, or CUDA parity. A fixed policy keeps the
feature useful and testable without expanding the scene schema.

The chosen probability clamp is conservative:

- `0.05` prevents extremely bright compensation on very low-throughput paths,
- `0.95` still allows some termination for high-throughput paths after the
  start depth,
- start depth `3` avoids adding variance to the most important early bounces.

## Testing Strategy

Add focused CPU path tracer tests:

- Low-depth scenes with `max_depth <= 4` are unchanged because the hard
  `max_depth` check runs before roulette could spawn another ray.
- A high-depth diffuse scene traces fewer path rays with roulette than a
  no-roulette-equivalent expectation would imply.
- Fixed seed, sampler, SPP, and thread count remain deterministic.
- Thread-count deterministic tests remain bitwise stable.
- Mirror or emissive paths before the start depth retain their existing expected
  results.

The tests should avoid relying on exact high-depth radiance values because
roulette is stochastic but deterministic for a fixed seed. Prefer robust
assertions on ray counts, deterministic equality, and unchanged low-depth
behavior.

## Manual Verification

After implementation:

- Run full Debug CTest.
- Build Release.
- Render `scenes/examples/cornell_box_path.toml`.
- Render `scenes/examples/material_v2_showcase.toml`.

The manual acceptance target is not a dramatic visual change at current scene
settings. The expected value is architectural: higher future `max_depth` values
become practical for Cornell Box quality, glass, and more complex material
transport.

## Documentation

Update README and architecture overview to state:

- the CPU path tracer uses fixed-policy Russian roulette,
- `render.max_depth` remains the hard upper bound,
- roulette parameters are internal in v1,
- user-configurable integrator parameters remain future work.

## Future Work

Consider exposing roulette parameters only when a broader integrator settings
design exists. That future design may include:

- `render.rr_start_depth`,
- `render.rr_min_survival`,
- `render.rr_max_survival`,
- sampler dimension allocation,
- path-depth or bounce-type stats,
- CUDA parity.

Russian roulette should also be revisited after glass, environment MIS, and a
formal Integrator API are introduced.

## Acceptance Criteria

- CPU path tracer applies Russian roulette after the fixed start depth.
- Surviving paths divide throughput by survival probability.
- `render.max_depth` remains a hard limit.
- No scene schema changes are made.
- Low-depth path tracer tests remain unchanged.
- High-depth deterministic tests pass.
- Full Debug tests pass.
- Cornell and material showcase manual renders succeed.
