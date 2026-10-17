#pragma once
#ifdef __ANDROID__
#include "platform/android_gameplay.hpp"
#include "android_ship_state.hpp"
#include "core.h"
#include "v_data.h"
#include "shared/sea_ai/sea_people.h"
#include "shared/messages.h"
#include "../../../libs/xinterface/src/string_service.h"

namespace storm::android
{
inline GameplaySnapshot readGameplaySnapshot(bool ready)
{
    GameplaySnapshot out;
    if (!ready) return out;
    // Resolve every frame: loading a save replaces script variables and entities.
    // Do not retain pointers or infer sailing from ownership of a ship.
    try
    {
        const auto integer = [](const char *name, int fallback = 0) {
            auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
            int32_t result = fallback;
            if (value) value->Get(result);
            return result;
        };
        auto *groupValue = static_cast<VDATA *>(core.GetScriptVariable("curKeyGroupName"));
        const char *group = nullptr;
        if (!groupValue || !groupValue->Get(group) || !group) return out;
        out.group = group;
        auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
        const int index = integer("nMainCharacterIndex", -1);
        if (!characters || index < 0 || static_cast<uint32_t>(index) >= characters->GetElementsNum()) return {};
        auto *character = characters->GetAClass(index);
        if (!character) return {};
        if (integer("dialogRun")) return {};
        auto *interfaceValue = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
        auto *interfaceAttrs = interfaceValue ? interfaceValue->GetAClass() : nullptr;
        if (interfaceAttrs && interfaceAttrs->GetAttributeAsDword("Launched", 0)) return {};
        auto *ai = character->FindAClass(character, "chr_ai.type");
        if (ai && ai->HasValue() && ai->GetValue() == "actor") return {};
        const bool sea = integer("bSeaActive") && !integer("bAbordageStarted");
        const auto commandActive = [](const char *name) {
            auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
            auto *attributes = value ? value->GetAClass() : nullptr;
            return attributes && attributes->GetAttributeAsDword("ComState", 0) != 0;
        };
        GameplayContext context;
        context.group = out.group;
        context.sea = sea;
        context.land = integer("bLandInterfaceStart");
        // BICommandList::SetActive writes ComState during entity Execute, after
        // scripts may have last updated curKeyGroupName. Gate taps immediately.
        context.blocked = commandActive("BattleInterface") || commandActive("objLandInterface") ||
            core.GetEntityId("WorldMap") != invalid_entity ||
            core.GetEntityId("CAviPlayer") != invalid_entity ||
            core.GetEntityId("WMVideoPlay") != invalid_entity;
        if (sea)
        {
            auto *pos = character->FindAClass(character, "Ship.POS");
            context.naval = pos && pos->GetAttributeAsDword("Mode", SHIP_SAIL) == SHIP_WAR;
        }
        else if (context.land && !context.blocked)
        {
            // Resolve the native Player, then verify it owns this exact main
            // character's attributes. Never query an arbitrary NPC or retain ids.
            const auto player = core.GetEntityId("Player");
            if (player != invalid_entity && core.Entity_GetAttributePointer(player) == character)
                context.nativeFight = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsFightMode") != 0;
        }
        out.mode = gameplayMode(context);
        if (out.mode == "unknown") return {};
        const char *location = character->GetAttribute("location");
        auto *mainShip = sea ? shipForCharacter(character) : nullptr;
        if (sea && !mainShip) return {};
        out.scene = std::to_string(index) + ":" + (location ? location : "") + ":" +
            std::to_string(sea ? mainShip->GetId() : core.GetEntityId("location"));
        if (auto *strings = static_cast<VSTRSERVICE *>(core.GetService("STRSERVICE")))
            if (const auto language = strings->GetLanguage()) out.language = std::string(language).substr(0, 32);
        auto *controls = static_cast<VDATA *>(core.GetScriptVariable("objControlsState"));
        auto *attributes = controls ? controls->GetAClass() : nullptr;
        const auto path = "keygroups." + out.group;
        auto *bindings = attributes ? attributes->FindAClass(attributes, path.c_str()) : nullptr;
        const auto add = [&](const char *name) {
            if (bindings && bindings->GetAttributeClass(name)) out.capabilities.emplace_back(name);
        };
        // Binding availability only; readiness, range, cargo and script guards
        // remain authoritative at action time.
        if (sea)
            for (auto name : {"Ship_TurnLeft", "Ship_TurnRight", "Ship_SailUp", "Ship_SailDown",
                 "Ship_Fire", "Sea_CameraSwitch", "hk_charge1", "hk_charge2", "hk_charge3", "hk_charge4",
                 "BICommandsActivate", "BIFastCommand", "TelescopeIn", "TelescopeOut"}) add(name);
        else
        {
            for (auto name : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight", "ChrFightMode",
                 "ChrRun", "LICommandsActivate", "BIFastCommand", "BOAL_UsePotion", "ChrAction"}) add(name);
            if (out.mode == "melee")
                for (auto name : {"ChrAltAttackBase", "ChrAttackBase", "ChrAttackBreakBase", "ChrAttackChoseBase", "ChrBlock", "ChrFire"}) add(name);
        }
        return out;
    }
    catch (...) { return {}; } // Missing/incompatible scripts fail closed.
}
} // namespace storm::android
#endif
