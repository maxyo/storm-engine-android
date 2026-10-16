# Android native engine

This repository owns the ARM64 native engine, Android runtime/storage policy,
open audio backend, pinned native dependency builds and native regression tests.
The Android application, Java launcher/importer, touch controls, APK packaging
and GitHub Releases live in https://github.com/maxyo/corsairs-android.

Run `bash tools/android/ci_dependencies.sh`, then
`bash tools/android/ci_engine.sh` with JDK 17, CMake, C++ compiler, Python venv,
FFmpeg, curl and unzip installed. `STORM_CI_WORK` overrides the build workspace.
The pinned NDK archive is checksum verified. This builds `libengine.so`,
`libmimalloc.so` and the runtime dependencies; it never builds or publishes APKs.
See `tools/android/README.md` for manual native build commands.

Historical APK/device evidence remains in `docs/android-install-audit.md`.
Native linking does not establish installation, Vulkan rendering or gameplay.

Android uses platform C/C++ allocation to avoid mimalloc TLS recursion and
shared-libc++ heap ownership mismatches. See [allocator audit](../docs/android-allocator-audit.md).
