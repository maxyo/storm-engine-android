# Host run on a game directory (Caribbean Legend or TEHO)

Runs the Linux host engine on a game directory in place, read-only, with a hard timeout.

## Game directory rule
On the Linux host the **working directory is the game directory** (`engine.ini`, `Program/`,
`Resource/` in any case). The binary stays in its build directory; next to it the engine target
stages `engine-shaders/RESOURCE/techniques` (from `src/techniques`), which is laid over the
game's techniques as the APK's engine-shaders layer is. Saves and `options` go to the user
storage, never the game directory: `$XDG_DATA_HOME/Akella/Sea Dogs/` (`SaveData/`, `options`,
`Logs/`, `Cache/`). The game profile (TEHO or CL, see `src/libs/core/include/game_profile.hpp`)
is detected at startup and logged in `system.log`; an unknown layout stops the engine.

## Build
```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DSTORM_HOST_SOURCES=<prepared sources> -DSTORM_AUDIO_SOURCES=<audio sources> [-DSTORM_SHADERC=<host shaderc>]
ninja -C build/host engine
```
(see `cmake/host.cmake` and `src/libs/renderer_bgfx/README.md`).

## Run
```sh
python3 tools/cl-host/run.py --game <game dir> --engine build/host/engine --out <writable dir> [--timeout 60]
```
- Starts a private Xvfb (or uses `--display`), software GL, dummy audio, `XDG_DATA_HOME=<out>/data`.
- Stops at the first `[critical]` line in `system.log` (a fatal error waits in a message box),
  at engine exit or at the timeout; prints the reason, the time and the tails of `system.log`,
  `error.log` and `compile.log`, and saves the last frame as `<out>/screen.png`.
- Fails if anything in the game directory changed during the run.
- Keep `<out>` outside the repository; screenshots of game content are local only.
