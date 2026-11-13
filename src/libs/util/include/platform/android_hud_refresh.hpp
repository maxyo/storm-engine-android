#pragma once
#include <cstdint>
#include <optional>

namespace storm::android
{
// When a touch-HUD adapter asks the scripts again for state only they compute.
//
// Such a question is a script event (a whole interpreter call per ask), and the answer depends
// on things that change without any event to observe (the player walking towards an NPC or a
// body), so it cannot be fully event-driven. What does have a cheap native signal (readiness,
// the owning entities, the native menu opening, the reload locator the player stands in) is
// passed as Signal: a change refreshes in the same loop iteration. Everything else is re-read
// once per period. Executing a command refreshes on its own right before acting.
template <typename Signal> class HudRefreshPolicy
{
    uint32_t period_;
    std::optional<uint32_t> last_;
    Signal signal_{};
    bool forced_ = false;

public:
    explicit HudRefreshPolicy(uint32_t periodMs) : period_(periodMs) {}
    uint32_t period() const { return period_; }
    // True when the adapter must ask now; the ask is then counted as done at this tick.
    bool due(uint32_t tick, const Signal &signal)
    {
        if (last_ && !forced_ && signal == signal_ && tick - *last_ < period_)
            return false;
        last_ = tick;
        signal_ = signal;
        forced_ = false;
        return true;
    }
    // The next due() asks, whatever the signal and the period say.
    void force() { forced_ = true; }
};
} // namespace storm::android
