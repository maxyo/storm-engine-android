# Android dependency build experiment

This directory builds actual native dependencies, not an engine executable or
APK. Do not treat successful dependency compilation as a playable port.

Inputs are local, legally obtained source/toolchain directories. The script
never downloads an SDK or accepts any license. Preserve the licenses and notices
in the original dependencies when packaging them.

Pinned sources:

- SDL 2.0.18: `2e9821423a237a1206e3c09020778faacfe430be`
  (`https://github.com/libsdl-org/SDL`)
- fmt 8.0.1: `d141cdbeb0fb422a3fb7173b285fd38e0d1772dc`
  (`https://github.com/fmtlib/fmt`)

The renderer (bgfx on OpenGL ES 3.0) is pinned and built by the engine itself;
see `src/libs/renderer_bgfx/README.md`. Put CMake and Ninja on PATH.

```sh
python3 tools/android/build_dependencies.py \
  --ndk /path/to/android-ndk-r30 \
  --sdl-source /path/to/SDL \
  --fmt-source /path/to/fmt \
  --build-dir /path/to/android-dependency-build \
  --prefix /path/to/android-dependency-install \
  --jobs 4
```

The SDL patch fixes C89 declaration ordering and replaces removed
`ALooper_pollAll` with `ALooper_pollOnce`. The fmt patch preserves consteval
checks while reusing a single string view under Clang 21.

The script applies patches to the supplied pinned SDL and fmt checkouts.
Use checkouts dedicated to this experiment. It also builds shared fmt. The resulting Android libraries still
need runtime validation and full engine integration.

## Full engine after dependency build

Prepare extra pinned sources with `engine_sources.py --directory /path/to/sources`
(spdlog 1.9.2, fast_float 3.4.0, mimalloc 2.0.3, Catch2 2.13.7, FFmpeg 8.1.3; exact Git hashes
are in that script). CMake builds the LGPL FFmpeg for video from it once and caches it
(`tools/ffmpeg/README.md`); package `runtime/ffmpeg/*.so` and `configure.txt` with the engine. Android uses the NDK/platform zlib ABI, not a bundled
replacement libz. Prepare hashed/patched audio headers with `tests/audio/run.py`.
The configure step builds the host shader compiler of the bgfx pin once per machine
(`src/libs/renderer_bgfx/cmake/storm_shaderc.cmake`) and compiles through ccache
(`tools/ccache/README.md`).

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

`verify_engine.py` expects every `CREATE_*` registration in `src` whose `#ifdef` guards hold
for the options in the build's `CMakeCache.txt` (found above `--library`), and fails when an
option-gated one (e.g. `SceneSweepLibrary`, `STORM_SCENE_SWEEP`) is linked with its option off.

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
host. Emulator measurements of arm64 builds include ARM translation; use an x86_64
build there (see ABIs below). They always include host graphics overhead.

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

## ABIs: arm64-v8a releases, x86_64 emulator builds

Every release is arm64-v8a. x86_64 exists only for debug builds that run natively on x86_64
emulators (arm64 libraries there run through ARM translation, far slower). The ABI is a
parameter of every Android build script, default `arm64-v8a`:

- `STORM_ANDROID_ABI=x86_64` for `ci_dependencies.sh` and `ci_engine.sh`
  (`abi.sh`); `--abi x86_64` for `build_dependencies.py`, `verify_engine.py`,
  `build_torrent.py` and `android/archive/build.py` (`android_abi.py`).
- CMake: `cmake/toolchains/android-x86_64.cmake` instead of `android-arm64.cmake`; FFmpeg
  builds the `android-x86_64` target (`tools/ffmpeg/README.md`).
- Default directories of other ABIs carry a suffix so the warm arm64 ones stay untouched:
  `$work/prefix-x86_64`, `$work/dependencies-x86_64`, `$work/engine-x86_64`, `$work/ui-x86_64`,
  `<torrent work>/android-x86_64`; pass the archive build its own `--build-dir`.
- The application packages one ABI per APK (`build_apk.py --abi`, under `lib/<abi>/`).

## Fast emulator

Emulator checks run close to device speed with an x86_64 build (above) and the emulator
rendering on the host GPU. The default `-gpu host` under a plain Xvfb display gives Mesa
llvmpipe, i.e. software GL on the CPU (1-2 fps in the game). With ANGLE in the guest the
game's GLES runs on the guest's Vulkan, which the emulator's gfxstream runs on the host GPU:

```sh
export DISPLAY=:71   # any X display (Xvfb is fine); without one the emulator falls back to SwiftShader
emulator -avd <avd> -port <port> -no-window -no-audio -no-boot-anim -no-snapshot \
  -gpu host -accel on -feature GuestAngle,VulkanNativeSwapchain
```

Check it: `adb shell dumpsys SurfaceFlinger | grep GLES:` names ANGLE on the host GPU (e.g.
"ANGLE (NVIDIA, Vulkan … RTX …)"), and the emulator log names the graphics adapter. Measured
on one AVD (main menu fps / HUD menu tap): arm64 + llvmpipe 1.3 fps / 1.2-1.8 s, x86_64 +
llvmpipe 2.3 / 0.7-1.0 s, arm64 + host GPU 13.3 / 0.2-0.4 s, x86_64 + host GPU 16.7 /
0.3-0.6 s; with the host GPU the emulator's frame present (~50 ms) is the limit. Release and
performance numbers still come from a real arm64 device.

## Frame pacing (Swappy)

The game window presents through Android Frame Pacing (Swappy 2.1.3 from the Android Game SDK,
`src/libs/renderer_bgfx/src/shim/frame_pacing.hpp`): Swappy GL on the OpenGL ES backend (bgfx calls it instead of
`eglSwapBuffers`, `patches/bgfx-swap-present-hook.patch`), Swappy Vk on the Vulkan backend (instead of
`vkQueuePresentKHR`, `patches/bgfx-vk-present-hook.patch`); the rest is the same code. It paces at the player's «Частота кадров» (settings, image
page; the app offers 30/60/90/120 and the display's own rates where a refresh rate is a whole multiple,
60 by default; applied at once): a whole number of refreshes of one of the display's modes (60 fps: 1 at
60 Hz, 2 at 120 Hz; a 90 Hz panel uses its 60 Hz mode where it has one; `swap_interval.hpp`,
`tests/frame_pacing`), and falls back to a steady 30 fps when frames do not fit. There is no frame limiter of our own and
no `max_fps` key. The ADPF hint session (application `GamePerformance`) aims at that interval
and gets each frame's CPU work without the present wait. Logged (tag `storm-bgfx`, also in
«Поделиться логом»):

- at start and at each «Частота кадров» change: `frame pacing: Swappy GL ...; frame rate N fps (setting); display
  modes ...; current refresh ...; swap interval ...` (`Swappy Vk` on the Vulkan backend);
- on a change of Swappy's interval or the display mode: `frame pacing: Swappy presents every ...`;
- every 30 s: `frame pacing: 30.0 s, N frames (fps) ...; frame ms p50 p95 p99 max; refreshes per
  frame 1:..% 2:..% ...; present wait mean ...` (intervals between presents, in display refreshes);
- without Swappy an error `frame pacing: Swappy GL unavailable (...)`: the game then presents with
  `eglSwapBuffers` at swap interval 1, the display's own rate, unpaced, and logs the same 30 s lines
  (`eglSwapBuffers, no Swappy`). Swappy's own switch `adb shell setprop swappy.disable 1` (root, e.g. an
  emulator after `adb root`) selects that path for an A/B with the same APK.

Emulator frame times are far above 16.7 ms (its present alone takes ~50 ms), so emulator runs show
that pacing works, not its quality; the numbers come from a phone's log.

## Tracy profiler (development builds)

`-DSTORM_TRACY=ON` (`cmake/tracy.cmake`, Tracy 0.14.1, BSD-3-Clause) links the Tracy client into a
development build. Use its own build directory next to the normal one (e.g. `build/android-tracy`):
only the instrumented sources (`storm_tracy_sources` in `src/CMakeLists.txt`) and bgfx (with
`BGFX_CONFIG_PROFILER`) compile differently, the rest comes from ccache. The application refuses such
a `libengine.so` for release APKs; a debug APK keeps the INTERNET permission for the profiler's socket.
The client records on demand (nothing until a viewer connects), listens on 127.0.0.1:8086 only, does
not broadcast and installs no crash handler; the engine log says
`storm-profiler: Tracy 0.14.1 client, on demand, listening on 127.0.0.1:8086`.

Recorded: frame marks at every present; zones of the main loop (UI host input, the frame), the core
frame (script frame events, save/load, services, `execute` with one zone per entity, named by its
class, `realize` the same, controls, run end with the present), the shim's present (UI render,
`bgfx::frame`, the Swappy wait) and bgfx's own scopes (bgfx callback profiler functions, render
thread = game thread); plots of bgfx's CPU and GPU frame timers, draws, texture/render target memory,
transient buffer use, resident memory, the present wait and Swappy's interval.

Connect from the development machine (the viewer must be Tracy 0.14.1, the protocol is versioned):

```sh
adb -s <serial> forward tcp:8086 tcp:8086
# GUI: tracy-profiler (Tracy's profiler/ directory, or the 0.14.1 Windows release), connect to 127.0.0.1
# headless: tracy-capture from Tracy's capture/ directory
tracy-capture -o game.tracy -a 127.0.0.1 -s 30   # 30 s, open game.tracy in the GUI later
```

A host build (`-DSTORM_HOST_SOURCES=...`) with `-DSTORM_TRACY=ON` is profiled the same way, without
the forward.

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
- The cache lives in the engine cache root (`fs::GetCachePath()/Cache`; on
  Android `(getExternalCacheDir() ?: getCacheDir())/engine/Cache`, about 13 MB
  for the test game), never next to saves or in the game folder. Android's
  «Очистить кэш» or low storage may delete it at any time: missing entries are
  compiled again (`Script cache: none ...`, `Script segment '...' compiled`).
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
