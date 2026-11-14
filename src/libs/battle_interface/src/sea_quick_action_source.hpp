#pragma once
#ifdef __ANDROID__
// Reads the sea quick action ILogAndActions offers right now, with the identity of what it
// would act on. Game thread only; nothing here is kept across a frame.
#include "platform/android_ui.hpp"
#include "core.h"
#include "sea/ships_list.h"
#include "v_data.h"

namespace storm::android
{
namespace quick
{
inline VDATA *variable(const char *name)
{
    return static_cast<VDATA *>(core.GetScriptVariable(name));
}
inline std::optional<int32_t> integer(const char *name)
{
    int32_t value = 0;
    auto *data = variable(name);
    return data && data->Get(value) ? std::optional<int32_t>(value) : std::nullopt;
}
inline std::string text(const char *name)
{
    const char *value = nullptr;
    auto *data = variable(name);
    return data && data->Get(value) && value ? value : "";
}
inline std::string attribute(ATTRIBUTES *attributes, const char *name)
{
    const char *value = attributes ? attributes->GetAttribute(name) : nullptr;
    return value ? value : "";
}
inline std::string pointer(const void *address)
{
    return std::to_string(reinterpret_cast<uintptr_t>(address));
}
inline std::string field(std::string_view value)
{
    return std::to_string(value.size()) + ":" + std::string(value);
}
// Moor is bound to the reload object the script references and its locator, never to a
// translated caption or a guessed location.
inline bool locatorIdentity(ATTRIBUTES *attributes, std::string &identity, unsigned depth = 0)
{
    if (!attributes || depth > 12 || identity.size() > 32768 || attributes->GetAttributesNum() > 256) return false;
    if (std::string_view(attributes->GetThisName()).size() > 128 || attributes->GetValue().size() > 4096) return false;
    identity += field(attributes->GetThisName()) + field(attributes->HasValue() ? std::string(attributes->GetValue()) : "");
    for (size_t i = 0; i < attributes->GetAttributesNum(); ++i)
        if (!locatorIdentity(attributes->GetAttributeClass(static_cast<uint32_t>(i)), identity, depth + 1)) return false;
    return identity.size() <= 32768;
}
} // namespace quick

// `selected` / `available`: the action ILogAndActions shows. The frame is complete only when
// the sea is live, the main ship is alive and the target of the action can be named.
inline SeaQuickActionFrame readSeaQuickAction(uint64_t host, ATTRIBUTES *attributes, const char *selected, bool available)
{
    using namespace quick;
    SeaQuickActionFrame frame;
    frame.host = host;
    frame.sea = attribute(attributes, "type") == "sea";
    if (!frame.sea) return frame;
    frame.action = selected ? selected : "";
    frame.available = available;
    if (!seaQuickLifecycleReady || !mobileScreen.empty() || !knownSeaQuickAction(frame.action)) return frame;
    // These guards bind the owner of the touch control. BI_FastCommand still owns every
    // refusal and effect of the action itself.
    if (integer("bSeaActive") != 1 || integer("bAbordageStarted").value_or(0) || integer("dialogRun").value_or(0) ||
        integer("bSeaReloadStarted").value_or(0)) return frame;
    auto *interfaces = variable("InterfaceStates");
    auto *launched = interfaces ? interfaces->GetAClass() : nullptr;
    if (launched && launched->GetAttributeAsDword("Launched", 0)) return frame;
    for (const auto *entity : {"WorldMap", "CAviPlayer", "WMVideoPlay", "fader"})
        if (core.GetEntityId(entity) != invalid_entity) return frame;
    const auto group = text("curKeyGroupName");
    if (group != "Sailing1Pers" && group != "Sailing3Pers") return frame;
    auto *ship = g_ShipList.GetMainCharacterShip();
    if (!ship || ship->isDead || !ship->pShip) return frame;
    auto *characters = variable("Characters");
    auto *character = characters && ship->characterIndex >= 0 && static_cast<uint32_t>(ship->characterIndex) < characters->GetElementsNum() ?
        characters->GetAClass(ship->characterIndex) : nullptr;
    if (!character) return frame;
    auto *battle = variable("BattleInterface");
    auto *battleAttributes = battle ? battle->GetAClass() : nullptr;
    auto *command = battleAttributes ? battleAttributes->FindAClass(battleAttributes, ("Commands." + frame.action).c_str()) : nullptr;
    frame.label = attribute(command, "note");
    if (frame.label.empty() || !landCommandsTextValid(frame.label, 1024)) return frame;
    frame.owner = std::to_string(host) + ":" + std::to_string(ship->pShip->GetId()) + ":" + std::to_string(ship->characterIndex) + ":" +
        pointer(character) + ":" + std::to_string(battle ? battle->GetEntityID() : invalid_entity) + ":" +
        std::to_string(shipTelemetry.revision()) + ":" + group;
    // The world map too, beside another action (moor, board…): the script's own permission
    // (bMapEnter, already false while bDisableMapEnter or enemies hold the squadron) and caption.
    if (frame.action != "Map" && integer("bMapEnter").value_or(0) == 1)
    {
        frame.mapLabel = attribute(battleAttributes ? battleAttributes->FindAClass(battleAttributes, "Commands.Map") : nullptr, "note");
        frame.map = !frame.mapLabel.empty() && landCommandsTextValid(frame.mapLabel, 1024);
    }
    if (frame.action == "Map")
        frame.target = frame.owner;
    else if (frame.action == "Moor")
    {
        auto *reload = variable("arIslandReload");
        auto *root = reload ? reload->GetAClass() : nullptr;
        const auto island = text("sIslandID"), locator = text("sIslandLocator");
        if (!root || root->GetAttributesNum() > 256 || island.empty() || locator.empty()) return frame;
        ATTRIBUTES *match = nullptr;
        for (size_t i = 0; i < root->GetAttributesNum(); ++i)
        {
            auto *entry = root->GetAttributeClass(static_cast<uint32_t>(i));
            if (attribute(entry, "name") != locator) continue;
            if (match) return frame; // two reload objects with one locator: no single target
            match = entry;
        }
        frame.target = field(island) + field(locator) + pointer(root);
        if (!match || !locatorIdentity(match, frame.target)) return frame;
    }
    else
    {
        const char *source = frame.action == "Board" ? "iAbordageShipEnemyCharacter" :
            frame.action == "LandTroops" ? "iAbordageFortEnemyCharacter" : "bi_nReloadTarget";
        const auto target = integer(source);
        if (!target || *target < 0 || static_cast<uint32_t>(*target) >= characters->GetElementsNum()) return frame;
        auto *targetCharacter = characters->GetAClass(*target);
        if (!targetCharacter) return frame;
        frame.target = std::string(source) + ":" + std::to_string(*target) + ":" + pointer(targetCharacter) + ":" +
            field(attribute(targetCharacter, "id"));
        if (frame.action == "LandTroops")
        {
            const auto fort = core.GetEntityId("AIFort");
            if (fort == invalid_entity) return frame;
            frame.target += ":" + std::to_string(fort);
        }
        else
        {
            auto *targetShip = g_ShipList.FindShip(*target);
            if (!targetShip || !targetShip->pShip) return frame;
            frame.target += ":" + std::to_string(targetShip->pShip->GetId());
        }
    }
    frame.complete = true;
    return frame;
}
} // namespace storm::android
#endif
