# Android ARM64 port status

This branch is preparatory work, **not a working Android engine or APK**.
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
no-op audio, or game assets have been added. Android activity/storage/lifecycle,
full-engine CMake integration, packaging and touch controls remain unfinished;
the root Android guard deliberately remains until these are implemented.
