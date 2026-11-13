// Fixed simulation steps for fx::SeaWaves (sea_waves.hpp), without the GPU: how many steps a frame of game time owes.
#pragma once

#include <algorithm>

namespace storm::fx
{

struct WaveClock
{
    static constexpr float kStep = 1.0f / 60.0f; // seconds of one step
    static constexpr int kMaxSteps = 3;          // per frame
    float pending = 0.0f;                        // game time not yet stepped

    // Steps due after `dt` seconds (0..kMaxSteps). A frame slower than kMaxSteps steps drops the rest: the water slows
    // down instead of falling behind.
    int Due(float dt)
    {
        pending += std::clamp(dt, 0.0f, 0.25f);
        const int steps = std::min(static_cast<int>(pending / kStep), kMaxSteps);
        pending = std::min(pending - static_cast<float>(steps) * kStep, kStep);
        return steps;
    }
};

} // namespace storm::fx
