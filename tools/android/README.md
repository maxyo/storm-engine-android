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

The script compiler can store each compiled segment and reload it on the next
start instead of recompiling the game scripts. It is **off by default**
(`cache_mode = 0`) and that default was deliberately not changed:

- `cache_mode = 2` loads from the cache and checks the scripts once per session;
  `cache_mode = 1` re-checks on every segment load (walks the script tree each
  time; only useful while editing scripts).
- The cache lives in the application's private storage
  (`files/storm/Cache`, next to saves and logs), never in the game folder.
- It is discarded and rebuilt when anything it depends on changes. The key
  covers every file of the resolved program directory and of the APK's
  interface overlay (relative path, size and modification time), the engine
  build (revision and compile time), the cache layout version and the absolute
  game directory. Before this revision the key was the sum of modification
  times of an unresolved `PROGRAM\` path, i.e. on Android it only saw the
  overlay and never noticed changed game scripts.
- Each cache file carries a size and hash header and is written through a
  temporary file; a damaged or unparsable entry is deleted and the segment is
  compiled from source.

What is verified: key and file-integrity logic on the host
(`tests/path_fingerprint`) and that the engine builds. What is not: a game
session started from a warm cache on a device or emulator, and how much startup
time it saves there. The cache replays compiler side effects (defines,
variables, functions, script libraries, event handlers, relocations) from its
own serialization, so a mismatch with a fresh compile would be a gameplay bug
rather than a crash; enable it by default only after such a run compares
equal. A file edited in place with unchanged size and modification time is not
detected (contents are not hashed).

APK packaging and releases belong to https://github.com/maxyo/corsairs-android.
