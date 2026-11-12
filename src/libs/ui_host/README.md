# ui_host — the game's portable UI inside the engine (Android)

One `storm::ui::Host` lives in the engine process, on the game thread, and draws through the
bgfx backend (`storm_ui_bgfx`) in the frame owner's UI views, after the game picture, at native
resolution. There is no second GL surface or thread for the game's UI.

## Data flow

| What | From | Path |
| --- | --- | --- |
| State channels (gameplay, combat, vitals, ship, worldMap, seaOrders, landCommands, seaQuickAction, contextAction, worldInfo, dialogue, spyglass, video, …) | engine publishers (`platform/android_ui.hpp`, dialog, engine app) | `storm::android::ui::state()` — in-process, kept and replayed when the host starts |
| Interface models (widget snapshots) | `xinterface` | `ui::interfaceModel()`; `ui::screen()` opens/closes (a close waits 150 ms for the next screen). An opening is `Host::ExpectInterface`: the model comes once the interface has loaded (seconds at times), and the touches in between are the UI's, held for the screen's first frame |
| Game log lines | engine | `ui::toast()` |
| Touches, `presentation`, `appSettings`, insets, editor answers | `GameActivity` / `MobileHud` (Java) | `EngineUi` natives: queued, applied by `ui::tick()` at the top of the next engine loop iteration. The activity asks Android for unbuffered touch dispatch, so every sample of the touch screen is in the queue as it happened; the moves of an iteration go in as samples (`TouchPhase::Sample`) and each pointer is then placed once for this frame's time (`Host::PlaceTouches`: between its samples, 5 ms back, as Android resamples for its own views). What a finger drags moves evenly from frame to frame, whatever the phase between the touch screen and the engine loop |
| `WantsTouch`, `WantsBack`, interface shown | Java, at pointer-down / back | answered at once from the host's last frame (thread-safe). While the game is between scenes (gameplay `blocked`: starting, loading, no-scene, interface not shown yet) the touches are the UI's and wait for the next interface; gameplay drops them, logged with the states that decided (`Host::WantsTouch`, `tests/routing_check.cpp`) |
| Engine commands of the UI (`widget:…`, `hold:…`, stick slots) | host actions | `ui::takeCommands()`, read in the same iteration as the activity's queue; a latest-value slot keeps only its newest command |
| Pause dialog, shortcuts mailbox, HUD presence, About, settings, system editor | host actions | JNI calls on `GameActivity` (`onUiHudInput`, `onUiAction`, `onUiTextRequest`, `uiFailed`) |

Size and density come from the frame owner (`SurfaceMetrics`: density = Android
`densityDpi / 160`); surface loss and device loss are the backend's (`FrameClient`).
Publishers whose commands carry an activity generation start a new one for each host
(`ui::generation()`).

A host that cannot start is an error shown to the player (`uiFailed`): the game has no other
renderer for its screens.

## One backend everywhere

The game and the application screens without the engine (launcher, downloads, gallery, their
own processes) load the same `libstorm_ui.so` (with `libbgfx.so`, the one bgfx of a process; see
src/libs/ui/README.md, «Android»): the same runtime and the same bgfx backend, bgfx drawing on the
EGL context of their GLSurfaceView (src/libs/ui/android/jni_host.cpp plays the frame owner). A GL
backend for them alone was weighed and dropped: two RmlUi renderers to keep pixel-equal
(RmlUi's GL3 renderer and ours), two code paths in the UI's picture handling, and a second
backend in host captures, against a slower first frame on the emulator (numbers in
src/libs/ui/README.md, «Renderer»). Those screens show no game pictures (no texture module
without the engine): the neutral placeholder stays, logged once.

Every screen switch logs one line (`StormUi` INFO, also in the player's shared log): `UI switch
<screen>: first frame N ms after the action …` — from the engine action that led to it through the
game opening its interface and the model arriving to the screen's first frame (with
the frames held back for it while its layer textures were made) and until its frames
stop re-laying out, with the CPU of those frames by stage (model apply, component rebuilds, styles,
layout, game pictures, render with texture uploads; `FrameStats`).

The frame timeline (`adb shell setprop log.tag.StormUiInput DEBUG` before the game starts, logcat tag
`StormUiInput`): per engine loop iteration every touch sample with its own time and its age when
applied, every action and model, and every frame the host made with what it showed (the screen and
the model revision it was built from), the frames held for it and its CPU by stage.

Debug recordings (`adb shell setprop log.tag.<tag> DEBUG`): `StormUiState` logs every state,
`StormUiModels` writes the newest model of each screen to `<external files>/ui-models/`.
