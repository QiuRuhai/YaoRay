YaoRay PBRT scene gallery
=========================

This directory hosts the curated set of PBRT v4 scenes used as the project's
demonstration corpus. Each subdirectory is a self-contained scene (geometry,
textures, environment maps) that the renderer is expected to handle end to
end.

Conventions
-----------

* Scenes live at `scenes/pbrt/<name>/<name>.pbrt` so external assets can be
  colocated.
* Output paths inside the `.pbrt` file should be relative to the scene file
  (e.g. `"out/<name>.png"`), keeping renders next to the source.
* External assets (PLY meshes, HDRIs, textures) should be referenced with
  relative paths from the scene file.

How to render
-------------

```
yaoray render scenes/pbrt/<name>/<name>.pbrt --backend cpu
```

The default output is written next to the scene under `out/<name>.png` unless
the `.pbrt` Film directive overrides the `filename`.
