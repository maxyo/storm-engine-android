Run `python3 tests/android_land_commands/run.py` from the engine checkout.

The production land command reducer runs with AddressSanitizer and
UndefinedBehaviorSanitizer. Tests cover acknowledged selection, duplicate-caption
identities, stale/recycled targets and scene/activity owners, removed/disabled
items, cooldown readiness, bounded metadata, coverage that lasts until it is
replaced or released (no per-frame lease) and queued actions. The handoff suite
also checks initial Android open, selection, child and Back continuity,
pending-vs-release ACKs, holds that end only with the UI's answer however slow
its frames (the waiting time only feeds a diagnostic), decline, late-token
rejection and clock rollover. Presentation
holding never grants current draw coverage or action authority. The missing-source
suite records the native Close boundary and verifies that absent presentation
support preserves stock ownership before/after takeover, revokes stale authority,
and remains distinct from explicit gameplay prohibition. Original BLI script predicates and command effects require device
validation with the application's `android_land_commands.c` presentation adapter.
