#pragma once

// Message and event layouts the ship exchanges with the game scripts. Players run the vanilla
// "To Each His Own" 1.7.3 scripts from their own game files, so these follow that script
// protocol (tests/ship_script_protocol checks them with the real MESSAGE).

#include <cstdint>
#include <optional>
#include <string>

#include "message.h"
#include "shared/sea_ai/script_defines.h"

namespace storm::ship
{

// MSG_SHIP_LIGHTSRESET. Vanilla 1.7.3 (WhrWeather.c, on a day/night change at sea) sends only
// the message code, "l", after Ship_SetLightsAndFlares has stored the new state in the
// character's ship.lights. Later scripts (sd-teho-public) append the light flag, "ll".
// Returns the flag when the message carries it, std::nullopt for the vanilla form; any other
// layout stays a MessageFormatError.
inline std::optional<bool> ReadLightsResetFlag(MESSAGE &message)
{
    if (message.GetFormat() == "l")
        return std::nullopt;
    return message.Long() != 0;
}

// SHIP_HULL_DAMAGE event, read by vanilla 1.7.3 Ship_HullDamage (sea_ai/AIShip.c) as: damage
// type, hull node number, hit point x/y/z, current damage, the hit character, the ball owner
// character and the hull node name (a name containing "baller" is the rudder).
inline void ResetHullDamageEvent(MESSAGE &message, int32_t hullNum, float x, float y, float z, float damage,
                                 ATTRIBUTES *hitCharacter, ATTRIBUTES *ballOwnerCharacter, const char *hullName)
{
    message.Reset("llffffaas", int32_t{SHIP_HULL_TOUCH_BALL}, hullNum, x, y, z, damage, hitCharacter,
                  ballOwnerCharacter, std::string(hullName));
}

} // namespace storm::ship
