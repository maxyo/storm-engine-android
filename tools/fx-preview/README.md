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

- bgfx is the game's: the pin and patches of `src/libs/renderer_bgfx/cmake/bgfx_pin.cmake`, and the shared host
  shaderc of that pin (`storm_shaderc.cmake`); offline builds pass
  `-DFETCHCONTENT_SOURCE_DIR_STORM_BGFX_UPSTREAM=<pinned, patched checkout>`.
- Needs EGL and GLES development packages. Without a display use Mesa's software renderer:
  `EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 build/fx-preview/fx_preview out`.
- `--height-maps=<folder>` replaces the procedural height textures of the normal-map scene with
  `bump_houses.pgm`, `rockBump.pgm`, `woodBump.pgm` and `sailBumpU1.pgm` (binary 8-bit PGM) converted from
  your own copy of the game, for local look-dev; those shots get the suffix `_game`. Never commit such
  files or the images made from them.
- Write the images outside the repository (or into an ignored directory); they are review evidence, not sources.

Scenes: open sea with a ship and an island in four weathers × the three quality presets; colour grades; tone
curve and bloom alone; a night harbour (bloom); the shore (water quality levels, environment cube, refraction);
the three sea geometry sources, with the GPU height field compared against the CPU mesh; a quay with a ship and
a figure (sun lighting flags, sun shadows, ambient occlusion); a night harbour with torches and lanterns (town
with baked vertex lighting, figure and ship lit dynamically: point lights, bloom); a broadside at dusk (flash
lights); a wall, a boulder, a plank deck and a sail with height textures (derived normal maps under the sun
from two sides and under a torch); before / after pairs for the cheap upgrades (anisotropic filtering, MSAA and
alpha to coverage on palms, a hedge and rigging, wind sway at two moments, linear-light sums, rim light, aerial
perspective, contact shadows, scene grades by hour, weather and interior, lens flare, dither); an aliasing
chart (FXAA, sharpening, MSAA, 75 % render scale). CPU products (baked
textures, sky panoramas, grading tables) are written as images too.

The run fails (exit code 1) if the package reports an error or if `ConstrainSettings()` has to change a
shot's settings: every image shows exactly the settings in its name.

The "original" shots use stand-ins for the legacy sea, sky and fixed-function lighting (per-vertex, the engine's seven nearest lights), since the real ones live in
the game; they show what the package leaves untouched, not how the game looks.
