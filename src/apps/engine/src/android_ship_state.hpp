#pragma once
#ifdef __ANDROID__
#include "platform/android_ship_telemetry.hpp"
#include "core.h"
#include "v_data.h"
#include "ship_base.h"
#include <cctype>
#include <limits>

namespace storm::android
{
inline ATTRIBUTES *shipMainCharacter()
{
    auto *index = static_cast<VDATA *>(core.GetScriptVariable("nMainCharacterIndex"));
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    int32_t number = -1;
    if (!index || !index->Get(number) || !characters || number < 0 ||
        static_cast<uint32_t>(number) >= characters->GetElementsNum()) return nullptr;
    return characters->GetAClass(number);
}
inline SHIP_BASE *shipForCharacter(ATTRIBUTES *character)
{
    if (!character) return nullptr;
    for (const auto id : core.GetEntityIds("ship"))
        if (auto *ship = static_cast<SHIP_BASE *>(core.GetEntityPointer(id)))
            if (ship->GetACharacter() == character && ship->GetAShip()) return ship;
    return nullptr;
}
inline std::optional<double> shipNumber(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    if (!value || !value->HasValue()) return {};
    return shipNumberValue(value->GetValue());
}
inline std::string shipText(ATTRIBUTES *attributes, const char *path)
{
    auto *value = attributes ? attributes->FindAClass(attributes, path) : nullptr;
    if (!value || !value->HasValue()) return {};
    return shipTextValue(value->GetValue());
}
inline ShipTelemetryEntry shipEntry(SHIP_BASE *ship)
{
    ShipTelemetryEntry entry;
    auto *character = ship->GetACharacter();
    auto *type = ship->GetAShip(); // Live RealShips attributes, not cached battle-interface maxima.
    entry.entityId = std::to_string(ship->GetId());
    entry.characterIndex = static_cast<int>(character->GetAttributeAsDword("index", -1));
    entry.name = shipText(character, "Ship.Name");
    entry.type = shipText(type, "Name");
    const auto metric = [character, type](const char *currentPath, const char *maximumPath) -> std::optional<ShipMetric> {
        const auto current = shipNumber(character, currentPath), maximum = shipNumber(type, maximumPath);
        return current && maximum ? shipMetric(*current, *maximum) : std::nullopt;
    };
    entry.hull = metric("Ship.HP", "HP");
    entry.crew = metric("Ship.Crew.Quantity", "MaxCrew");
    entry.sails = metric("Ship.SP", "SP");
    return entry;
}
inline bool shipSailControlAllowed(SHIP_BASE *ship, const GameplayState &gameplay)
{
    if (!ship || !ship->GetACharacter()) return false;
    const auto &mode = gameplay.snapshot().mode;
    auto *character = ship->GetACharacter();
    auto *stopped = character->FindAClass(character, "Ship.Stopped");
    auto *canDrop = character->GetAttributeClass("CanDropSails");
    ShipSailGuard guard;
    guard.sea = mode == "sailing" || mode == "naval";
    guard.up = gameplay.allows("Ship_SailUp");
    guard.down = gameplay.allows("Ship_SailDown");
    guard.mainCharacter = character->GetAttributeAsDword("MainCharacter", 0) != 0;
    guard.stopped = stopped && stopped->GetAttributeAsDword();
    guard.dead = ship->isDead();
    guard.canDropSails = !canDrop || canDrop->GetAttributeAsDword() != 0;
    guard.state = ship->GetSailState();
    return shipSailControlAllowed(guard);
}
inline ShipTelemetryFrame readShipTelemetry(const GameplayState &gameplay)
{
    ShipTelemetryFrame out;
    const auto &mode = gameplay.snapshot().mode;
    if (mode != "sailing" && mode != "naval") return out;
    try
    {
        auto *character = shipMainCharacter();
        auto *ship = shipForCharacter(character);
        if (!ship) return out;
        out.valid = true;
        out.ship = shipEntry(ship);
        readShipCaptions(out, ship->GetId());
        auto *powder = character->FindAClass(character, "Ship.Cargo.Goods.powder");
        if (powder && powder->HasValue()) out.powderCount = shipPowderCount(powder->GetValue());
        const auto sailState = ship->GetSailState();
        if (std::isfinite(sailState) && sailState >= 0 && sailState <= 1) out.sailState = sailState;
        out.canSetSails = shipSailControlAllowed(ship, gameplay);
        const auto ammoType = shipNumber(character, "Ship.Cannons.Charge.Type");
        auto *goods = static_cast<VDATA *>(core.GetScriptVariable("Goods"));
        if (ammoType && goods && *ammoType < goods->GetElementsNum() && std::floor(*ammoType) == *ammoType)
        {
            const auto index = static_cast<uint32_t>(*ammoType);
            auto name = shipText(goods->GetAClass(index), "Name");
            for (auto &c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "balls" || name == "grapes" || name == "knippels" || name == "bombs")
            {
                out.ammo = name;
                out.ammoType = static_cast<int>(index);
                out.ammoCount = shipNumber(character, ("Ship.Cargo.Goods." + name).c_str());
            }
        }
        // Original AddShipToInterface marks myShip from Fellows.Companions, not
        // relation/faction. Export only live companions, never remote stale stats.
        auto *companions = character->FindAClass(character, "Fellows.Companions");
        if (companions)
        {
            std::vector<int> added;
            // TEHO's COMPANION_MAX is 5 including the player's slot 0.
            // Higher Fellows attributes are not members of its active fleet.
            for (int slot = 1; slot <= 4; ++slot)
            {
                auto *member = companions->GetAttributeClass("id" + std::to_string(slot));
                if (!member || !member->HasValue()) continue;
                const int index = static_cast<int>(member->GetAttributeAsDword());
                if (index <= 0 || index == out.ship.characterIndex || std::find(added.begin(), added.end(), index) != added.end()) continue;
                for (const auto id : core.GetEntityIds("ship"))
                {
                    auto *companion = static_cast<SHIP_BASE *>(core.GetEntityPointer(id));
                    auto *owner = companion ? companion->GetACharacter() : nullptr;
                    if (owner && companion->GetAShip() && static_cast<int>(owner->GetAttributeAsDword("index", -1)) == index)
                    {
                        out.fleet.push_back(shipEntry(companion));
                        added.push_back(index);
                        break;
                    }
                }
            }
        }
    }
    catch (...) { return {}; }
    return out;
}
inline bool applyShipSailTarget(const GameplayState &gameplay, int detent)
{
    if (detent < 0 || detent > 2) return false;
    auto *ship = shipForCharacter(shipMainCharacter());
    if (!shipSailControlAllowed(ship, gameplay)) return false;
    // The same native state and canonical 0/.5/1 positions as W/S in SHIP::Execute.
    // An arbitrary analog fraction would bypass the script's discrete sail tuning.
    ship->SetSailState(detent * 0.5f);
    return true;
}
} // namespace storm::android
#endif
