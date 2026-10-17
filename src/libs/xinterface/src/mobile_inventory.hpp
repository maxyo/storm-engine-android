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
