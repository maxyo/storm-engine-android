// A location light's flame (lanterns, torches, fireplaces): a fast flicker that jumps to a random value every
// `period` seconds plus a slow one that moves linearly to a new random value every `periodSlow` seconds. The flicker is
// cosmetic: it is stepped by real time (storm/cosmetic_time.hpp), so the time scale changes neither its speed nor its
// amplitude.
#pragma once

#include <cmath>
#include <cstdlib>

namespace storm::location
{

struct FlameFlicker
{
    float time;      // Time since the last change of the fast intensity
    float timeSlow;  // Time since the last change of the slow intensity
    float itens;     // Fast intensity
    float itensSlow; // Slow intensity being approached
    float itensDlt;  // Slow intensity difference being interpolated
    float i;         // Resulting intensity offset
};

struct FlameFlickerType
{
    float flicker;     // Fast amplitude (0: none)
    float period;      // 1 / fast frequency
    float flickerSlow; // Slow amplitude (0: none)
    float freqSlow;
    float periodSlow;  // 1 / freqSlow
};

// Advances the flame by `seconds` and returns its intensity factor (1 + offset, never negative). A step longer than a
// period (a frame hitch) starts one new value, not a run of them on the following frames.
inline float StepFlameFlicker(FlameFlicker &s, const FlameFlickerType &t, float seconds)
{
    s.time += seconds;
    if (t.flicker > 0.0f && s.time > t.period) // (no fast flicker: no period either)
    {
        s.time = std::fmod(s.time, t.period);
        s.itens = (1.0f - rand() * 2.0f / RAND_MAX) * t.flicker;
    }
    s.timeSlow += seconds;
    float k = s.timeSlow * t.freqSlow;
    if (k < 0.0f)
        k = 0.0f;
    if (k >= 1.0f) // (freqSlow > 0: periodSlow is set)
    {
        k = 0.0f;
        s.timeSlow = std::fmod(s.timeSlow, t.periodSlow);
        s.itensDlt = s.itensSlow;
        s.itensSlow = (1.0f - rand() * 2.0f / RAND_MAX) * t.flickerSlow;
        s.itensDlt = s.itensSlow - s.itensDlt;
    }
    s.i = s.itensSlow - s.itensDlt + k * s.itensDlt + s.itens;
    k = 1.0f + s.i;
    return k < 0.0f ? 0.0f : k;
}

} // namespace storm::location
