# Bistro Local Benchmark

Bistro is the intended large-scene benchmark after FlightHelmet passes loader,
compiler, and CPU render smoke tests. Do not commit Bistro model files to this
repository.

## Source

- Asset: NVIDIA Amazon Lumberyard Bistro scene.
- Recommended local root: `external/assets/bistro/`.
- Rationale for local-only storage: the public Bistro archives are large
  production-style scene packages, commonly hundreds of MiB before extraction.

## Intended Use

Use Bistro to stress the same pipeline validated by FlightHelmet:

- glTF asset loading with many external images.
- Render compiler material and texture-slot mapping.
- CPU BVH build time and traversal cost on a larger triangle set.
- CPU material sampling cost with base-color, metallic-roughness, emissive, and
  normal textures.

Keep Bistro out of default CTest and out of normal CI. It is a manual benchmark
asset for long local renders, memory profiling, and future CUDA/OptiX backend
comparisons.

## Local Layout

Place the scene under:

```text
external/assets/bistro/
```

Future benchmark TOML files should reference files from that local root, and the
repository `.gitignore` should continue excluding `external/` so the large model
does not enter source control accidentally.
