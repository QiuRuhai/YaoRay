# Local Sponza Benchmark

This scene is a manual large-glTF validation target for YaoRay. The model files
are intentionally not committed to Git.

## Source

- Repository: `https://github.com/KhronosGroup/glTF-Sample-Assets`
- Model: `Models/Sponza/glTF/Sponza.gltf`
- Local expected path: `external/assets/large-gltf/sponza/Sponza/glTF/Sponza.gltf`

## Download

Run from the repository root:

```bash
mkdir -p external/assets/large-gltf/sponza
git clone --depth 1 --filter=blob:none --sparse https://github.com/KhronosGroup/glTF-Sample-Assets.git external/assets/large-gltf/sponza/glTF-Sample-Assets
git -C external/assets/large-gltf/sponza/glTF-Sample-Assets sparse-checkout set --no-cone /Models/Sponza/glTF /Models/Sponza/LICENSE.md /Models/Sponza/README.md /Models/Sponza/metadata.json
cp -R external/assets/large-gltf/sponza/glTF-Sample-Assets/Models/Sponza external/assets/large-gltf/sponza/Sponza
```

The repository `.gitignore` excludes `external/assets/`, so the downloaded model
and textures stay local.

## Smoke Render

```bash
cmake --build build --config Debug
./build/yaoray render scenes/examples/local_sponza.toml --backend cpu
test -s scenes/examples/out/local_sponza.png
test -s scenes/examples/out/local_sponza.checkpoint.png
test -s scenes/examples/out/local_sponza.yrcheckpoint
```

The smoke render is intentionally low resolution and low sample count. It proves
that the native glTF asset path, decoded texture path, CPU BVH prepare, and CPU
path tracer can handle a larger scene. It is not a final quality preset.

To resume a later run, set `resume = true` in `[offline]`. YaoRay validates the
checkpoint against the current render settings before continuing. If the scene,
resolution, target spp, seed, camera, or compiled resource counts change, resume
fails instead of mixing incompatible samples.
