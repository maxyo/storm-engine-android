# Host CI, the push gate and the land queue

| Tool | What | When |
| --- | --- | --- |
| `run_host_tests.py` | every `tests/*/run.py` (no NDK, device or game data) | CI on develop pushes and PRs; `--only`, `--changed-since REF` |
| `affected.py` | what a change can affect: host checks, game-data checks, the UI host and Android builds | used by the two below; `affected.py --base REF` prints the plan |
| `gate.py` | the push gate: builds and checks what the change affects, in parallel | before every push to develop (`--push`) |
| `land.py` | the merge queue: several agents' commits rebased and gated together, then pushed | instead of `gate.py --push` when other agents land too |

## The gate (`gate.py`)

```sh
tools/ci/gate.py --rebase            # rebase onto origin/develop, plan, run; no push
tools/ci/gate.py --rebase --push     # ... and push HEAD to develop if everything passed and develop did not move
tools/ci/gate.py --tests renderer_bgfx save_dangling_references   # add host checks by name
tools/ci/gate.py --full              # everything, whatever changed: before a release build
```

It plans from `git diff origin/develop...HEAD` and prints one line per stage, `RUN` with the reason or
`SKIP` with the reason; skipped host checks are counted on the console and listed with their reasons in
`build/gate/plan.txt`. Stages run in parallel, each into `build/gate/<stage>.log`:

- **host_checks**: the affected `tests/*/run.py` (`run_host_tests.py --only ...`, longest first);
- **game_checks**: the affected `tools/game_checks/*` on the local game (`$STORM_GAME_DIR`, default
  `<workspace>/local-testing/extracted/app`; missing game = the gate fails, it is not skipped);
- **ui_host**: `build/ui-host` (src/libs/ui) build + ctest;
- **android**: `build/android-engine`, arm64 Release (LTO) `engine`, SDK/NDK from `$STORM_CI_WORK`.

Rules of `affected.py` (also in its docstring):

- `*.md` affects nothing. `.github/`, `tools/ci/`, `tools/ccache/`, `cmake/`, the top-level
  `CMakeLists.txt` and `tests/<file>` affect everything.
- A host check is affected when its directory changed or its files name the changed file, its directory or
  its component (`src/libs/<lib>/`, `src/apps/<app>/`, `<top>/<name>/`). A CMake check already built in
  `build/host-ci` is narrowed by its Ninja graph: only changed files the build reads count.
- A build is affected when a changed file is in its Ninja graph (dependency log, inputs, configure inputs)
  or is a new file in a directory the build reads; without a built directory it always runs.
- A game-data check is affected when a header its `check.cpp` includes changed (compiler `-MM`).

Examples of what still runs (2026-11-14): a change of `src/libs/core/src/attributes.cpp` or `compiler.cpp`
runs 28 checks including `script_state_generation` (ATTRIBUTES re-parenting) and `save_dangling_references`
(ASan); `src/libs/location/src/grass_pack.hpp` runs the `grass_pack` host check and the `grass_pack` game check
on every map of the game; any `src/libs/renderer_bgfx` change runs `renderer_bgfx` (`preset_switch_check`:
the pause black frame, on OpenGL ES and Vulkan) and the Android build.

## The land queue (`land.py`)

```sh
tools/ci/land.py                 # queue HEAD's commits and wait until they landed (exit 0) or failed (exit 1)
tools/ci/land.py --tests NAME    # with extra host checks
tools/ci/land.py --status        # queue, last results; runner log in <git common dir>/storm-land/runner.log
```

All worktrees share one queue (`<git common dir>/storm-land`). The first agent without a running runner
becomes the runner; the others wait. The runner works in `<workspace>/storm-engine-land` (warm build
directories), rebases every queued entry onto origin/develop and the entries before it, gates the stack
once and pushes it. A failing batch is gated entry by entry; the passing entries land, each failing entry
gets its reason and its gate logs (`<git common dir>/storm-land/logs/<id>/`). A conflicting entry fails
with the conflict. Nobody rebases under a moving develop any more: the queue is the only local pusher. After
landing, `git fetch && git rebase origin/develop` in your worktree.

## Tiers

- **Before every push**: `gate.py` / `land.py` (what the change affects).
- **After a push**: CI (`.github/workflows/host.yml`) runs all host checks on develop.
- **Before a release build**: `gate.py --full`, plus the on-demand engine suites (`tools/scene_sweep`, full
  parity) only for engine/renderer changes; heavy runtime suites (lifecycle, resize) only when their code changed.
