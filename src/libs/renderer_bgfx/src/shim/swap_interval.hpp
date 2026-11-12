// The swap interval for a frame rate on a display with several refresh rates (frame_pacing.cpp; host-tested in
// tests/frame_pacing): a whole number of refreshes of one of the display's modes, closest to the target frame
// time; on a tie the mode the display is in (no mode switch), then the slower mode (less power).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace storm::shim
{

struct SwapChoice
{
    uint64_t period = 0;    // ns, of the chosen display mode; 0: no usable period
    uint64_t refreshes = 0; // of that mode per frame
    uint64_t interval() const
    {
        return period * refreshes;
    }
};

inline SwapChoice ChooseSwapInterval(std::span<const uint64_t> periods, uint64_t current, uint64_t target)
{
    constexpr double kTie = 1e5; // 0.1 ms
    SwapChoice best;
    double best_error = 0.0;
    for (const uint64_t period : periods)
    {
        if (period == 0)
            continue;
        const uint64_t refreshes = std::max<int64_t>(1, std::llround(static_cast<double>(target) / period));
        const double error = std::abs(static_cast<double>(refreshes * period) - static_cast<double>(target));
        const bool better = best.period == 0 || error < best_error - kTie ||
                            (error < best_error + kTie && best.period != current &&
                             (period == current || period > best.period));
        if (better)
        {
            best = {period, refreshes};
            best_error = error;
        }
    }
    return best;
}

// The slowest interval Swappy's auto mode may fall back to: `slowest` (ns) in whole refreshes of the chosen mode,
// never faster than the chosen interval.
inline uint64_t SlowestSwapInterval(const SwapChoice &choice, uint64_t slowest)
{
    if (choice.period == 0)
        return 0;
    const uint64_t refreshes = std::max<uint64_t>(choice.refreshes, std::llround(static_cast<double>(slowest) / choice.period));
    return choice.period * refreshes;
}

} // namespace storm::shim
