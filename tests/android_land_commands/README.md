Run `python3 tests/android_land_commands/run.py` from the engine checkout.

The production land command reducer runs with AddressSanitizer and
UndefinedBehaviorSanitizer. Tests cover acknowledged selection, duplicate-caption
identities, stale/recycled targets and scene/activity owners, removed/disabled
items, cooldown readiness, bounded metadata, expired coverage leases and queued
actions. The handoff suite also checks initial Android open, selection, child and
Back continuity, pending-vs-release ACKs, a fixed 1000 ms grace, timeout-to-native
Close without a stock frame, late-token rejection and clock rollover. Presentation
holding never grants current draw coverage or action authority. The missing-source
suite records the native Close boundary and verifies that absent presentation
support preserves stock ownership before/after takeover, revokes stale authority,
and remains distinct from explicit gameplay prohibition and a known timeout. Original BLI script predicates and command effects require device
validation with the application's `android_land_commands.c` presentation adapter.
