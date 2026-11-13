#pragma once
#include "android_gameplay.hpp"
#include <array>
#include <functional>

namespace storm::android
{
// Controls a touch HUD holds by name: the helm of the ship at sea and on the world map, the
// spyglass, the world-map sail lever and its camera button. The engine polls them like keys, so a
// hold is a virtual state merged into Controls::GetControlState. Game-thread owned. (Walking on
// deck is a vector, not keys: android_deck_walk.hpp.)
//
// Request "<gameplayRevision>\t<control>\t<0|1>[\t<strength 1..100>]". The strength is the analog
// part of the hold, what Controls hands out as CONTROL_STATE::fValue (strength / 100): the helm
// slider's deflection, so the ship turns as far as the slider is pulled (ship.cpp,
// wdm_player_ship.cpp read fValue). Without it the hold is a key pressed all the way (fValue 1),
// as a keyboard key is. A held control takes a new strength with another "1"; the activation
// phases do not restart. A hold is taken with "1" in the current
// gameplay revision and lasts until its "0" (pointer up or cancel in the UI), a cancelled
// input (cancelAndroidCombatInput: lifecycle, screen, UI loss) or the guard failing (the
// gameplay no longer binds the control or the player cannot act): events, never a clock. It
// used to need a repeat every 350 ms and to end with each gameplay revision, so a slow UI
// frame or an unrelated state change let go of a held helm or spyglass.
inline constexpr std::array<std::string_view, 8> HeldControlNames{"Ship_TurnLeft", "Ship_TurnRight", "TelescopeIn", "WMapShipSailUp",
                                                                 "WMapShipSailDown", "WMapShipTurnLeft", "WMapShipTurnRight", "WMapCameraSwitch"};
class HeldControls
{
    struct Held
    {
        bool wanted = false, previous = false;
        int strength = 100; // percent of a full press while wanted
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
        std::array<std::string_view, 4> fields{};
        size_t count = 0;
        for (std::string_view rest = request;; ++count)
        {
            if (count == fields.size()) return false; // a fifth field
            const auto tab = rest.find('\t');
            fields[count] = rest.substr(0, tab);
            if (tab == std::string_view::npos) { ++count; break; }
            rest.remove_prefix(tab + 1);
        }
        if (count < 3) return false;
        const auto number = [](std::string_view text, auto &out) {
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
            return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
        };
        uint64_t revision = 0;
        int strength = 100;
        const auto control = fields[1], value = fields[2];
        const int slot = index(control);
        if (!number(fields[0], revision) || slot < 0 || (value != "0" && value != "1")) return false;
        if (count == 4 && (value != "1" || !number(fields[3], strength) || strength < 1 || strength > 100)) return false;
        auto &held = held_[static_cast<size_t>(slot)];
        if (value == "0") { held.wanted = false; return true; } // a release always counts
        if (revision != revision_ || !revision_ || !guard || !guard(control)) return false;
        held.wanted = true;
        held.strength = strength;
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
    // The analog part of a held control, 0…1 (CONTROL_STATE::fValue); -1 while it is not held.
    float strength(std::string_view control) const
    {
        const int slot = index(control);
        if (slot < 0) return -1.0f;
        const auto &held = held_[static_cast<size_t>(slot)];
        return held.state == 1 || held.state == 3 ? static_cast<float>(held.strength) / 100.0f : -1.0f;
    }
    bool holding(std::string_view control) const
    {
        const int slot = index(control);
        return slot >= 0 && held_[static_cast<size_t>(slot)].wanted;
    }
};
inline HeldControls heldControls;
} // namespace storm::android
