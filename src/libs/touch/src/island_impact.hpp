#pragma once
// Hull damage power of a ship-island contact (SHIP_SHIP2ISLAND_COLLISION; the game script turns the power
// into hull hit points per event: Ship_Ship2IslandCollision, fHP = power * 3).
//
// TOUCH finds the contact every frame while the hull is against the island and used to send the point's speed
// as the power each time: the damage per second grew with the frame rate, and a ship pressed against a shore
// by the wind (speed shown as 0, a little forward motion each frame, pushed out again) lost its hull within a
// minute. Now:
// - a new contact (none for kEpisodeGap) is an impact: the point's speed, exactly as before, so a ram costs
//   what it always did;
// - while the contact goes on, only the speed into the island above kRestingSpeed counts, as that power per
//   kSustainedPeriod of contact: frame-rate independent (a ship driving on into the rock at 5 knots loses
//   12 HP per second), and nothing for a hull resting or scraping along it.

#include <algorithm>

namespace storm::touch
{

inline constexpr float kEpisodeGap = 0.5f;      // seconds without contact that end a contact episode
inline constexpr float kRestingSpeed = 1.0f;    // knots into the island that a resting / scraping hull may have
inline constexpr float kSustainedPeriod = 1.0f; // seconds of sustained contact that one power unit stands for

struct IslandContact
{
    float sinceContact = 1.0e9f; // seconds since the last contact of this ship
};

// Call every frame for every ship (dt in seconds).
inline void AdvanceIslandContact(IslandContact &contact, float dt)
{
    contact.sinceContact += dt;
}

// The power to report for a contact this frame (0: report none). pointSpeed: knots of the touching point,
// intoIsland: its knots towards the island.
inline float IslandImpactPower(IslandContact &contact, float pointSpeed, float intoIsland, float dt)
{
    const bool impact = contact.sinceContact > kEpisodeGap;
    contact.sinceContact = 0.0f;
    if (impact)
        return pointSpeed;
    return std::max(0.0f, intoIsland - kRestingSpeed) * (dt / kSustainedPeriod);
}

} // namespace storm::touch
