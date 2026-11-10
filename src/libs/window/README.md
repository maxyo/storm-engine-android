# Window and application events

`os_window.hpp` is the window abstraction (SDL2 implementation in
`src/sdl_window.cpp`). `app_events.hpp` is the engine-wide subscription point
for window and application lifecycle events; the full contract (threading,
early delivery, Android event order, queue bounds) is documented at the top of
that header. Summary:

| Event | Meaning | Typical subscriber |
| --- | --- | --- |
| `WindowResized` (width, height) | drawable size changed: rotation, fold, multi-window | renderer back buffer, UI layout |
| `WindowFocusGained` / `WindowFocusLost` | input focus | input, pause menu |
| `WindowMinimized` / `WindowRestored` | desktop minimize | renderer |
| `DisplayOrientationChanged` (orientation) | `SDL_DisplayOrientation` of the display | UI safe areas |
| `WillEnterBackground` | app is about to be suspended | **early**: release surface/swapchain, pause audio, autosave |
| `DidEnterBackground` | suspended | |
| `WillEnterForeground` / `DidEnterForeground` | resumed | recreate surface, resume audio |
| `LowMemory` | OS memory pressure | drop caches |
| `Terminating` | process is being ended | flush |

```cpp
#include "app_events.hpp"

// Normal subscriber: called by the main loop between frames, on the game thread.
const int id = storm::AppEvents::instance().subscribe([](const storm::AppEvent &e) {
    if (e.type == storm::AppEventType::WindowResized) resize(e.width, e.height);
});

// Early subscriber: runs inside the SDL call that raised the event when that
// happens on the game thread (Android pause/resume), i.e. before SDL gets
// control back. Normal subscribers run one dispatch() later, which with the
// engine's main loop is still before Android suspends the game thread.
storm::AppEvents::instance().subscribe([](const storm::AppEvent &e) {
    if (e.type == storm::AppEventType::WillEnterBackground) releaseSurface();
}, true);

storm::AppEvents::instance().unsubscribe(id);
```

All handlers run on the game thread. `apps/engine/src/main.cpp` installs the
hub after `SDL_InitSubSystem`, calls `dispatch()` once per loop iteration after
`SDL_PumpEvents()`, and is itself a subscriber:

- audio is paused on `WillEnterBackground` and resumed on `DidEnterForeground`
  through one gate that also accounts for window focus and Android audio focus
  (`audiofocus:gain|duck|loss` actions sent by the activity);
- `LowMemory` releases unused cached audio (`VSoundService::TrimCaches`);
- scripts receive `AppWillEnterBackground` (only when it can still run before
  suspension), `AppDidEnterForeground`, `AppLowMemory` and
  `AppWindowResized(width, height)` through the ordinary script event
  mechanism. No script handles them yet: an autosave on background is a
  one-line `SetEventHandler("AppWillEnterBackground", ...)` away, and is left
  to the game scripts to decide.

`OSWindow::Subscribe` (focus/close only) is unchanged and remains what
`main.cpp` uses to stop the loop on close.

Tests: `tests/app_events/run.py --sdl-source <SDL checkout>` (real host SDL,
ASan/UBSan and TSan) and `tests/android_window/run.py` for the window mailbox.
