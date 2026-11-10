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
  (the first underwater location with a sea, camera looking up), a sea battle, a boarding,
  the world map and a storm entered from the world map.

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
  display, writable data directory and hard-linked copy of the game (the engine's
  techniques laid over the game's, as the APK build does). Every engine boots once per shard
  and then loads its scenes back to back. A crashed or hung engine fails
  its current scene and the shard restarts with the remaining ones.

A scene fails on: a crash or fatal engine error, a timeout, a failed teleport to the entry locator, script errors (`error.log`),
missing textures or models, engine/renderer errors in the logs, a fader that never goes,
or a black, single-colour or unchanged frame.

## Running
Needs a Linux host engine configured with `-DSTORM_SCENE_SWEEP=ON`, Xvfb, Pillow and the
game files (`--out` must be on the same filesystem as `--game`). Keep one persistent build
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
  12 frames after a 4-frame warm-up; the boot scenes are captured once. Used for the per-effect cost of the fx presets.
- `--failed` reruns the failures of the previous report in `--out`; `--changed` runs only
  scenes whose scripts or model folders changed since it, plus its failures (everything
  when the engine binary or `scene_sweep.c` changed: use `--smoke` or `--failed` then).
  With nothing changed it reruns only the previous failures.
- `--baseline report.json` exits non-zero only for scenes that pass in that report.
- `--settle MS` how long each scene renders after its fader before the capture (default 300).
  Frame time and the renderer counters are means over this window: raise it (e.g. 5000) with
  `--only` on a few scenes and `--jobs 1` for performance measurements.

Output in `--out`: `report.json` (per-scene status, reasons, load time, frame time, draw
calls, RSS, and `renderer`: per-frame means of the bgfx shim counters, see
`src/libs/renderer_bgfx/include/storm/render/perf_stats.hpp`), `summary.txt`, `shots/` and contact sheets in `contact/` (failures framed red).
Keep reports out of the repository.
