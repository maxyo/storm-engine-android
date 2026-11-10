#pragma once

#include "script_libriary.h"

namespace steamapi
{
// Registers CL's Steam/overlay/anti-piracy stubs (SteamApiScriptLib::Init calls it).
void registerClPlatformNatives();

// CL's Discord rich presence library: explicit stubs (no Discord client).
class DISCORD_SCRIPT_LIBRARY : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
} // namespace steamapi
