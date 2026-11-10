#include "platform/android_held_controls.hpp"
#include <cassert>
#include <iostream>
using namespace storm::android;

int main()
{
    HeldControls held;
    bool allowed = true;
    held.guard = [&](std::string_view control) { return allowed && control != "WMapShipSailUp"; };
    held.update(7);
    const auto state = [&](const char *control) { return held.merge(control, 0); };

    // activated, active while repeated, released
    assert(held.offer("7\tShip_TurnLeft\t1", 100, 0));
    held.advance(110, 0);
    assert(state("Ship_TurnLeft") == 3 && state("Ship_TurnRight") == 0);
    held.advance(120, 0);
    assert(state("Ship_TurnLeft") == 1);
    assert(held.offer("7\tShip_TurnLeft\t1", 300, 0));
    held.advance(600, 0);
    assert(state("Ship_TurnLeft") == 1);
    assert(held.offer("7\tShip_TurnLeft\t0", 610, 0));
    held.advance(620, 0);
    assert(state("Ship_TurnLeft") == 2);
    held.advance(630, 0);
    assert(state("Ship_TurnLeft") == 0);

    // silence lets go
    assert(held.offer("7\tShip_TurnRight\t1", 1000, 0));
    held.advance(1010, 0);
    held.advance(1000 + HeldControls::leaseMs + 1, 0);
    assert(state("Ship_TurnRight") == 2 && !held.holding("Ship_TurnRight"));

    // a physical key keeps priority; an unknown control is left alone
    assert(held.offer("7\tShip_TurnRight\t1", 2000, 0));
    held.advance(2001, 0);
    assert(held.merge("Ship_TurnRight", 1) == 1 && held.merge("Ship_Fire", 0) == 0 && held.merge("Ship_Fire", 3) == 3);

    // another gameplay revision, a cancelled input, a control the gameplay does not bind
    held.update(8);
    held.advance(2002, 0);
    assert(state("Ship_TurnRight") == 2);
    assert(!held.offer("7\tShip_TurnRight\t1", 2003, 0) && held.offer("8\tShip_TurnRight\t1", 2004, 0));
    held.advance(2005, 1);
    assert(!held.holding("Ship_TurnRight"));
    assert(!held.offer("8\tWMapShipSailUp\t1", 2006, 1) && held.offer("8\tWMapShipSailDown\t1", 2006, 1));
    allowed = false;
    held.advance(2007, 1);
    assert(!held.holding("WMapShipSailDown"));
    allowed = true;

    for (const auto *bad : {"", "8", "8\tShip_TurnLeft", "8\tShip_TurnLeft\t2", "x\tShip_TurnLeft\t1", "8\tShip_Fire\t1", "8\tChrForward\t1"})
        assert(!held.offer(bad, 3000, 1));
    HeldControls unguarded;
    unguarded.update(1);
    assert(!unguarded.offer("1\tShip_TurnLeft\t1", 1, 0));
    std::cout << "held controls: activation phases, lease, priority, revision, cancellation, guard, malformed requests PASS\n";
}
