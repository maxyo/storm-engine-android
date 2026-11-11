# ui_host — the game's portable UI inside the engine (Android)

One `storm::ui::Host` lives in the engine process, on the game thread, and draws through the
bgfx backend (`storm_ui_bgfx`) in the frame owner's UI views, after the game picture, at native
resolution. There is no second GL surface or thread for the game's UI.

## Data flow

| What | From | Path |
| --- | --- | --- |
| State channels (gameplay, combat, vitals, ship, worldMap, seaOrders, landCommands, seaQuickAction, contextAction, worldInfo, dialogue, spyglass, video, …) | engine publishers (`platform/android_ui.hpp`, dialog, engine app) | `storm::android::ui::state()` — in-process, kept and replayed when the host starts |
| Interface models (widget snapshots) | `xinterface` | `ui::interfaceModel()`; `ui::screen()` opens/closes (a close waits 150 ms for the next screen) |
| Game log lines | engine | `ui::toast()` |
| Touches, `presentation`, `appSettings`, insets, editor answers | `GameActivity` / `MobileHud` (Java) | `EngineUi` natives: queued, applied by `ui::tick()` at the top of the next engine loop iteration |
| `WantsTouch`, `WantsBack`, interface shown | Java, at pointer-down / back | answered at once from the host's last frame (thread-safe) |
| Engine commands of the UI (`widget:…`, `hold:…`, stick slots) | host actions | `ui::takeCommands()`, read in the same iteration as the activity's queue; a latest-value slot keeps only its newest command |
| Pause dialog, shortcuts mailbox, HUD presence, About, settings, system editor | host actions | JNI calls on `GameActivity` (`onUiHudInput`, `onUiAction`, `onUiTextRequest`, `uiFailed`) |

Size and density come from the frame owner (`SurfaceMetrics`: density = Android
`densityDpi / 160`); surface loss and device loss are the backend's (`FrameClient`).
Publishers whose commands carry an activity generation start a new one for each host
(`ui::generation()`).

A host that cannot start is an error shown to the player (`uiFailed`): the game has no other
renderer for its screens. The application screens without the engine (launcher, downloads,
gallery) run in their own processes with `libstorm_ui.so` and the GL backend.

Debug recordings (`adb shell setprop log.tag.<tag> DEBUG`): `StormUiState` logs every state,
`StormUiModels` writes the newest model of each screen to `<external files>/ui-models/`.
