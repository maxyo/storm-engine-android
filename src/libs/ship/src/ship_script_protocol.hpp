#pragma once

// Message and event layouts the ship exchanges with the game scripts. Players run the vanilla
// "To Each His Own" 1.7.3 scripts from their own game files, so these follow that script
// protocol (tests/ship_script_protocol checks them with the real MESSAGE).

#include <optional>

#include "message.h"

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

} // namespace storm::ship
