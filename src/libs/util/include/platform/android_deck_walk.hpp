#pragma once
#include "android_combat_input.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string_view>

namespace storm::android
{
// Walking on deck in first person (DECK_CAMERA), from the touch HUD's walk stick: one vector,
// forward and sideways, as far as the stick is pushed. The game has no analog deck walk and no
// deck strafe at all: it binds DeckCamera_Forward / DeckCamera_Backward to keys, and its
// DeckCamera_Left / DeckCamera_Right turn the view (the scripts leave them unbound; the strafe of
// the original deck camera is commented out). So the stick is not fed as keys: the deck camera
// reads this vector beside its keys. Game-thread owned.
//
// Request "<gameplayRevision>\t<x>\t<y>": x to the right, y forward, -100…100 each and at most 100
// long together (the stick's travel after the HUD's dead zone and response curve). A walk is taken
// in the current gameplay revision and lasts, through any number of frames, until "0\t0" (pointer
// up or cancel in the UI, accepted in any revision), a cancelled input (cancelAndroidCombatInput)
// or the guard failing (no deck walk bound, the player cannot act): events, never a clock.
struct DeckStep
{
    float x = 0.0f, y = 0.0f; // -1…1, length <= 1
};
class DeckWalk
{
    uint64_t revision_ = 0, cancel_ = 0;
    int x_ = 0, y_ = 0;
    void synchronize()
    {
        const auto epoch = combatCancelEpoch.load(std::memory_order_acquire);
        if (epoch != cancel_) { cancel_ = epoch; x_ = y_ = 0; }
    }
    static bool number(std::string_view text, int &out)
    {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
        return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
    }
public:
    // Installed by the engine: the gameplay binds the deck walk and the player can act.
    std::function<bool()> guard;
    void update(uint64_t gameplayRevision) { revision_ = gameplayRevision; }
    bool offer(std::string_view request, uint64_t enqueueEpoch)
    {
        synchronize();
        if (enqueueEpoch != cancel_ || request.size() > 48) return false; // sent before a cancellation
        const auto first = request.find('\t'), last = request.rfind('\t');
        if (first == std::string_view::npos || last == first) return false;
        uint64_t revision = 0;
        const auto parsed = std::from_chars(request.data(), request.data() + first, revision);
        int x = 0, y = 0;
        if (parsed.ec != std::errc() || parsed.ptr != request.data() + first || !number(request.substr(first + 1, last - first - 1), x) ||
            !number(request.substr(last + 1), y) || std::abs(x) > 100 || std::abs(y) > 100 || x * x + y * y > 101 * 101) // 100 long, give or take the rounding of each part
            return false;
        if (!x && !y) { x_ = y_ = 0; return true; } // the end of a walk always counts
        if (revision != revision_ || !revision_ || !guard || !guard()) return false;
        x_ = x; y_ = y;
        return true;
    }
    // Read by the deck camera each frame: the walk, or nothing.
    std::optional<DeckStep> read()
    {
        synchronize();
        if (!x_ && !y_) return std::nullopt;
        if (!guard || !guard()) { x_ = y_ = 0; return std::nullopt; }
        const float length = std::max(100.0f, std::hypot(static_cast<float>(x_), static_cast<float>(y_)));
        return DeckStep{static_cast<float>(x_) / length, static_cast<float>(y_) / length};
    }
};
inline DeckWalk deckWalk;
} // namespace storm::android
