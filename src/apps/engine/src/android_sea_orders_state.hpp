#pragma once
#ifdef __ANDROID__
#include "android_ship_state.hpp"
#include "platform/android_sea_orders.hpp"

namespace storm::android
{
inline bool seaOrdersReady(bool ready)
{
    if (!ready) return false;
    const auto integer = [](const char *name) {
        auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
        int32_t result = 0;
        if (value) value->Get(result);
        return result;
    };
    if (!integer("bSeaActive") || integer("bAbordageStarted") || integer("dialogRun")) return false;
    auto *interfaces = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
    auto *attributes = interfaces ? interfaces->GetAClass() : nullptr;
    if (attributes && attributes->GetAttributeAsDword("Launched", 0)) return false;
    for (const auto *entity : {"WorldMap", "CAviPlayer", "WMVideoPlay", "fader"})
        if (core.GetEntityId(entity) != invalid_entity) return false;
    auto *character = shipMainCharacter();
    auto *ship = shipForCharacter(character);
    if (!character || !ship || ship->isDead()) return false;
    auto *type = character->FindAClass(character, "chr_ai.type");
    if (type && type->HasValue() && type->GetValue() == "actor") return false;
    auto *group = static_cast<VDATA *>(core.GetScriptVariable("curKeyGroupName"));
    const char *name = nullptr;
    if (!group || !group->Get(name) || !name) return false;
    const std::string_view current(name);
    // This is a separate order capability; normal gameplay remains blocked by
    // ComState and never receives movement/fire while the menu is active.
    return current == "Sailing1Pers" || current == "Sailing3Pers" || current == "BattleInterfaceControls";
}
}
#endif
