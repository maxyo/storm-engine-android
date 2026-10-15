# Android integration probe

This is a separate **SDL/Vulkan integration probe**, not the game or full engine.
It opens a Vulkan window/surface, enumerates physical devices, logs lifecycle and
touch events, and closes with Android Back. It draws no game scene and has no
swapchain, D3D9 or FMOD validation. A blank window is expected. No game data is
required or bundled. Original SDL Java classes are used from the pinned source.

## Build with installed tools

First build the dependencies using `tools/android/README.md`. Tested inputs:
NDK r30 (30.0.16248370), Android platform 35 revision 2, Build Tools 35.0.0,
OpenJDK 17, CMake 3.22.1, Ninja 1.13.0, pinned SDL 2.0.18. All SDK inputs must
be obtained under their applicable terms; these scripts do not accept licenses.

```sh
cmake -S android/native -B build/android-probe -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/android-arm64.cmake" \
  -DSTORM_ANDROID_NDK=/absolute/path/to/android-ndk-r30 \
  -DSTORM_ANDROID_DEPS=/absolute/path/to/dependency-prefix \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/android-probe
python3 tools/android/build_probe_apk.py \
  --android-jar /path/to/platforms/android-35/android.jar \
  --build-tools /path/to/build-tools/35.0.0 \
  --sdl-source /path/to/patched/SDL \
  --deps-prefix /path/to/dependency-prefix \
  --probe-library build/android-probe/libstorm_probe.so \
  --ndk /path/to/android-ndk-r30 \
  --build-dir build/probe-apk
```

The packaging script compiles Java and DEX, packages ARM64 libraries and notices,
aligns the APK, and signs it with a generated local **debug-only** key. Never use
that key for a release. The key and APK stay in the chosen build directory.
No SDK, games, or signed APKs are checked into the source repository.

A Gradle/Android Studio scaffold is also provided (AGP 8.6.1, Gradle 8.7,
JDK 17). Supply `-PstormSdlSource=/path/to/SDL` and
`-PstormNativePrefix=/path/to/prefix`. No Gradle wrapper is downloaded; the
verified packaging path above uses SDK tools directly. Gradle configuration
has not yet been executed.

## Storage contract and device checks

`platform/android_runtime.hpp` supplies the shared storage/lifecycle policy:

- Game files: SDL app-specific external files directory plus `/game`; falls
  back to app-private internal files plus `/game` if external storage is absent.
- Saves, logs and screenshots: internal files plus `/storm`, independent of
  temporary external-storage availability. Uninstalling the app removes these
  app-specific files; back up saves before uninstalling.
- No shared-storage permission, hardcoded `/sdcard`, URI-to-path guessing or
  automatic game download. A user-facing file importer remains future work.

The engine entry point sets its current directory to the game root and rebuilds
its case-insensitive resource index after Android JNI initialization. It reports
missing `engine.ini` clearly, but that file alone does not prove a complete game
installation. The executable-directory API returns the same game root.
Background/foreground/termination events are consumed on the game thread before
the legacy event flush. Background state prevents rendering even when the desktop
`run_in_background` option is set. Actual FMOD mixer suspend/resume and renderer
surface-loss recovery still require integration and device validation.

Device procedure (not yet performed): install `storm-integration-probe-debug.apk`,
launch `org.stormengine.port.probe`, inspect `adb logcat -s StormProbe`, then test
Home/resume, lock/unlock, orientation, touch and Back. The packaging script marks this probe debuggable, so its detailed log is
available with `adb shell run-as org.stormengine.port.probe cat
files/storm/Logs/android-probe.log`. Device success requires a created Vulkan
surface, at least one GPU, correct transitions and clean shutdown, not just
installation. A later probe must test swapchain/presentation and the actual D3D9
path before making any renderer support claim.

## Current evidence

NDK linked `libstorm_probe.so` with exported `SDL_main` and no unresolved link
errors. Java/DEX compilation, APK v2/v3 signature verification and zip alignment
passed. Host lifecycle/storage cases passed; their Android objects compile.
The real engine `main.cpp` and `file_service.cpp` compile for ARM64 against actual
SDL/spdlog/fmt/mimalloc/DXVK headers. These object checks do not link the engine.
No device/emulator was supplied, so installation and runtime remain unverified.
