#pragma once
#ifdef __ANDROID__
#include "android_gameplay_state.hpp"
#include "platform/android_ui.hpp"
#include "platform/android_combat_input.hpp"
#include "platform/android_movement.hpp"
#include "platform/android_pistol_state.hpp"
#include "core_private.h"

#include <cctype>

namespace storm::android
{
inline bool combatContextCurrent(const GameplayState &gameplay, bool ready)
{
    if (!ready || (gameplay.snapshot().mode != "explore" && gameplay.snapshot().mode != "melee")) return false;
    const auto current = readGameplaySnapshot(true);
    return current.mode == gameplay.snapshot().mode && current.scene == gameplay.snapshot().scene &&
        current.group == gameplay.snapshot().group && !current.scene.empty();
}
// The dialog character's script index, or -1 (FindDialogCharacter keeps it with hysteresis).
inline int64_t dialogTarget()
{
    const auto player = core.GetEntityId("Player");
    auto *character = shipMainCharacter();
    if (player == invalid_entity || !character || core.Entity_GetAttributePointer(player) != character ||
        core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsDead")) return -1;
    // Query the same range, facing, life and fight checks used by Player::Update.
    // The script's dialogready attribute is only a one-second hint and can be stale.
    const auto target = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "FindDialogCharacter");
    return validDialogTarget(target) ? target : -1;
}
// The game's way on after a boarding fight (LAi_boarding.c, TEHO and Caribbean Legend): once the enemy
// group is dead and its delay has passed, LAi_EnableReload arms the one-shot "Control Activation"
// handler LAi_ActivateReload, which continues the boarding on the action key (ChrAction) unless a quest
// set GenQuest.CannotReloadBoarding. The active-action icon it also sets is only a hint: leaving a
// locator, box or item resets it to "Nothing", and CL's BI_FastCommand ignores its BoardingReload. So
// the touch HUD offers the action key while that handler is armed, not while the icon happens to show.
inline bool boardingContinueArmed(CorePrivate *scripts)
{
    auto *process = static_cast<VDATA *>(core.GetScriptVariable("LAi_boarding_process"));
    int32_t boarding = 0;
    if (!process || !process->Get(boarding) || !boarding || !scripts ||
        !scripts->HasActiveEventHandler("Control Activation", "LAi_ActivateReload"))
        return false;
    auto *character = shipMainCharacter();
    return character && !character->FindAClass(character, "GenQuest.CannotReloadBoarding");
}
inline std::string combatInteractKind = "dialog";
// Whom the talk button talks to (the touch HUD's bottom hint): the script character's name and
// last name, as the game writes them; "" when it has none.
inline std::string combatInteractName, combatInteractDialog;
inline std::string dialogTargetName(int64_t target)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || target < 0 || static_cast<uint64_t>(target) >= characters->GetElementsNum()) return {};
    auto *character = characters->GetAClass(static_cast<uint32_t>(target));
    std::string name = shipText(character, "name");
    const std::string last = shipText(character, "lastname");
    if (!last.empty()) name += (name.empty() ? "" : " ") + last;
    return name;
}
// The talk target's role as the game knows it: its dialog file (Dialog.Filename, "Common_Store.c",
// "Usurer_dialog.c", "Enc_Officer_dialog.c"), the stem in lower case without folders; "" = none.
// The touch HUD picks the talk hint's icon by it (land.cpp LandTalkIcon).
inline std::string dialogTargetFile(int64_t target)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || target < 0 || static_cast<uint64_t>(target) >= characters->GetElementsNum()) return {};
    auto *character = characters->GetAClass(static_cast<uint32_t>(target));
    std::string stem = character ? shipText(character, "Dialog.Filename") : std::string();
    if (const auto slash = stem.find_last_of("\\/"); slash != std::string::npos) stem.erase(0, slash + 1);
    if (stem.size() > 2 && stem.compare(stem.size() - 2, 2, ".c") == 0) stem.erase(stem.size() - 2);
    for (auto &c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return stem.size() <= 96 ? stem : std::string();
}
inline void updateCombatState(const GameplayState &gameplay, CorePrivate *scripts)
{
    std::vector<CombatBinding> bindings;
    std::string scene, player, mode = "unknown", interactKind = "dialog", interactName, interactDialog;
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
        if (mode == "explore" && gameplay.allows("ChrAction"))
        {
            // The boarding continuation has no dialog target; otherwise the interact binding carries
            // the talk target it was offered for (a tap aimed at another target is refused).
            if (boardingContinueArmed(scripts))
            {
                interactKind = "boarding";
                add("interact",false,{"ChrAction"});
            }
            else if (const auto target = dialogTarget(); target >= 0)
            {
                interactName = dialogTargetName(target);
                interactDialog = dialogTargetFile(target);
                add("interact",false,{"ChrAction"});
                if (!bindings.empty() && bindings.back().action == "interact") bindings.back().target = target;
            }
        }
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
    combatInteractKind = std::move(interactKind);
    combatInteractName = std::move(interactName);
    combatInteractDialog = std::move(interactDialog);
}
inline uint64_t combatHost = 0;
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
    json += ",\"interactKind\":" + gameplayJsonString(combatInteractKind);
    json += ",\"interactName\":" + gameplayJsonString(combatInteractName);
    json += ",\"interactDialog\":" + gameplayJsonString(combatInteractDialog);
    json += ",\"pistol\":{\"state\":" + gameplayJsonString(pistol.state) +
            ",\"progress\":" + std::to_string(pistol.progress) + "}}";
    if (json == lastCombatState) return;
    lastCombatState = json;
    ui::state("combat", json);
}
}
#endif
