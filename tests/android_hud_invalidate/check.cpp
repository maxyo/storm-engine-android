// When the touch HUD asks the scripts for the land menu again (LandRefreshGate, android_land_commands.hpp).
#include "platform/android_land_commands.hpp"
#include <cassert>
#include <cstdio>

using storm::android::LandRefreshGate;

int main()
{
    LandRefreshGate gate;
    // The first poll asks: nothing has been read yet.
    assert(gate.due(true, 7, false));
    // Nothing invalidated: never again, however many loop iterations pass.
    for (int i = 0; i < 10000; ++i)
        assert(!gate.due(true, 7, false));
    // The land interface was told its list changed (update message, menu opened, command executed).
    assert(gate.due(true, 7, true));
    assert(!gate.due(true, 7, false));
    // Readiness changes both ways ask once each.
    assert(gate.due(false, 7, false));
    assert(!gate.due(false, 7, false));
    assert(gate.due(true, 7, false));
    assert(!gate.due(true, 7, false));
    // Another land interface entity (a new location) asks once; losing it too.
    assert(gate.due(true, 8, false));
    assert(!gate.due(true, 8, false));
    assert(gate.due(true, 0, false));
    assert(!gate.due(true, 0, false));
    // force(): the menu was emptied natively (closeLandCommands); the next poll asks once.
    gate.force();
    assert(gate.due(true, 0, false));
    assert(!gate.due(true, 0, false));
    std::puts("android_hud_invalidate: ok");
}
