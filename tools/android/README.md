# Android dependency build experiment

This directory builds actual native dependencies, not an engine executable or
APK. Do not treat successful dependency compilation as a playable port.

Inputs are local, legally obtained source/toolchain directories. The script
never downloads an SDK or accepts any license. Preserve the licenses and notices
in the original dependencies when packaging them.

Pinned sources:

- SDL 2.0.18: `2e9821423a237a1206e3c09020778faacfe430be`
  (`https://github.com/libsdl-org/SDL`)
- DXVK Native: `a2dc99c407340432d4ba5bfa29efa685c27942ea`
  (`https://github.com/Joshua-Ashton/dxvk-native`), the engine's upstream pin
- fmt 8.0.1: `d141cdbeb0fb422a3fb7173b285fd38e0d1772dc`
  (`https://github.com/fmtlib/fmt`)
- Host glslang 11.7.0: `925503088e2bcd76921b1e102c37fc320bace254`
  (`https://github.com/KhronosGroup/glslang`)

Build glslang with `-DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=ON`, then build the
`glslangValidator` CMake target. Put Meson 1.9.1 and Ninja 1.13.0 on PATH.

```sh
python3 tools/android/build_dependencies.py \
  --ndk /path/to/android-ndk-r30 \
  --sdl-source /path/to/SDL \
  --fmt-source /path/to/fmt \
  --dxvk-source /path/to/dxvk-native \
  --glslang /path/to/glslang-build/StandAlone/glslangValidator \
  --build-dir /path/to/android-dependency-build \
  --prefix /path/to/android-dependency-install \
  --jobs 4
```

The DXVK patch preserves x86 SSE, provides scalar ARM64 equivalents for NaN
replacement and aligned comparisons, uses the ARM yield instruction in the
spin loop, enables its POSIX source files on Android, and obtains SDL Vulkan
extensions without trying to create a second Android window. Vulkan capability
and visual correctness still require a real device; no capability emulation or
renderer/audio stubs are provided.

The SDL patch fixes C89 declaration ordering and replaces removed
`ALooper_pollAll` with `ALooper_pollOnce`. The fmt patch preserves consteval
checks while reusing a single string view under Clang 21.

The script applies patches to the supplied pinned SDL, fmt and DXVK checkouts.
Use checkouts dedicated to this experiment. It also builds shared fmt. The resulting Android libraries still
need runtime validation and full engine integration. FMOD is not downloaded or
licensed by this script.

## Full engine after dependency build

Prepare extra pinned sources with `engine_sources.py --directory /path/to/sources`
(spdlog 1.9.2, fast_float 3.4.0, mimalloc 2.0.3, Catch2 2.13.7; exact Git hashes
are in that script). Android uses the NDK/platform zlib ABI, not a bundled
replacement libz. Prepare hashed/patched audio headers with `tests/audio/run.py`.
Copy the built `libdxvk_d3d9.so` into the dependency prefix's `lib` directory.

```
cmake -S . -B build/android-engine -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-arm64.cmake" \
  -DSTORM_ANDROID_NDK=/path/to/android-ndk-r30 \
  -DSTORM_ANDROID_DEPS=/path/to/dependency-prefix \
  -DSTORM_ANDROID_SOURCES=/path/to/sources \
  -DSTORM_AUDIO_SOURCES="$PWD/build/audio/patched"
cmake --build build/android-engine --target engine --parallel 2
python3 tools/android/verify_engine.py --library build/android-engine/game/libengine.so \
  --ndk /path/to/android-ndk-r30
```

The source directory must also contain the pinned DXVK checkout.
`ci_dependencies.sh` prepares tools/dependencies and runs regressions;
`ci_engine.sh` builds and verifies only the native library.
It defaults to `Release`: `-O2 -g -DNDEBUG` with ThinLTO, hidden symbol
visibility and section garbage collection (`cmake/android.cmake`;
`-DSTORM_ANDROID_RELEASE_LTO=OFF` disables only LTO). Only `SDL_main` and the JNI
entry points remain exported. Debug info stays in the library so the
application's release packaging can strip it and keep the symbol file.
`STORM_NATIVE_BUILD_TYPE=Debug` gives an unoptimized diagnostic build. For
measurements against the previous non-LTO configuration, use an isolated
`RelWithDebInfo` build directory:

```sh
STORM_NATIVE_BUILD_TYPE=RelWithDebInfo \
STORM_NATIVE_BUILD_DIR="$PWD/build/android-ci/engine-relwithdebinfo" \
  bash tools/android/ci_engine.sh
```

Package both `game/libengine.so` and `runtime/libmimalloc.so` from that build.
Compare the same source revision, scene/save, camera, resolution and settings;
do not compare while other emulator instances or native builds compete for the
host. Emulator measurements include ARM translation and host graphics overhead.

`render_metrics=1` in the test game's `engine.ini` enables five-second logs of
successful Present cadence (FPS and frame interval p50/p95/p99/max), mean Present
call time and draw count. These are not GPU timestamps. The default is off.
`capture_render_scene.py --help` describes read-only scene capture, including
screenshots, logs, memory and the tested APK hash. Allow loading and warmup to
finish before capture; window boundaries can extend outside the capture interval.

`texture_anisotropy` controls smooth texture minification: `0` preserves the
game's techniques (default), `1` uses linear filtering, and `2`, `4`, `8`, or `16`
request anisotropic filtering capped by device capabilities. Explicit point
sampling, mip filtering and magnification are preserved. Android graphics
preferences override this key and `texture_degradation`, `vsync`, `screen_x`,
and `screen_y` at renderer startup; unset preferences use the game INI values.
Changes require restarting the game. Evaluate increased filtering on oblique
ground/deck textures and measure its cost before choosing a device-specific default.

## Host checks and CI

`python3 tools/ci/run_host_tests.py` fetches the pinned open-source sources into
`build/host-ci` and runs every `tests/*/run.py` in parallel with the arguments it
needs (run it under `xvfb-run -a` without a display; the SDL input check opens a
window). `.github/workflows/host.yml` runs it, plus the host build and unit test of
`src/libs/ui`, on pushes and pull requests to `develop`. Android builds and device
checks are not part of that workflow.

## Compiled script cache (`[script] cache_mode`)

The script compiler stores each compiled segment and reloads it on the next
start instead of recompiling the game scripts. It is **on by default**
(`cache_mode = 2`); `cache_mode = 0` in the game's `engine.ini` turns it off,
any other value is logged and also turns it off.

- `cache_mode = 2` (default) checks the key once per session, at the first
  segment load. `cache_mode = 1` recomputes it on every segment load, i.e. on
  every interface or location script the game loads; that only helps while
  editing scripts during a session, which does not happen on Android (game
  files are imported, the overlay is inside the APK).
- The cache lives in the application's private storage (`files/storm/Cache`,
  next to saves and logs; about 13 MB for the test game), never in the game
  folder.
- It is discarded and rebuilt when anything it depends on changes. The key
  covers every file of the resolved program directory and of the APK's
  interface overlay (relative path, size and modification time), the engine
  build (watermark with the Git revision, and compile time of `compiler.cpp`),
  the cache layout version and the absolute game directory.
- Every outcome is logged: `Script cache: none ...` (first start),
  `... scripts or engine build changed (key A -> B); rebuilding`,
  `... state file ... is damaged; rebuilding`,
  `Script cache entry '...' is damaged; recompiling` (torn, truncated or
  bit-flipped entry: deleted, compiled from source and written again),
  `... cannot be applied; recompiling` (intact entry that does not parse or
  clashes with a loaded name). If the folder cannot be reset the cache is off
  for the session with an error line. Each segment load logs
  `Script segment '...' loaded from cache|compiled in N ms`.
- Entries are written through a temporary file and renamed; each has a size and
  FNV-1a header (`src/libs/core/src/script_cache_files.hpp`).

Verified on the emulator (API 35 x86_64 image running the arm64 engine,
Release native build, test game, shared host) on 2026-11-10:

| Start to main menu | script segments (63 loads) | Play tap to main menu |
| --- | --- | --- |
| cache off | 6.7 s (`seadogs.c` 5.4 s) | 23.4 s (one run) |
| cold cache (compile and store) | 4.6-8.4 s | 13.0-25.5 s |
| warm cache | 0.15-0.29 s (0.5 s at host load 66) | 9.1-14.3 s |

The end-to-end column is noisy (UI polling, other emulators on the host); the
segment column is the engine's own measurement. Invalidation was observed for:
a touched game script (`PROGRAM/interface/quick_save.c`), a changed APK
overlay script, a different engine build, a bit-flipped and a truncated entry,
and a truncated state file; the session after each recompiled and then loaded
everything from the cache again. A warm-cache session that loads a save and
autosaves produced a state that differs from a fresh-compile session's only as
much as two fresh-compile sessions differ from each other (NPC AI states,
floating values advanced by time), with no script errors in either.

Host test: `tests/script_cache_files/run.py` (state and entry files: missing,
damaged, reset, round trip, torn/flipped/empty/old-layout entries) and
`tests/path_fingerprint/run.py` (key over the script tree).

Not detected: a script edited in place with unchanged size and modification
time (contents are not hashed), and a local rebuild where only files other than
`compiler.cpp` changed under the same Git revision; CI release builds are clean
builds, and changes to tokens or bytecode layout recompile `compiler.cpp`.

APK packaging and releases belong to https://github.com/maxyo/corsairs-android.
