// Hull damage of ship-island contacts (src/libs/touch/src/island_impact.hpp). The game script deals
// power * 3 hull hit points per SHIP_SHIP2ISLAND_COLLISION event (Ship_Ship2IslandCollision).
#include "island_impact.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{
using namespace storm::touch;

// Hull HP lost over `seconds` of contact at `fps`, the point moving `pointSpeed` knots, `into` towards the island.
float damage(float fps, float seconds, float pointSpeed, float into)
{
    IslandContact contact;
    const float dt = 1.0f / fps;
    float hp = 0.0f;
    for (float t = 0.0f; t < seconds; t += dt)
    {
        AdvanceIslandContact(contact, dt);
        hp += IslandImpactPower(contact, pointSpeed, into, dt) * 3.0f;
    }
    return hp;
}
} // namespace

int main()
{
    // A ship pressed against the shore by the wind: under a knot into the rock, for ten minutes, at 30 and 60 fps.
    // Only the first touch counts (as an impact of that small speed): far below any hull (a tartane has ~150 HP).
    for (const float fps : {30.0f, 60.0f})
        assert(damage(fps, 600.0f, 0.8f, 0.6f) < 5.0f);
    // A hard ram: the first contact is the same event the game always sent (power = the point's speed).
    {
        IslandContact contact;
        AdvanceIslandContact(contact, 1.0f / 30.0f);
        assert(IslandImpactPower(contact, 8.0f, 8.0f, 1.0f / 30.0f) == 8.0f);
    }
    // Driving on into the island: damage per second no longer depends on the frame rate.
    const float at30 = damage(30.0f, 10.0f, 5.0f, 5.0f), at60 = damage(60.0f, 10.0f, 5.0f, 5.0f);
    assert(at30 > 100.0f && std::fabs(at30 - at60) / at30 < 0.05f); // 5 kn: 15 (impact) + 4 * 3 HP/s
    // A new contact after a pause is a new impact.
    {
        IslandContact contact;
        AdvanceIslandContact(contact, 0.1f);
        IslandImpactPower(contact, 4.0f, 4.0f, 0.1f);
        AdvanceIslandContact(contact, kEpisodeGap + 0.1f);
        assert(IslandImpactPower(contact, 4.0f, 0.0f, 0.1f) == 4.0f);
    }
    std::puts("PASS: resting island contact does not sink a ship, rams cost as before, damage is frame-rate independent");
}
