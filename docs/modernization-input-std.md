# SDL input and standard compiler string storage

This change follows [storm-engine discussion #19](https://github.com/storm-devs/storm-engine/discussions/19).
That discussion proposes SDL2 or gainput and recommends retaining an abstraction
for rebinding. The engine already has `storm::Input` / `SDLInput`, links SDL2, and
uses SDL for keyboard, mouse, controller and Android JNI input. Adding another
input dependency would duplicate an existing backend.

## Actual input replacement

The remaining Windows path in `PCS_CONTROLS::Update` previously polled
`GetCursorPos`, subtracted a previous cursor position, computed a window center
with `GetWindowRect`, and called `SetCursorPos` every frame. It overwrote deltas
already received through the SDL mouse event path.

All platforms now consume the existing SDL `MouseMove` deltas and sensitivity
multipliers. `Input::SetRelativeMouseMode` delegates cursor capture to
[`SDL_SetRelativeMouseMode`](https://wiki.libsdl.org/SDL2/SDL_SetRelativeMouseMode).
It is enabled when the input service starts and when the game gains focus;
focus loss and service destruction disable it. Its boolean result exposes a
backend that cannot enter relative mode. No native cursor polling/warping remains
in PCS controls. Relative mode uses SDL's platform mouse behavior, so Windows
acceleration/feel should be checked interactively before release.

`PCS_CONTROLS::AppState` clears accumulated deltas on both focus transitions and
ignores movement while inactive. SDL flushes its own pending motion on mode
changes. Android's existing event-watch copy queue must discard the same stale
motion explicitly: only `SDL_MOUSEMOTION` is removed; key and button events,
including onPause releases, retain order. The queue remains necessary because the
main loop flushes the SDL event queue before `PCS_CONTROLS::Update`. Replacing it
without changing main-loop ownership would lose JNI events.

No VK/scancode tables, key/button polling, wheel factor, script control mapping,
Android JNI callbacks, HUD touch mapping, or camera delta scaling are changed.
No renderer, GLM, bgfx, window implementation, root CMake, or workflow changes
are included.

## Real custom container replacement

`STRINGS_LIST` remains the compiler-facing API for source-file names, labels,
loop break/continue patch offsets, and debug-expression scratch tables. Its
custom `malloc`/`realloc` pointer table and separate size/capacity bookkeeping
are replaced with `std::vector<std::unique_ptr<char[]>>`.

The separately allocated records preserve returned `char *` addresses through
table growth and deletion of other entries. Stable erase preserves ordinal
file/label IDs. Each record still contains `[uint32_t hash][payload bytes][text]`;
the hash, case-insensitive lookup, NUL text, zero-initialized opaque data, and
`SetStringDataSize` invalidation contract remain unchanged. Compiler consumers
use those public APIs, not raw serialization of the list object. Payload roundtrip
and ordinal ordering therefore matter; no save/cache format migration is added.

RAII releases every record, including after deleting the final entry, releasing
an empty list, and reusing a list for a new segment. The old overlapping `memcpy`
used to compact the pointer table disappears. Copying is explicitly disabled:
the old shallow copy would duplicate ownership and double-free the allocation;
no actual caller copies a list. The real compiler translation unit is compiled
to check its consumers.

## Reproducible checks

Use existing official SDL2 headers and DXVK compatibility headers; no download,
SDK installation, native engine build, or device is required. Run with a separate
Xvfb display so relative mouse capture cannot affect the user's desktop:

```bash
xvfb-run -a python3 tests/input_std/run.py \
  --sdl-source "$STORM_DEP_SOURCES/SDL" \
  --dxvk-source "$STORM_DEP_SOURCES/dxvk"
```

The runner uses the real installed host `libSDL2-2.0.so.0`, not an SDL stub.
Override `--sdl-library` for another installed soname. It runs the real desktop
adapter and separately compiles/runs the actual `__ANDROID__` deferred-dispatch
branch against host SDL. Tests verify signed relative deltas, mapping of W,
single delivery, callback thread, mode entry/exit, stale-motion removal,
retention of key-up across pause/resume, fresh motion after resume and capture
release on destruction. Events are pushed through the real SDL API;
[`SDL_PushEvent`](https://wiki.libsdl.org/SDL2/SDL_PushEvent) does not alter physical
device state, so these tests do not claim to test a physical keyboard or JNI.

The string container test links the real `strings_list.cpp` with ASan/UBSan and
checks growth by 4096 entries, pointer stability, deletion order, case-insensitive
lookup, odd-sized binary data, repeated release, final-element deletion,
reuse, size-change invalidation and compiler-style uint32 offset roundtrip.
All temporary binaries are created in a unique temporary directory.

To compile the real affected code for Android ARM64 as well:

```bash
xvfb-run -a python3 tests/input_std/run.py \
  --sdl-source "$STORM_DEP_SOURCES/SDL" \
  --dxvk-source "$STORM_DEP_SOURCES/dxvk" \
  --ndk "$STORM_NDK" --dependency-sources "$STORM_DEP_SOURCES"
```

This compiles `sdl_input.cpp`, `pcs_controls.cpp`, `strings_list.cpp`, and the
actual `compiler.cpp` consumer with the NDK API24 ARM64 compiler. Existing fmt
and legacy compiler warnings are retained. It does not link a full engine.

Verified locally: both real-SDL host paths, the ASan/UBSan container test, all four
real ARM64 objects, and `git diff --check` pass. Validation output is retained
locally in `build/input-std/validation.log` (ignored build evidence). No Android
runtime/device, Windows runtime, physical mouse, game save, full engine link,
or gameplay claims are made by these checks. Those checks remain necessary
before integrating this branch into the stable application.
