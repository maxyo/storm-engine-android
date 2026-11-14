# Sea camera fire side

Run `python3 tests/sea_fire_side/run.py` on the host. The player's camera-aimed broadside
(deck and outside camera, its crosshair, `GetFirePos`, the Android minimap's lit side) picks
its gun side with `src/libs/util/include/fire_side.hpp`: every view azimuth selects the
usable side whose fire zone is nearest (no dead sectors between the zones), and that side
aims at the nearest direction inside its own zone. The check runs the selection over the
full circle with the stock zones and asserts in the sources that AI fire (`Fire(position)`,
`Fire(enemy)`, `GetFirstFireBort`/`isCanFireBort`) keeps the stock zone rule.
