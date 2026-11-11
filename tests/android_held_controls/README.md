# Held HUD controls contract checks

Run `python3 tests/android_held_controls/run.py` on the host (ASan/UBSan).

The touch HUD holds the helm (`Ship_TurnLeft` / `Ship_TurnRight`, `WMapShipTurnLeft` /
`WMapShipTurnRight`), the spyglass (`TelescopeIn`), the world-map sail lever (`WMapShipSailUp` / `WMapShipSailDown`) and camera button (`WMapCameraSwitch`) and walking on deck (`DeckCamera_Forward` / `DeckCamera_Backward`)
by name: `hold:<gameplayRevision>\t<control>\t<0|1>`. `HeldControls` merges the hold into
`Controls::GetControlState` with the engine's activation phases. A hold is taken in the
current gameplay revision and lasts, through any number of slow frames and later revisions,
until its release, a cancelled input or the gameplay no longer binding the control (no
heartbeat, no lease). A physical key keeps priority.
