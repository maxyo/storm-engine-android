# Android analog movement

Run `python3 tests/android_movement/run.py` for the portable production input,
geometry, speed and special-action guard regression under ASan/UBSan.
`tests/android_gameplay/run.py` also exercises the actual gameplay snapshot's
modal/loading/scene/player guards. The movement runner also compiles the actual
Player release/physical fallback and Character displacement blocks with small
fixtures: residual native gait speed cannot cause an UP/center lurch, while
priority/attack/swim/jump displacement and external impulses remain unchanged.
The real descriptor collection is checked for one pass and lock/mapping guards.
These checks do not replace device validation
of camera orientation, animations and collision response.

Gameplay schema 1 adds optional `analogMovement` (boolean) and
`movementGeneration` (positive integer when supported). Old Android clients
continue using their existing digital controls. A new client uses analog only
when both fields and a nonempty `inputOwner` are present.

The existing mobile mailbox accepts:

```
move:1<TAB>inputOwner<TAB>movementGeneration<TAB>sequence<TAB>active<TAB>x<TAB>y
```

Axes are integers in [-1000,1000], right/forward positive. Sequence increases
strictly within one generation. Active is 0 or 1; inactive requires zero axes.
The client sends a 100 ms heartbeat while its pointer is held. The lease expires
after 350 ms and rotates generation, so delayed heartbeats cannot restart input.
Owner, Activity and native hard-cancel changes rotate generation as well.
Stance changes within one owner preserve the generation and the held vector.
A generation-only notification cancels the Java gesture locally; calling JNI
hard cancel again would create a feedback loop.

The circular deadzone is 150/1000. Remaining radius maps continuously to [0,1].
Normal walking reaches native walk speed at .65; remaining travel interpolates
to native run speed. Walking/running animations use corresponding playback pace.
Native run locks limit the maximum to walk speed. This explicit intent does not
use or invert the user's physical Shift/always-run preference.

Only ordinary Player locomotion receives the camera-relative vector, before the
existing collision/force pipeline. Backsteps and side dodges in combat retain
native discrete actions. Priority animation, scripted tasks, jumping, swimming,
locks and non-player movement retain native behavior. There is no synthetic
W/S/A/D/Shift input in the analog route.
