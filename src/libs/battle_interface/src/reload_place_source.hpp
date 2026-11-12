#pragma once
#ifdef __ANDROID__
// Where the door the hero stands at leads, in the game's own words: the reload the script waits
// on (characters_events.c chrCharacterEntryToLocator: chrWaitReloadRef = the location's reload
// list, chrWaitReloadLocator = the locator) carries a `label` ("Tavern", "ExitTown"), which the
// game names through LocLables.txt ("таверна", "выход в джунгли"), as for the location line.
// Game thread only.
#include "core.h"
#include "string_compare.hpp"
#include "v_data.h"
#include "../../xinterface/src/string_service.h"

#include <string>

namespace storm::android
{
// The place, or "" when the hero is at no reload or the reload has no label. A label the game's
// LocLables does not name is traced (once per locator entry) and gives "" (the context button still shows).
inline std::string reloadPlace()
{
    auto *locator_data = static_cast<VDATA *>(core.GetScriptVariable("chrWaitReloadLocator"));
    const char *locator = nullptr;
    if (!locator_data || !locator_data->Get(locator) || !locator || !*locator) return {};
    auto *reloads_data = static_cast<VDATA *>(core.GetScriptVariable("chrWaitReloadRef"));
    ATTRIBUTES *reloads = reloads_data ? reloads_data->GetAClass() : nullptr;
    if (!reloads) return {};
    const char *label = nullptr;
    for (uint32_t i = 0; i < reloads->GetAttributesNum() && !label; ++i)
        if (auto *reload = reloads->GetAttributeClass(i))
            if (const char *name = reload->GetAttribute("name"); name && storm::iEquals(name, locator))
                label = reload->GetAttribute("label");
    if (!label || !*label) return {};
    auto *strings = static_cast<VSTRSERVICE *>(core.GetService("STRSERVICE"));
    if (!strings) return {};
    const auto file = strings->OpenUsersStringFile("LocLables.txt");
    const char *place = strings->TranslateFromUsers(file, label);
    std::string result = place ? place : "";
    strings->CloseUsersStringFile(file);
    if (result.empty()) core.Trace("touch HUD: reload label '%s' (locator %s) has no LocLables name: no door hint", label, locator);
    return result;
}
} // namespace storm::android
#endif
