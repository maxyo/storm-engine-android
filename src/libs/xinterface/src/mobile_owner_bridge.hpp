#pragma once
#include "mobile_owner_selection.hpp"
#include "core.h"
#include "v_data.h"
#include "platform/android_ui.hpp"

namespace storm::mobile
{
inline const char *OwnerProfile()
{
    auto *profile = static_cast<VDATA *>(core.GetScriptVariable("PlayerProfile"));
    return profile && profile->GetAClass() ? profile->GetAClass()->GetAttribute("name") : nullptr;
}
inline std::string OwnerLabel(uint32_t index, bool ship)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || index >= characters->GetElementsNum()) return {};
    auto *person = characters->GetAClass(index);
    if (!person) return {};
    if (ship)
    {
        auto *vessel = person->GetAttributeClass("ship");
        const char *name = vessel ? vessel->GetAttribute("name") : nullptr;
        if (name && *name) return name;
    }
    std::string result;
    for (const char *field : {"name", "lastname"})
        if (const char *value = person->GetAttribute(field); value && *value)
        { if (!result.empty()) result += " "; result += value; }
    return result;
}
}
