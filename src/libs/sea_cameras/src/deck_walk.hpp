#pragma once
#include <cmath>
#include <cstdint>

// One frame of the walker on deck in first person (deck_camera.cpp), before the deck's
// geometry has its say: how long he walks and which way.

// He goes at his own pace, in real time: the game's time acceleration speeds the ship up, not
// his legs (with the scaled frame time he ran at x2…x8). Frozen time (a scaled frame of 0) stops him.
inline uint32_t deckWalkTime(uint32_t scaledDelta, uint32_t realDelta)
{
    return scaledDelta ? realDelta : 0u;
}

// The way, in the ship's horizontal plane (x, z), for a walk `side` to the right and `ahead`
// along the view (-1…1, at most 1 long; a key is 1): ahead goes as the original walk did, slower
// while looking down or up (`pitch`), sideways at full pace. Its length times the frame's
// distance is the step.
struct DeckShift
{
    float x = 0.0f, z = 0.0f;
};
inline DeckShift deckWalkShift(float side, float ahead, float pitch, float yaw)
{
    const float cx = std::cos(pitch), sy = std::sin(yaw), cy = std::cos(yaw);
    return {ahead * cx * sy + side * cy, ahead * cx * cy - side * sy};
}
