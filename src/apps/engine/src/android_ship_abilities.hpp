#pragma once
#ifdef __ANDROID__
// The ship's battle abilities a touch HUD offers as buttons: instant repair (carpenter) and
// immediate reload (cannoner). Everything is read from the scripts' own state:
//   holder   the main character when it has the perk, otherwise the officer of the perk's
//            OfficerType (pchar.Fellows.Passengers.<type>) who has it; none = no button;
//   active   holder.perks.list.<X>.active counts down from ChrPerksList TimeDuration;
//   recovery holder.perks.list.<X>.delay counts down from TimeDelay while not active;
//   ready    BattleInterface.Commands.<X>.enable, which the scripts' procSetUsingAbility
//            computes (the HUD asks for it a few times a second, never inside a command menu).
// A tap launches the ability exactly as the native command list does:
// Event("BI_LaunchCommand", "lsls", mainIndex, "BI_<X>", -1, "").
#include "platform/android_ship_telemetry.hpp"
#include "android_ship_state.hpp"
#include "core.h"
#include "v_data.h"

#include <array>
#include <cstdlib>

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

// Asks the scripts to recompute which abilities can be used now (procSetUsingAbility).
inline void refreshShipAbilities(uint32_t tick, bool commandMenuOpen)
{
    static uint32_t last = 0;
    if (commandMenuOpen || tick - last < 250)
        return;
    last = tick;
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
// The ship telemetry with the abilities; the scripts recompute what can be used first.
inline ShipTelemetryFrame readShipFrame(const GameplayState &gameplay, uint32_t tick, bool commandMenuOpen)
{
    auto frame = readShipTelemetry(gameplay);
    if (frame.valid)
    {
        refreshShipAbilities(tick, commandMenuOpen);
        frame.abilities = readShipAbilities();
    }
    return frame;
}
} // namespace storm::android
#endif
