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

    // activated, active, released: no repetition needed
    assert(held.offer("7\tShip_TurnLeft\t1", 0));
    held.advance(0);
    assert(state("Ship_TurnLeft") == 3 && state("Ship_TurnRight") == 0);
    held.advance(0);
    assert(state("Ship_TurnLeft") == 1);
    assert(held.offer("7\tShip_TurnLeft\t1", 0));
    held.advance(0);
    assert(state("Ship_TurnLeft") == 1);
    assert(held.offer("7\tShip_TurnLeft\t0", 0));
    held.advance(0);
    assert(state("Ship_TurnLeft") == 2);
    held.advance(0);
    assert(state("Ship_TurnLeft") == 0);

    // Silence does not let go (event model): held through any number of frames, slow or fast,
    // until its "0".
    assert(held.offer("7\tShip_TurnRight\t1", 0));
    for (int frame = 0; frame < 1000; ++frame) held.advance(0);
    assert(state("Ship_TurnRight") == 1 && held.holding("Ship_TurnRight"));
    assert(held.offer("7\tShip_TurnRight\t0", 0));
    held.advance(0);
    assert(state("Ship_TurnRight") == 2);

    // a physical key keeps priority; an unknown control is left alone
    assert(held.offer("7\tShip_TurnRight\t1", 0));
    held.advance(0);
    assert(held.merge("Ship_TurnRight", 1) == 1 && held.merge("Ship_Fire", 0) == 0 && held.merge("Ship_Fire", 3) == 3);

    // another gameplay revision keeps a taken hold (the guard decides) but refuses a new one made
    // in the old revision; a cancelled input and a control the gameplay does not bind let go
    held.update(8);
    held.advance(0);
    assert(state("Ship_TurnRight") == 1);
    assert(held.offer("7\tShip_TurnRight\t0", 0)); // the release always counts
    held.advance(0);
    assert(!held.offer("7\tShip_TurnRight\t1", 0) && held.offer("8\tShip_TurnRight\t1", 0));
    held.advance(1);
    assert(!held.holding("Ship_TurnRight"));
    assert(!held.offer("8\tWMapShipSailUp\t1", 1) && held.offer("8\tWMapShipSailDown\t1", 1));
    allowed = false;
    held.advance(1);
    assert(!held.holding("WMapShipSailDown"));
    allowed = true;

    for (const auto *bad : {"", "8", "8\tShip_TurnLeft", "8\tShip_TurnLeft\t2", "x\tShip_TurnLeft\t1", "8\tShip_Fire\t1", "8\tChrForward\t1"})
        assert(!held.offer(bad, 1));
    HeldControls unguarded;
    unguarded.update(1);
    assert(!unguarded.offer("1\tShip_TurnLeft\t1", 0));
    std::cout << "held controls: activation phases, held through silence until released, priority, revision, cancellation, guard, malformed requests PASS\n";
}
