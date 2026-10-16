# Current ownership after application split — 2026-10-16

This repository builds only Android native libraries. The application, importer,
APK packaging and APK/release workflows moved to
https://github.com/maxyo/corsairs-android. See `android/README.md` for current
native instructions. The checkpoints below are historical evidence; references
to removed APK scripts describe their former locations, not current commands.
Xiaomi installation and native gameplay remain unverified.

# Android ARM64 port status

This branch now builds a **full ARM64 engine APK with open audio**. It is not
yet a verified working Android game: device startup, graphics, audio routes and
gameplay remain untested. Earlier checkpoint sections below describe historical
limitations; the full-engine checkpoint at the end is the current build status.
Baseline: upstream `a68f43df197a50c5177f40209abc109f01f4b114` (`develop`).

## Verified changes

`STORM_SETUP` no longer executes cross-compiled test binaries on the host unless
`CMAKE_CROSSCOMPILING_EMULATOR` is supplied. Native post-build tests retain their
existing behavior. Cross-compiled tests are still built and must be run on the
target separately if no emulator is configured.

Run the dependency-free CMake regression:

```sh
python3 tests/cmake/cross_compile/run.py
```

The regression builds and links a fixture in native, Generic cross, and emulated
cross modes, and checks whether its executable actually ran. Generic mode uses
the host compiler deliberately: these checks validate build orchestration, not
ARM64 instructions, Android libraries, or engine behavior.

## Evidence and remaining work

The host tests passed with GCC 11.4.0. Conan 1.66.0 and NDK r30 are now
installed in the task workspace. Real Android ARM64 compilation succeeded for
the numerical check, SDL2, DXVK Native D3D9 and diagnostics objects (details
below). No Android device or game assets were supplied. Full engine linking,
APK packaging, startup and gameplay remain unverified.

Source inspection identified these independent porting requirements:

1. Separate Android from the current non-Windows Linux CMake path. The DXVK
   ExternalProject invokes host Meson/Ninja without a cross file; Gallium Nine
   links X11/xcb libraries. Neither path establishes an Android renderer.
2. Supply an Android-compatible D3D9 implementation and its dependencies, or
   implement another rendering backend. Validate real shaders and presentation
   on an Android device before claiming renderer support.
3. Validate the new scalar math and sea-normal paths with an actual Android
   device (compiler check passed). Model skinning already has a non-Windows scalar path; the
   remaining DirectXMath/SSE code there is Windows-only. Benchmark before
   deciding whether sea math needs a NEON implementation.
4. Resolve Android dependencies: the Conan 1 recipe currently requests custom
   prebuilt FMOD packages and imports desktop FMOD shared-library names.
   DirectX is now Windows-only, matching the existing platform renderer paths.
   Confirm Android ARM64 FMOD availability and redistribution terms; do not
   substitute no-op audio or renderer implementations.
5. Add an Android SDL activity/native shared library, lifecycle and storage paths,
   Gradle packaging, and touch input. Pin and verify SDK/NDK and dependency
   versions, then build the complete engine with `arm64-v8a`.
6. Test startup and gameplay with the user's legally obtained TEHO resources.
   Resources must remain outside this repository. Other Sea Dogs games follow
   after the initial TEHO launch and input validation.

The original GPL license and notices are unchanged. No game assets or third-party
engine code under incompatible licenses have been added.

## Portable numerical implementation

`float_to_int.h` selects SSE only on supported x86 targets. Other architectures
use checked scalar conversions, avoiding undefined casts for NaN, infinity and
out-of-range values. Rounding follows the current floating-point environment;
ties-to-even and legacy `ffloor`/`fceil` half-unit offsets are preserved. These
legacy helpers are intentionally not replaced by mathematical floor/ceil.
The scalar implementation reproduces returned values, not SSE exception flags
or unmasked exception traps. Do not compile these functions with fast-math.

Sea normal calculation now uses a four-lane scalar implementation on non-SSE
targets, retaining the existing SSE implementation on x86. Scalar square root
is more accurate than SSE's approximate reciprocal square root; results are
compared with 0.0005 absolute tolerance, not promised bit-identical. The full
wave pipeline and visual rendering have not yet been tested on Android.

Host checks (GCC 11.4, UBSan and float-cast-overflow sanitizer):

```sh
python3 tests/portability/run.py
python3 tests/portability/check_toolchain.py
```

Both native SSE and forced scalar pass 400056 conversion inputs (including the
public wrappers) across four rounding modes, and 40000 randomized normal lanes.
The toolchain test checks six validation cases using a dummy include file. It
does not emulate an NDK or establish Android compiler compatibility.

## Android toolchain and real compile check

`cmake/toolchains/android-arm64.cmake` wraps an existing NDK, checks arm64-v8a,
API >=24 and shared libc++, and propagates inputs into CMake compiler checks.
API 24 is the initial project baseline, not a claim about final device support.
No dependency or SDK is downloaded by the wrapper. The verified version is NDK r30 (`30.0.16248370`), Clang 21.0.0.
Its official Linux archive SHA1 was checked against the published value
`5107f898313790e449e87eee2183d9a20602dee9`. The user explicitly approved
Google's SDK agreement before installation; the wrapper never accepts terms.

With this NDK, compile the **standalone numerical check**, not the
engine, with:

```sh
cmake -S tests/portability/android -B build/android-math \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-arm64.cmake" \
  -DSTORM_ANDROID_NDK=/absolute/path/to/ndk
cmake --build build/android-math
```

Run `storm-math-check` on a device with the matching `libc++_shared.so` deployed
alongside it and an appropriate `LD_LIBRARY_PATH`. A passing check only proves
these numerical operations work; it does not launch the game. The root project
now explicitly rejects Android configuration before the incompatible desktop
Conan/DXVK path. Remove that guard when real Android dependencies and the SDL
application integration are implemented.

## Native dependency audit

| Dependency | Current source evidence | Android work still needed |
| --- | --- | --- |
| D3D9 | `renderer/CMakeLists.txt`, `cmake/linux.cmake` | Android renderer/window integration; cross-build and validate shaders |
| FMOD 2.02.05 | `conanfile.py`, `sound_service/CMakeLists.txt` | Obtain compatible ARM64 SDK and establish redistribution permissions |
| SDL 2.0.18 | `conanfile.py`, window/input modules | Android activity, lifecycle, native library packaging and touch input |
| Sentry 0.6.5 | Now conditional on STORM_ENABLE_CRASH_REPORTS | Keep disabled initially; enabled Android backend remains unvalidated |
| mimalloc 2.0.3 | Shared override in recipe, desktop imports | Validate Android allocator/linking behavior; remove desktop deployment assumptions |
| zlib 1.2.13 | Engine dependency | Cross-build or use explicit compatible package |
| spdlog 1.9.2 / fast_float 3.4.0 | Diagnostics/core | Resolve target-compatible Conan packages |
| libffi | Non-Windows engine SYSTEM_DEPS | Establish whether actually used, then remove unused link or cross-build |
| Steamworks | Optional STORM_ENABLE_STEAM | Keep disabled for initial Android port |
| Catch2 2.13.7 | Recipe build_requires | Correct host/target contexts; deploy target tests instead of host execution |

The custom JFrog Conan remote was tested after installing Conan 1.66.0. The
Release configuration downloaded the zlib recipe from ConanCenter, then failed
resolving `fmod/2.02.05@storm/prebuilt` after removing DirectX from Linux: the storm remote returned a JFrog landing
page (`text/html`) instead of the Conan API JSON. Full dependency resolution
and all Android binary packages remain unverified.

## Optional crash reporting and current toolchain checkpoint

`STORM_ENABLE_CRASH_REPORTS=OFF` now removes Sentry from the recipe and CMake
link dependencies, excludes its headers/callbacks/initialization, and skips
Crashpad/7zip imports. The existing logging worker still initializes and runs.
The enabled desktop Sentry code path is retained. This does not replace an
essential engine subsystem with a stub: only the already optional remote
crash reporter is disabled.

Checks completed:

- 16 combinations of Windows/Linux, crash reporting, Steam and Conan SDL using
  the actual Conan 1 recipe; dependency and deployed-file selection passed.
- Compiled the actual `lifecycle_diagnostics_service.cpp` with reporting OFF
  and ON using official SDL 2.0.18, spdlog 1.9.2 and Sentry 0.6.5 headers.
  `nm -u` confirms no Sentry references when OFF and references when ON.
  These are object-compilation checks, not full linking or runtime validation.

```sh
# Use an isolated Python environment containing Conan 1.66.0.
python tests/diagnostics/test_recipe.py
python3 tests/diagnostics/compile.py \
  --sdl-source /path/to/SDL \
  --spdlog-source /path/to/spdlog \
  --sentry-include /path/to/sentry-native/include
```

Local Conan/Meson/Ninja installation has been recovered and `pip check` passes.
Set `CONAN_USER_HOME` explicitly to the task-local tooling/conan directory when
running Conan; do not rely on its default home-directory cache. The isolated
versions are Conan 1.66.0, Meson 1.9.1, Ninja package 1.13.0.

## Reproducible native dependency checkpoint

See [dependency build instructions](../tools/android/README.md). The build uses
the engine's exact DXVK pin `a2dc99c407340432d4ba5bfa29efa685c27942ea`,
SDL 2.0.18, fmt 8.0.1 and host glslang 11.7.0. Patches retain dependency notices
and licenses. SDL audio, sensors and Vulkan are compiled, not stubbed out.

Verified outputs on NDK r30/API 24:

- `storm-math-check`: ELF64 AArch64 PIE; linked but not executed on Android.
- `libSDL2.so`, `libfmt.so` and `libdxvk_d3d9.so`: ELF64 AArch64 shared libraries; compiled
  and linked, without device presentation or shader/gameplay validation.
- Real diagnostics translation unit with Sentry OFF and ON: ARM64 object
  compilation and undefined-symbol checks passed; no Sentry library linked.
- fmt valid formatting executes on the host and compiles for Android; invalid
  integer-format/string arguments are still rejected at compile time.

The fmt patch makes compile-time format validation use the already constructed
string view, avoiding distinct literal pointers with Clang 21. It does not
turn off consteval validation. The diagnostics check accepts `--fmt-source` for
this external dependency, matching the Conan recipe's external fmt setup.

```sh
python3 tests/diagnostics/check_fmt.py --fmt-source /path/to/patched/fmt
# Set CXX to the NDK aarch64-linux-android24-clang++ for target checks.
python3 tests/diagnostics/check_fmt.py --fmt-source /path/to/patched/fmt --compile-only
```

FMOD's official download page (https://www.fmod.com/download) requires sign-in;
a compatible Android ARM64 SDK and its terms have not been obtained. This is a
remaining full-engine dependency blocker. No mirrored proprietary binaries,
no-op audio, or game assets have been added. Full-engine activity/lifecycle integration,
CMake integration, game packaging and touch controls remain unfinished;
the root Android guard deliberately remains until these are implemented.

## Android application integration checkpoint

A separate [SDL/Vulkan integration probe](../android/README.md) now builds to a
locally debug-signed APK. Native linking, Java/DEX compilation, APK signatures
and alignment have been checked with SDK Platform 35 revision 2 / Build Tools
35.0.0. This APK is **not the full engine**, has no game rendering/audio and has
not been installed or run on a device. The Gradle scaffold is provided but its
build has not been executed; direct SDK-tool packaging is the verified path.

The actual engine entry point consumes Android lifecycle events before flushing
SDL events and suppresses frames in the background. Storage initialization sets
the game working directory and refreshes the resource index after JNI setup;
saves/logs use app-private internal storage. Both modified engine translation
units compile for ARM64. FMOD suspend/resume, D3D9 surface recovery, touch game
controls and full-engine linking remain outstanding. The native probe exercises
the same storage/lifecycle policy without pretending to implement missing game
subsystems.

### FMOD acquisition and licensing checkpoint (2026-10-15)

The recipe's pinned baseline is **2.02.05**. No older minimum has been established
by a successful build. Use matching headers and ARM64 libraries from one Android
SDK; a newer 2.02.x needs explicit compile/runtime checks. Source uses Core API
(`fmod.hpp`, `fmod_errors.h`), not Studio API. Required inputs are Core headers,
`api/core/lib/arm64-v8a/libfmod.so` (or logging `libfmodL.so`) and `fmod.jar`.
See [official Android integration guidance](https://www.fmod.com/docs/2.02/api/platforms-android.html).
The Java component needs initialization before the native system and closing
after its release. Native mixer suspend/resume must be paired on the same thread.

[Official downloads](https://www.fmod.com/download) require sign-in; available
historical versions cannot be confirmed without it. No account or license was
accepted on the user's behalf. The [current EULA](https://www.fmod.com/legal)
restricts SDK redistribution and distribution as part of a game engine/toolset.
The repository's GPLv3 text contains no FMOD linking exception found in this
audit. Distribution compatibility for a combined binary is therefore unresolved;
the presence of FMOD in upstream's build recipe is not proof of permission to
redistribute it. Obtain applicable terms/permissions from the rights holders
before distributing such a binary, or implement and validate a compatible audio
backend. Keep proprietary SDK files outside the public repository.

## Application builds and releases

This engine repository has no GitHub Actions workflows. Android native support
and `tools/android/ci_engine.sh` remain available for local and downstream builds.
APK packaging, importer, mobile UI and release workflow live in
[the Android application](https://github.com/maxyo/seawolves-3-android), which
consumes this engine exclusively as a pinned Git submodule.

The application builds and attaches its debug APK only for a published GitHub
release. Releases are published on the user's command; pushes and pull requests
do not build an APK. Build success does not establish installation or gameplay.
SDK and dependency inputs are pinned; clean runners generate a debug signing key,
so repeatable build steps do not imply bit-identical APKs or a stable certificate.

## Open audio prototype

An isolated miniaudio/stb_vorbis experiment now lives in `android/audio`, with
synthetic host tests and an NDK ARM64 link check in `tests/audio/run.py`. The
original probe build ran both before packaging. See the
[audio experiment instructions](../android/audio/README.md) and the updated
[SoundService integration assessment](android-audio-audit.md). This is not yet
SoundService integration and does not make the APK audible or game-ready.


## Full-engine/open-audio checkpoint

The root Android target now uses explicit pinned open dependencies, builds all
engine modules as an SDL-loadable shared library, and links with `--no-undefined`.
The Android path does not invoke Conan/JFrog or use FMOD. Desktop's existing
FMOD source remains selected by default. Shared aliases/schemes were extracted
without behavioral changes; Android uses the tested open audio voice manager.

Resolved build blockers include non-Windows `__try`, Android's missing `gcvt`,
libc++ execution-policy availability, a non-const sort comparison, a dynamic
format string and inline-only definitions in the custom logging file helper.
The sea path is sequential on Android; its performance is unmeasured.

The full APK includes engine/SDL/DXVK/fmt/mimalloc/libc++, audio code and notices,
a local ZIP importer and basic touch key/mouse controls. No game resources or
proprietary SDKs are bundled. Window/lifecycle and input callbacks are dispatched
on the game thread on Android. Real-device surface recovery and route changes
still require validation.

`ci_engine.sh` builds the native library after dependency preparation;
`verify_engine.py` checks real engine/audio symbols and runtime dependencies.
The application packages that library and records build checks separately from
device/game evidence. A probe is not substituted for the complete engine APK.

See [device prerequisites and acceptance sequence in the application](https://github.com/maxyo/seawolves-3-android/blob/main/android/game/TESTING.md).
