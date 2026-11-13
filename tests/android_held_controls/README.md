# Held HUD controls contract checks

Run `python3 tests/android_held_controls/run.py` on the host (ASan/UBSan).

The touch HUD holds the helm (`Ship_TurnLeft` / `Ship_TurnRight`, `WMapShipTurnLeft` /
`WMapShipTurnRight`), the spyglass (`TelescopeIn`), the world-map sail lever (`WMapShipSailUp` / `WMapShipSailDown`) and camera button (`WMapCameraSwitch`)
by name: `hold:<gameplayRevision>\t<control>\t<0|1>[\t<strength 1..100>]`. `HeldControls` merges the hold into
`Controls::GetControlState` with the engine's activation phases; the strength is its
`CONTROL_STATE::fValue` (strength / 100, a plain hold 1), so the helm turns the ship as far as
it is pulled (ship.cpp, wdm_player_ship.cpp). Walking on deck is a vector, not keys:
`tests/android_deck_walk`. A hold is taken in the
current gameplay revision and lasts, through any number of slow frames and later revisions,
until its release, a cancelled input or the gameplay no longer binding the control (no
heartbeat, no lease). A physical key keeps priority.
A hold is its key held down also for the controls bound INVERSE to the same key: the original
bindings end the spyglass with `TelescopeOut` (VK_CONTROL inverted beside `TelescopeIn`), so its
release lowers the telescope.
