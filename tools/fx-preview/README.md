# fx-preview

Host harness for `src/libs/renderer_bgfx/fx`: renders procedural test scenes through the real passes with bgfx on a
headless OpenGL ES 3.0 context and writes PNG files for review. It uses no game data.

```sh
cmake -S tools/fx-preview -B build/fx-preview -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/fx-preview                          # fx_preview, storm_fx_core_check, ES 3.0 shader blobs
cmake --build build/fx-preview --target fx_shader_profiles   # every shader also for SPIR-V and Metal
ctest --test-dir build/fx-preview --output-on-failure   # CPU-side unit checks
build/fx-preview/fx_preview <output directory> [name filter]
```

- bgfx comes from the pin in `src/libs/renderer_bgfx/CMakeLists.txt`; offline builds pass
  `-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=<checkout>`.
- Needs EGL and GLES development packages. Without a display use Mesa's software renderer:
  `EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 build/fx-preview/fx_preview out`.
- Write the images outside the repository (or into an ignored directory); they are review evidence, not sources.

Scenes: open sea with a ship and an island in four weathers × the three quality presets; colour grades; tone
curve and bloom alone; a night harbour (bloom); the shore (water quality levels, environment cube, refraction);
the three sea geometry sources, with the GPU height field compared against the CPU mesh; a quay with a ship and
a figure (lighting flags); an aliasing chart (FXAA, sharpening, 75 % render scale). CPU products (baked
textures, sky panoramas, grading tables) are written as images too.

The "original" shots use stand-ins for the legacy sea and sky (vertex-coloured), since the real ones live in
the game; they show what the package leaves untouched, not how the game looks.
