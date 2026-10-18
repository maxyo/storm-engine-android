#pragma once

#include "attributes.h"
#include "mobile_widget.hpp"
#include <charconv>
#include <optional>
#include <string_view>

namespace storm::mobile
{
inline std::optional<uint32_t> InventoryNumber(const char *text)
{
    if (!text || !*text)
        return {};
    uint32_t value = 0;
    const auto end = text + std::char_traits<char>::length(text);
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc() || result.ptr != end)
        return {};
    return value;
}

// Existing table identities (ship/perks/etc.) take precedence. Only inventory
// rows have the legacy index + character-selector fallback.
inline std::string MobileTableRowId(std::string_view node, ATTRIBUTES *row, ATTRIBUTES *characters)
{
    auto *data = row ? row->GetAttributeClass("UserData") : nullptr;
    const char *id = data ? data->GetAttribute("ID") : nullptr;
    if (id)
        return id;
    if (node != "TABLE_ITEMS" || !row || !characters)
        return {};
    const auto item = InventoryNumber(row->GetAttribute("index"));
    const auto role = InventoryNumber(characters->GetAttribute("current"));
    if (!item || !role || *role == UINT32_MAX)
        return {};
    auto *entry = characters->GetAttributeClass("pic" + std::to_string(uint64_t(*role) + 1));
    const auto character = InventoryNumber(entry ? entry->GetAttribute("character") : nullptr);
    if (!character)
        return {};
    return "inventory:" + std::to_string(*item) + ":character:" + std::to_string(*character) +
           ":role:" + std::to_string(*role);
}

// Stock commerce TABLE_LIST rows identify goods/items through `index`, not
// UserData.ID. The selector is part of identity: the same good on another
// companion or passenger is a different operation. Screen lifetime is carried
// separately by the existing @screenRevision action envelope.
inline bool CommerceTable(std::string_view screen, std::string_view node)
{
    return node == "TABLE_LIST" &&
        (screen == "store" || screen == "storage" || screen == "contraband" ||
         screen == "cannons" || screen == "itemstrade" || screen == "itemsbox");
}

inline bool LeasedTableIdentityRequired(std::string_view screen, std::string_view node)
{
    return CommerceTable(screen, node) || (screen == "tradebook" && node == "TABLE_CITY");
}

inline std::string InterfaceTableRowId(std::string_view screen, std::string_view node,
                                       ATTRIBUTES *row, ATTRIBUTES *interfaceRoot)
{
    if (screen == "tradebook" && node == "TABLE_CITY")
    {
        auto *data = row ? row->GetAttributeClass("UserData") : nullptr;
        const char *city = data ? data->GetAttribute("CityID") : nullptr;
        if (!city || !*city) return {};
        const std::string id(city);
        if (id.size() > 128) return {};
        for (const unsigned char c : id) if (c <= 32 || c == 127 || c == '|') return {};
        return "tradebook:city:" + id;
    }
    if (!CommerceTable(screen, node))
        return MobileTableRowId(node, row,
            interfaceRoot ? interfaceRoot->GetAttributeClass("CHARACTERS_SCROLL") : nullptr);
    if (!row || !interfaceRoot)
        return {};
    const bool personal = screen == "itemstrade" || screen == "itemsbox";
    const auto item = InventoryNumber(row->GetAttribute("index"));
    auto *selector = interfaceRoot->GetAttributeClass(personal ? "CHARACTERS_SCROLL" : "SHIPS_SCROLL");
    const auto role = InventoryNumber(selector ? selector->GetAttribute("current") : nullptr);
    if (!item || !role || *role == UINT32_MAX)
        return {};
    auto *entry = selector->GetAttributeClass("pic" + std::to_string(uint64_t(*role) + 1));
    const auto owner = InventoryNumber(entry ? entry->GetAttribute(personal ? "character" : "companionIndex") : nullptr);
    if (!owner)
        return {};
    return "commerce:" + std::string(screen) + ":item:" + std::to_string(*item) +
        ":owner:" + std::to_string(*owner) + ":role:" + std::to_string(*role);
}

inline bool CommerceQuantityNode(std::string_view node)
{
    return node.rfind("QTY_", 0) == 0 || node == "qty_edit" || node == "ADD_BUTTON" ||
           node == "ADD_ALL_BUTTON" || node == "REMOVE_BUTTON" || node == "REMOVE_ALL_BUTTON";
}

inline std::optional<std::string> CommerceQuantityValue(std::string_view screen, std::string_view node,
    uint64_t epoch, ATTRIBUTES *interfaceRoot, const std::string &value)
{
    if (!CommerceTable(screen, "TABLE_LIST") || !CommerceQuantityNode(node) || !epoch || !interfaceRoot)
        return {};
    auto *table = interfaceRoot->GetAttributeClass("TABLE_LIST");
    const auto selected = InventoryNumber(table ? table->GetAttribute("select") : nullptr);
    if (!selected || !*selected) return {};
    const auto id = InterfaceTableRowId(screen, "TABLE_LIST",
        table->GetAttributeClass("tr" + std::to_string(*selected)), interfaceRoot);
    if (id.empty()) return {};
    const auto prefix = "guard@" + std::to_string(epoch) + ":" + id + "|";
    if (value.rfind(prefix, 0) != 0) return {};
    const auto original = value.substr(prefix.size());
    if (original.empty() || original.size() > 128 || original.find_first_of("\t\r\n|") != std::string::npos ||
        original.find('\0') != std::string::npos) return {};
    return original;
}

inline std::string InventorySelectedRowId(ATTRIBUTES *table, ATTRIBUTES *characters)
{
    const auto selected = InventoryNumber(table ? table->GetAttribute("select") : nullptr);
    if (!selected || !*selected)
        return {};
    return MobileTableRowId("TABLE_ITEMS", table->GetAttributeClass("tr" + std::to_string(*selected)), characters);
}

// A selection callback must reach a real widget publication before equip is
// authorized. Consuming a capability also prevents duplicate queued presses.
class InventoryEquipCapability
{
  public:
    void invalidate()
    {
        row_.clear();
        epoch_ = 0;
    }

    void published(uint64_t epoch, std::vector<MobileWidget> &widgets)
    {
        invalidate();
        for (const auto &widget : widgets)
            if (widget.node == "TABLE_ITEMS" && widget.type == "choice" && widget.enabled && widget.value == "1")
                row_ = widget.rowId;
        for (auto &widget : widgets)
            if (widget.node == "EQUIP_BUTTON" && widget.type == "button")
            {
                if (widget.enabled && !row_.empty())
                {
                    epoch_ = epoch;
                    widget.rowId = row_;
                    widget.actions.push_back("press");
                    return;
                }
                break;
            }
        invalidate();
    }

    bool consume(const std::string &action, uint64_t epoch, const std::string &liveRow, bool liveEnabled = true)
    {
        if (!liveEnabled || !epoch_ || epoch != epoch_ || row_.empty() || liveRow != row_ ||
            action != "press@" + std::to_string(epoch_) + ":" + row_)
            return false;
        invalidate();
        return true;
    }

  private:
    uint64_t epoch_ = 0;
    std::string row_;
};
} // namespace storm::mobile
