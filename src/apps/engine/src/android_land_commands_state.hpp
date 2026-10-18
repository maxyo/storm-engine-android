#pragma once
#ifdef __ANDROID__
#include "android_ship_state.hpp"
#include <platform/android_land_commands.hpp>
#include <platform/android_land_diagnostics.hpp>
namespace storm::android {
inline bool landCommandsReady(bool ready)
{
    const auto result = [](bool allowed, const char *reason) { traceLandAdapter(1, reason); return allowed; };
    if (!ready) return result(false, "gameplay-not-ready");
    const auto integer = [](const char *name) { auto *v = static_cast<VDATA *>(core.GetScriptVariable(name)); int32_t n = 0; if (v) v->Get(n); return n; };
    if (!integer("bLandInterfaceStart")) return result(false, "land-interface-not-started");
    if (integer("bSeaActive") && !integer("bAbordageStarted")) return result(false, "sea-without-boarding");
    if (integer("dialogRun")) return result(false, "dialogue");
    auto *value = static_cast<VDATA *>(core.GetScriptVariable("InterfaceStates")); auto *attrs = value ? value->GetAClass() : nullptr;
    if (attrs && attrs->GetAttributeAsDword("Launched", 0)) return result(false, "interface-launched");
    for (auto name : {"WorldMap", "CAviPlayer", "WMVideoPlay", "fader"}) if (core.GetEntityId(name) != invalid_entity) return result(false, name);
    auto *character = shipMainCharacter(); const auto player = core.GetEntityId("Player");
    if (!character) return result(false, "no-main-character");
    if (player == invalid_entity) return result(false, "no-player");
    if (core.Entity_GetAttributePointer(player) != character) return result(false, "player-owner-mismatch");
    auto *type = character->FindAClass(character, "chr_ai.type");
    if (type && type->HasValue() && type->GetValue() == "actor") return result(false, "actor-control");
    auto *group = static_cast<VDATA *>(core.GetScriptVariable("curKeyGroupName")); const char *name = nullptr;
    if (!group || !group->Get(name) || !name) return result(false, "missing-key-group");
    const std::string_view current(name);
    auto *land = static_cast<VDATA *>(core.GetScriptVariable("objLandInterface"));
    auto *root = land ? land->GetAClass() : nullptr;
    if (root && root->GetAttributeAsDword("ComState", 0)) {
        const bool allowed = current == "PrimaryLand" || current == "FightModeControls" || current == "BattleInterfaceControls";
        return result(allowed, allowed ? "ready-active" : "active-key-group-blocked");
    }
    if (current != "PrimaryLand" && current != "FightModeControls") return result(false, "key-group-blocked");
    auto *controls = static_cast<VDATA *>(core.GetScriptVariable("objControlsState"));
    auto *bindings = controls ? controls->GetAClass() : nullptr;
    const auto path = "keygroups." + std::string(current) + ".LICommandsActivate";
    const bool bound = bindings && bindings->FindAClass(bindings, path.c_str());
    return result(bound, bound ? "ready" : "missing-LICommandsActivate");
}
}
#endif
