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
    std::string scene, player, mode = "unknown";
    if (combatContextCurrent(gameplay, core.Controls != nullptr))
    {
        mode = gameplay.snapshot().mode;
        scene = gameplay.snapshot().scene;
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
            // The HUD's dash: no script control exists (the stock PC layout leaves ChrRecoil
            // out); Player polls this engine-only output and makes the native backstep.
            bindings.push_back({"dash","ChrRecoil",false});
        }
    }
    combatInput.update(gameplay.revision(), std::move(scene), std::move(player), std::move(mode), std::move(bindings));
}
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
inline uint64_t combatHost = 0;
inline std::string lastCombatState;
inline void publishCombatState()
{
    if (combatHost != ui::generation())
    {
        combatHost = ui::generation(); // a new UI host: nothing held through the old one counts
        lastCombatState.clear();
        combatInput.resetActivity();
        movementInput.resetActivity();
    }
    auto json=combatInput.json();
    const auto pistol = readPistolState();
    json.pop_back();
    json += ",\"interactKind\":" + gameplayJsonString("dialog");
    json += ",\"pistol\":{\"state\":" + gameplayJsonString(pistol.state) +
            ",\"progress\":" + std::to_string(pistol.progress) + "}}";
    if (json == lastCombatState) return;
    lastCombatState = json;
    ui::state("combat", json);
}
}
#endif
