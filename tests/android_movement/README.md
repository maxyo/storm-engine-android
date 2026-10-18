# Android movement: discrete gait, continuous direction

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

The circular deadzone starts movement above 180/1000 and stops it at or below
120/1000. Radius selects one of two discrete native speeds: walk, or run entered
at 800/1000 and left below 650/1000. The gaps provide hysteresis against thumb
jitter. UP, cancellation and expiry reset the gait. Native run locks immediately
limit movement to walk. Direction remains normalized and continuous at every
angle, with no diagonal speed boost. This explicit intent does not use or invert
the user's physical Shift/always-run preference.

In explore mode the actor faces its camera-relative travel direction and uses
the corresponding forward walk/run gait. The basis comes from LocationCamera,
not temporary renderer view state. Touch camera yaw is independent of these
joystick turns and remains stable after UP; right swipes orbit it. Cancellation,
scripted ownership and physical forward/back requests restore native ownership.
First-person keeps actor-relative forward/back/strafe and native look rotation;
independent camera yaw and automatic travel-facing apply only to third-person.
The camera regression executes the real CameraFollow::MoveCamera method;
`python3 tests/android_movement/camera.py --source-root PATH` can check an older
checkout against the same no-feedback camera contract.

Only ordinary Player locomotion receives the camera-relative vector, before the
existing collision/force pipeline. Backsteps and side dodges in combat retain
native discrete actions. Priority animation, scripted tasks, jumping, swimming,
locks and non-player movement retain native behavior. There is no synthetic
W/S/A/D/Shift input in the analog route.
