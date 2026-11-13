# Scene sweep

Loads every scene of the game in parallel headless engines and reports the scenes that
crash, log script errors, miss textures or models, time out or render a black/blank frame.

**On demand and engine-only.** A deliberate command for engine and renderer work. It is not
part of per-push/PR CI, pre-commit hooks or any other automatic check, and must not be added
to them: a full sweep takes tens of minutes of CPU and needs the player's game files.

## What it loads
`inventory.py` derives the scenes from the game's own scripts, so new content is picked up
without editing the tool:

- every location from the init functions that `PROGRAM/locations/locations_init.c` calls
  (towns, interiors, jungles, caves, forts, beaches, decks, cabins...), categorised by its `type`;
- the sea around every island of `PROGRAM/islands/islands_init.c`;
- special scenes: the main menu with its 3D back scene, the new-game intro, one location of
  every type at midnight, land fights (jungle, town, cave, deck), the sea surface seen from below
  (the first underwater location with a sea, camera looking up), the open sea at sunset (19:00) and at sunrise (7:00, the low sun in view), heading towards the sun, the hero's ship
  at midnight seen close from above (rigging against the water, bow wave, waterline), a sea battle, a boarding,
  the world map and a storm entered from the world map.
  The sea battle waits until the enemy ship has fired its cannons (`CANNON_FIRE` from that
  captain) and fails when it has not within 90 s of game time at sea.

After the new game the script reports the size of the game's own location and island tables;
the sweep fails (`inventory:tables`) when they differ from what `inventory.py` parsed.

`python3 tools/scene_sweep/run.py --game <game dir> --list` prints the counts by category.

## How it works
- The engine built with `-DSTORM_SCENE_SWEEP=ON` (never shipped builds) loads
  `scene_sweep.c` after `Main()` when `STORM_SCENE_SWEEP_DIR` is set
  (`src/libs/core/src/scene_sweep.cpp`). The script starts a new game through the main menu
  and enters each scene with the functions the game uses itself (quest reload to a location,
  quest sea login, world map, storm, boarding), at noon with the noon weather (or midnight).
  The hero enters a location where the game puts him: boarding decks and cabins at their
  `boarding.Loc.Hero` locator, other locations through their first door. A game day passes
  between locations so that phantom NPCs expire as in play.
- For each scene it waits until the scene is loaded and the loading fader is gone, renders
  at least 5 frames and 0.3 s, then asks the runner to capture the frame. The runner saves
  the screenshot, frame time, draw calls and memory, and attributes the engine's log lines
  to the scene.
- `run.py` splits the scenes into shards, one engine per shard, each with its own Xvfb
  display (the engine draws with Vulkan on the GPU and presents into it; see *Renderer*),
  writable data directory and hard-linked copy of the game (the engine's
  techniques laid over the game's, as the APK build does). Every engine boots once per shard
  and then loads its scenes back to back. A crashed or hung engine fails
  its current scene and the shard restarts with the remaining ones.
- Every file of a copy that the sweep or the game writes (`engine.ini`, `options`, the
  `PROGRAM/scene_sweep.c` script, the `engine` link) is unlinked before it is written, so the
  write never goes through the hard link into the player's game or another sweep's copy. After
  every engine run the sweep checks that no file of `--game` changed (inode, size, modification
  time) and stops with a harness error naming the files if one did.
- The sweep never deletes or writes through a symbolic link: a copy that holds one (`--game` itself
  or any folder in it linked elsewhere, e.g. `RESOURCE` linked to a shared extraction) is refused
  with a harness error naming the links, and every path it deletes or writes is checked to be no
  link and to resolve inside its own copy (tests/scene_sweep_links). Pass the real game folder. A
  read-only `--game` (local-testing/extracted/app) works: only the copy's own directories are made
  writable, the hard-linked files keep their modes. The sweep never chmods a regular file and never
  writes a file with another name (nlink > 1): a written file is unlinked first and checked to be its
  own inode, and the check of `--game` after every engine run includes the file modes.

A scene fails on: a crash or fatal engine error, a timeout, a failed teleport to the entry locator, script errors (`error.log`),
missing textures or models, engine/renderer errors in the logs, a fader that never goes,
or a black, single-colour or unchanged frame.

**Known game data defects.** A problem in the game's own files that the engine reports correctly and cannot fix
(we never ship or patch game data) is listed in `known_defects.json`: the game version, the scene, the file at fault,
the exact error text the sweep sees and the reason. A scene whose only problems match its entries is reported as
`KNOWN` (progress line, `status: known` in the report, its own section of the summary), not as failed; any other
problem still fails it. Remove an entry once its cause is gone.

## Running
Needs a Linux host engine configured with `-DSTORM_SCENE_SWEEP=ON`, Xvfb, Pillow, a Vulkan
GPU driver (or `--llvmpipe`) and the game files (`--out` must be on the same filesystem as
`--game`). Keep one persistent build
directory with the option on and rebuild it incrementally (`ninja -C <build> engine`); the
sweep copies the binary once at start, so the build can go on while it runs.

```
python3 tools/scene_sweep/run.py --game <game dir> --engine <build>/engine --out <report dir>
```

- `--jobs N` parallel engines; by default half of the idle cores (at least a quarter of all
  cores), capped by free memory at 2 GB per engine.
- `--only PATTERN...` scenes by shell pattern on `kind:name` or by category
  (`'loc:*_town' tavern map:world`).
- `--smoke` one scene of every category (about 45): a few minutes, the quick check after
  an engine change before a full sweep.
- `--fail-fast` stops at the first failing scene.
- `--variants 'fx_preset=0;fx_preset=2,fx_bloom=0'` captures every scene once per set of graphics
  options (applied at run time through the renderer, `STORM_SCENE_SWEEP_VARIANTS`): shots
  `<scene>@v<i>.jpg`, frame time and draws per variant in the report (`variants`), measured over
  12 frames after a 4-frame warm-up that starts once the variant draws (the renderer prepares its programs
  first: `SceneSweepVariantReady`); the boot scenes are captured once. Used for the per-effect cost of the fx presets. In locations the game's time stands still from the first variant on (`SetTimeScale(0)`;
  the sweep's clock then counts 100 ms a frame), so every variant shows the same picture: camera, people, sea and sky.
- `--failed` reruns the failures of the previous report in `--out`; `--changed` runs only
  scenes whose scripts or model folders changed since it, plus its failures (everything
  when the engine binary or `scene_sweep.c` changed: use `--smoke` or `--failed` then).
  With nothing changed it reruns only the previous failures.
- `--baseline report.json` exits non-zero only for scenes that pass in that report.
- `--llvmpipe` renders with Mesa llvmpipe on the CPU (OpenGL ES) instead of the GPU, see *Renderer*.
- `--settle MS` how long each scene renders after its fader before the capture (default 300).
  Frame time and the renderer counters are means over this window: raise it (e.g. 5000) with
  `--only` on a few scenes and `--jobs 1` for performance measurements.

Output in `--out`: `report.json` (per-scene status, reasons, load time, frame time, draw
calls, RSS, and `renderer`: per-frame means of the bgfx shim counters, see
`src/libs/renderer_bgfx/include/storm/render/perf_stats.hpp`), `summary.txt`, `shots/` and contact sheets in `contact/` (failures framed red).
Keep reports out of the repository.

## Renderer
By default every engine runs `render_backend = vulkan` and draws on the host's GPU; the Vulkan driver
(measured with NVIDIA's) presents into the shard's Xvfb display, whose framebuffer file the sweep captures
as before.
Frames take a few milliseconds instead of 0.1-3 s on llvmpipe, so long scenes (many variants, a long
`--settle`, scenes that wait for game time) finish in a fraction of the time; the boot and the new game take
as long as before (CPU-bound script and data loading). The GPU device nodes must be accessible to the user
running the sweep (NVIDIA: `/dev/nvidia*`; Mesa drivers: `/dev/dri/renderD*`, group `render`).

The sweep checks the renderer each engine logged (`bgfx renderer=`, the program cache's device) before its
first capture and stops with a harness error when it is not Vulkan on a hardware device: no GPU, a missing
Vulkan driver (the engine then falls back to OpenGL ES), or a software Vulkan device such as lavapipe. The
renderer is in `report.json` (`meta.render`) and the summary's first line.

`--llvmpipe` is the explicit opt-out: OpenGL ES on Mesa llvmpipe (`render_backend = gles`,
`LIBGL_ALWAYS_SOFTWARE=1`, `--gl-threads` rasteriser threads), checked the same way. Use it on hosts without a
GPU or to compare with reports made before the GPU default (`build.renderer` in the report; `--failed` and
`--changed` treat a report of the other renderer as a different build).

The GPU path is Vulkan, while phones default to OpenGL ES: an OpenGL ES-only rendering problem shows only with
`--llvmpipe`. OpenGL ES on the GPU is not available under Xvfb: NVIDIA's EGL fails to initialise on an Xvfb
display (its X11 platform needs an X server with the NVIDIA driver), and Mesa there is llvmpipe.
