# Android allocator startup failure — 2026-10-17

Local official Google APIs emulators (API 30 revision 16 and API 35 revision 9)
with `libndk_translation.so` installed the original ARM64 release APK and the
new full-engine APK successfully. This does not reproduce Xiaomi's reported
`package is null` installation error.

The first native startup on API 35 exposed two allocator ownership problems:

1. With `MI_OVERRIDE=ON`, loading mimalloc recursively entered
   `mi_get_default_heap -> mi_malloc -> __emutls_get_address -> mi_get_default_heap`.
   The ARM64 guest tombstone recorded 512 repeating frames and SIGSEGV before
   `SDL_main`. API 24 target compilation uses emulated TLS, whose initialization
   needs malloc; interposing malloc recurses before mimalloc TLS exists.
2. Turning off the C allocator override alone reached `SDL_main`, then aborted
   with `Scudo ERROR: corrupted chunk header` in `native_bridge_free`, called
   from `FILE_SERVICE::AddEntryToResourcePaths`. The remaining engine-only C++
   new/delete override mixed mimalloc allocations with shared libc++/Bionic frees.

Android now uses Bionic/libc++ for both ordinary C and C++ allocation.
`mimalloc-new-delete.h` remains enabled on desktop. The real mimalloc library
and explicit `mi_*` diagnostics remain available on Android; no fake allocator,
renderer or audio implementation was added.

`verify_engine.py --allocator-library` rejects libc malloc/free interposition
and engine-defined owning new/delete overrides, and checks that the real
mimalloc API remains present. It rejects the previously crashing library and
passes the corrected native build. `ci_engine.sh` runs this check.

After both changes, the complete APK on API 35 loads all native dependencies,
enters `SDL_main`, indexes the imported synthetic files, creates internal
`files/storm/Logs` and `SaveData`, logs engine initialization and remains alive
with the SDL surface and touch controls visible. `mimalloc-redirect status: false`
is expected. A synthetic ZIP imported via the real document picker contains
only an independently generated engine.ini and diagnostic marker, not game data.

This establishes native bring-up on a translated ARM64 environment, not game
menu rendering, Vulkan presentation, audio playback or compatibility with a
physical ARM64 phone. Full resources and affected-device installation logs are
still required. The application project holds the exact APK checksums and local
device evidence; no game resources belong in either repository.
