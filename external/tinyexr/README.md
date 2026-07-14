# tinyexr

Vendored single-header OpenEXR (.exr) reading + writing dependency.

- Source repository: https://github.com/syoyo/tinyexr
- Header URL: https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.7/tinyexr.h
- Retrieved: 2026-05-28
- Pinned version: v1.0.7
- Header: `tinyexr.h`
- Bundled dependency: `deps/miniz/miniz.h` + `deps/miniz/miniz.c` (zlib-compatible deflate/inflate, MIT-licensed) — also pulled from tinyexr's `v1.0.7` tag.
- License: BSD 3-clause (tinyexr core), with embedded BSD-style notices from upstream OpenEXR (Industrial Light & Magic). miniz is MIT. Full license text is at the top of `tinyexr.h` and `deps/miniz/miniz.c`.

YaoRay uses `tinyexr.h` for:

- `.exr` HDR texture / environment map loading in `src/io/image_loader_exr.cpp` (via `LoadEXR`).
- `.exr` HDR image output in `src/film/image_writer.cpp` (via `SaveEXR`, raw float RGB, no tone-mapping).

The `TINYEXR_IMPLEMENTATION` macro is defined in exactly one translation unit (`src/io/image_loader_exr.cpp`), mirroring the `STB_IMAGE_IMPLEMENTATION` pattern. Other TUs that include `tinyexr.h` get declarations only.
