# Android gameplay shortcuts: candidate awaiting device validation

The previous Android HUD sent F1/F2 down and scheduled key-up 180 ms later.
PCS controls sample current SDL keyboard state at game-frame boundaries, so a
complete pulse between slow frames can disappear. Selecting an Android dialog
item also occurs before that dialog is dismissed and game-window focus returns.
These are source-confirmed failure mechanisms, not proof that either caused a
particular observed no-op. Existing device logs do not contain the key/frame and
script-guard trace needed to distinguish them.

The actual game's controls map F1 to `MainMenu`, F2 to `Interface`; the handler
is registered on the exact event `Control Activation` (with a space).
Read-only inspection also found script guards for `dialogRun`,
`InterfaceStates.Launched`, `buttons.resume.enable`, combat/scene restrictions,
and character-menu availability. No proprietary script code is included or
modified. A menu command may correctly do nothing when one of these guards
rejects it.

## Menu command protocol

The paired app change sends `shortcut:mainmenu` or `shortcut:character` through
the existing Java action poll. Its selection callback waits for AlertDialog
OnDismiss, and the Java mailbox waits for resumed game-window focus. Each side
holds at most **one** outstanding menu intent: repeated taps coalesce, conflicting
commands are rejected, and no batch can open several different menus in turn.

The native mailbox executes once, after a completed game frame, with the core,
controls, focus, lifecycle and loading state ready and no native interface screen
open. It emits ordinary `Control Activation` with the whitelisted control name,
not a direct script-function call, fake key state, or unconditional menu launch.
The intent is removed before event dispatch and is never retried if a script
refuses it. Screen transitions and app background discard pending commands.

Both mailboxes expire after 30 seconds in their respective monotonic clock
domains. This handles slow frames without a short key-hold window, while ensuring
an old intent cannot replay indefinitely after missing focus/readiness. An
intent blocked beyond the bound is discarded; the user must tap again. Native
ticks use unsigned subtraction so 32-bit SDL tick rollover is handled.

The first 64 dispatched commands per process log the control name and read-only
`dialogRun`, `InterfaceStates.Launched`, `buttons.resume.enable` values. Attribute
strings are capped at 32 characters; missing variables are marked. Diagnostic
failure cannot stop or alter command delivery. A dispatch log is not proof that
the script opened a menu: pair it with the actual screen/scene outcome.

## Dialogue/cutscene pause is separate

The game's normal `VK_PAUSETimePause` control is itself behind `dialogRun` in the
ordinary handler, so it cannot solve dialogue pause while preserving that guard.
It is intentionally absent from this whitelist.

The paired app candidate uses SDLActivity's protected `pauseNativeThread` and
`resumeNativeThread` lifecycle operations with an Android Nautical pause dialog.
This uses the engine's existing SDL APP_BACKGROUND / APP_FOREGROUND handling to
stop frames and suspend/resume sound, rather than changing script time scale or
bypassing script guards. The existing audio mixer stops its device after its
configured bounded fade; actual voice/cursor continuity needs a device test.

The Java side releases held virtual keys/buttons before pause; explicit pause
survives HOME/onStart; Continue or Back dismisses it and requests resume only
for the same live SDL thread in a resumed, non-destroying Activity. OnDestroy
removes the dismiss listener before closing the window. Outside taps do not
unpause. Neither the SDL JNI lifecycle nor audio behavior was run in host tests.

## Checks and integration

`python3 tests/android_shortcuts/run.py` runs ASan/UBSan mailbox checks for the
whitelist, delayed readiness, coalescing/rejection, single delivery, expiry,
background invalidation and tick rollover. The paired app has a pure-Java
mailbox test, a real producer/consumer handoff check, and a full javac compile
against official SDL Java sources and SDK35.

The real modified `src/apps/engine/src/main.cpp` compiles to an ARM64 API24
object with `tests/android_runtime/compile_engine.py`; existing compiler warnings
remain. Source-only tests do not execute Core::Event, game scripts, SDL Android
JNI, actual focus, pause/resume, or audio. Native full link, APK packaging,
Android runtime and independent screenshot review remain required.

Integrate the native and app changes together. Old native code ignores these
new menu commands. No native game resource or script change is needed. The
stable engine/app, device and shared dependency caches were not modified by
this candidate work; local compile outputs use temporary directories.

Runtime acceptance protocol:

1. Record candidate engine/app/APK identity. Load the same genuine port save used
   for the no-op observation. Collect Android and native logs for the whole run.
2. Use Menu → Игровое меню; confirm Android dialog closes, focus returns, a single
   `Control Activation: MainMenu` log has guard values, and a real native menu
   appears. If it does not, report the exact guard values and script outcome.
   Repeat with character menu; reject duplicate cascades under rapid taps/slow FPS.
3. Confirm existing script restrictions still reject blocked menus. Never repair
   a blocked save by forcing `resume.enable`, `dialogRun`, or script functions.
4. During actual voiced dialogue/cutscene, tap the sole Pause control. Verify the
   Nautical pause dialog, frozen simulation/dialogue progress, and stopped voice
   after the existing audio fade. Capture actual frames/audio; do not infer pause
   merely from dialog visibility or a nativePause log.
5. Wait, HOME/return while explicitly paused, and verify no hidden simulation or
   voice progress. Continue and Back must return to the same dialogue/cutscene,
   resume voice without restarting/skipping, and keep all released inputs clear.
6. Exercise Activity/native-thread exit while paused. Dialog destruction must not
   resume or create another SDL thread. Repeat pause/resume and normal menu entry
   to find stale commands, missed focus transitions, stuck keys and audio state.
7. Give real screenshots/video and interaction evidence to an independent
   `corsairs-android-ui-review` reviewer. No runtime fix is claimed before this.
