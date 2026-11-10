// The ship's messages and events in the layout the vanilla 1.7.3 scripts use, built and read
// with the real MESSAGE.
#include "ship_script_protocol.hpp"

#include <cstdlib>
#include <iostream>

#include "shared/messages.h"

namespace
{
void require(bool condition, const char *what)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << what << '\n';
        std::exit(1);
    }
}

// SHIP::ProcessMessage reads the message code before dispatching.
MESSAGE lightsReset(MESSAGE message)
{
    message.Move2Start();
    require(message.Long() == MSG_SHIP_LIGHTSRESET, "message code");
    return message;
}

void lightsResetForms()
{
    // WhrWeather.c (vanilla 1.7.3): SendMessage(&characters[i], "l", MSG_SHIP_LIGHTSRESET)
    MESSAGE vanilla;
    vanilla.Reset("l", int32_t{MSG_SHIP_LIGHTSRESET});
    auto message = lightsReset(vanilla);
    require(!storm::ship::ReadLightsResetFlag(message).has_value(), "vanilla form carries no flag");

    for (const int32_t flag : {0, 1})
    {
        MESSAGE explicitFlag;
        explicitFlag.Reset("ll", int32_t{MSG_SHIP_LIGHTSRESET}, flag);
        message = lightsReset(explicitFlag);
        const auto read = storm::ship::ReadLightsResetFlag(message);
        require(read.has_value() && *read == (flag != 0), "explicit light flag");
    }

    MESSAGE wrong;
    wrong.Reset("lf", int32_t{MSG_SHIP_LIGHTSRESET}, 1.0f);
    message = lightsReset(wrong);
    bool rejected = false;
    try { storm::ship::ReadLightsResetFlag(message); }
    catch (const MessageFormatError &) { rejected = true; }
    require(rejected, "an unknown layout stays a format error");
}
void hullDamageEvent()
{
    // Stand-ins for the hit and the firing characters; the event only carries the pointers.
    auto *const hit = reinterpret_cast<ATTRIBUTES *>(0x1000);
    auto *const owner = reinterpret_cast<ATTRIBUTES *>(0x2000);
    MESSAGE event;
    storm::ship::ResetHullDamageEvent(event, 7, 1.0f, 2.0f, 3.0f, 0.25f, hit, owner, "baller_1");

    // Read in the order of vanilla 1.7.3 Ship_HullDamage (sea_ai/AIShip.c): iDamageType,
    // iHullNum, x, y, z, fDamage, rCharacter, aCharacter, HullName.
    event.Move2Start();
    require(event.Long() == SHIP_HULL_TOUCH_BALL, "iDamageType");
    require(event.Long() == 7, "iHullNum");
    require(event.Float() == 1.0f && event.Float() == 2.0f && event.Float() == 3.0f, "x, y, z");
    require(event.Float() == 0.25f, "fDamage");
    require(event.AttributePointer() == hit, "rCharacter: the hit character");
    require(event.AttributePointer() == owner, "aCharacter: the ball owner");
    require(event.String() == "baller_1", "HullName");
    bool exhausted = false;
    try { event.Long(); }
    catch (const MessageFormatError &) { exhausted = true; }
    require(exhausted, "nothing after HullName");
}
} // namespace

int main()
{
    lightsResetForms();
    hullDamageEvent();
    std::cout << "PASS ship script protocol: MSG_SHIP_LIGHTSRESET vanilla and explicit forms, "
                 "SHIP_HULL_DAMAGE in the vanilla Ship_HullDamage order\n";
}
