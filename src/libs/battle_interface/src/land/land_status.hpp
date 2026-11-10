// The land hero's status for the portable HUD, published as extra fields of the "vitals"
// channel next to the bars (battle_man_sign.cpp): the effects row, the potion count, the
// officers column and the dash cooldown (mockups V2-Land-Explore, -Combat, -Potions).
//
//   "effects":  ["poison", "drunk", "healing"]   chr_ai.poison / chr_ai.drunk / chr_ai.hp_bottle
//                                                 of the hero (Loc_ai/LAi_character.c)
//   "potions":  N                                 health potions the hero carries: items with
//                                                 potion.health, what the quick heal uses
//   "officers": [{"id", "name", "face", "health"}] data.icons.id1… of the land interface
//                                                 (landinterface.c SetCharacterIconData)
//   "dash":     0…1                               the backstep's remaining cooldown, only in
//                                                 fight mode (Character "RecoilWait")
#pragma once
#ifdef __ANDROID__
#include "core.h"
#include "entity.h"
#include "shared/messages.h"
#include "v_data.h"
#include <platform/android_gameplay.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <string_view>

namespace storm::android
{

inline ATTRIBUTES *landCharacter(int index)
{
    auto *characters = static_cast<VDATA *>(core.GetScriptVariable("Characters"));
    if (!characters || index < 0 || static_cast<uint32_t>(index) >= characters->GetElementsNum()) return nullptr;
    return characters->GetAClass(static_cast<uint32_t>(index));
}

inline std::string landText(ATTRIBUTES *root, const char *path)
{
    auto *a = root ? root->FindAClass(root, path) : nullptr;
    return a && a->HasValue() ? a->GetValue() : std::string();
}

inline float landNumber(ATTRIBUTES *root, const char *path)
{
    const std::string text = landText(root, path);
    const float value = text.empty() ? 0.f : std::strtof(text.c_str(), nullptr);
    return std::isfinite(value) ? value : 0.f;
}

// Ids of the items that heal (potion.health), read once: the item table does not change.
inline const std::set<std::string> &healthPotionIds()
{
    static std::set<std::string> ids;
    static bool read = false;
    if (read) return ids;
    auto *items = static_cast<VDATA *>(core.GetScriptVariable("Items"));
    if (!items || items->GetElementsNum() == 0) return ids;
    for (uint32_t i = 0; i < items->GetElementsNum(); ++i)
        if (auto *item = items->GetAClass(i); item && item->FindAClass(item, "potion.health"))
            if (const std::string id = landText(item, "id"); !id.empty()) ids.insert(id);
    read = true;
    return ids;
}

inline int healthPotions(ATTRIBUTES *hero)
{
    auto *items = hero ? hero->GetAttributeClass("items") : nullptr;
    if (!items) return 0;
    const auto &potions = healthPotionIds();
    int count = 0;
    for (size_t i = 0; i < items->GetAttributesNum(); ++i)
        if (auto *item = items->GetAttributeClass(static_cast<uint32_t>(i)); item && potions.count(item->GetThisName()))
            count += std::max(0, static_cast<int>(std::strtol(item->HasValue() ? item->GetValue().c_str() : "0", nullptr, 10)));
    return count;
}

// `icons` is data.icons of the land interface; id0 is the hero.
inline std::string landStatusJson(ATTRIBUTES *icons)
{
    auto *heroIcon = icons ? icons->GetAttributeClass("id0") : nullptr;
    ATTRIBUTES *hero = heroIcon ? landCharacter(static_cast<int>(heroIcon->GetAttributeAsDword("chrindex", -1))) : nullptr;
    std::string effects;
    for (const auto &[name, path] : {std::pair{"poison", "chr_ai.poison"}, {"drunk", "chr_ai.drunk"}, {"healing", "chr_ai.hp_bottle"}})
        if (landNumber(hero, path) > 0.f) effects += std::string(effects.empty() ? "" : ",") + "\"" + name + "\"";
    std::string json = ",\"effects\":[" + effects + "],\"potions\":" + std::to_string(healthPotions(hero)) + ",\"officers\":[";
    bool first = true;
    for (size_t i = 0; icons && i < icons->GetAttributesNum(); ++i)
    {
        auto *icon = icons->GetAttributeClass(static_cast<uint32_t>(i));
        if (!icon || icon == heroIcon || std::string_view(icon->GetThisName()).substr(0, 2) != "id") continue;
        const int index = static_cast<int>(icon->GetAttributeAsDword("chrindex", -1));
        auto *officer = landCharacter(index);
        if (!officer) continue;
        const std::string name = landText(officer, "name"), last = landText(officer, "lastname");
        const float health = std::clamp(icon->GetAttributeAsFloat("health", 0.f), 0.f, 1.f);
        json += std::string(first ? "" : ",") + "{\"id\":" + std::to_string(index) +
                ",\"name\":" + gameplayJsonString(last.empty() ? name : name + " " + last) +
                ",\"face\":" + gameplayJsonString(landText(officer, "FaceId")) +
                ",\"health\":" + std::to_string(std::isfinite(health) ? health : 0.f) + "}";
        first = false;
    }
    json += "]";
    const auto player = core.GetEntityId("Player");
    if (player != invalid_entity && core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "IsFightMode"))
    {
        const auto wait = core.Send_Message(player, "ls", MSG_CHARACTER_EX_MSG, "RecoilWait");
        json += ",\"dash\":" + std::to_string(std::clamp(static_cast<float>(wait) / 800.f, 0.f, 1.f));
    }
    return json;
}

} // namespace storm::android
#endif
