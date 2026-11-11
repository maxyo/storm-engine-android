#pragma once
#include "android_gameplay.hpp"
#include <array>
#include <functional>

namespace storm::android
{
// Controls a touch HUD holds by name: the helm of the ship at sea and on the world map, the
// spyglass, the world-map sail lever and its camera button, walking on deck in first person. The engine polls them like keys, so a hold is a virtual state
// merged into Controls::GetControlState. Game-thread owned.
//
// Request "<gameplayRevision>\t<control>\t<0|1>". A hold is taken with "1" in the current
// gameplay revision and lasts until its "0" (pointer up or cancel in the UI), a cancelled
// input (cancelAndroidCombatInput: lifecycle, screen, UI loss) or the guard failing (the
// gameplay no longer binds the control or the player cannot act): events, never a clock. It
// used to need a repeat every 350 ms and to end with each gameplay revision, so a slow UI
// frame or an unrelated state change let go of a held helm or spyglass.
inline constexpr std::array<std::string_view, 10> HeldControlNames{"Ship_TurnLeft", "Ship_TurnRight", "TelescopeIn", "WMapShipSailUp",
                                                                  "WMapShipSailDown", "WMapShipTurnLeft", "WMapShipTurnRight", "WMapCameraSwitch",
                                                                  "DeckCamera_Forward", "DeckCamera_Backward"};
class HeldControls
{
    struct Held
    {
        bool wanted = false, previous = false;
        int state = 0; // 0 inactive, 1 active, 2 released, 3 activated: the CONTROL_STATE contract
    };
    std::array<Held, HeldControlNames.size()> held_{};
    uint64_t revision_ = 0, cancel_ = 0;
    static int index(std::string_view control)
    {
        for (size_t i = 0; i < HeldControlNames.size(); ++i)
            if (HeldControlNames[i] == control) return static_cast<int>(i);
        return -1;
    }
    void release()
    {
        for (auto &held : held_) held.wanted = false;
    }
public:
    // Installed by the engine: the current gameplay binds this control and the player can act.
    std::function<bool(std::string_view)> guard;
    // The revision a new hold must be made in; holds already taken carry on (the guard decides).
    void update(uint64_t gameplayRevision) { revision_ = gameplayRevision; }
    bool offer(std::string_view request, uint64_t cancelEpoch)
    {
        if (cancelEpoch != cancel_) { cancel_ = cancelEpoch; release(); }
        if (request.size() > 96) return false;
        const auto first = request.find('\t'), last = request.rfind('\t');
        if (first == std::string_view::npos || last == first) return false;
        uint64_t revision = 0;
        const auto parsed = std::from_chars(request.data(), request.data() + first, revision);
        const auto control = request.substr(first + 1, last - first - 1), value = request.substr(last + 1);
        const int slot = index(control);
        if (parsed.ec != std::errc() || parsed.ptr != request.data() + first || slot < 0 || (value != "0" && value != "1")) return false;
        auto &held = held_[static_cast<size_t>(slot)];
        if (value == "0") { held.wanted = false; return true; } // a release always counts
        if (revision != revision_ || !revision_ || !guard || !guard(control)) return false;
        held.wanted = true;
        return true;
    }
    // Called at Controls::Update, once per frame.
    void advance(uint64_t cancelEpoch)
    {
        if (cancelEpoch != cancel_) { cancel_ = cancelEpoch; release(); }
        for (size_t i = 0; i < held_.size(); ++i)
        {
            auto &held = held_[i];
            if (held.wanted && (!guard || !guard(HeldControlNames[i]))) held.wanted = false;
            held.state = held.wanted ? (held.previous ? 1 : 3) : (held.previous ? 2 : 0);
            held.previous = held.wanted;
        }
    }
    // A physical activation or hold keeps priority.
    int merge(std::string_view control, int physical) const
    {
        const int slot = index(control);
        if (slot < 0 || physical == 1 || physical == 3) return physical;
        const int state = held_[static_cast<size_t>(slot)].state;
        return state == 0 ? physical : state;
    }
    // What a control bound INVERSE to the same key as the held `control` sees: the hold is that
    // key held down. The original bindings end a hold this way (TelescopeOut is VK_CONTROL
    // inverted beside TelescopeIn on VK_CONTROL), so a released spyglass lowers the telescope.
    // -1 while the control is not held: the physical state stands.
    int inverse(std::string_view control) const
    {
        const int slot = index(control);
        if (slot < 0) return -1;
        switch (held_[static_cast<size_t>(slot)].state)
        {
        case 1: return 0; // held: the inverse is inactive
        case 3: return 2; // pressed: the inverse lets go
        case 2: return 3; // released: the inverse activates
        default: return -1;
        }
    }
    bool holding(std::string_view control) const
    {
        const int slot = index(control);
        return slot >= 0 && held_[static_cast<size_t>(slot)].wanted;
    }
};
inline HeldControls heldControls;
} // namespace storm::android
