#pragma once
#include <cmath>
#include <cstdint>
#include <span>

namespace storm::android
{
// The touch talk target (Character::FindDialogCharacter on Android). The talk button shows
// this target and a tap talks to it, so it must not flicker while people walk about the edge
// of the cone: a target is taken within the enter reach and kept within the wider exit reach
// (hysteresis, enter < exit). The native PC reach was 3 m, a 30 degree cone and 2 m up/down.
struct DialogCandidate
{
    uint64_t id = 0;
    float dx = 0, dy = 0, dz = 0, d2 = 0; // from the hero to the candidate; d2 in the xz plane
    bool eligible = true;                 // alive, not fighting
};
struct DialogReach
{
    float radius, angle, rise; // metres, full cone degrees, metres up or down
};
inline constexpr DialogReach dialogEnter{3.5f, 40.0f, 2.0f}, dialogExit{4.5f, 70.0f, 2.5f};

// Distance weighted by the angle (the native ranking), or a negative value when out of reach.
inline float dialogRank(const DialogCandidate &c, float heroYaw, const DialogReach &reach)
{
    if (!c.eligible || std::fabs(c.dy) > reach.rise || c.d2 > reach.radius * reach.radius) return -1.0f;
    const float d = std::sqrt(c.d2);
    const float cs = c.d2 > 0.0f ? (c.dx * std::sin(heroYaw) + c.dz * std::cos(heroYaw)) / d : -1.0f;
    return cs < std::cos(0.5f * reach.angle * 3.14159265358979f / 180.0f) ? -1.0f : d / cs;
}
// The id to talk to (0 none): the kept target while it stays within the exit reach, else the
// best candidate within the enter reach.
inline uint64_t dialogTargetChoice(std::span<const DialogCandidate> candidates, float heroYaw, uint64_t kept)
{
    for (const auto &c : candidates)
        if (kept && c.id == kept && dialogRank(c, heroYaw, dialogExit) >= 0.0f) return kept;
    uint64_t best = 0;
    float bestRank = 0.0f;
    for (const auto &c : candidates)
        if (const float r = dialogRank(c, heroYaw, dialogEnter); r >= 0.0f && (!best || r < bestRank)) { best = c.id; bestRank = r; }
    return best;
}
} // namespace storm::android
