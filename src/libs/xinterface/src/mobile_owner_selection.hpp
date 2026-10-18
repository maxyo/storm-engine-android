#pragma once
#include "mobile_inventory.hpp"
#include "mobile_officer_selection.hpp"

namespace storm::mobile
{
inline const char *OwnerField(std::string_view screen, std::string_view node, bool vertical)
{
    if (vertical)
        return node == "SHIPS_SCROLL" && (screen == "ship" || screen == "shipyard") ? "character" : nullptr;
    if (node == "SHIPS_SCROLL" && (screen == "store" || screen == "storage" || screen == "contraband" ||
        screen == "cannons" || screen == "hirecrew" || screen == "repair")) return "companionIndex";
    if (node == "CHARACTERS_SCROLL" && (screen == "itemstrade" || screen == "itemsbox")) return "character";
    if (node == "PASSENGERSLIST" && (screen == "shipyard" || screen == "ship" || screen == "character" ||
        screen == "lscchar_all" || screen == "transfer_main")) return "character";
    return nullptr;
}

// Resolve names from actual Characters attributes, never from an item index or
// a copied fixture. Missing trailing image slots are stock padding, not owners.
template<class Resolve>
bool OwnerRows(std::string_view screen, std::string_view node, bool vertical,
               ATTRIBUTES *attrs, size_t capacity, uint64_t revision, const char *profile,
               bool enabled, int order, Resolve resolve, const std::string &emptyShip,
               OfficerListIdentity &state, std::vector<MobileWidget> &rows)
{
    const char *field = OwnerField(screen, node, vertical);
    if (!field || !attrs || !revision || !profile || !*profile) return false;
    const auto count = InventoryNumber(attrs->GetAttribute("ListSize"));
    const auto selected = InventoryNumber(attrs->GetAttribute("current"));
    if (!count || !*count || *count > 1000 || *count > capacity || !selected || *selected >= *count) return false;
    state = {}; rows.clear(); state.revision = revision;
    state.profile = std::string(profile) + "\n" + std::string(screen) + "\n" + std::string(node);
    for (uint32_t i = 0; i < *count; ++i)
    {
        auto *entry = attrs->GetAttributeClass("pic" + std::to_string(uint64_t(i) + 1));
        if (!entry) { state.rows.push_back("padding"); continue; }
        const char *raw = entry->GetAttribute(field);
        const bool empty = screen == "shipyard" && node == "SHIPS_SCROLL" && raw && std::string(raw) == "-1";
        auto owner = InventoryNumber(raw);
        if (!empty && !owner) return false;
        std::string label = empty ? emptyShip : resolve(*owner, node == "SHIPS_SCROLL");
        if (label.empty()) return false;
        state.rows.push_back(std::string(raw) + "\n" + label);
        rows.push_back({std::string(node), "choice", label, *selected == i ? "1" : "0",
            static_cast<int>(i), order + static_cast<int>(i), enabled});
    }
    return !rows.empty();
}
// Stock hirecrew updates refCharacter in ProcessFrame, after scrolling. The
// selector alone is insufficient proof that transactions address that owner.
inline bool HireCrewBoundOwner(ATTRIBUTES *selector, ATTRIBUTES *character)
{
    if (!selector || !character) return false;
    const auto selected = InventoryNumber(selector->GetAttribute("current"));
    const auto count = InventoryNumber(selector->GetAttribute("ListSize"));
    const auto actual = InventoryNumber(character->GetAttribute("index"));
    if (!selected || !count || *selected >= *count || !actual) return false;
    auto *row = selector->GetAttributeClass("pic" + std::to_string(uint64_t(*selected) + 1));
    const auto owner = InventoryNumber(row ? row->GetAttribute("companionIndex") : nullptr);
    return owner && *owner == *actual;
}

inline bool HireCrewQuantityNode(std::string_view node)
{
    return node == "QTY_EDIT" || node == "qty_edit" || node == "QTY_OK_BUTTON" ||
           node == "QTY_CANCEL_BUTTON" || node == "QTY_ADD_BUTTON" || node == "QTY_REMOVE_BUTTON" ||
           node == "QTY_ADD_ALL_BUTTON" || node == "QTY_REMOVE_ALL_BUTTON";
}

// Validate against the last published owner capability without selecting a
// ship or dispatching a script event. A queued edit must not target a new owner.
inline std::optional<std::string> HireCrewQuantityValue(std::string_view screen, std::string_view node,
    const OfficerSelection &published, const OfficerListIdentity &state,
    const std::vector<MobileWidget> &rows, const std::string &value)
{
    if (screen != "hirecrew" || !HireCrewQuantityNode(node) || value.rfind("guard@", 0) != 0) return {};
    const auto bar = value.find('|');
    if (bar == std::string::npos) return {};
    const auto original = value.substr(bar + 1);
    if (original.empty() || original.size() > 128 || original.find_first_of("\t\r\n|") != std::string::npos ||
        original.find('\0') != std::string::npos) return {};
    const MobileWidget *selected = nullptr;
    for (const auto &row : rows) if (row.value == "1") {
        if (selected || !row.enabled || row.node != "SHIPS_SCROLL") return {};
        selected = &row;
    }
    if (!selected || !published.accepts(state, selected->index, "select" + value.substr(5, bar - 5))) return {};
    return original;
}

}
