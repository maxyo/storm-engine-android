// EFFECTS_LIBRARY: the natives Caribbean Legend declares next to the Effects entity
// (CheckAndCreateEffect, ReloadEffect, ReloadAllEffects, UnloadEffect, UnloadAllEffects).
#pragma once

#include "script_libriary.h"

class EFFECTS_LIBRARY final : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
