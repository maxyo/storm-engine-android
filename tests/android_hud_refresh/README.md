Run `python3 tests/android_hud_refresh/run.py` (C++20 compiler with ASan/UBSan; `CXX` override supported).

Checks the refresh policy of the touch HUD's land command list (`platform/android_hud_refresh.hpp`,
`LandRefreshSignal` in `platform/android_land_commands.hpp`): the scripts' `AndroidLandRefresh` event is
asked once per period while nothing native changes, in the same loop iteration when readiness, the
owning entities, the native menu state or the reload locator change, once after `force()`, and across
the `SDL_GetTicks` wrap. The Python part checks the wiring in the production sources: the main loop polls
before taking a tap, a tap is re-checked against a fresh script read before it acts, `closeLandCommands`
forces the next ask, and the signal itself calls no script event. Not a device check.
