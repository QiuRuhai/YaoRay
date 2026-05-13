# YaoRay PNG Output Design

Date: 2026-05-13

## Purpose

YaoRay currently renders the CPU debug output to ASCII PPM only. PPM is useful for a tiny correctness loop, but it is awkward to view, share, and use for future visual showcase work. This slice adds PNG output while preserving the existing PPM path for tests and low-friction debugging.

PNG does not improve render quality by itself. The first PNG writer stores the same tone-mapped 8-bit RGB values that the PPM writer currently emits. Its value is interoperability: common image viewers, smaller files, and a better default output format for examples.

## Goals

- Add `.png` output support to the film image writer.
- Keep `.ppm` output support unchanged for debug and test usage.
- Add a format-dispatching `WriteImage()` API so scene `film.output` controls the output format.
- Use the existing tone mapping path for both PPM and PNG.
- Write PNG as 8-bit RGB without alpha.
- Vendor `stb_image_write.h` as the only new dependency for PNG encoding.
- Update the CLI render path to call `WriteImage()`.
- Change the human-facing example scenes to produce PNG by default.
- Add unit and CLI tests that verify PNG output through file signatures.
- Update README and architecture docs.

## Non-Goals

- No HDR output.
- No 16-bit PNG output.
- No alpha channel.
- No ICC profile, gamma chunk, EXIF, or other PNG metadata.
- No image decoding or golden-image visual comparisons.
- No changes to Film accumulation, tone mapping algorithms, camera rays, BVH traversal, materials, or lighting.
- No showcase-quality scene in this slice.

## Approved Decisions

PNG is the default human-facing format for this step. PPM remains supported because it is still useful for small tests and inspecting exact numeric output.

Use `stb_image_write.h` instead of writing a custom PNG encoder. PNG requires zlib/deflate and chunk checksums; a proven single-header writer is a better fit for this stage than a hand-rolled encoder. The upstream `stb_image_write.h` API exposes `stbi_write_png(...)`, writes 8-bit interleaved RGB/RGBA-style data, and returns non-zero on success. The header is available from the official `nothings/stb` repository.

## Architecture

The film module remains the only module that knows how to write image files:

```text
RenderBackend
  -> Film
  -> yaoray_film image writer
  -> .ppm or .png file
```

Add these APIs to `include/yaoray/film/image_writer.hpp`:

```cpp
ImageWriteResult WritePng(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);

ImageWriteResult WriteImage(
    const Film& film,
    const ToneMapSettings& tone_map,
    const std::filesystem::path& path
);
```

Keep the existing `WritePpm()` public API.

`WriteImage()` dispatches by normalized extension:

- `.ppm` -> `WritePpm()`
- `.png` -> `WritePng()`
- anything else -> unsupported extension error

`WritePng()` builds an interleaved `std::vector<unsigned char>` in row-major top-to-bottom order. Each channel is generated from:

```cpp
ToByte(ToDisplayColor(film.LinearPixel(x, y), tone_map).channel)
```

This guarantees PPM and PNG use the same visible color transform.

## Dependency

Vendor `stb_image_write.h` under:

```text
external/stb/stb_image_write.h
external/stb/README.md
```

Add an interface CMake target:

```cmake
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)
```

Link `yaoray_film` privately to `stb`.

Only one implementation translation unit should define:

```cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
```

That translation unit is `src/film/image_writer.cpp`.

## Error Handling

PPM and PNG writers both create missing parent directories before opening or writing the target file.

Expected errors:

- `WritePpm()` rejects non-PPM paths.
- `WritePng()` rejects non-PNG paths.
- `WriteImage()` rejects unsupported extensions with a message mentioning `.ppm` and `.png`.
- Directory creation failure returns `ImageWriteResult{false, ...}`.
- `stbi_write_png(...) == 0` returns `ImageWriteResult{false, "failed to write PNG image: ..."}`.

## CLI And Examples

The CLI changes from:

```cpp
WritePpm(*render_result.film, tone_map, scene.film.output);
```

to:

```cpp
WriteImage(*render_result.film, tone_map, scene.film.output);
```

The printed `Rendered image: ...` line remains unchanged.

The render help should describe CPU debug images as PNG/PPM instead of PPM-only.

Change these human-facing example outputs to PNG:

```text
scenes/examples/minimal.toml
scenes/examples/obj_pyramid.toml
```

CLI render fixtures should also use PNG so CTest verifies the end-to-end path through the app.

## Testing Strategy

Film tests:

- `WritePng()` rejects a non-`.png` extension.
- `WritePng()` writes a valid PNG signature.
- `WriteImage()` dispatches `.ppm` to the existing PPM writer.
- `WriteImage()` dispatches `.png` to the PNG writer.
- `WriteImage()` rejects unsupported extensions.

CLI tests:

- Built-in triangle render produces a `.png` file.
- OBJ render produces a `.png` file.
- Both CLI tests verify the PNG magic bytes:

```text
89 50 4E 47 0D 0A 1A 0A
```

Existing PPM tests remain so the older debug writer is not accidentally broken.

## Documentation

Update `README.md` and `docs/architecture/overview.md` to state:

- PNG output is implemented.
- CPU debug render examples write PNG by default.
- PPM output is still supported for debugging/tests.
- PNG is a file-format usability improvement, not a render-quality improvement.

## Completion Criteria

- `WritePng()` and `WriteImage()` are implemented.
- `WritePpm()` remains supported and tested.
- CLI render uses `WriteImage()`.
- Example scene outputs are `.png`.
- CLI render CTests verify PNG signatures.
- Docs no longer list PNG output as future work.
- Full Debug build and CTest pass.

## Future Work

Likely follow-up slices:

1. Add image comparison tooling for renderer regression tests.
2. Add higher-quality showcase scenes once materials and path tracing exist.
3. Add HDR or EXR output for linear high dynamic range data.
4. Add optional PNG metadata or color management if viewer consistency becomes a real issue.
