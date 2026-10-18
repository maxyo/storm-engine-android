#pragma once
#ifdef __ANDROID__
#include "android_gameplay_state.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_combat_input.hpp"
#include "platform/android_movement.hpp"
#include "platform/android_pistol_state.hpp"

namespace storm::android
{
inline bool combatContextCurrent(const GameplayState &gameplay, bool ready)
{
    if (!ready || (gameplay.snapshot().mode != "explore" && gameplay.snapshot().mode != "melee")) return false;
    const auto current = readGameplaySnapshot(true);
    return current.mode == gameplay.snapshot().mode && current.scene == gameplay.snapshot().scene &&
        current.group == gameplay.snapshot().group && !current.scene.empty();
}
inline bool dialogTargetAvailable()
{
    const auto player = core.GetEntityId("Player");
    auto *character = shipMainCharacter();
    if (player == invalid_entity || !character || core.Entity_GetAttributePointer(player) != character ||
        core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsDead")) return false;
    // Query the same range, facing, life and fight checks used by Player::Update.
    // The script's dialogready attribute is only a one-second hint and can be stale.
    return validDialogTarget(core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "FindDialogCharacter"));
}
inline void updateCombatState(const GameplayState &gameplay)
{
    std::vector<CombatBinding> bindings;
    std::string player, mode = "unknown";
    if (combatContextCurrent(gameplay, core.Controls != nullptr))
    {
        mode = gameplay.snapshot().mode;
        player = std::to_string(core.GetEntityId("Player"));
        std::vector<std::string> allowed;
        for (int32_t i=0; i<core.Controls->GetControlsNum(); ++i)
        {
            USER_CONTROL descriptor{};
            if (!core.Controls->GetControlDesc(i, descriptor) || !descriptor.name ||
                descriptor.bLocked || descriptor.bSystemLocked) continue;
            if (descriptor.control_type != UCT_ControlTree && descriptor.system_code == UNASSIGNED_CONTROL) continue;
            allowed.emplace_back(descriptor.name);
        }
        const auto add = [&](const char *action, bool held, std::initializer_list<const char *> aliases) {
            for (const auto *alias : aliases)
                if (std::find(allowed.begin(), allowed.end(), alias) != allowed.end())
                {
                    // Require a real configured native binding or script-created
                    // output node, not a name invented by the Android adapter.
                    bindings.push_back({action, alias, held}); return;
                }
        };
        if (gameplay.allows("ChrFightMode")) add("weapon",false,{"ChrFightMode"});
        if (gameplay.allows("BOAL_UsePotion")) add("potion",false,{"BOAL_UsePotion"});
        if (mode == "explore" && gameplay.allows("ChrAction") && dialogTargetAvailable())
            add("interact",false,{"ChrAction"});
        add("freelook",true,{"ChrCamSpecMode"});
        if (mode == "melee")
        {
            add("force",false,{"ChrAttackForce2","ChrAttackForce"});
            add("fast",false,{"ChrAttackFast2","ChrAttackFast"});
            add("break",false,{"ChrAttackBreak","ChrAttackBreak2"});
            add("parry",false,{"ChrParry","ChrParry2"});
            add("feint",false,{"ChrAttackFient","ChrAttackFient2"});
            add("round",false,{"ChrAttackRound","ChrAttackRound2"});
            add("pistol",false,{"ChrFire"});
            add("block",true,{"ChrBlock","ChrBlock2"});
        }
    }
    combatInput.update(gameplay.revision(), std::move(player), std::move(mode), std::move(bindings));
}
inline jweak combatActivity = nullptr;
inline jmethodID combatMethod = nullptr;
inline bool combatMethodKnown = false;
inline std::string lastCombatState;
inline PistolState readPistolState()
{
    if (combatInput.player().empty()) return {};
    const auto player = core.GetEntityId("Player");
    auto *character = shipMainCharacter();
    if (player == invalid_entity || !character || core.Entity_GetAttributePointer(player) != character ||
        combatInput.player() != std::to_string(player)) return {};
    const bool equipped = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsSetGun") != 0;
    return pistolState(equipped, shipText(character, "chr_ai.tmpl") == "player",
                       shipNumber(character, "chr_ai.charge"), shipNumber(character, "chr_ai.chargeprc"));
}
inline void publishCombatState()
{
    auto *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    auto activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) return;
    if (!combatActivity || !env->IsSameObject(combatActivity, activity))
    {
        if (combatActivity) env->DeleteWeakGlobalRef(combatActivity);
        combatActivity = env->NewWeakGlobalRef(activity);
        combatMethod = nullptr; combatMethodKnown = false; lastCombatState.clear();
        combatInput.resetActivity();
        movementInput.resetActivity();
    }
    if (!combatMethodKnown && !env->ExceptionCheck())
    {
        auto type=env->GetObjectClass(activity);
        if(type)
        {
            combatMethod=env->GetMethodID(type,"showCombatState","([B)V");
            combatMethodKnown=true;env->DeleteLocalRef(type);
        }
        if(env->ExceptionCheck())env->ExceptionClear();
    }
    auto json=combatInput.json();
    const auto pistol = readPistolState();
    json.pop_back();
    json += ",\"interactKind\":" + gameplayJsonString("dialog");
    json += ",\"pistol\":{\"state\":" + gameplayJsonString(pistol.state) +
            ",\"progress\":" + std::to_string(pistol.progress) + "}}";
    if(combatMethod && json!=lastCombatState && !env->ExceptionCheck())
    {
        auto bytes=env->NewByteArray(static_cast<jsize>(json.size()));
        if(bytes && !env->ExceptionCheck())env->SetByteArrayRegion(bytes,0,static_cast<jsize>(json.size()),reinterpret_cast<const jbyte *>(json.data()));
        if(bytes && !env->ExceptionCheck())env->CallVoidMethod(activity,combatMethod,bytes);
        if(bytes && !env->ExceptionCheck())lastCombatState=json;
        if(bytes)env->DeleteLocalRef(bytes);
    }
    if(env->ExceptionCheck())env->ExceptionClear();
    env->DeleteLocalRef(activity);
}
}
#endif
