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
    // Every way out without a mode says why (GameplaySnapshot::blocked).
    const auto blocked = [](std::string_view why) {
        GameplaySnapshot none;
        none.blocked = std::string(why);
        return none;
    };
    if (!ready) return blocked("starting");
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
        if (!groupValue || !groupValue->Get(group) || !group) return blocked("no-scene");
        out.group = group;
        auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
        const int index = integer("nMainCharacterIndex", -1);
        if (!characters || index < 0 || static_cast<uint32_t>(index) >= characters->GetElementsNum()) return blocked("no-scene");
        auto *character = characters->GetAClass(index);
        if (!character) return blocked("no-scene");
        if (integer("dialogRun")) return blocked("dialogue");
        auto *interfaceValue = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates"));
        auto *interfaceAttrs = interfaceValue ? interfaceValue->GetAClass() : nullptr;
        if (interfaceAttrs && interfaceAttrs->GetAttributeAsDword("Launched", 0)) return blocked("interface");
        auto *ai = character->FindAClass(character, "chr_ai.type");
        if (ai && ai->HasValue() && ai->GetValue() == "actor") return blocked("cutscene");
        const auto worldmap = core.GetEntityId("WorldMap");
        const bool sea = worldmap == invalid_entity && integer("bSeaActive") && !integer("bAbordageStarted");
        const auto commandActive = [](const char *name) {
            auto *value = static_cast<VDATA *>(core.GetScriptVariable(name));
            auto *attributes = value ? value->GetAClass() : nullptr;
            return attributes && attributes->GetAttributeAsDword("ComState", 0) != 0;
        };
        GameplayContext context;
        context.group = out.group;
        context.worldmap = worldmap != invalid_entity;
        context.sea = sea;
        context.land = integer("bLandInterfaceStart");
        context.location = core.GetEntityId("location") != invalid_entity;
        // BICommandList::SetActive writes ComState during entity Execute, after
        // scripts may have last updated curKeyGroupName. Gate taps immediately.
        context.video = core.GetEntityId("CAviPlayer") != invalid_entity || core.GetEntityId("WMVideoPlay") != invalid_entity;
        context.blocked = commandActive("BattleInterface") || commandActive("objLandInterface") || context.video;
        auto player = invalid_entity;
        if (sea)
        {
            auto *pos = character->FindAClass(character, "Ship.POS");
            context.naval = pos && pos->GetAttributeAsDword("Mode", SHIP_SAIL) == SHIP_WAR;
        }
        else if (context.land && !context.blocked)
        {
            // Resolve the native Player, then verify it owns this exact main
            // character's attributes. Never query an arbitrary NPC or retain ids.
            player = core.GetEntityId("Player");
            if (player != invalid_entity && core.Entity_GetAttributePointer(player) == character)
                context.nativeFight = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsFightMode") != 0;
        }
        out.mode = gameplayMode(context);
        if (out.mode == "unknown") return blocked(gameplayBlocked(context));
        const char *location = character->GetAttribute("location");
        auto *mainShip = sea ? shipForCharacter(character) : nullptr;
        if (sea && !mainShip) return blocked("no-scene");
        out.scene = std::to_string(index) + ":" + (location ? location : "") + ":" +
            std::to_string(context.worldmap ? worldmap : sea ? mainShip->GetId() : core.GetEntityId("location"));
        if (!sea && !context.worldmap) out.inputOwner = out.scene + ":" + std::to_string(player);
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
        if (context.worldmap)
            for (auto name : {"WMapShipSailUp", "WMapShipSailDown", "WMapShipTurnLeft", "WMapShipTurnRight",
                 "WMapCancel", "WMapCameraSwitch", "WMapCameraRotate"}) add(name);
        else if (sea)
        {
            for (auto name : {"Ship_TurnLeft", "Ship_TurnRight", "Ship_SailUp", "Ship_SailDown",
                 "Ship_Fire", "Sea_CameraSwitch", "hk_charge1", "hk_charge2", "hk_charge3", "hk_charge4",
                 "BICommandsActivate", "BIFastCommand",
                 "DeckCamera_Forward", "DeckCamera_Backward"}) add(name); // walking: first person (Sailing1Pers) only
            // The scripts raise the telescope only with a spyglass equipped (Telescope_Request reads
            // equip.spyglass in every supported script set): without one the spyglass is not offered,
            // instead of a button the scripts silently ignore.
            if (auto *spyglass = character->FindAClass(character, "equip.spyglass"); spyglass && spyglass->HasValue() && !spyglass->GetValue().empty())
                for (auto name : {"TelescopeIn", "TelescopeOut"}) add(name);
        }
        else
        {
            for (auto name : {"ChrForward", "ChrBackward", "ChrStrafeLeft", "ChrStrafeRight", "ChrFightMode",
                 "ChrRun", "LICommandsActivate", "BIFastCommand", "BOAL_UsePotion", "ChrAction"}) add(name);
            if (out.mode == "melee")
                for (auto name : {"ChrAltAttackBase", "ChrAttackBase", "ChrAttackBreakBase", "ChrAttackChoseBase", "ChrBlock", "ChrFire"}) add(name);
        }
        // The time scale controls are global (no key group): offered when the scripts created them.
        auto *created = attributes ? attributes->FindAClass(attributes, "map.controls") : nullptr;
        // A touch HUD offers four steps of them (android_time_scale.hpp).
        if (sea && !context.worldmap && created && created->GetAttributeClass("TimeScaleFaster") && created->GetAttributeClass("TimeScaleSlower"))
            for (auto name : {"TimeScaleX1", "TimeScaleX2", "TimeScaleX4", "TimeScaleX8"}) out.capabilities.emplace_back(name);
        // The battle abilities the scripts' command list knows (launched like its own buttons).
        auto *battleData = static_cast<VDATA *>(core.GetScriptVariable("BattleInterface"));
        auto *battle = sea && !context.worldmap && battleData ? battleData->GetAClass() : nullptr;
        if (auto *commands = battle ? battle->FindAClass(battle, "Commands") : nullptr)
            for (auto name : {"InstantRepair", "ImmediateReload"})
                if (commands->GetAttributeClass(name)) out.capabilities.emplace_back(std::string("BI_") + name);
        return out;
    }
    catch (...) { return blocked("error"); } // Missing/incompatible scripts fail closed.
}
} // namespace storm::android
#endif
