#pragma once
#ifdef __ANDROID__
// The ship's battle abilities a touch HUD offers as buttons: instant repair (carpenter) and
// immediate reload (cannoner). Everything is read from the scripts' own state:
//   holder   the main character when it has the perk, otherwise the officer of the perk's
//            OfficerType (pchar.Fellows.Passengers.<type>) who has it; none = no button;
//   active   holder.perks.list.<X>.active counts down from ChrPerksList TimeDuration;
//   recovery holder.perks.list.<X>.delay counts down from TimeDelay while not active;
//   ready    BattleInterface.Commands.<X>.enable, which the scripts' procSetUsingAbility
//            computes (asked when its inputs change, never inside a command menu: see
//            refreshShipAbilities).
// A tap launches the ability exactly as the native command list does:
// Event("BI_LaunchCommand", "lsls", mainIndex, "BI_<X>", -1, "").
#include "platform/android_ship_telemetry.hpp"
#include "android_ship_state.hpp"
#include "core.h"
#include "v_data.h"

#include <array>
#include <cmath>
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace storm::android
{
inline constexpr std::array<std::string_view, 2> ShipAbilityNames{"InstantRepair", "ImmediateReload"};

namespace abilities
{
inline ATTRIBUTES *scriptObject(const char *name)
{
    auto *data = static_cast<VDATA *>(core.GetScriptVariable(name));
    return data ? data->GetAClass() : nullptr;
}
inline std::optional<double> number(ATTRIBUTES *attributes, const std::string &path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path.c_str()) : nullptr;
    return value && value->HasValue() ? shipNumberValue(value->GetValue()) : std::nullopt;
}
inline bool has(ATTRIBUTES *attributes, const std::string &path)
{
    return attributes && attributes->FindAClass(attributes, path.c_str());
}
// The character who holds the perk for the main character, or nullptr.
inline ATTRIBUTES *holder(ATTRIBUTES *main, ATTRIBUTES *perks, std::string_view name)
{
    const std::string perk = "perks.list." + std::string(name);
    if (has(main, perk))
        return main;
    auto *type = perks ? perks->FindAClass(perks, ("list." + std::string(name) + ".OfficerType").c_str()) : nullptr;
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!type || !type->HasValue() || !characters)
        return nullptr;
    const auto index = number(main, "Fellows.Passengers." + std::string(type->GetValue()));
    if (!index || *index < 0 || *index >= characters->GetElementsNum() || std::floor(*index) != *index)
        return nullptr;
    auto *officer = characters->GetAClass(static_cast<int>(*index));
    return has(officer, perk) ? officer : nullptr;
}
} // namespace abilities

// The abilities as the HUD shows them; empty outside a sea scene or without the scripts' data.
inline std::vector<ShipAbility> readShipAbilities()
{
    std::vector<ShipAbility> out;
    try
    {
        auto *main = shipMainCharacter();
        auto *perks = abilities::scriptObject("ChrPerksList");
        auto *battle = abilities::scriptObject("BattleInterface");
        if (!main || !perks || !battle)
            return out;
        for (const auto name : ShipAbilityNames)
        {
            auto *owner = abilities::holder(main, perks, name);
            if (!owner)
                continue;
            const std::string perk = "perks.list." + std::string(name), base = "list." + std::string(name);
            ShipAbility ability;
            ability.id = std::string(name);
            const auto active = abilities::number(owner, perk + ".active"), delay = abilities::number(owner, perk + ".delay");
            const auto duration = abilities::number(perks, base + ".TimeDuration"), cooldown = abilities::number(perks, base + ".TimeDelay");
            if (active && *active > 0 && duration && *duration > 0)
            {
                ability.state = ShipAbility::Active;
                ability.fill = std::clamp(*active / *duration, 0.0, 1.0); // what is left of the effect
            }
            else if (delay && *delay > 0 && cooldown && *cooldown > 0)
            {
                ability.state = ShipAbility::Recovery;
                ability.fill = std::clamp(1.0 - *delay / *cooldown, 0.0, 1.0); // how far it has recovered
            }
            else
                ability.state = abilities::number(battle, "Commands." + std::string(name) + ".enable").value_or(0) != 0 ? ShipAbility::Ready
                                                                                                                    : ShipAbility::Blocked;
            ability.fill = std::round(ability.fill * 100.0) / 100.0;
            out.push_back(std::move(ability));
        }
    }
    catch (...)
    {
        out.clear(); // missing or incompatible scripts: no buttons
    }
    return out;
}

// What procSetUsingAbility (BattleInterface.c) decides Commands.<X>.enable from, read natively:
// each ability's holder and whether its perk is there, running (.active) or recovering (.delay);
// the officers (Fellows.Passengers); the main ship's hull and sails in whole percent of their
// maximum (CheckInstantRepairCondition compares them with InstantRepairRATE) and its planks and
// sailcloth (Ship.Cargo.Goods.Planks / .Sailcloth, the GOOD_PLANKS / GOOD_SAILCLOTH names).
//
// The original asks procSetUsingAbility only when its command menu opens (BI_SetPossibleCommands)
// and shows readiness only there. The touch HUD shows the ability buttons all the time, and these
// inputs have no script-side signal: a hit lowers the hull, a perk's delay runs out, the hold is
// emptied, all without any event or message to the battle interface. So this is the one state
// the HUD re-asks for on a change of its inputs instead of on an update message; nothing is
// asked while they stay the same.
inline std::string shipAbilityInputs(const ShipTelemetryFrame &frame)
{
    std::string out;
    auto *main = shipMainCharacter();
    auto *perks = abilities::scriptObject("ChrPerksList");
    if (!main || !perks)
        return out;
    const auto value = [&](ATTRIBUTES *root, const char *path) {
        auto *a = root ? root->FindAClass(root, path) : nullptr;
        out += a ? (a->HasValue() ? a->GetValue() : "+") : "-";
        out += '|';
    };
    for (const auto name : ShipAbilityNames)
    {
        auto *owner = abilities::holder(main, perks, name);
        const std::string perk = "perks.list." + std::string(name);
        out += owner ? std::to_string(reinterpret_cast<uintptr_t>(owner)) : "none";
        out += owner && abilities::has(owner, perk) ? "P" : "";
        out += owner && abilities::has(owner, perk + ".active") ? "A" : "";
        out += owner && abilities::has(owner, perk + ".delay") ? "D" : "";
        out += '|';
    }
    if (auto *passengers = main->FindAClass(main, "Fellows.Passengers"))
        for (uint32_t i = 0; i < passengers->GetAttributesNum(); ++i)
            if (auto *officer = passengers->GetAttributeClass(i); officer && officer->HasValue())
                (out += officer->GetValue()) += ',';
    const auto percent = [&](const std::optional<ShipMetric> &metric) {
        out += metric && metric->maximum > 0 ? std::to_string(static_cast<int>(std::floor(100.0 * metric->current / metric->maximum)))
                                             : "-";
        out += '|';
    };
    percent(frame.ship.hull);
    percent(frame.ship.sails);
    value(main, "Ship.Cargo.Goods.Planks");
    value(main, "Ship.Cargo.Goods.Sailcloth");
    return out;
}

// Asks the scripts to recompute which abilities can be used now (procSetUsingAbility) when its
// inputs changed (shipAbilityInputs). Inside the command menu the menu computes it itself; after
// the menu the HUD asks again.
inline void refreshShipAbilities(const ShipTelemetryFrame &frame, bool commandMenuOpen)
{
    static std::string last;
    if (commandMenuOpen)
    {
        last.clear();
        return;
    }
    auto inputs = shipAbilityInputs(frame);
    if (inputs.empty() || inputs == last)
        return;
    last = std::move(inputs);
    // Debug level: one line per ask, with the inputs that made it (the documented exception above).
    spdlog::debug("ship abilities: inputs changed, asking procSetUsingAbility: {}", last);
    auto *main = shipMainCharacter();
    const auto index = main ? abilities::number(main, "index") : std::nullopt;
    if (index && *index >= 0)
        core.Event("evntSetUsingAbility", "l", static_cast<int32_t>(*index));
}

// A tap: the ability is launched only while the scripts call it ready.
inline bool launchShipAbility(std::string_view control)
{
    for (const auto name : ShipAbilityNames)
    {
        if (control != "BI_" + std::string(name))
            continue;
        for (const auto &ability : readShipAbilities())
            if (ability.id == name && ability.state == ShipAbility::Ready)
            {
                const auto index = abilities::number(shipMainCharacter(), "index");
                if (!index)
                    return false;
                core.Event("BI_LaunchCommand", "lsls", static_cast<int32_t>(*index), std::string(control).c_str(), -1, "");
                return true;
            }
        return false;
    }
    return false;
}
// The ship telemetry with the abilities; the scripts recompute what can be used first when its inputs changed.
inline ShipTelemetryFrame readShipFrame(const GameplayState &gameplay, bool commandMenuOpen)
{
    auto frame = readShipTelemetry(gameplay);
    if (frame.valid)
    {
        refreshShipAbilities(frame, commandMenuOpen);
        frame.abilities = readShipAbilities();
    }
    return frame;
}
} // namespace storm::android
#endif
