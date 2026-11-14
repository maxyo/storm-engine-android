// The gun side the player's camera aim fires (owner rule: the full circle, no dead sectors).
// Every direction selects the usable side whose fire zone is nearest to it (0 inside the zone,
// else the angle to the zone's edge; inside, the side whose centre is nearest), and that side
// aims at the nearest direction within its own zone. The sea AI's camera fire, its crosshair
// and the Android minimap's lit side all answer "which side fires" with it. AI fire keeps the
// stock zone rules and does not use it. Angles are azimuths in radians, any common origin.
// Header-only, standard library only.
#pragma once

#include <cmath>
#include <optional>

namespace storm::fire_side
{

constexpr float kTwoPi = 6.28318530717958647692f;

struct Side
{
    float direction = 0.0f; // centre of the fire zone
    float zone = 0.0f;      // full width of the fire zone
};

// The angle from `look` to the side's zone centre, 0…pi.
inline float offCentre(float look, const Side &side)
{
    return std::fabs(std::remainder(look - side.direction, kTwoPi));
}

// The angle from `look` to the side's zone: 0 inside it, else to its nearer edge.
inline float gap(float look, const Side &side)
{
    return std::fmax(0.0f, offCentre(look, side) - side.zone * 0.5f);
}

// The index of the side that fires toward `look`, or -1 when no side can fire.
// side(i) for i in [0, count) gives std::optional<Side>: nullopt for an absent or destroyed side.
template <class SideAt> int nearest(float look, int count, SideAt side)
{
    int best = -1;
    float bestGap = 0.0f, bestOff = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const std::optional<Side> s = side(i);
        if (!s)
            continue;
        const float g = gap(look, *s), off = offCentre(look, *s);
        if (best < 0 || g < bestGap || (g == bestGap && off < bestOff))
        {
            best = i;
            bestGap = g;
            bestOff = off;
        }
    }
    return best;
}

// The azimuth the side aims at for `look`: `look` itself inside the zone, else the zone's nearer edge.
inline float clampInto(float look, const Side &side)
{
    const float delta = std::remainder(look - side.direction, kTwoPi);
    const float half = side.zone * 0.5f;
    return side.direction + std::fmax(-half, std::fmin(half, delta));
}

} // namespace storm::fire_side
