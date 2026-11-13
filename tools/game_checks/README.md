# Game-data checks

Checks that need an installed game (its RESOURCE folder), so they are not host checks (`tests/*/run.py` run without
game data in CI, `tools/ci/run_host_tests.py`). Run them before a release, like the scene sweep
(`tools/scene_sweep`).

- `grass_pack/run.py <game directory>`: every grass map (`.grs`) packs into the grass vertices
  (`src/libs/location/src/grass_pack.hpp`): positions exact, heights within half a step, the window within 15 bits.
  Prints the largest map and the peak buffer of a window.
