# Held HUD controls contract checks

Run `python3 tests/android_held_controls/run.py` on the host (ASan/UBSan).

The touch HUD holds the helm (`Ship_TurnLeft` / `Ship_TurnRight`, `WMapShipTurnLeft` /
`WMapShipTurnRight`), the spyglass (`TelescopeIn`) and the world-map sail lever (`WMapShipSailUp` / `WMapShipSailDown`)
by name: `hold:<gameplayRevision>\t<control>\t<0|1>`. `HeldControls` merges the hold into
`Controls::GetControlState` with the engine's activation phases. A hold must be repeated
within 350 ms, belongs to one gameplay revision, needs the gameplay to bind the control,
and ends with a cancelled input. A physical key keeps priority.
