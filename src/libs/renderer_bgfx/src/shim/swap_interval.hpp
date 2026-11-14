// The swap interval for a frame rate on a display with several refresh rates (frame_pacing.cpp; host-tested in
// tests/frame_pacing): a whole number of refreshes of one of the display's modes, never faster than the target frame
// time (the setting is a cap) and closest to it; on a tie the mode the display is in (no mode switch), then the slower
// mode (less power).
//
// The display may not enter the mode chosen (a 40 Hz mode a phone lists but keeps at 60 Hz): frame_pacing.cpp then
// chooses again with the period the display is in as the only mode (DisplayInMode).
//
// Swappy's auto swap interval paces an interval only while it is at most the threshold given with
// Swappy*_setMaxAutoSwapIntervalNS; above it Swappy stops waiting for the vsync slot and lets frames run as fast as
// they finish (SwappyCommon::waitForNextFrame). Its ladder may step from any paced interval to a slower one
// (SwapIntervalLadder.h swapSlower), so one step past the slowest paced interval is reachable: that state is
// unpaced, and the log says so (SwapPaced).
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

// Refreshes of `period` for a frame of `target`: the fewest that are not faster than it, within 5 % of a refresh
// (a 59.94 Hz panel counts as 60 Hz).
inline uint64_t RefreshesFor(uint64_t period, uint64_t target)
{
    constexpr double kSlack = 0.05;
    return static_cast<uint64_t>(std::max(1.0, std::ceil(static_cast<double>(target) / period - kSlack)));
}

inline SwapChoice ChooseSwapInterval(std::span<const uint64_t> periods, uint64_t current, uint64_t target)
{
    constexpr double kTie = 1e5; // 0.1 ms
    SwapChoice best;
    double best_error = 0.0;
    for (const uint64_t period : periods)
    {
        if (period == 0)
            continue;
        const uint64_t refreshes = RefreshesFor(period, target);
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

// The threshold for Swappy*_setMaxAutoSwapIntervalNS: half a refresh above the slowest interval, so that interval is
// still paced although the display's measured period wobbles by microseconds, and the next slower one is not.
inline uint64_t SwapThreshold(const SwapChoice &choice, uint64_t slowest_interval)
{
    return choice.period == 0 ? 0 : slowest_interval + choice.period / 2;
}

// Swappy paces `interval` under `threshold` (SwappyCommon::waitForNextFrame: interval <= threshold).
inline bool SwapPaced(uint64_t interval, uint64_t threshold)
{
    return interval <= threshold;
}

// The display runs the chosen mode: its refresh period is the mode's within 2 %.
inline bool DisplayInMode(uint64_t chosen_period, uint64_t display_period)
{
    if (chosen_period == 0 || display_period == 0)
        return false;
    const double ratio = static_cast<double>(display_period) / static_cast<double>(chosen_period);
    return ratio > 0.98 && ratio < 1.02;
}

} // namespace storm::shim
